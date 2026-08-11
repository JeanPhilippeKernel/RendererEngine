#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
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
        arena->CreateSubArena(ZMega(20), &s_Instance->ThreadLocalArena);
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

    void AssetManager::Run()
    {
        Helpers::ThreadPoolHelper::Submit([instance = s_Instance]() { instance->__Run(); });
    }

    void AssetManager::Shutdown()
    {
        if (!s_Instance)
            return;
        std::lock_guard l(s_Instance->Mut);
        s_Instance->RequestShutdown.store(true, std::memory_order_release);
        s_Instance->Cond.notify_all();
    }

    bool AssetManager::IsLoadingAsset()
    {
        return s_Instance->IsLoading.load(std::memory_order_acquire);
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

    void AssetManager::LoadAssetFile(const Importers::AssetImporterOutput& file)
    {
        if (file.Path.empty())
            return;
        s_Instance->PendingAssetFiles.Enqueue(file);
        s_Instance->Cond.notify_one();
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

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& id)
    {
        if (!Registry)
            return nullptr;

        // First check if we have a direct mesh→hierarchy mapping via MeshUUID on the records
        const Core::VFS::AssetRecord* mesh_rec = Registry->FindByUUID(id);
        if (!mesh_rec)
            return nullptr;

        // Walk all node hierarchies to find the one referencing this mesh UUID
        // (NodeHierarchy's MeshUUID links back to the mesh)
        for (size_t i = 0; i < NodeHierarchies.size(); ++i)
        {
            if (NodeHierarchies[i].MeshUUID == id)
                return &NodeHierarchies[i];
        }
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

        const auto tex_path = absolute ? std::string(file) : fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, file);

        new_tex.Handle      = s_Instance->Device->AsyncResLoader->Submit(0, 0, {.TextureUpload = {.Filename = tex_path.c_str()}});
        new_tex.Path.init(&s_Instance->Arena, file);

        RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, asset_id);
        UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
        return &new_tex;
    }

    void AssetManager::__Run()
    {
        while (true)
        {
            if (!s_Instance)
                break;

            auto&            mut      = s_Instance->Mut;
            auto&            cond     = s_Instance->Cond;
            auto&            files    = s_Instance->PendingAssetFiles;
            auto&            meshes   = s_Instance->PendingAssetMeshes;
            auto&            hiers    = s_Instance->PendingAssetNodeHierarchies;
            auto&            mats     = s_Instance->PendingAssetMaterials;
            auto&            textures = s_Instance->PendingAssetTextures;

            std::unique_lock l(mut);
            cond.wait(l, [&] { return s_Instance->RequestShutdown.load(std::memory_order_acquire) || !files.Empty() || !meshes.Empty() || !hiers.Empty() || !mats.Empty() || !textures.Empty(); });

            if (s_Instance->RequestShutdown.load(std::memory_order_acquire))
                break;

            AssetMesh mesh = {};
            if (meshes.Pop(mesh))
            {
                auto  slot = static_cast<uint32_t>(Meshes.size());
                auto& m    = Meshes.push_use({});
                m.MeshUUID = mesh.MeshUUID;
                m.SubMeshes.init(&s_Instance->Arena, mesh.SubMeshes.size());
                m.Vertices.init(&s_Instance->Arena, mesh.Vertices.size(), mesh.Vertices.size());
                m.Indices.init(&s_Instance->Arena, mesh.Indices.size(), mesh.Indices.size());

                Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
                Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));
                for (auto& sub : mesh.SubMeshes)
                    m.SubMeshes.push(sub);

                RegisterAsset(AssetType::MESH, m.MeshUUID, slot);
                continue;
            }

            AssetNodeHierarchy hier = {};
            if (hiers.Pop(hier))
            {
                auto  slot = static_cast<uint32_t>(NodeHierarchies.size());
                auto& h    = NodeHierarchies.push_use({});
                h.MeshUUID = hier.MeshUUID;

                h.Hierarchies.init(&s_Instance->Arena, hier.Hierarchies.size(), hier.Hierarchies.size());
                h.LocalTransforms.init(&s_Instance->Arena, hier.LocalTransforms.size(), hier.LocalTransforms.size());
                h.GlobalTransforms.init(&s_Instance->Arena, hier.GlobalTransforms.size(), hier.GlobalTransforms.size());
                h.Names.init(&s_Instance->Arena, hier.Names.size());
                h.MaterialNames.init(&s_Instance->Arena, hier.MaterialNames.size());
                h.NodeNames.init(&s_Instance->Arena, hier.NodeNames.size() > 32 ? hier.NodeNames.size() * 2 : 64);
                h.NodeMeshes.init(&s_Instance->Arena, hier.NodeMeshes.size() > 32 ? hier.NodeMeshes.size() * 2 : 64);
                h.NodeMaterials.init(&s_Instance->Arena, hier.NodeMaterials.size() > 32 ? hier.NodeMaterials.size() * 2 : 64);

                Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(AssetNodeHierarchy), hier.Hierarchies.data(), hier.Hierarchies.size() * sizeof(AssetNodeHierarchy));
                Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(Core::Maths::Mat4f), hier.LocalTransforms.data(), hier.LocalTransforms.size() * sizeof(Core::Maths::Mat4f));
                Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f), hier.GlobalTransforms.data(), hier.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f));

                for (auto& name : hier.Names)
                {
                    auto& n = h.Names.push_use({});
                    n.init(&s_Instance->Arena, name.c_str());
                }
                for (auto& mat_name : hier.MaterialNames)
                {
                    auto& n = h.MaterialNames.push_use({});
                    n.init(&s_Instance->Arena, mat_name.c_str());
                }
                for (const auto& [k, v] : hier.NodeNames)
                    h.NodeNames.insert(k, v);
                for (const auto& [k, v] : hier.NodeMeshes)
                    h.NodeMeshes.insert(k, v);
                for (const auto& [k, v] : hier.NodeMaterials)
                    h.NodeMaterials.insert(k, v);

                RegisterAsset(AssetType::MESH_HIERARCHY, h.NodeHierarchyUUID, slot);
                continue;
            }

            Array<AssetTexture> tex_batch = {};
            if (textures.Pop(tex_batch))
            {
                for (auto& tex : tex_batch)
                {
                    auto  slot          = static_cast<uint32_t>(Textures.size());
                    auto& new_tex       = Textures.push_use({});
                    new_tex.TextureUUID = tex.TextureUUID;

                    const auto path     = fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, tex.Path.c_str());
                    new_tex.Handle      = s_Instance->Device->AsyncResLoader->Submit(0, 0, {.TextureUpload = {.Filename = path.c_str()}});
                    new_tex.Path.init(&s_Instance->Arena, tex.Path.c_str());

                    RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, slot);
                    UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
                }
                continue;
            }

            AssetMaterial mat = {};
            if (mats.Pop(mat))
            {
                auto slot = static_cast<uint32_t>(Materials.size());
                Materials.push(mat);

                RegisterAsset(AssetType::MATERIAL, mat.MaterialUUID, slot);

                Rendering::Meshes::MeshMaterial& gpu_mat = GPUMeshMaterials.push_use({});
                gpu_mat.AlbedoColor                      = mat.AlbedoColor;
                gpu_mat.EmissiveColor                    = mat.EmissiveColor;
                gpu_mat.RoughnessColor                   = mat.RoughnessColor;
                gpu_mat.SpecularColor                    = mat.SpecularColor;
                gpu_mat.AmbientColor                     = mat.AmbientColor;
                gpu_mat.Factors                          = mat.Factors;

                auto tex_handle                          = [&](const uuids::uuid& id) -> uint32_t {
                    if (id.is_nil())
                        return 0;
                    auto* h = UUIDToTextureHandle.find(id);
                    return h ? h->Index : 0;
                };
                gpu_mat.AlbedoMap   = tex_handle(mat.AlbedoTexUUID);
                gpu_mat.EmissiveMap = tex_handle(mat.EmissiveTexUUID);
                gpu_mat.NormalMap   = tex_handle(mat.NormalTexUUID);
                gpu_mat.OpacityMap  = tex_handle(mat.OpacityTexUUID);
                gpu_mat.SpecularMap = tex_handle(mat.SpecularTexUUID);
                continue;
            }

            ThreadLocalArena.Clear();
            Importers::AssetImporterOutput file = {};
            if (files.Pop(file))
            {
                auto path = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);

                if (file.Type == Importers::AssetFileType::MESH)
                {
                    AssetMesh          m  = {};
                    AssetNodeHierarchy nh = {};
                    IAssetImporter::DeserializeMeshAssetFile(&ThreadLocalArena, path.c_str(), m, nh);
                    PendingAssetMeshes.Enqueue(m);
                    PendingAssetNodeHierarchies.Enqueue(nh);
                }
                else if (file.Type == Importers::AssetFileType::TEXTURES)
                {
                    Array<AssetTexture> t = {};
                    IAssetImporter::DeserializeTextureAssetFile(&ThreadLocalArena, path.c_str(), t);
                    PendingAssetTextures.Enqueue(t);
                }
                else if (file.Type == Importers::AssetFileType::MATERIAL)
                {
                    AssetMaterial m = {};
                    IAssetImporter::DeserializeMaterialAssetFile(&ThreadLocalArena, path.c_str(), m);
                    PendingAssetMaterials.Enqueue(m);
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
