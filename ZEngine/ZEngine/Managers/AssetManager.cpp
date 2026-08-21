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
        s_Instance = ZPushStructCtor(arena, AssetManager);
        arena->CreateSubArena(ZMega(78), &s_Instance->Arena);

        s_Instance->Device                  = device;
        s_Instance->CurrentWorkingSpacePath = working_space_path;

        s_Instance->NodeHierarchies.init(&s_Instance->Arena, 5000);
        s_Instance->Meshes.init(&s_Instance->Arena, 5000);
        s_Instance->Materials.init(&s_Instance->Arena, 5000);
        s_Instance->GPUMeshMaterials.init(&s_Instance->Arena, 5000);
        s_Instance->Textures.init(&s_Instance->Arena, 5000);
        s_Instance->UUIDToTextureHandle.init(&s_Instance->Arena, 5000);
        s_Instance->MeshToHierarchySlot.init(&s_Instance->Arena, 5000);
        s_Instance->UUIDToMaterialSlot.init(&s_Instance->Arena, 5000);

        static Core::VFS::AssetRegistry s_registry;
        s_registry.Initialize(&s_Instance->Arena);
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

    void        AssetManager::Shutdown() {}

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
        m.SubMeshes.init(&s_Instance->Arena, mesh.SubMeshes.size());
        m.Vertices.init(&s_Instance->Arena, mesh.Vertices.size(), mesh.Vertices.size());
        m.Indices.init(&s_Instance->Arena, mesh.Indices.size(), mesh.Indices.size());
        Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
        Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
        for (auto& sub : mesh.SubMeshes)
            m.SubMeshes.push(sub);
        RegisterAsset(AssetType::MESH, m.MeshUUID, mesh_slot);

        auto  hier_slot     = static_cast<uint32_t>(s_Instance->NodeHierarchies.size());
        auto& h             = s_Instance->NodeHierarchies.push_use({});
        h.NodeHierarchyUUID = hierarchy.NodeHierarchyUUID;
        h.MeshUUID          = hierarchy.MeshUUID;

        h.Hierarchies.init(&s_Instance->Arena, hierarchy.Hierarchies.size(), hierarchy.Hierarchies.size());
        h.LocalTransforms.init(&s_Instance->Arena, hierarchy.LocalTransforms.size(), hierarchy.LocalTransforms.size());
        h.GlobalTransforms.init(&s_Instance->Arena, hierarchy.GlobalTransforms.size(), hierarchy.GlobalTransforms.size());
        h.Names.init(&s_Instance->Arena, hierarchy.Names.size());
        h.MaterialNames.init(&s_Instance->Arena, hierarchy.MaterialNames.size());
        h.NodeNames.init(&s_Instance->Arena, hierarchy.NodeNames.size() > 32 ? hierarchy.NodeNames.size() * 2 : 64);
        h.NodeMeshes.init(&s_Instance->Arena, hierarchy.NodeMeshes.size() > 32 ? hierarchy.NodeMeshes.size() * 2 : 64);
        h.NodeMaterials.init(&s_Instance->Arena, hierarchy.NodeMaterials.size() > 32 ? hierarchy.NodeMaterials.size() * 2 : 64);

        Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(Helpers::NodeHierarchy), hierarchy.Hierarchies.data(), hierarchy.Hierarchies.size() * sizeof(Helpers::NodeHierarchy));
        Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.LocalTransforms.data(), hierarchy.LocalTransforms.size() * sizeof(Core::Maths::Mat4f));
        Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchy.GlobalTransforms.data(), hierarchy.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f));

        for (auto& name : hierarchy.Names)
        {
            auto& n = h.Names.push_use({});
            n.init(&s_Instance->Arena, name.c_str());
        }
        for (auto& mat_name : hierarchy.MaterialNames)
        {
            auto& n = h.MaterialNames.push_use({});
            n.init(&s_Instance->Arena, mat_name.c_str());
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
            return *h;

        auto  slot          = static_cast<uint32_t>(s_Instance->Textures.size());
        auto& new_tex       = s_Instance->Textures.push_use({});
        new_tex.TextureUUID = uuid;
        new_tex.Path.init(&s_Instance->Arena, path.c_str());

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

        // Resolve handle for a texture slot: UUID lookup first, then fall back to uploading
        // from the stored path — handles scene-reload and dragged-.zmesh cases where
        // IngestTextures may not have run yet for this material's textures.
        auto tex_handle                          = [&](const uuids::uuid& id, const Core::Containers::String& path) -> uint32_t {
            if (id.is_nil())
                return INVALID_MAP_HANDLE;
            auto* h = s_Instance->UUIDToTextureHandle.find(id);
            if (h)
                return h->Index;
            if (!path.empty())
                return IngestTexture(id, path).Index;
            return INVALID_MAP_HANDLE;
        };
        gpu_mat.AlbedoMap   = tex_handle(mat.AlbedoTexUUID, mat.AlbedoTexPath);
        gpu_mat.EmissiveMap = tex_handle(mat.EmissiveTexUUID, mat.EmissiveTexPath);
        gpu_mat.NormalMap   = tex_handle(mat.NormalTexUUID, mat.NormalTexPath);
        gpu_mat.OpacityMap  = tex_handle(mat.OpacityTexUUID, mat.OpacityTexPath);
        gpu_mat.SpecularMap = tex_handle(mat.SpecularTexUUID, mat.SpecularTexPath);
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

        // Meshes — deserialize each .zemesh that has not been ingested yet.
        {
            auto result = s_Instance->Registry->Query({.Type = AssetType::MESH}, scratch);
            for (uint32_t i = 0; i < result.Handles.size(); ++i)
            {
                auto* rec = s_Instance->Registry->Access(result.Handles[i]);
                if (!rec || rec->UUID.is_nil())
                    continue;
                if (s_Instance->MeshToHierarchySlot.find(rec->UUID) != nullptr)
                    continue;

                char native[MAX_FILE_PATH_COUNT] = {};
                rec->Path.ResolveNative(s_Instance->CurrentWorkingSpacePath, native, sizeof(native));

                AssetMesh          mesh = {};
                AssetNodeHierarchy hier = {};
                Importers::AssetCodec::DeserializeMeshAssetFile(scratch, native, mesh, hier);
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
