#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/HashMap.h>
#include <Core/Containers/Strings.h>
#include <Core/Memory/Allocator.h>
#include <Helpers/ThreadSafeQueue.h>
#include <Importers/AssetTypes.h>
#include <Importers/IAssetImporter.h>
#include <condition_variable>
#include <mutex>

namespace ZEngine::Managers
{
    enum class AssetType : uint8_t
    {
        MESH = 0,
        MATERIAL,
        TEXTURE,
        MESH_HIERARCHY
    };

    struct AssetManager
    {
        using AssetHandle                                                                                  = uint32_t;

        Core::Memory::ArenaAllocator                                               Arena                   = {};
        Core::Memory::ArenaAllocator                                               ThreadLocalArena        = {};

        cstring                                                                    CurrentWorkingSpacePath = "";

        std::atomic_bool                                                           IsLoading               = false;
        std::atomic_bool                                                           RequestShutdown         = false;

        Core::Containers::Array<Importers::AssetNodeHierarchy>                     NodeHierarchies         = {};
        Core::Containers::Array<Importers::AssetMesh>                              Meshes                  = {};
        Core::Containers::Array<Importers::AssetMaterial>                          Materials               = {};
        Core::Containers::Array<Importers::AssetTexture>                           Textures                = {};

        Core::Containers::Array<Rendering::Meshes::MeshMaterial>                   GPUMeshMaterials        = {};

        Core::Containers::HashMap<uuids::uuid, AssetHandle>                        UUIDToHandle            = {};
        Core::Containers::HashMap<AssetHandle, uuids::uuid>                        HandleToUUID            = {};
        Core::Containers::HashMap<uuids::uuid, uuids::uuid>                        MeshToNodeHierarchy     = {};
        Core::Containers::HashMap<uuids::uuid, uuids::uuid>                        NodeHierarchyToMesh     = {};

        Core::Containers::HashMap<uuids::uuid, Rendering::Textures::TextureHandle> UUIDToTextureHandle     = {};

        Hardwares::StorageBufferSetHandle                                          MaterialBufferHandle    = {};

        std::mutex                                                                 Mut;
        std::condition_variable                                                    Cond;
        Helpers::ThreadSafeQueue<Importers::AssetImporterOutput>                   PendingAssetFiles           = {};

        Helpers::ThreadSafeQueue<Importers::AssetMesh>                             PendingAssetMeshes          = {};
        Helpers::ThreadSafeQueue<Importers::AssetNodeHierarchy>                    PendingAssetNodeHierarchies = {};
        Helpers::ThreadSafeQueue<Importers::AssetMaterial>                         PendingAssetMaterials       = {};
        Helpers::ThreadSafeQueue<Core::Containers::Array<Importers::AssetTexture>> PendingAssetTextures        = {};

        Hardwares::VulkanDevice*                                                   Device                      = nullptr;

        Importers::AssetMesh*                                                      GetMeshAsset(const uuids::uuid& id);
        Importers::AssetNodeHierarchy*                                             GetMeshNodeHierarchy(const uuids::uuid& id);
        AssetHandle                                                                GetMeshNodeHierarchyHandle(const uuids::uuid& id);
        AssetHandle                                                                GetMaterialHandleFromUUID(const uuids::uuid& material_uuid);

        Importers::AssetTexture*                                                   LoadTextureFileAsAsset(cstring file, bool absolute);

        static AssetManager*                                                       Instance();

        static AssetHandle                                                         CreateHandle(uint32_t, AssetType);
        static uint32_t                                                            ReadAssetHandleIndex(AssetHandle);
        static AssetType                                                           ReadAssetHandleType(AssetHandle);

        static void                                                                Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path);
        static void                                                                Run();
        static void                                                                Shutdown();

        static bool                                                                IsLoadingAsset();
        static AssetHandle                                                         RegisterAsset(AssetType type, const uuids::uuid& uid, uint32_t asset_id);

        static void                                                                LoadAssetFile(const Importers::AssetImporterOutput& file);

        template <typename T, typename K>
        static T* GetAsset(K key)
        {
            return nullptr;
        }

    private:
        void __Run();
    };

    template <>
    inline Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, AssetManager::AssetHandle>(AssetManager::AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        if (index < Instance()->Meshes.size())
        {
            return &Instance()->Meshes[index];
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, AssetManager::AssetHandle>(AssetManager::AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        if (index < Instance()->Materials.size())
        {
            return &Instance()->Materials[index];
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, AssetManager::AssetHandle>(AssetManager::AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        if (index < Instance()->Textures.size())
        {
            return &Instance()->Textures[index];
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetNodeHierarchy* AssetManager::GetAsset<Importers::AssetNodeHierarchy, AssetManager::AssetHandle>(AssetManager::AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        if (index < Instance()->NodeHierarchies.size())
        {
            return &Instance()->NodeHierarchies[index];
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, uuids::uuid>(uuids::uuid id)
    {
        if (Instance()->UUIDToHandle.contains(id))
        {
            const auto& handle = Instance()->UUIDToHandle.at(id);
            return GetAsset<Importers::AssetMesh, AssetHandle>(handle);
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, uuids::uuid>(uuids::uuid id)
    {
        if (Instance()->UUIDToHandle.contains(id))
        {
            const auto& handle = Instance()->UUIDToHandle.at(id);
            return GetAsset<Importers::AssetMaterial, AssetHandle>(handle);
        }
        return nullptr;
    }

    template <>
    inline Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, uuids::uuid>(uuids::uuid id)
    {
        if (Instance()->UUIDToHandle.contains(id))
        {
            const auto& handle = Instance()->UUIDToHandle.at(id);
            return GetAsset<Importers::AssetTexture, AssetHandle>(handle);
        }
        return nullptr;
    }
} // namespace ZEngine::Managers