#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Cook/CookInputPath.h>
#include <ZEngine/Core/Hash/SourceContentHash.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        VFS::VFSResult<void> EnvironmentSettings(const VFS::StrictMetaFileData& metadata, float* exposure)
        {
            if (!metadata.Has(VFS::MetaFileField::ArtifactPath) || !metadata.Has(VFS::MetaFileField::SourceHash) || metadata.Data.SettingsCount != 6)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            const auto path = ParseCookInputPath(metadata.Data.ArtifactPath);
            if (path.Failed())
            {
                return VFS::VFSResult<void>::Fail(path.Error());
            }
            const auto                         text = path.Value().Logical.IsValid() ? path.Value().Logical.View() : Core::Containers::StringView(path.Value().Context.CStr(), path.Value().Context.Length());
            const Core::Containers::StringView extension(".zenvmap");
            if (text.size() < extension.size())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
            }
            for (size_t i = 0; i < extension.size(); ++i)
            {
                const char value = text[text.size() - extension.size() + i];
                if ((value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value) != extension[i])
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
                }
            }
            constexpr cstring keys[]   = {"artifact_version", "pixel_format", "color_space", "orientation", "mip_policy", "exposure"};
            constexpr cstring values[] = {"2", "rgba32f", "linear_scene", "renderer_canonical_v1", "generate_on_gpu"};
            uint32_t          seen     = 0;
            for (const auto& setting : Core::Containers::ArrayView<const VFS::MetaKeyValuePair>(metadata.Data.Settings, 6))
            {
                size_t index = 0;
                while (index < 6 && !(Core::Containers::StringView(setting.Key) == keys[index]))
                {
                    ++index;
                }
                if (index == 6 || (seen & (1u << index)))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
                seen |= 1u << index;
                const Core::Containers::StringView value(setting.Value);
                if (index < 5)
                {
                    if (!(value == values[index]))
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
                    }
                }
                else
                {
                    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), *exposure, std::chars_format::general);
                    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || !std::isfinite(*exposure) || *exposure <= 0)
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                    }
                }
            }
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> ValidateEnvironment(const CookAssetInput& input, Core::Containers::ArrayView<uint8_t> stream, float exposure, ICookAllocator& scratch)
        {
            if (!input.EnvironmentSource->Size())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            if (input.EnvironmentSource->Size() > input.Limits->MaxEncodedAssetBytes)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            // The receipt has its own fixed 4096-byte partial-chunk state;
            // it is not contained in the separately charged stream slab.
            CookMemoryCharge fixed   = {};
            const auto       charged = scratch.TryCharge(sizeof(Core::Hash::SourceContentHash) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Digest256) + sizeof(uint64_t) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
            if (charged.Failed())
            {
                return charged;
            }
            uint64_t imported_hash = 0;
            {
                Core::Hash::SourceContentHash source_hash   = {};
                Core::Hash::SHA256            source_digest = {};
                for (uint64_t offset = 0; offset < input.EnvironmentSource->Size();)
                {
                    const size_t count = static_cast<size_t>(std::min<uint64_t>(stream.size(), input.EnvironmentSource->Size() - offset));
                    const auto   bytes = stream.first(count);
                    const auto   read  = ReadCookCapturedRange(*input.EnvironmentSource, offset, bytes);
                    if (read.Failed())
                    {
                        return read;
                    }
                    const auto hashed = source_hash.Update(bytes);
                    if (hashed.Failed())
                    {
                        return hashed;
                    }
                    const auto digested = source_digest.Update(bytes);
                    if (digested.Failed())
                    {
                        return digested;
                    }
                    offset += count;
                }
                Digest256  digest   = {};
                const auto hashed   = source_hash.Finalize(&imported_hash);
                const auto digested = source_digest.Finalize(&digest);
                if (hashed.Failed() || digested.Failed())
                {
                    return VFS::VFSResult<void>::Fail(hashed.Failed() ? hashed.Error() : digested.Error());
                }
                if (imported_hash != input.Metadata->Data.SourceHash || digest != input.EnvironmentSource->Digest())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            Detail::CapturedByteSource            bridge(*input.Artifact);
            Codec::EnvironmentMapFileHeader       header    = {};
            const Codec::EnvironmentMapReadLimits limits    = {input.Limits->MaxEncodedAssetBytes, input.Limits->MaxDecodedAssetBytes, input.Limits->MaxImageDimension};
            const auto                            validated = Codec::ReadEnvironmentMapAsset(bridge.View(), stream, &header, imported_hash, limits);
            if (validated.Failed())
            {
                return validated;
            }
            return header.Exposure == exposure ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }

        VFS::VFSResult<void> PrepareEnvironment(const CookAssetInput& input, ICookAllocator& scratch, CookStage stage, CookDiagnostics* diagnostics, CookWorkspace& workspace, Core::Containers::ArrayView<uint8_t>* stream, Digest256* metadata_key)
        {
            const auto metadata = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::EnvironmentMap, stage, diagnostics, metadata_key);
            if (metadata.Failed())
            {
                return metadata;
            }
            float      exposure = 0;
            const auto settings = EnvironmentSettings(*input.Metadata, &exposure);
            if (settings.Failed())
            {
                return Detail::AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::InvalidMetadata, settings.Error(), "Environment metadata must declare the exact current artifact contract.");
            }
            const uint64_t maximum = std::max(input.Artifact->Size(), input.EnvironmentSource->Size());
            if (!maximum || maximum > input.Limits->MaxEncodedAssetBytes)
            {
                return Detail::AdapterFailure(input, stage, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Environment capture exceeds its encoded limit.");
            }
            const size_t count     = static_cast<size_t>(std::min<uint64_t>(maximum, ZKilo(64))) & ~(sizeof(float) - 1);
            const auto   allocated = workspace.Initialize(scratch, std::max(count, sizeof(float)), CookMemoryCategory::Codecs);
            if (allocated.Failed())
            {
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(allocated.Error()), allocated.Error(), "Environment validation stream allocation failed.");
            }
            const size_t capacity = workspace.Arena()->m_total_size;
            *stream               = {static_cast<uint8_t*>(workspace.Arena()->AllocateNoZero(capacity, alignof(uint8_t))), capacity};
            const auto validated  = ValidateEnvironment(input, *stream, exposure, scratch);
            return validated.Failed() ? Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(validated.Error()), validated.Error(), "Environment artifact, captured source hash, settings or complete float payload is invalid; reimport upstream.") : validated;
        }

        VFS::VFSResult<void> InspectEnvironment(const void*, const CookAssetInput& input, ICookAllocator& scratch, CookAssetInspection* output, CookDiagnostics* diagnostics)
        {
            if (!output)
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Environment inspection requires result storage.");
            }
            CookWorkspace                        workspace    = {};
            Core::Containers::ArrayView<uint8_t> stream       = {};
            Digest256                            metadata_key = {};
            const auto                           prepared     = PrepareEnvironment(input, scratch, CookStage::Discover, diagnostics, workspace, &stream, &metadata_key);
            if (prepared.Failed())
            {
                return prepared;
            }
            CookMemoryCharge fixed   = {};
            const auto       charged = scratch.TryCharge(CookFingerprint::FixedWorkingBytes() + sizeof(Digest256) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
            if (charged.Failed())
            {
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, charged.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(charged.Error()), charged.Error(), "Environment semantic fingerprint reservation was refused.");
            }
            CookFingerprint key = {};
            (void) key.String("ze.cook.environment.v1");
            (void) key.Digest(input.EnvironmentSource->Digest());
            (void) key.Digest(input.Artifact->Digest());
            Digest256  local_key = {};
            const auto finalized = key.Finalize(&local_key);
            return finalized.Failed() ? finalized : output->Assign("zenvmap-v2-le64", local_key, metadata_key, {}, scratch);
        }

        VFS::VFSResult<void> EncodeEnvironment(const void*, const CookAssetInput& input, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, CookTarget target, ICookByteSink& sink, ICookAllocator& scratch, CookDiagnostics* diagnostics)
        {
            if (target != CookTarget::DesktopVulkanLE64)
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Environment target is unsupported.");
            }
            if (dependencies.size())
            {
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Environment artifacts are leaves.");
            }
            CookWorkspace                        workspace    = {};
            Core::Containers::ArrayView<uint8_t> stream       = {};
            Digest256                            metadata_key = {};
            const auto                           prepared     = PrepareEnvironment(input, scratch, CookStage::Encode, diagnostics, workspace, &stream, &metadata_key);
            if (prepared.Failed())
            {
                return prepared;
            }
            const auto copied = Detail::CopyCapturedAsset(*input.Artifact, sink, stream, input.Limits->MaxEncodedAssetBytes, scratch);
            return copied.Failed() ? Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(copied.Error()), copied.Error(), "Validated environment passthrough could not be completed.") : copied;
        }
    } // namespace

    CookAdapterFns CookEnvironmentAdapter() noexcept
    {
        return {CookAssetKind::EnvironmentMap, 1, nullptr, InspectEnvironment, EncodeEnvironment};
    }
} // namespace ZEngine::Cook
