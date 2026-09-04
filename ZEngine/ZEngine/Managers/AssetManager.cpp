#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <random>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Importers;

namespace ZEngine::Managers
{
    static AssetManager* s_Instance = nullptr;

    AssetManager*        AssetManager::Instance()
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
        s_Instance                          = ZPushStructCtor(arena, AssetManager);
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
        s_Instance->GPUMeshMaterials.init(s_Instance->Arena, 5000);
        s_Instance->Textures.init(s_Instance->Arena, 5000);
        s_Instance->UUIDToTextureHandle.init(slab, 5000);
        s_Instance->MeshToHierarchySlot.init(s_Instance->Arena, 5000);
        s_Instance->UUIDToMaterialSlot.init(slab, 5000);

        static Core::VFS::AssetRegistry s_registry;
        s_registry.Initialize(s_Instance->Arena);
        s_Instance->Registry = &s_registry;
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
            s_Instance->ContainerSlab.Shutdown();
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

    void AssetManager::IngestMesh(AssetMesh&& mesh, AssetNodeHierarchy&& hierarchy)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);
        // Use MeshToHierarchySlot — populated only after data is actually ingested.
        // IsRegistered / GetAsset both give false positives because VFSScanner
        // pre-registers UUIDs with SlotHandle=0 before any mesh data exists.
        if (s_Instance->MeshToHierarchySlot.find(mesh.MeshUUID) != nullptr)
            return;

        auto  mesh_slot = static_cast<uint32_t>(s_Instance->Meshes.size());
        auto& m         = s_Instance->Meshes.push_use({});
        m.MeshUUID      = mesh.MeshUUID;
        m.SubMeshes.init(s_Instance->Arena, mesh.SubMeshes.size());
        m.Vertices.init(s_Instance->Arena, mesh.Vertices.size(), mesh.Vertices.size());
        m.Indices.init(s_Instance->Arena, mesh.Indices.size(), mesh.Indices.size());
        Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
        Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
        for (auto& sub : mesh.SubMeshes)
            m.SubMeshes.push(sub);

        // Compute bounding sphere (centroid + max-distance radius) from vertex positions.
        // Vertices are 8 floats each: pos(xyz) nrm(xyz) uv(uv).
        {
            const uint32_t vcount = static_cast<uint32_t>(m.Vertices.size() / 8);
            if (vcount > 0)
            {
                Core::Maths::Vec3f sum = {};
                for (uint32_t vi = 0; vi < vcount; ++vi)
                    sum = sum + Core::Maths::Vec3f(m.Vertices[vi * 8], m.Vertices[vi * 8 + 1], m.Vertices[vi * 8 + 2]);
                m.BoundsCenter = sum * (1.f / static_cast<float>(vcount));

                float maxR     = 0.f;
                for (uint32_t vi = 0; vi < vcount; ++vi)
                {
                    Core::Maths::Vec3f d = Core::Maths::Vec3f(m.Vertices[vi * 8], m.Vertices[vi * 8 + 1], m.Vertices[vi * 8 + 2]) - m.BoundsCenter;
                    float              r = d.magnitude();
                    if (r > maxR)
                        maxR = r;
                }
                m.BoundsRadius = maxR;
            }
        }

        RegisterAsset(AssetType::MESH, m.MeshUUID, mesh_slot);

        auto  hier_slot     = static_cast<uint32_t>(s_Instance->NodeHierarchies.size());
        auto& h             = s_Instance->NodeHierarchies.push_use({});
        h.NodeHierarchyUUID = hierarchy.NodeHierarchyUUID;
        h.MeshUUID          = hierarchy.MeshUUID;

        h.Hierarchies.init(s_Instance->Arena, hierarchy.Hierarchies.size(), hierarchy.Hierarchies.size());
        h.LocalTransforms.init(s_Instance->Arena, hierarchy.LocalTransforms.size(), hierarchy.LocalTransforms.size());
        h.GlobalTransforms.init(s_Instance->Arena, hierarchy.GlobalTransforms.size(), hierarchy.GlobalTransforms.size());
        h.Names.init(s_Instance->Arena, hierarchy.Names.size());
        h.MaterialNames.init(s_Instance->Arena, hierarchy.MaterialNames.size());
        h.NodeNames.init(s_Instance->Arena, hierarchy.NodeNames.size() > 32 ? hierarchy.NodeNames.size() * 2 : 64);
        h.NodeMeshes.init(s_Instance->Arena, hierarchy.NodeMeshes.size() > 32 ? hierarchy.NodeMeshes.size() * 2 : 64);
        h.NodeMaterials.init(s_Instance->Arena, hierarchy.NodeMaterials.size() > 32 ? hierarchy.NodeMaterials.size() * 2 : 64);

        Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(Helpers::NodeHierarchy), hierarchy.Hierarchies.data(), hierarchy.Hierarchies.size() * sizeof(Helpers::NodeHierarchy));
        Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.LocalTransforms.data(), hierarchy.LocalTransforms.size() * sizeof(Core::Maths::Mat4f));
        Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.GlobalTransforms.data(), hierarchy.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f));

        for (auto& name : hierarchy.Names)
        {
            auto& n = h.Names.push_use({});
            n.init(s_Instance->Arena, name.c_str());
        }
        for (auto& mat_name : hierarchy.MaterialNames)
        {
            auto& n = h.MaterialNames.push_use({});
            n.init(s_Instance->Arena, mat_name.c_str());
        }
        for (const auto& [k, v] : hierarchy.NodeNames)
            h.NodeNames.insert(k, v);
        for (const auto& [k, v] : hierarchy.NodeMeshes)
            h.NodeMeshes.insert(k, v);
        for (const auto& [k, v] : hierarchy.NodeMaterials)
            h.NodeMaterials.insert(k, v);

        RegisterAsset(AssetType::MESH_HIERARCHY, h.NodeHierarchyUUID, hier_slot);
        s_Instance->MeshToHierarchySlot.insert(h.MeshUUID, hier_slot);

        // Notify the registry that both assets are now loaded so hot-reload callbacks fire.
        if (s_Instance->Registry)
        {
            s_Instance->Registry->SetState(m.MeshUUID, Core::VFS::AssetState::Loaded);
            s_Instance->Registry->SetState(h.NodeHierarchyUUID, Core::VFS::AssetState::Loaded);
        }
    }

    Rendering::Textures::TextureHandle AssetManager::IngestTexture(const uuids::uuid& uuid, const Core::Containers::String& path)
    {
        if (!s_Instance)
            return {};
        std::lock_guard lock(s_Instance->IngestMutex);

        // Dedup — return existing handle if already uploaded.
        // Use UUIDToTextureHandle (not IsRegistered): VFSScanner pre-registers texture
        // UUIDs without uploading them, so IsRegistered gives a false positive.
        if (auto* h = s_Instance->UUIDToTextureHandle.find(uuid))
        {
            // Already known — this is a reimport signal (e.g. TextureImporter after a file
            // edit). The handle value doesn't change: RRM's reload path reconstructs the
            // existing GPU resource in place. Ask RRM to do that on the render thread.
            if (s_Instance->Device && s_Instance->Device->RRM)
                static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM)->ScheduleTextureReload(uuid);
            return *h;
        }

        auto  slot          = static_cast<uint32_t>(s_Instance->Textures.size());
        auto& new_tex       = s_Instance->Textures.push_use({});
        new_tex.TextureUUID = uuid;
        new_tex.Path.init(s_Instance->Arena, path.c_str());

        if (!new_tex.Path.empty() && s_Instance->Device && s_Instance->Device->RRM)
        {
            char full_path[MAX_FILE_PATH_COUNT] = {};
            snprintf(full_path, sizeof(full_path), "%s%c%s", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, new_tex.Path.c_str());
            auto* rrm      = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
            new_tex.Handle = rrm->SubmitTextureFile(0, 0, full_path);
            if (!new_tex.Handle.Valid())
            {
                ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
                ZENGINE_LOG_ASSET_WARN("Texture not found: '{}' — using fallback", full_path)
                new_tex.Handle = s_Instance->FallbackTextureHandle;
            }
        }
        else
        {
            ZENGINE_VALIDATE_ASSERT(s_Instance->FallbackTextureHandle.Valid(), "FallbackTextureHandle not initialized — InitFallbackTexture must be called before ingesting assets")
            ZENGINE_LOG_ASSET_WARN("Texture {} has no path — extraction may have failed, using fallback", uuids::to_string(uuid))
            new_tex.Handle = s_Instance->FallbackTextureHandle;
        }

        RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, slot);
        s_Instance->UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
        return new_tex.Handle;
    }

    void AssetManager::IngestTextures(Core::Containers::Array<AssetTexture>&& textures)
    {
        if (!s_Instance)
            return;
        for (size_t i = 0; i < textures.size(); ++i)
            IngestTexture(textures[i].TextureUUID, textures[i].Path);
    }

    void AssetManager::IngestMaterial(AssetMaterial&& mat)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);
        // Use UUIDToMaterialSlot — populated only after data is actually ingested.
        // GetAsset gives false positives: VFSScanner pre-registers all .zematerial UUIDs
        // with SlotHandle=0, so GetAsset<AssetMaterial>(uuid_N) returns Materials[0]
        // (the first ingested material) for every subsequent material → all skipped.
        if (s_Instance->UUIDToMaterialSlot.find(mat.MaterialUUID) != nullptr)
            return;

        auto slot = static_cast<uint32_t>(s_Instance->Materials.size());
        s_Instance->Materials.push(mat);
        RegisterAsset(AssetType::MATERIAL, mat.MaterialUUID, slot);
        s_Instance->UUIDToMaterialSlot.insert(mat.MaterialUUID, slot);

        Rendering::Meshes::MeshMaterial& gpu_mat = s_Instance->GPUMeshMaterials.push_use({});
        gpu_mat.AlbedoColor                      = mat.AlbedoColor;
        gpu_mat.EmissiveColor                    = mat.EmissiveColor;
        gpu_mat.RoughnessColor                   = mat.RoughnessColor;
        gpu_mat.SpecularColor                    = mat.SpecularColor;
        gpu_mat.AmbientColor                     = mat.AmbientColor;
        gpu_mat.Factors                          = mat.Factors;

        // Resolve handle for each texture slot: UUID lookup first, then fall back to
        // uploading from the stored path — handles scene-reload and dragged-.zmesh cases
        // where IngestTextures may not have run yet for this material's textures.
        gpu_mat.AlbedoMap                        = ResolveTextureMapIndex(mat.AlbedoTexUUID, mat.AlbedoTexPath);
        gpu_mat.EmissiveMap                      = ResolveTextureMapIndex(mat.EmissiveTexUUID, mat.EmissiveTexPath);
        gpu_mat.NormalMap                        = ResolveTextureMapIndex(mat.NormalTexUUID, mat.NormalTexPath);
        gpu_mat.OpacityMap                       = ResolveTextureMapIndex(mat.OpacityTexUUID, mat.OpacityTexPath);
        gpu_mat.SpecularMap                      = ResolveTextureMapIndex(mat.SpecularTexUUID, mat.SpecularTexPath);
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

    void AssetManager::ReleaseTexture(const uuids::uuid& uuid)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->PendingTextureReleaseMutex);
        if (s_Instance->PendingTextureReleaseCount >= MAX_PENDING_TEXTURE_RELEASES)
        {
            ZENGINE_LOG_ASSET_WARN("[AssetManager] Pending texture release queue full — dropping release for {}", uuids::to_string(uuid))
            return;
        }
        s_Instance->PendingTextureReleases[s_Instance->PendingTextureReleaseCount++] = uuid;
    }

    void AssetManager::FlushTextureReleases()
    {
        if (!s_Instance)
            return;

        uuids::uuid local[MAX_PENDING_TEXTURE_RELEASES];
        uint32_t    count = 0;
        {
            std::lock_guard lock(s_Instance->PendingTextureReleaseMutex);
            count = s_Instance->PendingTextureReleaseCount;
            Helpers::secure_memcpy(local, sizeof(local), s_Instance->PendingTextureReleases, count * sizeof(local[0]));
            s_Instance->PendingTextureReleaseCount = 0;
        }
        if (count == 0)
            return;

        // IngestMutex, not just PendingTextureReleaseMutex: Materials/GPUMeshMaterials are
        // unsynchronized and also written by IngestMaterial/IngestTexture under this lock.
        std::lock_guard lock(s_Instance->IngestMutex);
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
                    gpu_mat.AlbedoMap = INVALID_MAP_HANDLE;
                if (mat.EmissiveTexUUID == local[r])
                    gpu_mat.EmissiveMap = INVALID_MAP_HANDLE;
                if (mat.NormalTexUUID == local[r])
                    gpu_mat.NormalMap = INVALID_MAP_HANDLE;
                if (mat.OpacityTexUUID == local[r])
                    gpu_mat.OpacityMap = INVALID_MAP_HANDLE;
                if (mat.SpecularTexUUID == local[r])
                    gpu_mat.SpecularMap = INVALID_MAP_HANDLE;
            }
        }
    }

    Importers::AssetMesh* AssetManager::GetMeshAsset(const uuids::uuid& id)
    {
        if (!Registry)
            return nullptr;
        const Core::VFS::AssetRecord* rec = Registry->FindByUUID(id);
        if (!rec)
            return nullptr;
        uint32_t index = ReadAssetHandleIndex(rec->SlotHandle);
        return index < Meshes.size() ? &Meshes[index] : nullptr;
    }

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& mesh_id)
    {
        if (!s_Instance)
            return nullptr;
        const uint32_t* slot = s_Instance->MeshToHierarchySlot.find(mesh_id);
        return (slot && *slot < s_Instance->NodeHierarchies.size()) ? &s_Instance->NodeHierarchies[*slot] : nullptr;
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
                if (rec->State == Core::VFS::AssetState::Loaded)
                    continue;
                if (s_Instance->MeshToHierarchySlot.find(rec->UUID) != nullptr)
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

} // namespace ZEngine::Managers
