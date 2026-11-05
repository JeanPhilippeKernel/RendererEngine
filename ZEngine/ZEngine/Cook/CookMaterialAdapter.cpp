#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <algorithm>
#include <new>
#include <type_traits>

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        // One transient block for observed bytes and the complete owned reader.
        // No second full document on the stack, parser heap/DOM or file-cap slab.
        VFS::VFSResult<void> ReadMaterial(const CookAssetInput& input, ICookAllocator& scratch, CookWorkspace& workspace, const Codec::MaterialAssetDocument** output, CookStage stage, CookDiagnostics* diagnostics)
        {
            const auto size = input.Artifact->Size();
            if (size > input.Limits->MaxMaterialBytes || size > input.Limits->MaxEncodedAssetBytes || sizeof(Codec::MaterialAssetDocument) > input.Limits->MaxDecodedAssetBytes || size > SIZE_MAX - sizeof(Codec::MaterialAssetReader) - DEFAULT_ALIGNMENT)
            {
                return Detail::AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Material exceeds its encoded byte limit.");
            }
            if (!size)
            {
                return Detail::AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Material payload is empty.");
            }
            const auto allocated = workspace.Initialize(scratch, static_cast<size_t>(size) + sizeof(Codec::MaterialAssetReader) + DEFAULT_ALIGNMENT, CookMemoryCategory::Codecs);
            if (allocated.Failed())
            {
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(allocated.Error()), allocated.Error(), "Material scratch allocation failed.");
            }
            static_assert(std::is_trivially_destructible_v<Codec::MaterialAssetReader>);
            auto* storage = workspace.Arena()->AllocateNoZero(sizeof(Codec::MaterialAssetReader), alignof(Codec::MaterialAssetReader));
            auto* bytes   = static_cast<uint8_t*>(workspace.Arena()->AllocateNoZero(static_cast<size_t>(size), 1));
            if (!storage || !bytes)
            {
                return Detail::AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Material arena capacity was exhausted.");
            }
            auto*      reader = new (storage) Codec::MaterialAssetReader{};
            const auto read   = ReadCookCapturedRange(*input.Artifact, 0, {bytes, static_cast<size_t>(size)});
            if (read.Failed())
            {
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(read.Error()), read.Error(), "Material capture could not be read completely.");
            }
            const auto parsed = reader->Read({bytes, static_cast<size_t>(size)}, input.Identity.UUID, input.Limits->MaxMaterialBytes);
            if (parsed.Failed())
            {
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(parsed.Error()), parsed.Error(), "Material does not satisfy the strict current wire schema.");
            }
            *output = parsed.Value();
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> InspectMaterial(const void*, const CookAssetInput& input, ICookAllocator& scratch, CookAssetInspection* output, CookDiagnostics* diagnostics)
        {
            if (!output)
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Material inspection requires result storage.");
            }
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Material, CookStage::Discover, diagnostics, &metadata_key);
            if (metadata.Failed())
            {
                return metadata;
            }
            CookWorkspace                       workspace = {};
            const Codec::MaterialAssetDocument* document  = nullptr;
            const auto                          parsed    = ReadMaterial(input, scratch, workspace, &document, CookStage::Discover, diagnostics);
            if (parsed.Failed())
            {
                return parsed;
            }
            CookMemoryCharge fixed   = {};
            const auto       charged = scratch.TryCharge(sizeof(Core::Containers::FixedArray<CookDependency, static_cast<size_t>(Codec::MaterialTextureSlot::Count)>) + sizeof(Digest256) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
            if (charged.Failed())
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, charged.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(charged.Error()), charged.Error(), "Material inspection fixed workspace reservation was refused.");
            }
            Core::Containers::FixedArray<CookDependency, static_cast<size_t>(Codec::MaterialTextureSlot::Count)> dependencies = {};
            size_t                                                                                               count        = 0;
            for (size_t i = 0; i < document->Textures.size(); ++i)
            {
                const auto& texture = document->Textures[i];
                if (!texture.UUID.is_nil())
                {
                    dependencies[count++] = {
                        {CookAssetKind::Texture, texture.UUID},
                        Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(i)), texture.Path
                    };
                }
            }
            if (count > input.Limits->MaxEdges)
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Material dependency count exceeds the edge limit.");
            }
            Digest256  local_key = {};
            const auto hashed    = ComputeCookMaterialContentKey(scratch, *document, &local_key);
            if (hashed.Failed())
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, hashed.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(hashed.Error()), hashed.Error(), "Material semantic fingerprint failed.");
            }
            const auto assigned = output->Assign("zematerial-v1-json", local_key, metadata_key, {dependencies.data(), count}, scratch);
            return assigned.Failed() ? Detail::AdapterFailure(input, CookStage::Discover, diagnostics, Detail::AdapterErrorCode(assigned.Error()), assigned.Error(), "Material inspection storage failed.") : assigned;
        }

        struct MaterialOutput
        {
            ICookByteSink*              Sink      = nullptr;
            uint64_t                    Remaining = 0;

            static VFS::VFSResult<void> Write(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                auto& self = *static_cast<MaterialOutput*>(context);
                if (bytes.size() > self.Remaining)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                const auto written = self.Sink->Write(bytes);
                if (written.Succeeded())
                {
                    self.Remaining -= bytes.size();
                }
                return written;
            }
        };

        VFS::VFSResult<void> EncodeMaterial(const void*, const CookAssetInput& input, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, CookTarget target, ICookByteSink& sink, ICookAllocator& scratch, CookDiagnostics* diagnostics)
        {
            if (target != CookTarget::DesktopVulkanLE64)
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Material target is unsupported.");
            }
            if (dependencies.size() > static_cast<size_t>(Codec::MaterialTextureSlot::Count) || (dependencies.size() && !dependencies.data()))
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Material resolved dependency list is invalid.");
            }
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Material, CookStage::Encode, diagnostics, &metadata_key);
            if (metadata.Failed())
            {
                return metadata;
            }
            CookWorkspace                       workspace = {};
            const Codec::MaterialAssetDocument* document  = nullptr;
            const auto                          parsed    = ReadMaterial(input, scratch, workspace, &document, CookStage::Encode, diagnostics);
            if (parsed.Failed())
            {
                return parsed;
            }
            CookMemoryCharge fixed   = {};
            const auto       charged = scratch.TryCharge(sizeof(Core::Containers::FixedArray<VFS::VFSRelativePath, static_cast<size_t>(Codec::MaterialTextureSlot::Count)>) + sizeof(MaterialOutput) + sizeof(Core::Serialization::CanonicalJSONWriter) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
            if (charged.Failed())
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, charged.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(charged.Error()), charged.Error(), "Material output fixed workspace reservation was refused.");
            }
            Core::Containers::FixedArray<VFS::VFSRelativePath, static_cast<size_t>(Codec::MaterialTextureSlot::Count)> paths  = {};
            size_t                                                                                                     cursor = 0;
            for (size_t i = 0; i < document->Textures.size(); ++i)
            {
                const auto& texture = document->Textures[i];
                if (texture.UUID.is_nil())
                {
                    continue;
                }
                if (cursor == dependencies.size())
                {
                    return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::MissingDependency, VFS::VFSError::NotFound, "Material resolved slot is missing.");
                }
                const auto& child  = dependencies[cursor++];
                const auto* format = FindCookAssetFormat(child.Format);
                if (!(child.Slot == Codec::MaterialTextureSlotKey(static_cast<Codec::MaterialTextureSlot>(i))) || child.Identity != CookAssetIdentity{CookAssetKind::Texture, texture.UUID} || !format || format->Kind != CookAssetKind::Texture || !IsCookObjectDescriptorValid(*format, child.ObjectPath, child.ObjectDigest, child.ObjectSize))
                {
                    return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Material resolved slot, identity or object descriptor is invalid.");
                }
                if (child.ObjectSize > input.Limits->MaxEncodedAssetBytes)
                {
                    return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Material child object exceeds the encoded byte limit.");
                }
                paths[i] = child.ObjectPath;
            }
            if (cursor != dependencies.size())
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Material has extra resolved dependencies.");
            }
            MaterialOutput output  = {&sink, std::min(input.Limits->MaxMaterialBytes, input.Limits->MaxEncodedAssetBytes)};
            const auto     emitted = Codec::WriteCanonicalMaterialAsset(*document, paths, MaterialOutput::Write, &output);
            return emitted.Failed() ? Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(emitted.Error()), emitted.Error(), "Material output could not be completed.") : emitted;
        }
    } // namespace

    CookAdapterFns CookMaterialAdapter() noexcept
    {
        return {CookAssetKind::Material, 1, nullptr, InspectMaterial, EncodeMaterial};
    }
} // namespace ZEngine::Cook
