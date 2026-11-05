#include <ZEngine/Cook/CookAdapterSupport.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <algorithm>
#include <charconv>
#include <new>

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        constexpr cstring MeshSlotPrefix   = "submeshes/";
        constexpr cstring MeshSlotSuffix   = "/material";
        constexpr size_t  MeshSlotCapacity = sizeof("submeshes/4294967295/material");

        // Visit [0, count) in decimal lexical order: 0, 1, 10, 100, ... .
        // Descend to a present child, otherwise skip exhausted suffixes and
        // visit the next sibling. Each decimal-tree edge is traversed at most
        // twice: O(count), constant scalar storage, no recursion or sort.
        // A parent's '/' suffix sorts before its children's decimal digits.
        size_t            NextMeshSlotIndex(size_t index, size_t count) noexcept
        {
            if (index && index <= (count - 1) / 10)
                return index * 10;
            while (index && (index % 10 == 9 || index + 1 >= count))
                index /= 10;
            return index + 1;
        }

        Core::Containers::StringView WriteMeshSlot(uint32_t index, char* bytes)
        {
            constexpr size_t prefix = sizeof("submeshes/") - 1;
            Helpers::secure_memcpy(bytes, MeshSlotCapacity, MeshSlotPrefix, prefix);
            const auto number = std::to_chars(bytes + prefix, bytes + MeshSlotCapacity - sizeof("/material"), index);
            if (number.ec != std::errc{})
                return {};
            const size_t offset = static_cast<size_t>(number.ptr - bytes);
            Helpers::secure_memcpy(number.ptr, MeshSlotCapacity - offset, MeshSlotSuffix, sizeof("/material"));
            return {bytes, offset + sizeof("/material") - 1};
        }

        bool ReadMeshSlot(Core::Containers::StringView text, uint32_t* output)
        {
            constexpr size_t prefix = sizeof("submeshes/") - 1, suffix = sizeof("/material") - 1;
            if (!text.data() || text.size() < prefix + 1 + suffix || text.size() >= MeshSlotCapacity || !(Core::Containers::StringView(text.data(), prefix) == MeshSlotPrefix) || !(Core::Containers::StringView(text.data() + text.size() - suffix, suffix) == MeshSlotSuffix))
                return false;
            const auto* first = text.data() + prefix;
            const auto* last  = text.data() + text.size() - suffix;
            if (*first < '0' || *first > '9' || (*first == '0' && first + 1 != last))
                return false;
            uint32_t   index  = 0;
            const auto parsed = std::from_chars(first, last, index);
            if (parsed.ec != std::errc{} || parsed.ptr != last)
                return false;
            *output = index;
            return true;
        }

        VFS::VFSResult<void> ReadMesh(const CookAssetInput& input, ICookAllocator& scratch, CookWorkspace& workspace, Importers::AssetMesh* mesh, Importers::AssetNodeHierarchy* hierarchy, CookStage stage, CookDiagnostics* diagnostics)
        {
            CookMemoryCharge fixed   = {};
            const auto       charged = scratch.TryCharge(Codec::MeshAssetReadFixedWorkingBytes() + sizeof(Codec::MeshAssetReadLimits) + sizeof(Detail::CapturedByteSource) + sizeof(Codec::AssetByteSource) + sizeof(fixed), CookMemoryCategory::Codecs, &fixed);
            if (charged.Failed())
                return Detail::AdapterFailure(input, stage, diagnostics, charged.Error() == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : Detail::AdapterErrorCode(charged.Error()), charged.Error(), "Mesh reader fixed working reservation was refused.");
            Codec::MeshAssetReadLimits limits = {};
            limits.MaxFileBytes               = std::min(limits.MaxFileBytes, input.Limits->MaxEncodedAssetBytes);
            limits.MaxDecodedBytes            = static_cast<size_t>(std::min<uint64_t>(limits.MaxDecodedBytes, input.Limits->MaxDecodedAssetBytes));
            Detail::CapturedByteSource bridge(*input.Artifact);
            const auto                 source   = bridge.View();
            size_t                     capacity = 0;
            const auto                 measured = Codec::MeasureMeshAssetReadWorkspace(source, &capacity, input.Identity.UUID, limits);
            if (measured.Failed())
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(measured.Error()), measured.Error(), "Mesh layout does not satisfy the bounded current wire schema.");
            const auto allocated = workspace.Initialize(scratch, capacity, CookMemoryCategory::Codecs);
            if (allocated.Failed())
                return Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(allocated.Error()), allocated.Error(), "Mesh decoded workspace allocation was refused.");
            const auto read = Codec::ReadMeshAsset(source, workspace.Arena(), mesh, hierarchy, input.Identity.UUID, limits);
            return read.Failed() ? Detail::AdapterFailure(input, stage, diagnostics, Detail::AdapterErrorCode(read.Error()), read.Error(), "Mesh validation failed before output.") : read;
        }

        VFS::VFSResult<size_t> MaterialCount(const Importers::AssetMesh& mesh, const CookLimits& limits)
        {
            size_t count = 0;
            for (const auto& submesh : mesh.SubMeshes)
                if (!submesh.MaterialUUID.is_nil())
                    ++count;
            return count > limits.MaxEdges ? VFS::VFSResult<size_t>::Fail(VFS::VFSError::SizeLimitExceeded) : VFS::VFSResult<size_t>::Ok(count);
        }

        VFS::VFSResult<void> InspectMesh(const void*, const CookAssetInput& input, ICookAllocator& scratch, CookAssetInspection* output, CookDiagnostics* diagnostics)
        {
            if (!output)
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::InvalidRequest, VFS::VFSError::InvalidPath, "Mesh inspection requires result storage.");
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Mesh, CookStage::Discover, diagnostics, &metadata_key);
            if (metadata.Failed())
                return metadata;
            CookWorkspace                 decoded   = {};
            Importers::AssetMesh          mesh      = {};
            Importers::AssetNodeHierarchy hierarchy = {};
            const auto                    read      = ReadMesh(input, scratch, decoded, &mesh, &hierarchy, CookStage::Discover, diagnostics);
            if (read.Failed())
                return read;
            const auto counted = MaterialCount(mesh, *input.Limits);
            if (counted.Failed())
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::BudgetExceeded, counted.Error(), "Mesh material dependencies exceed the edge limit.");
            const size_t count = counted.Value();
            if (!count)
                return output->Assign("zemesh-v1-le64", input.Artifact->Digest(), metadata_key, {}, scratch);
            if (count > (SIZE_MAX - DEFAULT_ALIGNMENT) / (sizeof(CookDependency) + MeshSlotCapacity))
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Mesh dependency scratch size overflowed.");
            CookWorkspace temporary = {};
            const auto    allocated = temporary.Initialize(scratch, count * (sizeof(CookDependency) + MeshSlotCapacity) + DEFAULT_ALIGNMENT, CookMemoryCategory::Codecs);
            if (allocated.Failed())
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, Detail::AdapterErrorCode(allocated.Error()), allocated.Error(), "Mesh dependency scratch allocation failed.");
            Core::Containers::Array<CookDependency> dependencies = {};
            dependencies.m_allocator                             = temporary.Arena();
            if (!dependencies.try_reserve(count))
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Mesh dependency arena capacity was exhausted.");
            auto* slots = static_cast<char*>(temporary.Arena()->AllocateNoZero(count * MeshSlotCapacity, alignof(char)));
            if (!slots)
                return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::BudgetExceeded, VFS::VFSError::SizeLimitExceeded, "Mesh dependency slot storage was exhausted.");
            // Emit wire order directly, including children of nil-material
            // submeshes. Slots still contain the original numeric mesh index.
            for (size_t i = 0, visited = 0; visited < mesh.SubMeshes.size(); ++visited, i = NextMeshSlotIndex(i, mesh.SubMeshes.size()))
            {
                const auto& submesh = mesh.SubMeshes[i];
                if (submesh.MaterialUUID.is_nil())
                    continue;
                const auto slot = WriteMeshSlot(static_cast<uint32_t>(i), slots + dependencies.size() * MeshSlotCapacity);
                if (!slot.size())
                    return Detail::AdapterFailure(input, CookStage::Discover, diagnostics, CookDiagnosticCode::InvalidAsset, VFS::VFSError::Corrupted, "Mesh material slot could not be represented.");
                new (dependencies.m_data + dependencies.m_size++) CookDependency{
                    {CookAssetKind::Material, submesh.MaterialUUID},
                    slot, {}
                };
            }
            const auto assigned = output->Assign("zemesh-v1-le64", input.Artifact->Digest(), metadata_key, dependencies, scratch);
            return assigned.Failed() ? Detail::AdapterFailure(input, CookStage::Discover, diagnostics, Detail::AdapterErrorCode(assigned.Error()), assigned.Error(), "Mesh inspection storage failed.") : assigned;
        }

        VFS::VFSResult<void> ValidateResolvedMesh(const Importers::AssetMesh& mesh, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, const CookLimits& limits)
        {
            const auto counted = MaterialCount(mesh, limits);
            if (counted.Failed())
                return VFS::VFSResult<void>::Fail(counted.Error());
            if (dependencies.size() != counted.Value() || (dependencies.size() && !dependencies.data()))
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            for (size_t i = 0; i < dependencies.size(); ++i)
            {
                const auto& child   = dependencies[i];
                uint32_t    submesh = 0;
                if (!ReadMeshSlot(child.Slot, &submesh) || submesh >= mesh.SubMeshes.size() || (i && !CookDependencySlotBefore(dependencies[i - 1].Slot, child.Slot)) || !child.Identity.IsValid() || child.Identity.Kind != CookAssetKind::Material || child.Identity.UUID != mesh.SubMeshes[submesh].MaterialUUID)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                const auto* format = FindCookAssetFormat(child.Format);
                if (!format || format->Kind != CookAssetKind::Material || !IsCookObjectDescriptorValid(*format, child.ObjectPath, child.ObjectDigest, child.ObjectSize))
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                if (child.ObjectSize > limits.MaxMaterialBytes || child.ObjectSize > limits.MaxEncodedAssetBytes)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            return VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> EncodeMesh(const void*, const CookAssetInput& input, Core::Containers::ArrayView<const CookResolvedDependency> dependencies, CookTarget target, ICookByteSink& sink, ICookAllocator& scratch, CookDiagnostics* diagnostics)
        {
            if (target != CookTarget::DesktopVulkanLE64)
                return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, CookDiagnosticCode::UnsupportedTarget, VFS::VFSError::Unsupported, "Mesh target is unsupported.");
            Digest256  metadata_key = {};
            const auto metadata     = Detail::ValidateAdapterInput(scratch, input, CookAssetKind::Mesh, CookStage::Encode, diagnostics, &metadata_key);
            if (metadata.Failed())
                return metadata;
            CookWorkspace workspace = {};
            {
                Importers::AssetMesh          mesh      = {};
                Importers::AssetNodeHierarchy hierarchy = {};
                const auto                    read      = ReadMesh(input, scratch, workspace, &mesh, &hierarchy, CookStage::Encode, diagnostics);
                if (read.Failed())
                    return read;
                const auto resolved = ValidateResolvedMesh(mesh, dependencies, *input.Limits);
                if (resolved.Failed())
                    return Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(resolved.Error()), resolved.Error(), "Mesh resolved material slots, identities or object descriptors are invalid.");
            }
            // Every decoded container is destroyed before rewinding. Reuse the
            // same already-charged storage for passthrough; no encoded copy or
            // additional stream allocation. No input lease or native reopen.
            auto* arena = workspace.Arena();
            arena->Clear();
            const size_t count  = std::min(arena->m_total_size, size_t(ZKilo(64)));
            auto*        stream = static_cast<uint8_t*>(arena->AllocateNoZero(count, alignof(uint8_t)));
            const auto   copied = Detail::CopyCapturedAsset(*input.Artifact, sink, {stream, count}, input.Limits->MaxEncodedAssetBytes, scratch);
            return copied.Failed() ? Detail::AdapterFailure(input, CookStage::Encode, diagnostics, Detail::AdapterErrorCode(copied.Error()), copied.Error(), "Validated mesh passthrough could not be completed.") : copied;
        }
    } // namespace

    CookAdapterFns CookMeshAdapter() noexcept
    {
        return {CookAssetKind::Mesh, 1, nullptr, InspectMesh, EncodeMesh};
    }
} // namespace ZEngine::Cook
