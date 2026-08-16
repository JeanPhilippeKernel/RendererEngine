#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
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

    // ── Direct ingest methods — called from ImportCoordinator thread ─────────────

    void AssetManager::IngestMesh(AssetMesh&& mesh, AssetNodeHierarchy&& hierarchy)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);

        // Mesh
        auto            mesh_slot = static_cast<uint32_t>(s_Instance->Meshes.size());
        auto&           m         = s_Instance->Meshes.push_use({});
        m.MeshUUID                = mesh.MeshUUID;
        m.SubMeshes.init(&s_Instance->Arena, mesh.SubMeshes.size());
        m.Vertices.init(&s_Instance->Arena, mesh.Vertices.size(), mesh.Vertices.size());
        m.Indices.init(&s_Instance->Arena, mesh.Indices.size(), mesh.Indices.size());
        Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
        Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
        for (auto& sub : mesh.SubMeshes)
            m.SubMeshes.push(sub);
        RegisterAsset(AssetType::MESH, m.MeshUUID, mesh_slot);

        // Hierarchy
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

        Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(AssetNodeHierarchy), hierarchy.Hierarchies.data(), hierarchy.Hierarchies.size() * sizeof(AssetNodeHierarchy));
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

        // Notify the registry that both assets are now loaded so hot-reload callbacks fire.
        if (s_Instance->Registry)
        {
            s_Instance->Registry->SetState(m.MeshUUID, Core::VFS::AssetState::Loaded);
            s_Instance->Registry->SetState(h.NodeHierarchyUUID, Core::VFS::AssetState::Loaded);
        }
    }

    void AssetManager::IngestTextures(Core::Containers::Array<AssetTexture>&& textures)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);

        for (size_t i = 0; i < textures.size(); ++i)
        {
            auto& tex           = textures[i];
            auto  slot          = static_cast<uint32_t>(s_Instance->Textures.size());
            auto& new_tex       = s_Instance->Textures.push_use({});
            new_tex.TextureUUID = tex.TextureUUID;

            // Store the path in the arena first so the pointer is stable
            // for the async GPU upload that outlives this stack frame.
            new_tex.Path.init(&s_Instance->Arena, tex.Path.c_str());
            if (!new_tex.Path.empty() && s_Instance->Device->RRM)
            {
                const auto               path = fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, new_tex.Path.c_str());
                Core::Containers::String stable_path;
                stable_path.init(&s_Instance->Arena, path.c_str());
                auto* rrm      = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
                new_tex.Handle = rrm->SubmitTextureFile(0, 0, stable_path.c_str());
                if (!new_tex.Handle.Valid())
                {
                    ZENGINE_LOG_ASSET_WARN("Texture not found: '{}' — using fallback", stable_path.c_str())
                    new_tex.Handle = s_Instance->FallbackTextureHandle;
                }
            }
            else if (new_tex.Path.empty())
            {
                // No path at all — use fallback so the slot is never null
                new_tex.Handle = s_Instance->FallbackTextureHandle;
            }

            RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, slot);
            s_Instance->UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
        }
    }

    void AssetManager::IngestMaterial(AssetMaterial&& mat)
    {
        if (!s_Instance)
            return;
        std::lock_guard lock(s_Instance->IngestMutex);

        auto            slot = static_cast<uint32_t>(s_Instance->Materials.size());
        s_Instance->Materials.push(mat);
        RegisterAsset(AssetType::MATERIAL, mat.MaterialUUID, slot);

        Rendering::Meshes::MeshMaterial& gpu_mat = s_Instance->GPUMeshMaterials.push_use({});
        gpu_mat.AlbedoColor                      = mat.AlbedoColor;
        gpu_mat.EmissiveColor                    = mat.EmissiveColor;
        gpu_mat.RoughnessColor                   = mat.RoughnessColor;
        gpu_mat.SpecularColor                    = mat.SpecularColor;
        gpu_mat.AmbientColor                     = mat.AmbientColor;
        gpu_mat.Factors                          = mat.Factors;

        auto tex_handle                          = [&](const uuids::uuid& id) -> uint32_t {
            if (id.is_nil())
                return 0;
            auto* h = s_Instance->UUIDToTextureHandle.find(id);
            return h ? h->Index : 0;
        };
        gpu_mat.AlbedoMap   = tex_handle(mat.AlbedoTexUUID);
        gpu_mat.EmissiveMap = tex_handle(mat.EmissiveTexUUID);
        gpu_mat.NormalMap   = tex_handle(mat.NormalTexUUID);
        gpu_mat.OpacityMap  = tex_handle(mat.OpacityTexUUID);
        gpu_mat.SpecularMap = tex_handle(mat.SpecularTexUUID);
    }

    // ── CPU buffer accessors ──────────────────────────────────────────────────────

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

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& id)
    {
        if (!Registry)
            return nullptr;
        for (size_t i = 0; i < NodeHierarchies.size(); ++i)
            if (NodeHierarchies[i].MeshUUID == id)
                return &NodeHierarchies[i];
        return nullptr;
    }

    AssetHandle AssetManager::GetMeshNodeHierarchyHandle(const uuids::uuid& id)
    {
        if (!Registry)
            return 0;
        const Core::VFS::AssetRecord* rec = Registry->FindByUUID(id);
        return rec ? rec->SlotHandle : 0;
    }

    AssetHandle AssetManager::GetMaterialHandleFromUUID(const uuids::uuid& material_uuid)
    {
        if (!Registry)
            return 0;
        const Core::VFS::AssetRecord* rec = Registry->FindByUUID(material_uuid);
        return rec ? rec->SlotHandle : 0;
    }

    Importers::AssetTexture* AssetManager::LoadTextureFileAsAsset(cstring file, bool absolute)
    {
        if (!Helpers::secure_strlen(file))
            return nullptr;

        auto                         asset_id = static_cast<uint32_t>(Textures.size());
        auto&                        new_tex  = Textures.push_use({});

        std::random_device           rd;
        std::mt19937                 generator(rd());
        uuids::uuid_random_generator gen{generator};
        new_tex.TextureUUID = gen();

        new_tex.Path.init(&s_Instance->Arena, file);
        {
            const auto               tex_path_str = absolute ? std::string(file) : fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, file);
            Core::Containers::String stable_path;
            stable_path.init(&s_Instance->Arena, tex_path_str.c_str());
            if (s_Instance->Device->RRM)
            {
                auto* rrm      = static_cast<Rendering::RenderResourceManager*>(s_Instance->Device->RRM);
                new_tex.Handle = rrm->SubmitTextureFile(0, 0, stable_path.c_str());
                if (!new_tex.Handle.Valid())
                {
                    ZENGINE_LOG_ASSET_WARN("Texture not found: '{}' — using fallback", stable_path.c_str())
                    new_tex.Handle = s_Instance->FallbackTextureHandle;
                }
            }
        }

        RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, asset_id);
        UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
        return &new_tex;
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
