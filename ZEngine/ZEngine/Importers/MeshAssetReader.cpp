#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <fstream>
#include <limits>
#include <new>

using ZEngine::Core::Containers::Array;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::HashEntry;
using ZEngine::Core::Containers::String;
using ZEngine::Core::Containers::UnorderedHashMap;
using ZEngine::Core::Memory::ArenaAllocator;
using ZEngine::Core::Memory::ArenaCheckpoint;
using ZEngine::Core::Memory::ArenaScratch;
using ZEngine::Core::VFS::IVFSContext;
using ZEngine::Core::VFS::IVFSFile;
using ZEngine::Core::VFS::VFSError;
using ZEngine::Core::VFS::VFSOpenFlags;
using ZEngine::Core::VFS::VFSPath;
using ZEngine::Core::VFS::VFSResult;

namespace ZEngine::Importers::AssetCodec
{
    namespace
    {
        // v1 wrote native structs/counts. Its existing Windows/macOS/Linux 64-bit
        // little-endian layout is explicit here; this is not a portable v2 format.
        static_assert(std::endian::native == std::endian::little && sizeof(size_t) == 8);
        static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);
        static_assert(sizeof(uuids::uuid) == 16 && sizeof(AssetSubMesh) == 52);
        static_assert(sizeof(Helpers::NodeHierarchy) == 20 && sizeof(Core::Maths::Mat4f) == 64);

        struct Reader
        {
            AssetByteSource     Source        = {};
            ArenaAllocator*     Arena         = nullptr;
            MeshAssetReadLimits Limits        = {};
            uint64_t            Position      = 0;
            size_t              Start         = 0;
            size_t              StringBytes   = 0;
            VFSError            Error         = VFSError::OK;
            uint8_t             Buffer[16384] = {};
            uint64_t            BufferedAt    = 0;
            size_t              BufferedBytes = 0;
            bool                Measuring     = false;
            size_t              MeasuredBytes = 0;

            bool                Fail(VFSError error = VFSError::Corrupted)
            {
                if (Error == VFSError::OK)
                {
                    Error = error;
                }
                return false;
            }
            bool Bytes(void* output, size_t bytes)
            {
                if (bytes > Source.Size - Position)
                {
                    return Fail();
                }
                auto* destination = static_cast<uint8_t*>(output);
                while (bytes)
                {
                    if (Position >= BufferedAt && Position - BufferedAt < BufferedBytes)
                    {
                        const size_t at    = size_t(Position - BufferedAt);
                        const size_t count = std::min(bytes, BufferedBytes - at);
                        Helpers::secure_memcpy(destination, bytes, Buffer + at, count);
                        destination += count;
                        bytes       -= count;
                        Position    += count;
                        continue;
                    }
                    if (bytes < sizeof(Buffer))
                    {
                        const size_t count  = size_t(std::min(uint64_t(sizeof(Buffer)), Source.Size - Position));
                        const auto   result = Source.Read(Source.Context, {Buffer, count}, Position);
                        if (result.Failed())
                        {
                            return Fail(result.Error());
                        }
                        if (!result.Value() || result.Value() > count)
                        {
                            return Fail();
                        }
                        BufferedAt    = Position;
                        BufferedBytes = result.Value();
                        continue;
                    }
                    // Bound native stream requests even on implementations whose
                    // streamsize is narrower than a complete artifact payload.
                    const size_t count  = std::min(bytes, size_t(ZMega(1ULL)));
                    const auto   result = Source.Read(Source.Context, {destination, count}, Position);
                    if (result.Failed())
                    {
                        return Fail(result.Error());
                    }
                    if (!result.Value() || result.Value() > count)
                    {
                        return Fail();
                    }
                    Position    += result.Value();
                    destination += result.Value();
                    bytes       -= result.Value();
                }
                return true;
            }
            template <typename T>
            bool Scalar(T& value)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                return Bytes(&value, sizeof(value));
            }
            bool Budget(size_t bytes, size_t alignment)
            {
                if (!bytes)
                {
                    return true;
                }
                const size_t    used    = Measuring ? MeasuredBytes : Arena->m_current_offset - Start;
                const uintptr_t address = Measuring ? MeasuredBytes : reinterpret_cast<uintptr_t>(Arena->m_memory) + Arena->m_current_offset;
                const size_t    padding = (alignment - address % alignment) % alignment;
                if (used > Limits.MaxDecodedBytes || padding > Limits.MaxDecodedBytes - used || bytes > Limits.MaxDecodedBytes - used - padding)
                {
                    return Fail(VFSError::SizeLimitExceeded);
                }
                if (Measuring)
                {
                    MeasuredBytes += padding + bytes;
                }
                return true;
            }
            bool Skip(size_t bytes)
            {
                if (bytes > Source.Size - Position)
                {
                    return Fail();
                }
                Position += bytes;
                return true;
            }
            template <typename T>
            bool ArrayData(Array<T>& array, uint64_t maximum)
            {
                static_assert(std::is_trivially_copyable_v<T>);
                uint64_t count = 0;
                if (!Scalar(count))
                {
                    return false;
                }
                if (count > maximum)
                {
                    return Fail(VFSError::SizeLimitExceeded);
                }
                if (count > (Source.Size - Position) / sizeof(T))
                {
                    return Fail();
                }
                if (!Budget(size_t(count) * sizeof(T), ZAlignof<T>()))
                {
                    return false;
                }
                if (Measuring)
                {
                    array.m_size = size_t(count);
                    return Skip(size_t(count) * sizeof(T));
                }
                array.m_allocator = Arena;
                if (!array.try_reserve(size_t(count)))
                {
                    return Fail(VFSError::OutOfMemory);
                }
                array.m_size = size_t(count);
                return Bytes(array.data(), array.size() * sizeof(T));
            }
            bool Names(Array<String>& names)
            {
                uint32_t count = 0;
                if (!Scalar(count))
                {
                    return false;
                }
                if (count > Limits.MaxNames)
                {
                    return Fail(VFSError::SizeLimitExceeded);
                }
                if (count > (Source.Size - Position) / sizeof(uint64_t))
                {
                    return Fail();
                }
                if (!Budget(size_t(count) * sizeof(String), ZAlignof<String>()))
                {
                    return false;
                }
                names.m_allocator = Arena;
                if (!Measuring && !names.try_reserve(count))
                {
                    return Fail(VFSError::OutOfMemory);
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    uint64_t length = 0;
                    if (!Scalar(length))
                    {
                        return false;
                    }
                    if (length > Limits.MaxStringBytes || length + 1 > Limits.MaxStringTotalBytes - StringBytes)
                    {
                        return Fail(VFSError::SizeLimitExceeded);
                    }
                    StringBytes             += size_t(length) + 1;
                    const size_t wire_bytes  = length ? size_t(length) + 1 : 0;
                    if (wire_bytes > Source.Size - Position)
                    {
                        return Fail();
                    }
                    if (!Budget(size_t(length) + 1, alignof(char)))
                    {
                        return false;
                    }
                    if (Measuring)
                    {
                        if (!Skip(wire_bytes))
                        {
                            return false;
                        }
                        continue;
                    }
                    auto* data = static_cast<char*>(Arena->AllocateNoZero(size_t(length) + 1, alignof(char)));
                    if (!data)
                    {
                        return Fail(VFSError::OutOfMemory);
                    }
                    if (!Bytes(data, wire_bytes))
                    {
                        return false;
                    }
                    if (length && (data[length] != '\0' || !Helpers::IsValidUTF8(data, size_t(length))))
                    {
                        return Fail();
                    }
                    data[length]      = '\0';
                    auto* name        = new (&names.m_data[i]) String{};
                    name->m_allocator = Arena;
                    name->m_data      = data;
                    name->m_size      = size_t(length);
                    name->m_capacity  = size_t(length) + 1;
                    ++names.m_size;
                }
                if (Measuring)
                {
                    names.m_size = count;
                }
                return true;
            }
            bool Map(UnorderedHashMap<uint32_t, uint32_t>& map, size_t nodes, size_t values)
            {
                uint64_t count = 0;
                if (!Scalar(count))
                {
                    return false;
                }
                if (count > nodes || count > (Source.Size - Position) / (2 * sizeof(uint32_t)))
                {
                    return Fail();
                }
                size_t capacity = 16;
                while (capacity < count * 2)
                {
                    capacity *= 2;
                }
                if (!Budget(capacity * sizeof(HashEntry<uint32_t, uint32_t>), ZAlignof<HashEntry<uint32_t, uint32_t>>()))
                {
                    return false;
                }
                if (Measuring)
                {
                    return Skip(size_t(count) * 2 * sizeof(uint32_t));
                }
                if (!map.try_init(Arena, capacity))
                {
                    return Fail(VFSError::OutOfMemory);
                }
                for (uint64_t i = 0; i < count; ++i)
                {
                    uint32_t key = 0, value = 0;
                    if (!Scalar(key) || !Scalar(value))
                    {
                        return false;
                    }
                    if (key >= nodes || value >= values || map.contains(key))
                    {
                        return Fail();
                    }
                    map.insert(key, value);
                }
                return true;
            }
        };

        bool Validate(Reader& reader, const AssetMesh& mesh, const AssetNodeHierarchy& hierarchy)
        {
            if (reader.Measuring)
            {
                return reader.Budget(hierarchy.Hierarchies.size(), alignof(uint8_t));
            }
            if (mesh.Vertices.empty() || mesh.Vertices.size() % 8 || mesh.Indices.empty() || mesh.SubMeshes.empty() || hierarchy.Hierarchies.empty() || hierarchy.LocalTransforms.size() != hierarchy.Hierarchies.size() || hierarchy.GlobalTransforms.size() != hierarchy.Hierarchies.size())
            {
                return reader.Fail();
            }
            for (float value : mesh.Vertices)
            {
                if (!std::isfinite(value))
                {
                    return reader.Fail();
                }
            }
            const size_t vertices   = mesh.Vertices.size() / 8;
            size_t       next_index = 0;
            for (const auto& sub : mesh.SubMeshes)
            {
                // Index ranges partition the stream. Besides preventing hidden
                // unused indices, this bounds validation to O(V + I + S), not
                // repeated scans of attacker-controlled overlapping ranges.
                if (!sub.VertexCount || !sub.IndexCount || sub.IndexCount % 3 || sub.VertexOffset > vertices || sub.VertexCount > vertices - sub.VertexOffset || sub.IndexOffset != next_index || sub.IndexCount > mesh.Indices.size() - next_index || sub.VertexUnitStreamSize != 8 * sizeof(float) || sub.IndexUnitStreamSize != sizeof(uint32_t))
                {
                    return reader.Fail();
                }
                if (sub.StreamOffset != uint64_t(sub.VertexOffset) * sub.VertexUnitStreamSize || sub.IndexStreamOffset != uint64_t(sub.IndexOffset) * sub.IndexUnitStreamSize || sub.TotalByteSize != uint64_t(sub.VertexCount) * sub.VertexUnitStreamSize + uint64_t(sub.IndexCount) * sub.IndexUnitStreamSize)
                {
                    return reader.Fail();
                }
                for (size_t i = next_index; i < next_index + sub.IndexCount; ++i)
                {
                    if (mesh.Indices[i] >= sub.VertexCount)
                    {
                        return reader.Fail();
                    }
                }
                next_index += sub.IndexCount;
            }
            if (next_index != mesh.Indices.size())
            {
                return reader.Fail();
            }

            const auto count = hierarchy.Hierarchies.size();
            for (size_t i = 0; i < count; ++i)
            {
                const auto& node        = hierarchy.Hierarchies[i];
                const auto  index_valid = [&](int index) { return index == -1 || (index >= 0 && size_t(index) < count && size_t(index) != i); };
                if (!index_valid(node.Parent) || !index_valid(node.FirstChild) || !index_valid(node.LastChild) || !index_valid(node.RightSibling) || node.DepthLevel < 0 || size_t(node.DepthLevel) >= count)
                {
                    return reader.Fail();
                }
                if (node.Parent == -1)
                {
                    if (node.DepthLevel != 0 || node.RightSibling != -1)
                    {
                        return reader.Fail();
                    }
                }
                else
                {
                    const int depth = hierarchy.Hierarchies[node.Parent].DepthLevel;
                    if (depth < 0 || size_t(depth) >= count || node.DepthLevel != depth + 1)
                    {
                        return reader.Fail();
                    }
                }
                for (const auto* matrix : {&hierarchy.LocalTransforms[i], &hierarchy.GlobalTransforms[i]})
                {
                    for (const auto& column : matrix->m_data)
                    {
                        for (float value : column)
                        {
                            if (!std::isfinite(value))
                            {
                                return reader.Fail();
                            }
                        }
                    }
                }
            }
            // Child/sibling consistency in O(N), with one byte per node of
            // temporary state. Parent-depth validation above already excludes
            // cycles without recursive DFS or walking every ancestor chain.
            if (!reader.Budget(count, alignof(uint8_t)))
            {
                return false;
            }
            ArenaScratch temporary(reader.Arena);
            auto*        seen = static_cast<uint8_t*>(temporary.Arena()->Allocate(count, alignof(uint8_t)));
            if (!seen)
            {
                return reader.Fail(VFSError::OutOfMemory);
            }
            for (size_t i = 0; i < count; ++i)
            {
                int last = -1;
                for (int child = hierarchy.Hierarchies[i].FirstChild; child != -1; child = hierarchy.Hierarchies[child].RightSibling)
                {
                    if (seen[child] || hierarchy.Hierarchies[child].Parent != int(i))
                    {
                        return reader.Fail();
                    }
                    seen[child] = 1;
                    last        = child;
                }
                if (hierarchy.Hierarchies[i].LastChild != -1 && hierarchy.Hierarchies[i].LastChild != last)
                {
                    return reader.Fail();
                }
            }
            for (size_t i = 0; i < count; ++i)
            {
                if ((hierarchy.Hierarchies[i].Parent != -1) != (seen[i] != 0))
                {
                    return reader.Fail();
                }
            }
            return true;
        }

        VFSResult<void> ReadPayload(Reader& reader, AssetMesh& mesh, AssetNodeHierarchy& hierarchy, const uuids::uuid& expected_uuid)
        {
            uint32_t magic = 0, version = 0;
            if (!reader.Scalar(magic) || !reader.Scalar(version) || magic != ZEMESH_MAGIC)
            {
                return VFSResult<void>::Fail(reader.Error == VFSError::OK ? VFSError::Corrupted : reader.Error);
            }
            if (version != ASSET_FILE_VERSION)
            {
                return VFSResult<void>::Fail(VFSError::Unsupported);
            }
            if (!reader.Scalar(mesh.MeshUUID) || mesh.MeshUUID.is_nil() || (!expected_uuid.is_nil() && mesh.MeshUUID != expected_uuid))
            {
                return VFSResult<void>::Fail(reader.Error == VFSError::OK ? VFSError::Corrupted : reader.Error);
            }
            const auto& limits = reader.Limits;
            if (!reader.ArrayData(mesh.Vertices, uint64_t(limits.MaxVertices) * 8) || !reader.ArrayData(mesh.Indices, limits.MaxIndices) || !reader.ArrayData(mesh.SubMeshes, limits.MaxSubMeshes) || !reader.Scalar(hierarchy.NodeHierarchyUUID) || !reader.Scalar(hierarchy.MeshUUID))
            {
                return VFSResult<void>::Fail(reader.Error);
            }
            if (hierarchy.NodeHierarchyUUID.is_nil() || hierarchy.NodeHierarchyUUID == mesh.MeshUUID || hierarchy.MeshUUID != mesh.MeshUUID)
            {
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }
            if (!reader.ArrayData(hierarchy.Hierarchies, limits.MaxNodes) || !reader.ArrayData(hierarchy.LocalTransforms, limits.MaxNodes) || !reader.ArrayData(hierarchy.GlobalTransforms, limits.MaxNodes) || !reader.Names(hierarchy.Names) || !reader.Names(hierarchy.MaterialNames) || !reader.Map(hierarchy.NodeNames, hierarchy.Hierarchies.size(), hierarchy.Names.size()) || !reader.Map(hierarchy.NodeMeshes, hierarchy.Hierarchies.size(), mesh.SubMeshes.size()) || !reader.Map(hierarchy.NodeMaterials, hierarchy.Hierarchies.size(), hierarchy.MaterialNames.size()) ||
                !Validate(reader, mesh, hierarchy))
            {
                return VFSResult<void>::Fail(reader.Error);
            }
            if (reader.Position != reader.Source.Size)
            {
                return VFSResult<void>::Fail(VFSError::Corrupted);
            }
            return VFSResult<void>::Ok();
        }
    } // namespace

    size_t MeshAssetReadFixedWorkingBytes() noexcept
    {
        return sizeof(Reader) + sizeof(AssetMesh) + sizeof(AssetNodeHierarchy) + sizeof(ArenaCheckpoint) + sizeof(ArenaScratch);
    }

    VFSResult<void> MeasureMeshAssetReadWorkspace(const AssetByteSource& input, size_t* output, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        if (!output || !input.Read)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (input.Size > limits.MaxFileBytes)
        {
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        }
        Reader             reader{.Source = input, .Limits = limits, .Measuring = true};
        AssetMesh          mesh      = {};
        AssetNodeHierarchy hierarchy = {};
        const auto         measured  = ReadPayload(reader, mesh, hierarchy, expected_uuid);
        if (measured.Failed())
        {
            return measured;
        }
        *output = reader.MeasuredBytes;
        return VFSResult<void>::Ok();
    }

    VFSResult<void> ReadMeshAsset(const AssetByteSource& input, ArenaAllocator* arena, AssetMesh* out_mesh, AssetNodeHierarchy* out_hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        if (!arena || !arena->m_memory || !out_mesh || !out_hierarchy || !input.Read || arena->m_current_offset > arena->m_total_size)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        if (input.Size > limits.MaxFileBytes)
        {
            return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
        }
        ArenaCheckpoint    transaction(arena);
        Reader             reader{.Source = input, .Arena = arena, .Limits = limits, .Start = arena->m_current_offset};
        AssetMesh          mesh      = {};
        AssetNodeHierarchy hierarchy = {};
        const auto         decoded   = ReadPayload(reader, mesh, hierarchy, expected_uuid);
        if (decoded.Failed())
        {
            return decoded;
        }
        *out_mesh      = std::move(mesh);
        *out_hierarchy = std::move(hierarchy);
        transaction.Commit();
        return VFSResult<void>::Ok();
    }

    VFSResult<void> ReadMeshAssetFileVFS(ArenaAllocator* arena, IVFSContext& vfs, const VFSPath& path, AssetMesh* mesh, AssetNodeHierarchy* hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        if (!arena || !arena->m_memory || !mesh || !hierarchy)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        ArenaCheckpoint transaction(arena);
        const auto      opened = vfs.Open(path, VFSOpenFlags::Read);
        if (opened.Failed())
        {
            return VFSResult<void>::Fail(opened.Error());
        }
        auto* file = opened.Value();
        if (!file)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        const auto size = file->Size();
        if (size.Failed())
        {
            (void) vfs.CloseChecked(file);
            return VFSResult<void>::Fail(size.Error());
        }
        AssetByteSource    input             = {.Context = file, .Read = [](void* context, ArrayView<uint8_t> bytes, uint64_t offset) { return static_cast<IVFSFile*>(context)->Read(bytes, offset); }, .Size = size.Value()};
        AssetMesh          pending_mesh      = {};
        AssetNodeHierarchy pending_hierarchy = {};
        const auto         decoded           = ReadMeshAsset(input, arena, &pending_mesh, &pending_hierarchy, expected_uuid, limits);
        const auto         closed            = vfs.CloseChecked(file);
        if (decoded.Failed())
        {
            return decoded;
        }
        if (closed.Failed())
        {
            return closed;
        }
        *mesh      = std::move(pending_mesh);
        *hierarchy = std::move(pending_hierarchy);
        transaction.Commit();
        return VFSResult<void>::Ok();
    }

    VFSResult<void> ReadMeshAssetFile(ArenaAllocator* arena, const char* path, AssetMesh* mesh, AssetNodeHierarchy* hierarchy, const uuids::uuid& expected_uuid, const MeshAssetReadLimits& limits)
    {
        if (!path || !path[0] || !arena || !arena->m_memory || !mesh || !hierarchy)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        ArenaCheckpoint transaction(arena);
        std::ifstream   file(path, std::ios::binary | std::ios::ate);
        if (!file.is_open())
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        const auto end = file.tellg();
        if (end < 0)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        AssetByteSource input = {
            .Context = &file,
            .Read =
                [](void* context, ArrayView<uint8_t> bytes, uint64_t offset) {
                    auto& source = *static_cast<std::ifstream*>(context);
                    source.clear();
                    source.seekg(static_cast<std::streamoff>(offset));
                    if (!source.good())
                    {
                        return VFSResult<size_t>::Fail(VFSError::IOError);
                    }
                    source.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                    if (source.bad())
                    {
                        return VFSResult<size_t>::Fail(VFSError::IOError);
                    }
                    return VFSResult<size_t>::Ok(static_cast<size_t>(source.gcount()));
                },
            .Size = static_cast<uint64_t>(end)};
        AssetMesh          pending_mesh      = {};
        AssetNodeHierarchy pending_hierarchy = {};
        const auto         decoded           = ReadMeshAsset(input, arena, &pending_mesh, &pending_hierarchy, expected_uuid, limits);
        file.clear();
        file.close();
        if (decoded.Failed())
        {
            return decoded;
        }
        if (file.fail())
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        *mesh      = std::move(pending_mesh);
        *hierarchy = std::move(pending_hierarchy);
        transaction.Commit();
        return VFSResult<void>::Ok();
    }

} // namespace ZEngine::Importers::AssetCodec
