#include <AssetManager.h>
#include <Helpers/MemoryOperations.h>
#include <Helpers/ThreadPool.h>
#include <Importers/IAssetImporter.h>
#include <Rendering/Meshes/Mesh.h>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Importers;

namespace ZEngine::Managers
{
    static AssetManager* s_Instance = nullptr;

    AssetManager*        AssetManager::Instance()
    {
        return s_Instance;
    }

    AssetManager::AssetHandle AssetManager::CreateHandle(uint32_t id, AssetType at)
    {
        return ((uint32_t(at) & 0xF) << 28) | (id & 0x0FFFFFFF);
    }

    uint32_t AssetManager::ReadAssetHandleIndex(AssetHandle h)
    {
        return (h & 0x0FFFFFFF);
    }

    AssetType AssetManager::ReadAssetHandleType(AssetHandle h)
    {
        return AssetType((h >> 28) & 0xF);
    }

    void AssetManager::Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path)
    {
        s_Instance = ZPushStructCtor(arena, AssetManager);
        arena->CreateSubArena(ZMega(100), &(s_Instance->ThreadLocalArena));
        arena->CreateSubArena(ZMega(70), &(s_Instance->Arena));

        s_Instance->Device                  = device;
        s_Instance->CurrentWorkingSpacePath = working_space_path;

        s_Instance->NodeHierarchies.init(&(s_Instance->Arena), 5000);
        s_Instance->Meshes.init(&(s_Instance->Arena), 5000);
        s_Instance->Materials.init(&(s_Instance->Arena), 5000);
        s_Instance->GPUMeshMaterials.init(&(s_Instance->Arena), 5000);
        s_Instance->Textures.init(&(s_Instance->Arena), 5000);
        s_Instance->UUIDToHandle.init(&(s_Instance->Arena), 5000);
        s_Instance->HandleToUUID.init(&(s_Instance->Arena), 5000);
        s_Instance->UUIDToTextureHandle.init(&(s_Instance->Arena), 5000);

        s_Instance->MeshToNodeHierarchy.init(&(s_Instance->Arena), 5000);
        s_Instance->NodeHierarchyToMesh.init(&(s_Instance->Arena), 5000);
    }

    void AssetManager::Run()
    {
        Helpers::ThreadPoolHelper::Submit([instance = s_Instance]() { instance->__Run(); });
    }

    void AssetManager::Shutdown()
    {
        {
            if (!s_Instance)
            {
                return;
            }

            std::lock_guard l(s_Instance->Mut);
            s_Instance->RequestShutdown.store(true, std::memory_order_release);
            s_Instance->Cond.notify_all();
        }
    }

    bool AssetManager::IsLoadingAsset()
    {
        return s_Instance->IsLoading.load(std::memory_order_acquire);
    }

    AssetManager::AssetHandle AssetManager::RegisterAsset(AssetType type, const uuids::uuid& uid, uint32_t asset_id)
    {
        auto handle = CreateHandle(asset_id, type);
        if (!s_Instance->UUIDToHandle.contains(uid))
        {
            s_Instance->UUIDToHandle.insert(uid, handle);
        }

        if (!s_Instance->HandleToUUID.contains(handle))
        {
            s_Instance->HandleToUUID.insert(handle, uid);
        }
        return handle;
    }

    void AssetManager::LoadAssetFile(const Importers::AssetImporterOutput& file)
    {
        if (file.Path.empty())
        {
            return;
        }

        s_Instance->PendingAssetFiles.Enqueue(file);
        s_Instance->Cond.notify_one();
    }

    Importers::AssetMesh* AssetManager::GetMeshAsset(const uuids::uuid& id)
    {
        if (!UUIDToHandle.contains(id))
        {
            return nullptr;
        }

        auto&    handle = UUIDToHandle.at(id);
        uint32_t index  = ReadAssetHandleIndex(handle);
        if (index >= Meshes.size())
        {
            return nullptr;
        }
        return &Meshes[index];
    }

    Importers::AssetNodeHierarchy* AssetManager::GetMeshNodeHierarchy(const uuids::uuid& id)
    {
        Importers::AssetNodeHierarchy* output = nullptr;
        if (!s_Instance)
        {
            return output;
        }

        if (MeshToNodeHierarchy.contains(id))
        {
            auto& hierarchy_uuid = MeshToNodeHierarchy.at(id);
            auto  handle         = UUIDToHandle.at(hierarchy_uuid);
            auto  id             = ReadAssetHandleIndex(handle);
            output               = &NodeHierarchies[id];
            return output;
        }

        for (auto& h : NodeHierarchies)
        {
            if (h.MeshUUID == id)
            {
                output = &h;
                break;
            }
        }

        if (output)
        {
            MeshToNodeHierarchy.insert(id, output->NodeHierarchyUUID);
            NodeHierarchyToMesh.insert(output->NodeHierarchyUUID, id);
        }

        return output;
    }

    AssetManager::AssetHandle AssetManager::GetMeshNodeHierarchyHandle(const uuids::uuid& id)
    {
        AssetManager::AssetHandle handle    = {};

        auto                      hierarchy = GetMeshNodeHierarchy(id);
        if (hierarchy)
        {
            handle = UUIDToHandle.at(hierarchy->NodeHierarchyUUID);
        }
        return handle;
    }

    AssetManager::AssetHandle AssetManager::GetMaterialHandleFromUUID(const uuids::uuid& material_uuid)
    {
        AssetManager::AssetHandle handle = {};
        if (UUIDToHandle.contains(material_uuid))
        {
            handle = UUIDToHandle.at(material_uuid);
        }
        return handle;
    }

    Importers::AssetTexture* AssetManager::LoadTextureFileAsAsset(cstring file, bool absolute)
    {
        if (!Helpers::secure_strlen(file))
        {
            return nullptr;
        }

        auto                         asset_id = (uint32_t) Textures.size();

        auto&                        new_tex  = Textures.push_use({});

        std::random_device           rd;
        std::mt19937                 generator(rd());
        uuids::uuid_random_generator gen(&generator);
        new_tex.TextureUUID          = gen();

        const auto tex_absolute_path = absolute ? std::string(file) : fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, file);
        new_tex.Handle               = s_Instance->Device->AsyncResLoader->Submit(0, 0, {.TextureUpload = {.Filename = tex_absolute_path.c_str()}});
        new_tex.Path.init(&(s_Instance->Arena), file);

        RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, asset_id);

        UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);

        return &new_tex;
    }

    void AssetManager::__Run()
    {
        while (true)
        {
            if (!s_Instance)
            {
                break;
            }

            auto&            mut                             = s_Instance->Mut;
            auto&            cond                            = s_Instance->Cond;
            auto&            pendings_asset_files            = s_Instance->PendingAssetFiles;
            auto&            pendings_asset_meshes           = s_Instance->PendingAssetMeshes;
            auto&            pendings_asset_node_hierarchies = s_Instance->PendingAssetNodeHierarchies;
            auto&            pendings_asset_materials        = s_Instance->PendingAssetMaterials;
            auto&            pendings_asset_textures         = s_Instance->PendingAssetTextures;

            std::unique_lock l(mut);
            cond.wait(l, [&] { return (true == s_Instance->RequestShutdown.load(std::memory_order_acquire)) || !pendings_asset_files.Empty() || !pendings_asset_meshes.Empty() || !pendings_asset_node_hierarchies.Empty() || !pendings_asset_materials.Empty() || !pendings_asset_textures.Empty(); });

            if (auto shutdown = s_Instance->RequestShutdown.load(std::memory_order_acquire))
            {
                break;
            }

            AssetMesh mesh = {};
            if (pendings_asset_meshes.Pop(mesh))
            {
                auto  asset_id = (uint32_t) Meshes.size();
                auto& m        = Meshes.push_use({});
                m.MeshUUID     = mesh.MeshUUID;
                m.SubMeshes.init(&(s_Instance->Arena), mesh.SubMeshes.size());
                m.Vertices.init(&(s_Instance->Arena), mesh.Vertices.size(), mesh.Vertices.size());
                m.Indices.init(&(s_Instance->Arena), mesh.Indices.size(), mesh.Indices.size());

                Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
                Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));

                for (auto& submesh : mesh.SubMeshes)
                {
                    m.SubMeshes.push(submesh);
                }

                RegisterAsset(AssetType::MESH, m.MeshUUID, asset_id);

                continue;
            }

            AssetNodeHierarchy hierarchies = {};
            if (pendings_asset_node_hierarchies.Pop(hierarchies))
            {
                auto  asset_id = NodeHierarchies.size();
                auto& h        = NodeHierarchies.push_use({});
                h.MeshUUID     = hierarchies.MeshUUID;

                h.Hierarchies.init(&(s_Instance->Arena), hierarchies.Hierarchies.size(), hierarchies.Hierarchies.size());
                h.LocalTransforms.init(&(s_Instance->Arena), hierarchies.LocalTransforms.size(), hierarchies.LocalTransforms.size());
                h.GlobalTransforms.init(&(s_Instance->Arena), hierarchies.GlobalTransforms.size(), hierarchies.GlobalTransforms.size());

                h.Names.init(&(s_Instance->Arena), hierarchies.Names.size());
                h.MaterialNames.init(&(s_Instance->Arena), hierarchies.MaterialNames.size());
                h.NodeNames.init(&(s_Instance->Arena), hierarchies.NodeNames.size() > 32 ? hierarchies.NodeNames.size() * 2 : 64);
                h.NodeMeshes.init(&(s_Instance->Arena), hierarchies.NodeMeshes.size() > 32 ? hierarchies.NodeMeshes.size() * 2 : 64);
                h.NodeMaterials.init(&(s_Instance->Arena), hierarchies.NodeMaterials.size() > 32 ? hierarchies.NodeMaterials.size() * 2 : 64);

                Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(AssetNodeHierarchy), hierarchies.Hierarchies.data(), hierarchies.Hierarchies.size() * sizeof(AssetNodeHierarchy));
                Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchies.LocalTransforms.data(), hierarchies.LocalTransforms.size() * sizeof(Core::Maths::Mat4f));
                Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f), hierarchies.GlobalTransforms.data(), hierarchies.GlobalTransforms.size() * sizeof(Core::Maths::Mat4f));

                for (auto& name : hierarchies.Names)
                {
                    auto& n = h.Names.push_use({});
                    n.init(&(s_Instance->Arena), name.c_str());
                }

                for (auto& mat_name : hierarchies.MaterialNames)
                {
                    auto& n = h.MaterialNames.push_use({});
                    n.init(&(s_Instance->Arena), mat_name.c_str());
                }

                for (const auto& [k, v] : hierarchies.NodeNames)
                {
                    h.NodeNames.insert(k, v);
                }

                for (const auto& [k, v] : hierarchies.NodeMeshes)
                {
                    h.NodeMeshes.insert(k, v);
                }

                for (const auto& [k, v] : hierarchies.NodeMaterials)
                {
                    h.NodeMaterials.insert(k, v);
                }

                RegisterAsset(AssetType::MESH_HIERARCHY, h.NodeHierarchyUUID, asset_id);
                continue;
            }

            Array<AssetTexture> textures = {};
            if (pendings_asset_textures.Pop(textures))
            {
                for (auto& tex : textures)
                {
                    auto  asset_id               = (uint32_t) Textures.size();
                    auto& new_tex                = Textures.push_use({});
                    new_tex.TextureUUID          = tex.TextureUUID;

                    const auto tex_absolute_path = fmt::format("{0}{1}{2}", s_Instance->CurrentWorkingSpacePath, PLATFORM_OS_BACKSLASH, tex.Path.c_str());
                    new_tex.Handle               = s_Instance->Device->AsyncResLoader->Submit(0, 0, {.TextureUpload = {.Filename = tex_absolute_path.c_str()}});
                    new_tex.Path.init(&(s_Instance->Arena), tex.Path.c_str());

                    RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, asset_id);

                    UUIDToTextureHandle.insert(new_tex.TextureUUID, new_tex.Handle);
                }

                continue;
            }

            AssetMaterial material = {};
            if (pendings_asset_materials.Pop(material))
            {
                auto asset_id = (uint32_t) Materials.size();
                Materials.push(material);

                RegisterAsset(AssetType::MATERIAL, material.MaterialUUID, asset_id);

                Rendering::Meshes::MeshMaterial& gpu_mesh_mat = GPUMeshMaterials.push_use({});
                gpu_mesh_mat.AlbedoColor                      = material.AlbedoColor;
                gpu_mesh_mat.EmissiveColor                    = material.EmissiveColor;
                gpu_mesh_mat.RoughnessColor                   = material.RoughnessColor;
                gpu_mesh_mat.SpecularColor                    = material.SpecularColor;
                gpu_mesh_mat.AmbientColor                     = material.AmbientColor;
                gpu_mesh_mat.Factors                          = material.Factors;

                if (!material.AlbedoTexUUID.is_nil())
                {
                    gpu_mesh_mat.AlbedoMap = UUIDToTextureHandle.at(material.AlbedoTexUUID).Index;
                }

                if (!material.EmissiveTexUUID.is_nil())
                {
                    gpu_mesh_mat.EmissiveMap = UUIDToTextureHandle.at(material.EmissiveTexUUID).Index;
                }

                if (!material.NormalTexUUID.is_nil())
                {
                    gpu_mesh_mat.NormalMap = UUIDToTextureHandle.at(material.NormalTexUUID).Index;
                }

                if (!material.OpacityTexUUID.is_nil())
                {
                    gpu_mesh_mat.OpacityMap = UUIDToTextureHandle.at(material.OpacityTexUUID).Index;
                }

                if (!material.SpecularTexUUID.is_nil())
                {
                    gpu_mesh_mat.SpecularMap = UUIDToTextureHandle.at(material.SpecularTexUUID).Index;
                }

                continue;
            }

            ThreadLocalArena.Clear();
            Importers::AssetImporterOutput file = {};
            if (pendings_asset_files.Pop(file))
            {
                if (file.Type == Importers::AssetFileType::MESH)
                {
                    AssetMesh          mesh        = {};
                    AssetNodeHierarchy hierarchies = {};
                    auto               path        = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);
                    IAssetImporter::DeserializeMeshAssetFile(&ThreadLocalArena, path.c_str(), mesh, hierarchies);
                    PendingAssetMeshes.Enqueue(mesh);
                    PendingAssetNodeHierarchies.Enqueue(hierarchies);
                }

                else if (file.Type == Importers::AssetFileType::TEXTURES)
                {
                    Array<AssetTexture> textures = {};
                    auto                path     = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);
                    IAssetImporter::DeserializeTextureAssetFile(&ThreadLocalArena, path.c_str(), textures);
                    PendingAssetTextures.Enqueue(textures);
                }

                else if (file.Type == Importers::AssetFileType::MATERIAL)
                {
                    AssetMaterial material = {};
                    auto          path     = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);
                    IAssetImporter::DeserializeMaterialAssetFile(&ThreadLocalArena, path.c_str(), material);
                    PendingAssetMaterials.Enqueue(material);
                }
            }
        }
    }
} // namespace ZEngine::Managers
