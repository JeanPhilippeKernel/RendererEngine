#include <pch.h>
#include <AssetManager.h>
#include <Importers/IAssetImporter.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>

using namespace ZEngine::Core::Containers;
using namespace Tetragrama::Importers;

namespace Tetragrama::Managers
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

    void AssetManager::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena)
    {
        s_Instance = ZPushStructCtor(arena, AssetManager);
        arena->CreateSubArena(ZMega(100), &(s_Instance->ThreadLocalArena));
        arena->CreateSubArena(ZMega(70), &(s_Instance->Arena));

        s_Instance->NodeHierarchies.init(&(s_Instance->Arena), 5000);
        s_Instance->Meshes.init(&(s_Instance->Arena), 5000);
        s_Instance->Materials.init(&(s_Instance->Arena), 5000);
        s_Instance->Textures.init(&(s_Instance->Arena), 5000);
        s_Instance->UUIDToHandle.init(&(s_Instance->Arena), 5000);
        s_Instance->HandleToUUID.init(&(s_Instance->Arena), 5000);
    }

    void AssetManager::Run()
    {
        ZEngine::Helpers::ThreadPoolHelper::Submit([instance = s_Instance]() { instance->__Run(); });
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

                ZEngine::Helpers::secure_memcpy(m.Vertices.data(), m.Vertices.size() * sizeof(float), mesh.Vertices.data(), mesh.Vertices.size() * sizeof(float));
                ZEngine::Helpers::secure_memcpy(m.Indices.data(), m.Indices.size() * sizeof(uint32_t), mesh.Indices.data(), mesh.Indices.size() * sizeof(uint32_t));

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

                h.Hierarchies.init(&(s_Instance->Arena), hierarchies.Hierarchies.size());
                h.LocalTransforms.init(&(s_Instance->Arena), hierarchies.LocalTransforms.size());
                h.GlobalTransforms.init(&(s_Instance->Arena), hierarchies.GlobalTransforms.size());
                h.Names.init(&(s_Instance->Arena), hierarchies.Names.size());
                h.MaterialNames.init(&(s_Instance->Arena), hierarchies.MaterialNames.size());
                h.NodeNames.init(&(s_Instance->Arena), hierarchies.NodeNames.size());
                h.NodeMeshes.init(&(s_Instance->Arena), hierarchies.NodeMeshes.size());
                h.NodeMaterials.init(&(s_Instance->Arena), hierarchies.NodeMaterials.size());

                ZEngine::Helpers::secure_memcpy(h.Hierarchies.data(), h.Hierarchies.size() * sizeof(AssetNodeHierarchy), hierarchies.Hierarchies.data(), hierarchies.Hierarchies.size() * sizeof(AssetNodeHierarchy));
                ZEngine::Helpers::secure_memcpy(h.LocalTransforms.data(), h.LocalTransforms.size() * sizeof(glm::mat4), hierarchies.LocalTransforms.data(), hierarchies.LocalTransforms.size() * sizeof(glm::mat4));
                ZEngine::Helpers::secure_memcpy(h.GlobalTransforms.data(), h.GlobalTransforms.size() * sizeof(glm::mat4), hierarchies.GlobalTransforms.data(), hierarchies.GlobalTransforms.size() * sizeof(glm::mat4));

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

                auto node_names_view = hierarchies.NodeNames.view();
                for (auto [k, v] : node_names_view)
                {
                    h.NodeNames.insert(k, v);
                }

                auto node_meshes_view = hierarchies.NodeMeshes.view();
                for (auto [k, v] : node_meshes_view)
                {
                    h.NodeMeshes.insert(k, v);
                }

                auto node_mat_view = hierarchies.NodeMaterials.view();
                for (auto [k, v] : node_mat_view)
                {
                    h.NodeMaterials.insert(k, v);
                }

                RegisterAsset(AssetType::MESH_HIERARCHY, h.NodeHierarchyUUID, asset_id);
                continue;
            }

            AssetMaterial material = {};
            if (pendings_asset_materials.Pop(material))
            {
                auto asset_id = (uint32_t) Materials.size();
                Materials.push(material);

                RegisterAsset(AssetType::MATERIAL, material.MaterialUUID, asset_id);
                continue;
            }

            Array<AssetTexture> textures = {};
            if (pendings_asset_textures.Pop(textures))
            {
                for (auto& tex : textures)
                {
                    auto  asset_id      = (uint32_t) Textures.size();
                    auto& new_tex       = Textures.push_use({});
                    new_tex.TextureUUID = tex.TextureUUID;
                    new_tex.Path.init(&(s_Instance->Arena), tex.Path.c_str());

                    RegisterAsset(AssetType::TEXTURE, new_tex.TextureUUID, asset_id);
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

                else if (file.Type == Importers::AssetFileType::MATERIAL)
                {
                    AssetMaterial material = {};
                    auto          path     = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);
                    IAssetImporter::DeserializeMaterialAssetFile(&ThreadLocalArena, path.c_str(), material);
                    PendingAssetMaterials.Enqueue(material);
                }

                else if (file.Type == Importers::AssetFileType::TEXTURES)
                {
                    Array<AssetTexture> textures = {};
                    auto                path     = fmt::format("{0}{1}{2}", file.RootPath, PLATFORM_OS_BACKSLASH, file.Path);
                    IAssetImporter::DeserializeTextureAssetFile(&ThreadLocalArena, path.c_str(), textures);
                    PendingAssetTextures.Enqueue(textures);
                }
            }
        }
    }
} // namespace Tetragrama::Managers
