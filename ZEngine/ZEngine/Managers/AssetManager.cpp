#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/IntrusivePtr.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Profiling/Profiling.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <new>
#include <random>
#include <type_traits>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Importers;

namespace ZEngine::Managers
{
    static AssetManager* s_Instance = nullptr;

    namespace
    {
        struct PayloadStorageDeleter
        {
            void operator()(AssetManager::AssetPayloadAllocation* storage) const
            {
                AssetManager::AssetPayloadAllocation::Destroy(storage);
            }
        };
        using PayloadStorage = std::unique_ptr<AssetManager::AssetPayloadAllocation, PayloadStorageDeleter>;

        PayloadStorage AllocatePayload(TLSFSlab& slab, size_t bytes)
        {
            bytes = std::max(size_t(1), bytes);
            if (bytes > SIZE_MAX - sizeof(AssetManager::AssetPayloadAllocation))
            {
                return {};
            }
            auto* block = slab.TryRealloc(nullptr, sizeof(AssetManager::AssetPayloadAllocation) + bytes);
            if (!block)
            {
                return {};
            }
            PayloadStorage storage(new (block) AssetManager::AssetPayloadAllocation{});
            storage->Slab = &slab;
            if (!storage->Arena.InitializeBorrowed(static_cast<char*>(block) + sizeof(AssetManager::AssetPayloadAllocation), bytes, "AssetManager/Payload"))
            {
                return {};
            }
            return storage;
        }

        PayloadStorage CloneMaterial(TLSFSlab& slab, const AssetMaterial& source, AssetMaterial& destination)
        {
            const String* strings[] = {&source.Name, &source.AlbedoTexPath, &source.EmissiveTexPath, &source.NormalTexPath, &source.OpacityTexPath, &source.SpecularTexPath};
            size_t        bytes     = 0;
            for (const auto* string : strings)
            {
                if (string->size() == SIZE_MAX || string->size() + 1 > SIZE_MAX - bytes)
                {
                    return {};
                }
                bytes += string->size() + 1;
            }
            auto storage = AllocatePayload(slab, bytes);
            if (!storage)
            {
                return {};
            }
            destination      = source;
            String* copies[] = {&destination.Name, &destination.AlbedoTexPath, &destination.EmissiveTexPath, &destination.NormalTexPath, &destination.OpacityTexPath, &destination.SpecularTexPath};
            for (size_t index = 0; index < std::size(strings); ++index)
            {
                if (!copies[index]->try_init(&storage->Arena, strings[index]->c_str()))
                {
                    return {};
                }
            }
            return storage;
        }

        template <typename T>
        bool ReserveMeshSlot(Array<T>& slots, size_t index)
        {
            if (index < slots.capacity())
                return true;
            if (index == SIZE_MAX || slots.capacity() > SIZE_MAX / 2)
                return false;
            // Reserve geometrically, not one cell per import: moving the whole
            // descriptor table on each append would make large batches quadratic.
            return slots.try_reserve(std::max(index + 1, std::max(size_t(4), slots.capacity() * 2)));
        }

        using NodeMap = UnorderedHashMap<uint32_t, uint32_t>;

        // Immutable copies need space for live entries, not the importer's often
        // oversized hash table. Keep the same <= 50% occupancy contract as init.
        size_t MapSlots(const NodeMap& source)
        {
            if (source.empty() || source.size() > SIZE_MAX / 2)
                return 0;
            size_t slots = 16;
            while (slots < source.size() * 2)
            {
                if (slots > SIZE_MAX / 2)
                    return 0;
                slots *= 2;
            }
            return slots;
        }

        struct PayloadSize
        {
            size_t Bytes = 0;

            template <typename T>
            bool Add(size_t count)
            {
                if (!count)
                    return true;
                constexpr auto padding = alignof(T) - 1;
                if (Bytes > SIZE_MAX - padding || count > (SIZE_MAX - Bytes - padding) / sizeof(T))
                    return false;
                Bytes += padding + count * sizeof(T);
                return true;
            }

            bool Names(const Array<String>& names)
            {
                if (!Add<String>(names.size()))
                    return false;
                for (const auto& name : names)
                    if (name.size() == SIZE_MAX || !Add<char>(name.size() + 1))
                        return false;
                return true;
            }

            bool Map(const NodeMap& map)
            {
                const auto slots = MapSlots(map);
                return (map.empty() || slots) && Add<NodeMap::Entry>(slots);
            }
        };

        template <typename T>
        bool CopyArray(const Array<T>& source, Array<T>& destination, ArenaAllocator& arena)
        {
            static_assert(std::is_trivially_copyable_v<T>);
            destination.m_allocator = &arena;
            if (source.empty())
                return true;
            if (source.size() > SIZE_MAX / sizeof(T))
                return false;
            auto* data = static_cast<T*>(arena.AllocateNoZero(source.size() * sizeof(T), alignof(T)));
            if (!data || Helpers::secure_memcpy(data, source.size() * sizeof(T), source.data(), source.size() * sizeof(T)) != Helpers::MEMORY_OP_SUCCESS)
                return false;
            destination.m_data = data;
            destination.m_size = destination.m_capacity = source.size();
            return true;
        }

        bool CopyNames(const Array<String>& source, Array<String>& destination, ArenaAllocator& arena)
        {
            destination.m_allocator = &arena;
            if (source.empty())
                return true;
            if (source.size() > SIZE_MAX / sizeof(String))
                return false;
            auto* names = static_cast<String*>(arena.AllocateNoZero(source.size() * sizeof(String), alignof(String)));
            if (!names)
                return false;
            destination.m_data     = names;
            destination.m_capacity = source.size();
            for (const auto& source_name : source)
            {
                auto* name = new (&names[destination.m_size]) String{};
                ++destination.m_size;
                const auto bytes = source_name.size() + 1; // PayloadSize checked overflow.
                name->m_data     = static_cast<char*>(arena.AllocateNoZero(bytes, alignof(char)));
                if (!name->m_data || (source_name.size() && Helpers::secure_memcpy(name->m_data, bytes, source_name.c_str(), source_name.size()) != Helpers::MEMORY_OP_SUCCESS))
                    return false;
                name->m_data[source_name.size()] = '\0';
                name->m_allocator                = &arena;
                name->m_size                     = source_name.size();
                name->m_capacity                 = bytes;
            }
            return true;
        }

        bool CopyMap(const UnorderedHashMap<uint32_t, uint32_t>& source, UnorderedHashMap<uint32_t, uint32_t>& destination, ArenaAllocator& arena)
        {
            if (source.empty())
                return true;
            if (!destination.try_init(&arena, MapSlots(source)))
                return false;
            for (const auto& [key, value] : source)
                destination.insert(key, value);
            return true;
        }

        bool MeasureMesh(const AssetMesh& source, const AssetNodeHierarchy& source_hierarchy, PayloadSize& size)
        {
            if (!size.Add<float>(source.Vertices.size()) || !size.Add<uint32_t>(source.Indices.size()) || !size.Add<AssetSubMesh>(source.SubMeshes.size()))
                return false;
            if (!size.Add<Helpers::NodeHierarchy>(source_hierarchy.Hierarchies.size()) || !size.Add<Core::Maths::Mat4f>(source_hierarchy.LocalTransforms.size()) || !size.Add<Core::Maths::Mat4f>(source_hierarchy.GlobalTransforms.size()))
                return false;
            if (!size.Names(source_hierarchy.Names) || !size.Names(source_hierarchy.MaterialNames) || !size.Map(source_hierarchy.NodeNames) || !size.Map(source_hierarchy.NodeMeshes) || !size.Map(source_hierarchy.NodeMaterials))
                return false;
            return true;
        }

        PayloadStorage CloneMesh(TLSFSlab& slab, const AssetMesh& source, const AssetNodeHierarchy& source_hierarchy, AssetMesh& mesh, AssetNodeHierarchy& hierarchy)
        {
            PayloadSize size = {};
            if (!MeasureMesh(source, source_hierarchy, size))
                return {};
            auto storage = AllocatePayload(slab, size.Bytes);
            if (!storage)
                return {};
            auto& arena = storage->Arena;
            if (!CopyArray(source.Vertices, mesh.Vertices, arena) || !CopyArray(source.Indices, mesh.Indices, arena) || !CopyArray(source.SubMeshes, mesh.SubMeshes, arena))
                return {};
            if (!CopyArray(source_hierarchy.Hierarchies, hierarchy.Hierarchies, arena) || !CopyArray(source_hierarchy.LocalTransforms, hierarchy.LocalTransforms, arena) || !CopyArray(source_hierarchy.GlobalTransforms, hierarchy.GlobalTransforms, arena))
                return {};
            if (!CopyNames(source_hierarchy.Names, hierarchy.Names, arena) || !CopyNames(source_hierarchy.MaterialNames, hierarchy.MaterialNames, arena))
                return {};
            if (!CopyMap(source_hierarchy.NodeNames, hierarchy.NodeNames, arena) || !CopyMap(source_hierarchy.NodeMeshes, hierarchy.NodeMeshes, arena) || !CopyMap(source_hierarchy.NodeMaterials, hierarchy.NodeMaterials, arena))
                return {};
            mesh.MeshUUID               = source.MeshUUID;
            mesh.BoundsCenter           = source.BoundsCenter;
            mesh.BoundsRadius           = source.BoundsRadius;
            mesh.BoundsMin              = source.BoundsMin;
            mesh.BoundsMax              = source.BoundsMax;
            mesh.BoundsValid            = source.BoundsValid;
            hierarchy.MeshUUID          = source_hierarchy.MeshUUID;
            hierarchy.NodeHierarchyUUID = source_hierarchy.NodeHierarchyUUID;
            return storage;
        }

        void Bounds(AssetMesh& mesh)
        {
            // Checked decode guarantees a nonempty, finite 8-float vertex stream.
            auto minimum = Core::Maths::Vec3f(mesh.Vertices[0], mesh.Vertices[1], mesh.Vertices[2]);
            auto maximum = minimum;
            for (size_t i = 8; i < mesh.Vertices.size(); i += 8)
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    minimum[axis] = std::min(minimum[axis], mesh.Vertices[i + axis]);
                    maximum[axis] = std::max(maximum[axis], mesh.Vertices[i + axis]);
                }
            double squared = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                mesh.BoundsCenter[axis]  = float((double(minimum[axis]) + maximum[axis]) * 0.5);
                const double extent      = std::max(double(maximum[axis]) - mesh.BoundsCenter[axis], double(mesh.BoundsCenter[axis]) - minimum[axis]);
                squared                 += extent * extent;
            }
            const double radius = std::sqrt(squared);
            if (radius < std::numeric_limits<float>::max())
            {
                mesh.BoundsRadius = float(radius);
                if (double(mesh.BoundsRadius) < radius)
                    mesh.BoundsRadius = std::nextafter(mesh.BoundsRadius, std::numeric_limits<float>::infinity());
            }
            mesh.BoundsMin   = minimum;
            mesh.BoundsMax   = maximum;
            mesh.BoundsValid = true;
        }
    } // namespace

    AssetManager* AssetManager::Instance()
    {
        return s_Instance;
    }

    AssetHandle AssetManager::CreateHandle(uint32_t index, AssetType type)
    {
        return ((uint32_t(type) & 0xF) << 28) | (index & 0x0FFFFFFF);
    }

    uint32_t AssetManager::ReadAssetHandleIndex(AssetHandle h)
    {
        return h & 0x0FFFFFFF;
    }

    AssetType AssetManager::ReadAssetHandleType(AssetHandle h)
    {
        return static_cast<AssetType>((h >> 28) & 0xF);
    }

    void AssetManager::Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path)
    {
        s_Instance                          = ZPushStructCtor<AssetManager>(arena);
        s_Instance->Arena                   = arena;

        s_Instance->Device                  = device;
        s_Instance->CurrentWorkingSpacePath = working_space_path;

        // Initialise the container slab. Growing containers (Meshes, NodeHierarchies,
        // Materials, UUIDToTextureHandle, UUIDToMaterialSlot) back into the slab so
        // realloc can extend in-place — zero dead-block accumulation on grow.
        s_Instance->ContainerSlab.Init(s_Instance->Arena, AssetManager::CONTAINER_SLAB_BYTES);
        auto* slab = &s_Instance->ContainerSlab;

        s_Instance->NodeHierarchies.init(slab, 5000);
        s_Instance->Meshes.init(slab, 5000);
        s_Instance->Materials.init(slab, 5000);
        s_Instance->GPUMeshMaterials.init(slab, 5000);
        s_Instance->Textures.init(slab, 5000);
        s_Instance->UUIDToTextureHandle.init(slab, 5000);
        s_Instance->MeshBindings.init(s_Instance->Arena, 5000);
        s_Instance->UUIDToMaterialSlot.init(slab, 5000);
        s_Instance->MeshCacheAllocations.init(slab, 128);
        s_Instance->MaterialCacheAllocations.init(slab, 128);
        s_Instance->TextureCacheAllocations.init(slab, 128);

        // Registry callbacks borrow their observers. A process-static registry
        // would retain those pointers after this manager and its arena retire.
        // Construct fresh registry state in the same bounded owner instead.
        s_Instance->Registry = ZPushStructCtor<Core::VFS::AssetRegistry>(s_Instance->Arena);
        s_Instance->Registry->Initialize(s_Instance->Arena);

        // Reserve slot 0 as an intentional fallback material: AppRenderPipeline defaults
        // an unresolved submesh to index 0, so without this it would silently alias
        // whichever real material happens to be ingested first.
        Importers::AssetMaterial fallback_mat = {};
        s_Instance->Materials.push(fallback_mat);
        s_Instance->MaterialCacheAllocations.push(nullptr);
        Rendering::Meshes::MeshMaterial fallback_gpu = {};
        fallback_gpu.AlbedoColor                     = Core::Maths::Vec4f(1.0f, 0.078f, 0.576f, 1.0f); // matches FallbackTextureHandle's (255, 20, 147)
        s_Instance->GPUMeshMaterials.push(fallback_gpu);
    }

    void AssetManager::InitFallbackTexture()
    {
        if (!s_Instance || !s_Instance->Device || !s_Instance->Device->RRM)
            return;
        auto* rrm                         = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
        s_Instance->FallbackTextureHandle = rrm->GetOrCreateFallbackTexture();
        ZENGINE_LOG_ASSET_INFO("Fallback texture ready")
    }

    void AssetManager::Shutdown()
    {
        if (s_Instance)
        {
            for (const auto& pins : s_Instance->CpuAssetOwnerPins)
            {
                ZENGINE_VALIDATE_ASSERT(pins.load(std::memory_order_acquire) == 0, "CPU asset owner survived shutdown of its cache slab")
            }
            for (auto* allocation : s_Instance->MeshCacheAllocations)
                AssetPayloadAllocation::Destroy(allocation);
            s_Instance->MeshCacheAllocations.clear();
            for (auto* allocation : s_Instance->MaterialCacheAllocations)
            {
                AssetPayloadAllocation::Destroy(allocation);
            }
            s_Instance->MaterialCacheAllocations.clear();
            for (auto* allocation : s_Instance->TextureCacheAllocations)
            {
                AssetPayloadAllocation::Destroy(allocation);
            }
            s_Instance->TextureCacheAllocations.clear();
            if (s_Instance->Registry)
            {
                s_Instance->Registry->~AssetRegistry();
                s_Instance->Registry = nullptr;
            }
            s_Instance->ContainerSlab.Shutdown();
        }
        s_Instance = nullptr;
    }

    AssetHandle AssetManager::RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta)
    {
        AssetHandle handle = CreateHandle(slot_index, type);
        if (s_Instance->Registry)
        {
            Core::VFS::MetaFileData reg_meta = meta;
            reg_meta.AssetUUID               = uuid;
            s_Instance->Registry->RegisterLoaded(uuid, static_cast<AssetType>(type), path, reg_meta, handle);
        }
        return handle;
    }

    bool AssetManager::IsRegistered(const uuids::uuid& id)
    {
        return s_Instance && s_Instance->Registry && s_Instance->Registry->FindByUUID(id) != nullptr;
    }

    bool AssetManager::IngestMesh(AssetMesh&& mesh, AssetNodeHierarchy&& hierarchy, const AssetRecordSnapshot* expected_record)
    {
        if (!s_Instance)
            return false;
        std::lock_guard lock(s_Instance->IngestMutex);
        if (!s_Instance->Registry || mesh.MeshUUID.is_nil() || hierarchy.MeshUUID != mesh.MeshUUID || hierarchy.NodeHierarchyUUID.is_nil() || hierarchy.NodeHierarchyUUID == mesh.MeshUUID)
            return false;
        AssetRecordSnapshot expected = {};
        if (expected_record)
            expected = *expected_record;
        else if (!s_Instance->Registry->TryReadRecord(mesh.MeshUUID, &expected))
        {
            if (!s_Instance->Registry->Register(mesh.MeshUUID, AssetType::MESH, {}, {}).IsOk() || !s_Instance->Registry->TryReadRecord(mesh.MeshUUID, &expected))
                return false;
        }
        if (expected.Record.Type != AssetType::MESH || expected.Record.UUID != mesh.MeshUUID)
            return false;
        // Stage without touching retained slots. Registry deletion/re-registration
        // can race ingestion; PublishMesh checks the exact record generation.
        AssetMesh          m       = {};
        AssetNodeHierarchy h       = {};
        auto               storage = CloneMesh(s_Instance->ContainerSlab, mesh, hierarchy, m, h);
        if (!storage)
            return false;

        // Runtime bounds are not part of the .zemesh codec. Reset on every reload
        // so an empty/malformed replacement cannot retain the previous box/sphere.
        // All submeshes use this vertex space and the same entity matrix in the
        // draw path; applying imported node transforms only here would disagree
        // with that renderer rather than fix its separate hierarchy limitation.
        m.BoundsCenter = {};
        m.BoundsRadius = 0.f;
        m.BoundsMin    = {};
        m.BoundsMax    = {};
        m.BoundsValid  = false;
        // Vertices are 8 floats each: pos(xyz) nrm(xyz) uv(uv).
        if (!m.Vertices.empty() && m.Vertices.size() % 8 == 0)
        {
            const size_t       vcount  = m.Vertices.size() / 8;
            Core::Maths::Vec3f minimum = {m.Vertices[0], m.Vertices[1], m.Vertices[2]};
            Core::Maths::Vec3f maximum = minimum;
            double             sum[3]  = {};
            bool               valid   = true;
            for (size_t vi = 0; vi < vcount && valid; ++vi)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const float value = m.Vertices[vi * 8 + axis];
                    if (!std::isfinite(value))
                    {
                        valid = false;
                        break;
                    }
                    minimum[axis]  = std::min(minimum[axis], value);
                    maximum[axis]  = std::max(maximum[axis], value);
                    sum[axis]     += double(value);
                }
            }
            if (valid)
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                    m.BoundsCenter[axis] = float(std::clamp(sum[axis] / double(vcount), double(minimum[axis]), double(maximum[axis])));
                double max_squared = 0.0;
                for (size_t vi = 0; vi < vcount; ++vi)
                {
                    double squared = 0.0;
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        const double delta  = double(m.Vertices[vi * 8 + axis]) - m.BoundsCenter[axis];
                        squared            += delta * delta;
                    }
                    max_squared = std::max(max_squared, squared);
                }
                const double radius = std::sqrt(max_squared);
                if (radius <= std::numeric_limits<float>::max())
                {
                    m.BoundsRadius = float(radius);
                    if (double(m.BoundsRadius) < radius)
                        m.BoundsRadius = std::nextafter(m.BoundsRadius, std::numeric_limits<float>::infinity());
                }
                // An unrepresentable sphere keeps radius zero (always visible in
                // culling). The finite AABB is still available for decorations.
                m.BoundsMin   = minimum;
                m.BoundsMax   = maximum;
                m.BoundsValid = true;
            }
        }

        if (s_Instance->PublishMesh(expected, std::move(m), std::move(h), storage.get()).Failed())
            return false;
        (void) storage.release();
        return true;
    }

    Rendering::Textures::TextureHandle AssetManager::IngestTexture(const uuids::uuid& uuid, const Core::Containers::String& path)
    {
        return IngestTextureInternal(uuid, path, false, true);
    }

    Rendering::Textures::TextureHandle AssetManager::IngestTextureInternal(const uuids::uuid& uuid, const Core::Containers::String& path, bool required, bool reload)
    {
        if (!s_Instance || uuid.is_nil())
            return {};
        std::lock_guard lock(s_Instance->IngestMutex);

        // Dedup — return existing handle if already uploaded.
        // Use UUIDToTextureHandle (not IsRegistered): VFSScanner pre-registers texture
        // UUIDs without uploading them, so IsRegistered gives a false positive.
        if (auto* h = s_Instance->UUIDToTextureHandle.find(uuid))
        {
            // Already known — this is a reimport signal (e.g. TextureImporter after a file
            // edit). Keep returning the current image until RRM publishes a ready
            // copy-on-write replacement on the render thread.
            if (reload && s_Instance->Device && s_Instance->Device->RRM)
                static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM)->ScheduleTextureReload(uuid);
            if (!required || *h != s_Instance->FallbackTextureHandle)
            {
                return *h;
            }
            // A compatibility preview may have cached the debug fallback before
            // the source became available. Required loading must retry admission,
            // not permanently treat that placeholder as the texture's identity.
        }

        if (!s_Instance->Registry || (s_Instance->UUIDToTextureHandle.size() + 1) * 4 > s_Instance->UUIDToTextureHandle.capacity() * 3 || path.size() == SIZE_MAX)
        {
            return {};
        }
        auto         storage = AllocatePayload(s_Instance->ContainerSlab, path.size() + 1);
        AssetTexture new_tex = {};
        if (!storage || !new_tex.Path.try_init(&storage->Arena, path.c_str()))
        {
            return {};
        }
        AssetRecordSnapshot expected = {};
        if (!s_Instance->Registry->TryReadRecord(uuid, &expected))
        {
            if (!s_Instance->Registry->Register(uuid, AssetType::TEXTURE, {}, {}).IsOk() || !s_Instance->Registry->TryReadRecord(uuid, &expected))
            {
                return {};
            }
        }
        if (expected.Record.Type != AssetType::TEXTURE)
        {
            return {};
        }
        const auto slot = expected.Record.HasLoadedSlot ? size_t(ReadAssetHandleIndex(expected.Record.SlotHandle)) : s_Instance->Textures.size();
        if (slot > s_Instance->Textures.size() || (slot < s_Instance->Textures.size() && s_Instance->Textures[slot].TextureUUID != uuid) || slot >= AssetCacheLease::SlotsPerType || !ReserveMeshSlot(s_Instance->Textures, slot) || !ReserveMeshSlot(s_Instance->TextureCacheAllocations, slot))
        {
            return {};
        }
        new_tex.TextureUUID = uuid;

        if (!new_tex.Path.empty() && s_Instance->Device && s_Instance->Device->RRM)
        {
            char       full_path[MAX_FILE_PATH_COUNT] = {};
            const auto count                          = snprintf(full_path, sizeof(full_path), "%s%c%s", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, new_tex.Path.c_str());
            if (count < 0 || size_t(count) >= sizeof(full_path))
            {
                return {};
            }
            auto* rrm      = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
            new_tex.Handle = rrm->SubmitTextureFile(full_path, {}, true);
            if (!new_tex.Handle.Valid())
            {
                if (required)
                {
                    return {};
                }
                ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
                ZENGINE_LOG_ASSET_WARN("Texture not found: '{}' — using fallback", full_path)
                new_tex.Handle = s_Instance->FallbackTextureHandle;
            }
        }
        else
        {
            if (required)
            {
                return {};
            }
            ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
            ZENGINE_LOG_ASSET_WARN("Texture {} has no path — extraction may have failed, using fallback", uuids::to_string(uuid))
            new_tex.Handle = s_Instance->FallbackTextureHandle;
        }

        if (!s_Instance->Registry->TryBindLoadedAsset(expected, CreateHandle(uint32_t(slot), AssetType::TEXTURE)))
        {
            if (new_tex.Handle.Valid() && new_tex.Handle != s_Instance->FallbackTextureHandle && s_Instance->Device && s_Instance->Device->RRM)
            {
                static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM)->ReleaseTexture(new_tex.Handle);
            }
            return {};
        }
        if (const auto* previous = s_Instance->UUIDToTextureHandle.find(uuid); previous && *previous == s_Instance->FallbackTextureHandle)
        {
            bool retire_previous = false;
            if (!TryReplaceTexture(uuid, *previous, new_tex.Handle, &retire_previous))
            {
                static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM)->ReleaseTexture(new_tex.Handle);
                return {};
            }
            ZENGINE_VALIDATE_ASSERT(!retire_previous, "The shared fallback texture must never be retired by promotion")
        }
        while (s_Instance->TextureCacheAllocations.size() <= slot)
        {
            s_Instance->TextureCacheAllocations.push(nullptr);
        }
        storage->OwnerPins = &s_Instance->CpuAssetOwnerPins[AssetCacheLease::SlotsPerType * 2 + slot];
        storage->LastUse   = s_Instance->NextCpuCacheUse();
        AssetPayloadAllocation::Destroy(s_Instance->TextureCacheAllocations[slot]);
        s_Instance->TextureCacheAllocations[slot] = storage.release();
        const auto handle                         = new_tex.Handle;
        if (slot == s_Instance->Textures.size())
        {
            s_Instance->Textures.push(std::move(new_tex));
        }
        else
        {
            s_Instance->Textures[slot] = std::move(new_tex);
        }
        s_Instance->UUIDToTextureHandle.insert(uuid, handle);
        (void) s_Instance->Registry->SetState(uuid, AssetState::Loaded);
        return handle;
    }

    void AssetManager::IngestTextures(Core::Containers::Array<AssetTexture>&& textures)
    {
        if (!s_Instance)
            return;
        for (size_t i = 0; i < textures.size(); ++i)
            IngestTexture(textures[i].TextureUUID, textures[i].Path);
    }

    bool AssetManager::IngestMaterial(AssetMaterial&& mat)
    {
        return TryIngestMaterial(mat, nullptr, false);
    }

    bool AssetManager::ResolveMaterialTextures(const AssetMaterial& mat, Rendering::Meshes::MeshMaterial& gpu_mat, bool required)
    {
        const uuids::uuid* ids[]   = {&mat.AlbedoTexUUID, &mat.EmissiveTexUUID, &mat.NormalTexUUID, &mat.OpacityTexUUID, &mat.SpecularTexUUID};
        const String*      paths[] = {&mat.AlbedoTexPath, &mat.EmissiveTexPath, &mat.NormalTexPath, &mat.OpacityTexPath, &mat.SpecularTexPath};
        uint32_t*          maps[]  = {&gpu_mat.AlbedoMap, &gpu_mat.EmissiveMap, &gpu_mat.NormalMap, &gpu_mat.OpacityMap, &gpu_mat.SpecularMap};
        for (size_t index = 0; index < std::size(ids); ++index)
        {
            *maps[index] = INVALID_MAP_HANDLE;
            if (ids[index]->is_nil())
            {
                continue;
            }
            if (!required && paths[index]->empty() && !s_Instance->UUIDToTextureHandle.find(*ids[index]))
            {
                continue;
            }
            const auto texture = IngestTextureInternal(*ids[index], *paths[index], required, false);
            if (required && !texture.Valid())
            {
                return false;
            }
            if (texture.Valid())
            {
                *maps[index] = texture.Index;
            }
        }
        return true;
    }

    bool AssetManager::TryIngestMaterial(const AssetMaterial& mat, const AssetRecordSnapshot* expected_record, bool required)
    {
        if (!s_Instance || !s_Instance->Registry || mat.MaterialUUID.is_nil())
        {
            return false;
        }
        std::lock_guard lock(s_Instance->IngestMutex);
        // Use UUIDToMaterialSlot — populated only after data is actually ingested.
        // GetAsset gives false positives: VFSScanner pre-registers all .zematerial UUIDs
        // with SlotHandle=0, so GetAsset<AssetMaterial>(uuid_N) returns Materials[0]
        // (the first ingested material) for every subsequent material → all skipped.
        const auto*     cached_slot = s_Instance->UUIDToMaterialSlot.find(mat.MaterialUUID);
        if (cached_slot && *cached_slot < s_Instance->MaterialCacheAllocations.size() && s_Instance->MaterialCacheAllocations[*cached_slot])
        {
            if (required)
            {
                auto gpu = s_Instance->GPUMeshMaterials[*cached_slot];
                if (!ResolveMaterialTextures(mat, gpu, true))
                {
                    return false;
                }
                s_Instance->GPUMeshMaterials[*cached_slot] = gpu;
            }
            return true;
        }
        const auto slot = cached_slot ? size_t(*cached_slot) : s_Instance->Materials.size();
        if (slot >= AssetCacheLease::SlotsPerType || (s_Instance->UUIDToMaterialSlot.size() + (cached_slot ? 0 : 1)) * 4 > s_Instance->UUIDToMaterialSlot.capacity() * 3 || s_Instance->GPUMeshMaterials.size() != s_Instance->Materials.size() || !ReserveMeshSlot(s_Instance->Materials, slot) || !ReserveMeshSlot(s_Instance->GPUMeshMaterials, slot) || !ReserveMeshSlot(s_Instance->MaterialCacheAllocations, slot))
        {
            return false;
        }
        AssetMaterial copy    = {};
        auto          storage = CloneMaterial(s_Instance->ContainerSlab, mat, copy);
        if (!storage)
        {
            return false;
        }
        AssetRecordSnapshot expected = {};
        if (expected_record)
        {
            expected = *expected_record;
        }
        else if (!s_Instance->Registry->TryReadRecord(mat.MaterialUUID, &expected))
        {
            if (!s_Instance->Registry->Register(mat.MaterialUUID, AssetType::MATERIAL, {}, {}).IsOk() || !s_Instance->Registry->TryReadRecord(mat.MaterialUUID, &expected))
            {
                return false;
            }
        }
        if (expected.Record.Type != AssetType::MATERIAL || expected.Record.UUID != mat.MaterialUUID)
        {
            return false;
        }
        Rendering::Meshes::MeshMaterial gpu_mat = {};
        gpu_mat.AlbedoColor                     = {mat.AlbedoColor[0], mat.AlbedoColor[1], mat.AlbedoColor[2], mat.AlbedoColor[3]};
        gpu_mat.EmissiveColor                   = {mat.EmissiveColor[0], mat.EmissiveColor[1], mat.EmissiveColor[2], mat.EmissiveColor[3]};
        gpu_mat.RoughnessColor                  = {mat.RoughnessColor[0], mat.RoughnessColor[1], mat.RoughnessColor[2], mat.RoughnessColor[3]};
        gpu_mat.SpecularColor                   = {mat.SpecularColor[0], mat.SpecularColor[1], mat.SpecularColor[2], mat.SpecularColor[3]};
        gpu_mat.AmbientColor                    = {mat.AmbientColor[0], mat.AmbientColor[1], mat.AmbientColor[2], mat.AmbientColor[3]};
        gpu_mat.Factors                         = {mat.Factors[0], mat.Factors[1], mat.Factors[2], mat.Factors[3]};
        gpu_mat.AlphaMode                       = mat.AlphaMode;

        // Resolve handle for each texture slot: UUID lookup first, then fall back to
        // uploading from the stored path — handles scene-reload and dragged-.zmesh cases
        // where IngestTextures may not have run yet for this material's textures.
        if (!ResolveMaterialTextures(mat, gpu_mat, required))
        {
            return false;
        }
        if (!s_Instance->Registry->TryBindLoadedAsset(expected, CreateHandle(uint32_t(slot), AssetType::MATERIAL)))
        {
            return false;
        }
        storage->OwnerPins = &s_Instance->CpuAssetOwnerPins[AssetCacheLease::SlotsPerType + slot];
        storage->LastUse   = s_Instance->NextCpuCacheUse();
        if (cached_slot)
        {
            s_Instance->Materials[slot]                = std::move(copy);
            s_Instance->GPUMeshMaterials[slot]         = gpu_mat;
            s_Instance->MaterialCacheAllocations[slot] = storage.release();
        }
        else
        {
            s_Instance->Materials.push(std::move(copy));
            s_Instance->GPUMeshMaterials.push(gpu_mat);
            s_Instance->MaterialCacheAllocations.push(storage.release());
        }
        s_Instance->UUIDToMaterialSlot.insert(mat.MaterialUUID, uint32_t(slot));
        (void) s_Instance->Registry->SetState(mat.MaterialUUID, AssetState::Loaded);
        return true;
    }

    bool AssetManager::IngestMaterialFromUUID(Core::Memory::ArenaAllocator* scratch, const uuids::uuid& material_uuid)
    {
        if (!s_Instance || !s_Instance->Registry || !scratch || material_uuid.is_nil())
            return false;
        {
            // The map is mutated by import workers. Its fast deduplication path
            // still needs the same ownership guard as the eventual insertion.
            std::lock_guard lock(s_Instance->IngestMutex);
            const auto*     slot = s_Instance->UUIDToMaterialSlot.find(material_uuid);
            if (slot && *slot < s_Instance->MaterialCacheAllocations.size() && s_Instance->MaterialCacheAllocations[*slot])
            {
                return TryIngestMaterial(s_Instance->Materials[*slot], nullptr, true);
            }
        }

        AssetRecordSnapshot expected = {};
        auto*               registry = s_Instance->Registry;
        if (!registry->TryReadRecord(material_uuid, &expected) || expected.Record.Type != AssetType::MATERIAL || !expected.Record.Path.IsValid() || !expected.Record.Path.Extension().Equals(".zematerial"))
            return false;

        char native[MAX_FILE_PATH_COUNT] = {};
        expected.Record.Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

        Importers::AssetMaterial mat  = {};
        const auto               read = Importers::AssetCodec::TryDeserializeMaterialAssetFile(scratch, native, mat, material_uuid);
        if (read.Failed())
            return false;
        return TryIngestMaterial(mat, &expected, true);
    }

    uint32_t AssetManager::ResolveTextureMapIndex(const uuids::uuid& id, const Core::Containers::String& path)
    {
        if (!s_Instance || id.is_nil())
            return INVALID_MAP_HANDLE;
        auto* h = s_Instance->UUIDToTextureHandle.find(id);
        if (h)
            return h->Index;
        if (!path.empty())
            return IngestTexture(id, path).Index;
        return INVALID_MAP_HANDLE;
    }

    Rendering::Textures::TextureHandle AssetManager::FindTextureHandle(const uuids::uuid& uuid)
    {
        if (!s_Instance)
            return {};
        std::lock_guard lock(s_Instance->IngestMutex);
        auto*           h = s_Instance->UUIDToTextureHandle.find(uuid);
        return h ? *h : Rendering::Textures::TextureHandle{};
    }

    bool AssetManager::TryReplaceTexture(const uuids::uuid& uuid, Rendering::Textures::TextureHandle expected, Rendering::Textures::TextureHandle replacement, bool* retire_previous)
    {
        if (retire_previous)
        {
            *retire_previous = false;
        }
        if (!s_Instance || !replacement.Valid() || replacement.Index >= INVALID_MAP_HANDLE || !retire_previous)
        {
            return false;
        }
        std::scoped_lock lock(s_Instance->IngestMutex, s_Instance->PendingTextureReleaseMutex);
        auto*            current = s_Instance->UUIDToTextureHandle.find(uuid);
        if (!current || current->Index != expected.Index || current->Generation != expected.Generation || (expected.Index == replacement.Index && expected.Generation == replacement.Generation))
        {
            return false;
        }
        for (uint32_t index = 0; index < s_Instance->PendingTextureReleaseCount; ++index)
        {
            if (s_Instance->PendingTextureReleases[index] == uuid)
            {
                return false;
            }
        }
        Importers::AssetTexture* descriptor = nullptr;
        for (auto& texture : s_Instance->Textures)
        {
            if (texture.TextureUUID == uuid && texture.Handle.Index == expected.Index && texture.Handle.Generation == expected.Generation)
            {
                descriptor = &texture;
                break;
            }
        }
        if (!descriptor || s_Instance->GPUMeshMaterials.size() < s_Instance->Materials.size())
        {
            return false;
        }
        *current           = replacement;
        descriptor->Handle = replacement;
        const auto map     = static_cast<uint32_t>(replacement.Index);
        for (uint32_t index = 0; index < s_Instance->Materials.size(); ++index)
        {
            const auto& material = s_Instance->Materials[index];
            auto&       gpu      = s_Instance->GPUMeshMaterials[index];
            if (material.AlbedoTexUUID == uuid)
            {
                gpu.AlbedoMap = map;
            }
            if (material.EmissiveTexUUID == uuid)
            {
                gpu.EmissiveMap = map;
            }
            if (material.NormalTexUUID == uuid)
            {
                gpu.NormalMap = map;
            }
            if (material.OpacityTexUUID == uuid)
            {
                gpu.OpacityMap = map;
            }
            if (material.SpecularTexUUID == uuid)
            {
                gpu.SpecularMap = map;
            }
        }
        const auto fallback = s_Instance->FallbackTextureHandle;
        *retire_previous    = expected.Valid() && (expected.Index != fallback.Index || expected.Generation != fallback.Generation);
        for (const auto entry : s_Instance->UUIDToTextureHandle)
        {
            if (entry.second.Index == expected.Index && entry.second.Generation == expected.Generation)
            {
                *retire_previous = false;
                break;
            }
        }
        return true;
    }

    void AssetManager::ReleaseTexture(const uuids::uuid& uuid)
    {
        if (!s_Instance)
            return;
        std::scoped_lock lock(s_Instance->IngestMutex, s_Instance->PendingTextureReleaseMutex);
        if (!s_Instance->UUIDToTextureHandle.find(uuid))
        {
            return;
        }
        for (uint32_t index = 0; index < s_Instance->PendingTextureReleaseCount; ++index)
        {
            if (s_Instance->PendingTextureReleases[index] == uuid)
            {
                return;
            }
        }
        ZENGINE_VALIDATE_ASSERT(s_Instance->PendingTextureReleaseCount < MAX_PENDING_TEXTURE_RELEASES, "Texture release must fit the admitted UUID cache")
        s_Instance->PendingTextureReleases[s_Instance->PendingTextureReleaseCount++] = uuid;
    }

    void AssetManager::FlushTextureReleases()
    {
        if (!s_Instance)
        {
            return;
        }

        // Do not drain pending deletion before taking the cache lock: a reload
        // could otherwise publish in that gap and the delayed UUID patch would
        // erase a different image than the one captured for GPU retirement.
        std::scoped_lock cache_lock(s_Instance->IngestMutex, s_Instance->PendingTextureReleaseMutex);
        uuids::uuid      local[MAX_PENDING_TEXTURE_RELEASES] = {};
        const uint32_t   count                               = s_Instance->PendingTextureReleaseCount;
        Helpers::secure_memcpy(local, sizeof(local), s_Instance->PendingTextureReleases, count * sizeof(local[0]));
        s_Instance->PendingTextureReleaseCount = 0;
        if (count == 0)
        {
            return;
        }

        // IngestMutex, not just PendingTextureReleaseMutex: Materials/GPUMeshMaterials are
        // unsynchronized and also written by IngestMaterial/IngestTexture under this lock.
        for (uint32_t r = 0; r < count; ++r)
        {
            s_Instance->UUIDToTextureHandle.remove(local[r]);

            // Sentinel set directly, not via ResolveTextureMapIndex — its path fallback
            // would re-ingest (undo) the release for any material with a stored path.
            for (uint32_t i = 0; i < s_Instance->Materials.size(); ++i)
            {
                auto& mat     = s_Instance->Materials[i];
                auto& gpu_mat = s_Instance->GPUMeshMaterials[i];
                if (mat.AlbedoTexUUID == local[r])
                {
                    gpu_mat.AlbedoMap = INVALID_MAP_HANDLE;
                }
                if (mat.EmissiveTexUUID == local[r])
                {
                    gpu_mat.EmissiveMap = INVALID_MAP_HANDLE;
                }
                if (mat.NormalTexUUID == local[r])
                {
                    gpu_mat.NormalMap = INVALID_MAP_HANDLE;
                }
                if (mat.OpacityTexUUID == local[r])
                {
                    gpu_mat.OpacityMap = INVALID_MAP_HANDLE;
                }
                if (mat.SpecularTexUUID == local[r])
                {
                    gpu_mat.SpecularMap = INVALID_MAP_HANDLE;
                }
            }
        }
    }

    Importers::AssetMesh* AssetManager::GetMeshAsset(const uuids::uuid& id)
    {
        if (!Registry)
            return nullptr;
        AssetHandle slot = 0;
        if (!Registry->TryReadSlot(id, AssetType::MESH, &slot) || ReadAssetHandleType(slot) != AssetType::MESH)
            return nullptr;
        uint32_t index = ReadAssetHandleIndex(slot);
        return index < Meshes.size() && index < MeshCacheAllocations.size() && MeshCacheAllocations[index] && Meshes[index].MeshUUID == id ? &Meshes[index] : nullptr;
    }

    bool AssetManager::TryGetMeshDrawCount(const uuids::uuid& id, uint32_t* out_draws)
    {
        if (!out_draws || id.is_nil())
            return false;
        std::lock_guard lock(IngestMutex);
        return TryGetMeshDrawCountLocked(id, out_draws);
    }

    bool AssetManager::TryGetMeshDrawCountLocked(const uuids::uuid& id, uint32_t* out_draws)
    {
        const auto* mesh = GetMeshAsset(id);
        if (!mesh || mesh->Vertices.empty() || mesh->Vertices.size() % 8 != 0 || mesh->Indices.empty() || mesh->SubMeshes.empty() || mesh->SubMeshes.size() > UINT32_MAX)
            return false;
        const auto vertices = mesh->Vertices.size() / 8;
        for (const auto& sub : mesh->SubMeshes)
            if (!sub.VertexCount || !sub.IndexCount || sub.VertexOffset > vertices || sub.VertexCount > vertices - sub.VertexOffset || sub.IndexOffset > mesh->Indices.size() || sub.IndexCount > mesh->Indices.size() - sub.IndexOffset)
                return false;
        *out_draws = static_cast<uint32_t>(mesh->SubMeshes.size());
        return true;
    }

    bool AssetManager::TryGetMeshBounds(const uuids::uuid& id, Core::Maths::Vec3f& out_center, float& out_radius)
    {
        std::lock_guard lock(IngestMutex);
        auto*           mesh = GetMeshAsset(id);
        if (!mesh || !std::isfinite(mesh->BoundsRadius) || mesh->BoundsRadius <= 0.0f || !std::isfinite(mesh->BoundsCenter.x) || !std::isfinite(mesh->BoundsCenter.y) || !std::isfinite(mesh->BoundsCenter.z))
            return false;

        out_center = mesh->BoundsCenter;
        out_radius = mesh->BoundsRadius;
        return true;
    }

    bool AssetManager::TryGetMeshAABB(const uuids::uuid& id, Core::Maths::Vec3f& out_min, Core::Maths::Vec3f& out_max)
    {
        std::unique_lock lock(IngestMutex, std::try_to_lock);
        if (!lock.owns_lock())
            return false; // Optional per-frame decoration must not wait on a full ingestion scan.
        const auto* mesh = GetMeshAsset(id);
        if (!mesh || !mesh->BoundsValid)
            return false;
        for (uint32_t axis = 0; axis < 3; ++axis)
            if (!std::isfinite(mesh->BoundsMin[axis]) || !std::isfinite(mesh->BoundsMax[axis]) || mesh->BoundsMin[axis] > mesh->BoundsMax[axis])
                return false;
        out_min = mesh->BoundsMin;
        out_max = mesh->BoundsMax;
        return true;
    }

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& mesh_id)
    {
        if (!s_Instance)
            return nullptr;
        const auto* binding = s_Instance->MeshBindings.find(mesh_id);
        return (s_Instance->GetMeshAsset(mesh_id) && binding && binding->HierarchySlot < s_Instance->NodeHierarchies.size()) ? &s_Instance->NodeHierarchies[binding->HierarchySlot] : nullptr;
    }

    AssetHandle AssetManager::GetMeshNodeHierarchyHandle(const uuids::uuid& id)
    {
        if (!Registry)
            return 0;
        const Core::VFS::AssetRecord* rec = Registry->FindByUUID(id);
        return rec ? rec->SlotHandle : 0;
    }

    void AssetManager::ReloadFromDisk(Core::Memory::ArenaAllocator* scratch)
    {
        if (!s_Instance || !s_Instance->Registry || !scratch)
            return;
        // Materials — deserialize each .zematerial that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::MATERIAL}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->UUIDToMaterialSlot.find(rec->UUID) != nullptr)
                    continue;

                char native[MAX_FILE_PATH_COUNT] = {};
                rec->Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

                AssetMaterial mat = {};
                Importers::AssetCodec::DeserializeMaterialAssetFile(scratch, native, mat);
                if (!mat.MaterialUUID.is_nil())
                {
                    ZENGINE_LOG_ASSET_INFO("Reloading material from disk: {}", native)
                    IngestMaterial(std::move(mat));
                }
            }
        }

        // Textures — ingest each raster texture that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::TEXTURE}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->UUIDToTextureHandle.find(rec->UUID) != nullptr)
                    continue;

                Core::Containers::String rel_path = {};
                rel_path.init(scratch, rec->Path.CStr());

                ZENGINE_LOG_ASSET_INFO("Reloading texture from disk: {}", rec->Path.CStr())
                IngestTexture(rec->UUID, rel_path);
            }
        }

        // Meshes — deserialize each .zemesh that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::MESH}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->GetMeshAsset(rec->UUID) != nullptr)
                    continue;

                char native[MAX_FILE_PATH_COUNT] = {};
                rec->Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

                AssetMesh          mesh = {};
                AssetNodeHierarchy hier = {};
                // Use the AssetManager's own arena — meshes can be hundreds of MB,
                // far exceeding the caller's scratch. After IngestMesh copies the data
                // permanently, the deserialization buffers become dead weight but are
                // acceptable as a one-time startup cost.
                Importers::AssetCodec::DeserializeMeshAssetFile(s_Instance->Arena, native, mesh, hier);
                if (!mesh.MeshUUID.is_nil())
                {
                    ZENGINE_LOG_ASSET_INFO("Reloading mesh from disk: {}", native)
                    IngestMesh(std::move(mesh), std::move(hier));
                }
            }
        }
    }

    uuids::uuid AssetManager::GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name)
    {
        auto hash = Core::VFS::MetaFileIO::ComputeHash(ctx, asset_path);
        auto meta = Core::VFS::MetaFileIO::GetOrCreate(ctx, asset_path, importer_name, hash.Succeeded() ? hash.Value() : 0);
        if (meta.Succeeded())
            return meta.Value().AssetUUID;

        std::random_device           rd;
        std::mt19937                 generator(rd());
        uuids::uuid_random_generator gen{generator};
        return gen();
    }

    VFSResult<uint32_t> AssetManager::PublishMesh(const AssetRecordSnapshot& expected, AssetMesh&& mesh, AssetNodeHierarchy&& hierarchy, AssetPayloadAllocation* storage)
    {
        const auto uuid = mesh.MeshUUID;
        if (!Registry || !storage || uuid != expected.Record.UUID || expected.Record.Type != AssetType::MESH || hierarchy.MeshUUID != uuid)
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        auto*                      binding            = MeshBindings.find(uuid);
        const bool                 replacing          = binding != nullptr;
        const auto                 mesh_slot          = replacing ? binding->MeshSlot : Meshes.size();
        const auto                 hierarchy_slot     = replacing ? binding->HierarchySlot : NodeHierarchies.size();
        AssetRecordSnapshot        retained_hierarchy = {};
        const AssetRecordSnapshot* retained           = nullptr;
        if (replacing)
        {
            if (mesh_slot >= Meshes.size() || mesh_slot >= MeshCacheAllocations.size() || hierarchy_slot >= NodeHierarchies.size() || Meshes[mesh_slot].MeshUUID != uuid || NodeHierarchies[hierarchy_slot].MeshUUID != uuid)
                return VFSResult<uint32_t>::Fail(VFSError::Corrupted);
            hierarchy.NodeHierarchyUUID = NodeHierarchies[hierarchy_slot].NodeHierarchyUUID;
            if (Registry->TryReadRecord(hierarchy.NodeHierarchyUUID, &retained_hierarchy))
                retained = &retained_hierarchy;
        }
        if (mesh_slot >= AssetCacheLease::SlotsPerType || hierarchy_slot >= AssetCacheLease::SlotsPerType || (!replacing && (MeshBindings.size() + 1) * 4 > MeshBindings.capacity() * 3))
            return VFSResult<uint32_t>::Fail(VFSError::SizeLimitExceeded);
        if (!ReserveMeshSlot(Meshes, mesh_slot) || !ReserveMeshSlot(NodeHierarchies, hierarchy_slot) || !ReserveMeshSlot(MeshCacheAllocations, mesh_slot))
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        const auto mesh_handle      = CreateHandle(uint32_t(mesh_slot), AssetType::MESH);
        const auto hierarchy_handle = CreateHandle(uint32_t(hierarchy_slot), AssetType::MESH_HIERARCHY);
        if (!Registry->TryBindLoadedMesh(expected, hierarchy.NodeHierarchyUUID, mesh_handle, hierarchy_handle, retained))
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        const auto draws          = uint32_t(mesh.SubMeshes.size());
        const auto hierarchy_uuid = hierarchy.NodeHierarchyUUID;
        storage->OwnerPins        = &CpuAssetOwnerPins[mesh_slot];
        storage->LastUse          = NextCpuCacheUse();
        // Capacity is established. No allocation, IO or callback between paired
        // registry publication and the complete CPU descriptor commit.
        if (replacing)
        {
            Meshes[mesh_slot]               = std::move(mesh);
            NodeHierarchies[hierarchy_slot] = std::move(hierarchy);
            auto* previous                  = MeshCacheAllocations[mesh_slot];
            MeshCacheAllocations[mesh_slot] = storage;
            AssetPayloadAllocation::Destroy(previous);
        }
        else
        {
            new (&Meshes.m_data[mesh_slot]) AssetMesh(std::move(mesh));
            ++Meshes.m_size;
            new (&NodeHierarchies.m_data[hierarchy_slot]) AssetNodeHierarchy(std::move(hierarchy));
            ++NodeHierarchies.m_size;
            MeshCacheAllocations.push(storage);
            MeshBindings.insert(uuid, {uint32_t(mesh_slot), uint32_t(hierarchy_slot)});
        }
        if (replacing)
        {
            // OnReady alone skips an existing GPU buffer. Invalidate it before
            // announcing readiness so both rendering and thumbnail consumers see
            // the new payload, including a source deleted and reimported in place.
            Registry->MarkStale(uuid);
            Registry->MarkStale(hierarchy_uuid);
        }
        (void) Registry->SetState(uuid, AssetState::Loaded);
        (void) Registry->SetState(hierarchy_uuid, AssetState::Loaded);
        return VFSResult<uint32_t>::Ok(draws);
    }

    AssetPayloadPin AssetManager::PinMeshPayload(const uuids::uuid& uuid)
    {
        std::lock_guard lock(IngestMutex);
        const auto*     binding = MeshBindings.find(uuid);
        if (!binding || !GetMeshAsset(uuid))
        {
            return {};
        }
        auto* allocation    = MeshCacheAllocations[binding->MeshSlot];
        allocation->LastUse = NextCpuCacheUse();
        return AssetPayloadPin(allocation);
    }

    bool AssetManager::RetainMeshAssets(const uuids::uuid& uuid, AssetCacheLease& lease)
    {
        std::lock_guard lock(IngestMutex);
        const auto*     binding = MeshBindings.find(uuid);
        const auto*     mesh    = GetMeshAsset(uuid);
        if (!lease.Initialized() || !binding || !mesh || binding->MeshSlot >= AssetCacheLease::SlotsPerType)
        {
            return false;
        }
        // Validate every potentially failing lookup before changing the lease.
        // Once admission succeeds, retaining the closure cannot allocate/fail.
        for (const auto& submesh : mesh->SubMeshes)
        {
            if (!submesh.MaterialUUID.is_nil())
            {
                const auto* slot = UUIDToMaterialSlot.find(submesh.MaterialUUID);
                if (!slot || *slot >= MaterialCacheAllocations.size() || *slot >= AssetCacheLease::SlotsPerType)
                {
                    return false;
                }
            }
        }
        (void) lease.Retain(binding->MeshSlot, MeshCacheAllocations[binding->MeshSlot]);
        const auto use                                   = NextCpuCacheUse();
        MeshCacheAllocations[binding->MeshSlot]->LastUse = use;
        for (const auto& submesh : mesh->SubMeshes)
        {
            if (submesh.MaterialUUID.is_nil())
            {
                continue;
            }
            const auto* slot = UUIDToMaterialSlot.find(submesh.MaterialUUID);
            if (!slot || *slot >= MaterialCacheAllocations.size() || *slot >= AssetCacheLease::SlotsPerType)
            {
                return false;
            }
            // Scalar material/GPU descriptors remain in their stable tables;
            // only optional source strings can have been reclaimed previously.
            if (MaterialCacheAllocations[*slot])
            {
                MaterialCacheAllocations[*slot]->LastUse = use;
            }
            if (MaterialCacheAllocations[*slot] && !lease.Retain(AssetCacheLease::SlotsPerType + *slot, MaterialCacheAllocations[*slot]))
            {
                return false;
            }
            const auto&       material   = Materials[*slot];
            const uuids::uuid textures[] = {material.AlbedoTexUUID, material.EmissiveTexUUID, material.NormalTexUUID, material.OpacityTexUUID, material.SpecularTexUUID};
            for (const auto& texture : textures)
            {
                AssetHandle handle = {};
                if (!texture.is_nil() && Registry->TryReadSlot(texture, AssetType::TEXTURE, &handle))
                {
                    const auto index = ReadAssetHandleIndex(handle);
                    if (index < TextureCacheAllocations.size() && TextureCacheAllocations[index])
                    {
                        TextureCacheAllocations[index]->LastUse = use;
                    }
                    if (index < TextureCacheAllocations.size() && TextureCacheAllocations[index] && !lease.Retain(AssetCacheLease::SlotsPerType * 2 + index, TextureCacheAllocations[index]))
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    uint64_t AssetManager::NextCpuCacheUse() noexcept
    {
        if (CpuCacheClock != UINT64_MAX)
        {
            ++CpuCacheClock;
        }
        return CpuCacheClock;
    }

    size_t AssetManager::TrimCpuCache(size_t target_bytes)
    {
        // Never reclaim between a worker's dependency decode and final pin.
        std::unique_lock decode_lock(MeshLoadMutex, std::try_to_lock);
        std::unique_lock cache_lock(IngestMutex, std::try_to_lock);
        if (!decode_lock.owns_lock() || !cache_lock.owns_lock())
        {
            return 0;
        }
        return TrimCpuCacheLocked(nullptr, target_bytes);
    }

    size_t AssetManager::TrimCpuCacheLocked(const AssetMesh* protected_source, size_t target_bytes)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.CpuCacheMaintenance");
        if (!target_bytes)
        {
            return 0;
        }
        constexpr auto slots                   = AssetCacheLease::SlotsPerType;
        bool           dependencies[slots * 2] = {};
        const auto     protect_materials       = [&](const AssetMesh& mesh) {
            for (const auto& submesh : mesh.SubMeshes)
            {
                const auto* material = UUIDToMaterialSlot.find(submesh.MaterialUUID);
                if (material && *material < slots)
                {
                    dependencies[*material] = true;
                }
            }
        };
        if (protected_source)
        {
            protect_materials(*protected_source);
        }
        // Logical UUID pins also protect dependencies introduced by reimport
        // while an owner still retains its previous immutable CPU version.
        for (size_t slot = 0; slot < MeshCacheAllocations.size(); ++slot)
        {
            const auto* allocation = MeshCacheAllocations[slot];
            if (allocation && (allocation->References.load(std::memory_order_acquire) != 1 || CpuAssetOwnerPins[slot].load(std::memory_order_acquire) != 0))
            {
                protect_materials(Meshes[slot]);
            }
        }
        for (size_t slot = 0; slot < Materials.size(); ++slot)
        {
            if (!dependencies[slot] && CpuAssetOwnerPins[slots + slot].load(std::memory_order_acquire) == 0)
            {
                continue;
            }
            const auto&       material   = Materials[slot];
            const uuids::uuid textures[] = {material.AlbedoTexUUID, material.EmissiveTexUUID, material.NormalTexUUID, material.OpacityTexUUID, material.SpecularTexUUID};
            for (const auto& uuid : textures)
            {
                AssetHandle handle = {};
                if (!uuid.is_nil() && Registry && Registry->TryReadSlot(uuid, AssetType::TEXTURE, &handle))
                {
                    const auto index = ReadAssetHandleIndex(handle);
                    if (index < slots)
                    {
                        dependencies[slots + index] = true;
                    }
                }
            }
        }
        uint32_t   count   = 0;
        const auto collect = [&](const Array<AssetPayloadAllocation*>& allocations, uint32_t base) {
            for (uint32_t slot = 0; slot < allocations.size(); ++slot)
            {
                const auto  key        = base + slot;
                const auto* allocation = allocations[slot];
                if (allocation && allocation->References.load(std::memory_order_acquire) == 1 && CpuAssetOwnerPins[key].load(std::memory_order_acquire) == 0 && (base == 0 || !dependencies[key - slots]))
                {
                    CpuEvictionOrder[count++] = key;
                }
            }
        };
        collect(MeshCacheAllocations, 0);
        collect(MaterialCacheAllocations, slots);
        collect(TextureCacheAllocations, slots * 2);
        const auto allocation_at = [&](uint32_t key) -> AssetPayloadAllocation*& {
            if (key < slots)
            {
                return MeshCacheAllocations[key];
            }
            if (key < slots * 2)
            {
                return MaterialCacheAllocations[key - slots];
            }
            return TextureCacheAllocations[key - slots * 2];
        };
        std::sort(CpuEvictionOrder, CpuEvictionOrder + count, [&](uint32_t left, uint32_t right) {
            const auto a = allocation_at(left)->LastUse;
            const auto b = allocation_at(right)->LastUse;
            return a == b ? left < right : a < b;
        });
        size_t reclaimed = 0;
        size_t bytes     = 0;
        for (uint32_t index = 0; index < count && bytes < target_bytes; ++index)
        {
            const auto key        = CpuEvictionOrder[index];
            auto*&     allocation = allocation_at(key);
            if (key < slots)
            {
                const auto* binding = MeshBindings.find(Meshes[key].MeshUUID);
                if (!binding || binding->HierarchySlot >= NodeHierarchies.size())
                {
                    continue;
                }
                const auto uuid                                           = Meshes[key].MeshUUID;
                const auto hierarchy_uuid                                 = NodeHierarchies[binding->HierarchySlot].NodeHierarchyUUID;
                Meshes[key]                                               = {};
                Meshes[key].MeshUUID                                      = uuid;
                NodeHierarchies[binding->HierarchySlot]                   = {};
                NodeHierarchies[binding->HierarchySlot].MeshUUID          = uuid;
                NodeHierarchies[binding->HierarchySlot].NodeHierarchyUUID = hierarchy_uuid;
            }
            else if (key < slots * 2)
            {
                // Scalar UUID/GPU descriptors retain their stable table slots.
                // All borrowed source views must disappear before block release.
                auto& material           = Materials[key - slots];
                material.Name            = {};
                material.AlbedoTexPath   = {};
                material.EmissiveTexPath = {};
                material.NormalTexPath   = {};
                material.OpacityTexPath  = {};
                material.SpecularTexPath = {};
            }
            else
            {
                Textures[key - slots * 2].Path = {};
            }
            const auto released = allocation->Arena.m_total_size + sizeof(AssetPayloadAllocation);
            bytes               = released > SIZE_MAX - bytes ? SIZE_MAX : bytes + released;
            AssetPayloadAllocation::Destroy(allocation);
            allocation = nullptr;
            ++reclaimed;
        }
        return reclaimed;
    }

    VFSResult<uint32_t> AssetManager::LoadMeshAsset(IVFSContext& vfs, const uuids::uuid& uuid, const AssetCodec::MeshAssetReadLimits& limits, const std::atomic<bool>* cancelled, AssetCacheLease* lease)
    {
        ZENGINE_PROFILE_SCOPE("SceneLoad.Worker.MeshLoad");
        const auto stopped = [&] { return cancelled && cancelled->load(std::memory_order_acquire); };
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        if (lease && !lease->Initialized())
        {
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        }
        if (CpuRetention.load(std::memory_order_relaxed) == CpuCacheRetention::ZeroRetention)
        {
            (void) TrimCpuCache();
        }
        uint32_t   draws = 0;
        const auto reuse = [&] {
            std::lock_guard lock(IngestMutex);
            if (!TryGetMeshDrawCountLocked(uuid, &draws))
            {
                return false;
            }
            const auto* binding = MeshBindings.find(uuid);
            if (binding)
            {
                for (const auto& submesh : Meshes[binding->MeshSlot].SubMeshes)
                {
                    if (submesh.MaterialUUID.is_nil())
                    {
                        continue;
                    }
                    const auto* material = UUIDToMaterialSlot.find(submesh.MaterialUUID);
                    if (!material || *material >= MaterialCacheAllocations.size() || !MaterialCacheAllocations[*material] || !TryIngestMaterial(Materials[*material], nullptr, true))
                    {
                        return false;
                    }
                }
                MeshCacheAllocations[binding->MeshSlot]->LastUse = NextCpuCacheUse();
            }
            return !lease || RetainMeshAssets(uuid, *lease);
        };
        if (reuse())
            return VFSResult<uint32_t>::Ok(draws);
        std::unique_lock decode_lock(MeshLoadMutex, std::try_to_lock);
        if (!decode_lock.owns_lock())
            return VFSResult<uint32_t>::Fail(VFSError::AlreadyExists);
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        if (reuse())
            return VFSResult<uint32_t>::Ok(draws);
        AssetRecordSnapshot expected = {};
        auto*               registry = Registry;
        if (!registry || !registry->TryReadRecord(uuid, &expected))
            return VFSResult<uint32_t>::Fail(VFSError::NotFound);
        if (expected.Record.Type != AssetType::MESH || !expected.Record.Path.IsValid() || !expected.Record.Path.Extension().Equals(".zemesh"))
            return VFSResult<uint32_t>::Fail(VFSError::InvalidPath);
        if (!limits.MaxDecodedBytes || limits.MaxDecodedBytes > ZMega(512ULL))
            return VFSResult<uint32_t>::Fail(VFSError::SizeLimitExceeded);

        ArenaAllocator scratch = {};
        scratch.Initialize(limits.MaxDecodedBytes, 0, "AssetManager/MeshRead");
        AssetMesh          source           = {};
        AssetNodeHierarchy source_hierarchy = {};
        const auto         read             = AssetCodec::ReadMeshAssetFileVFS(&scratch, vfs, expected.Record.Path, &source, &source_hierarchy, uuid, limits);
        if (read.Failed())
            return VFSResult<uint32_t>::Fail(read.Error());
        if (stopped())
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        Bounds(source);

        // A mesh is not independently renderable: each submesh references a
        // material which in turn owns the texture paths required by its GPU
        // record. Scene open uses this checked loader rather than ReloadFromDisk,
        // so ingest those dependencies before publishing the mesh draw records.
        // Each material decode is transient; its clone and texture work are
        // retained by AssetManager before the temporary range is released.
        for (const AssetSubMesh& submesh : source.SubMeshes)
        {
            if (submesh.MaterialUUID.is_nil())
                continue;
            ArenaScratch material_decode(&scratch);
            if (!IngestMaterialFromUUID(material_decode.Arena(), submesh.MaterialUUID))
                return VFSResult<uint32_t>::Fail(VFSError::NotFound);
            if (stopped())
                return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        }

        {
            std::lock_guard lock(IngestMutex);
            if (stopped() || Registry != registry)
                return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
            // Avoid a clone if an importer won during decoding.
            if (reuse())
                return VFSResult<uint32_t>::Ok(draws);
        }
        AssetMesh          mesh      = {};
        AssetNodeHierarchy hierarchy = {};
        // Large payload copies must not hold the lock needed by frame queries.
        auto               storage   = CloneMesh(ContainerSlab, source, source_hierarchy, mesh, hierarchy);
        for (uint32_t attempt = 0; !storage && attempt < 2; ++attempt)
        {
            // Keep the single decode lease throughout recovery: releasing it
            // while scratch is live would admit a second full workspace.
            std::unique_lock cache_lock(IngestMutex, std::try_to_lock);
            if (cache_lock.owns_lock())
            {
                PayloadSize requested = {};
                if (!MeasureMesh(source, source_hierarchy, requested))
                {
                    break;
                }
                const auto target = attempt == 0 ? requested.Bytes + sizeof(AssetPayloadAllocation) : SIZE_MAX;
                if (!TrimCpuCacheLocked(&source, target))
                {
                    break;
                }
                cache_lock.unlock();
                storage = CloneMesh(ContainerSlab, source, source_hierarchy, mesh, hierarchy);
            }
        }
        if (!storage)
            return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
        std::lock_guard lock(IngestMutex);
        if (stopped() || Registry != registry)
            return VFSResult<uint32_t>::Fail(VFSError::Cancelled);
        // A competing ingestion may also win during staging. Placement reuses
        // its complete binding; it must never replace or mark that binding stale.
        if (reuse())
            return VFSResult<uint32_t>::Ok(draws);
        const auto published = PublishMesh(expected, std::move(mesh), std::move(hierarchy), storage.get());
        if (published.Succeeded())
        {
            (void) storage.release();
            if (lease && !RetainMeshAssets(uuid, *lease))
            {
                return VFSResult<uint32_t>::Fail(VFSError::OutOfMemory);
            }
        }
        return published;
    }
} // namespace ZEngine::Managers
