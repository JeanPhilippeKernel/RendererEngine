#pragma once
#include <Importers/AssetTypes.h>
#include <Importers/IAssetImporter.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <condition_variable>
#include <mutex>

namespace Tetragrama::Managers
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
        using AssetHandle                                                                                                    = uint32_t;

        ZEngine::Core::Memory::ArenaAllocator                                                        Arena                   = {};
        ZEngine::Core::Memory::ArenaAllocator                                                        ThreadLocalArena        = {};

        cstring                                                                                      CurrentWorkingSpacePath = "";

        std::atomic_bool                                                                             IsLoading               = false;
        std::atomic_bool                                                                             RequestShutdown         = false;

        ZEngine::Core::Containers::Array<Importers::AssetNodeHierarchy>                              NodeHierarchies         = {};
        ZEngine::Core::Containers::Array<Importers::AssetMesh>                                       Meshes                  = {};
        ZEngine::Core::Containers::Array<Importers::AssetMaterial>                                   Materials               = {};
        ZEngine::Core::Containers::Array<Importers::AssetTexture>                                    Textures                = {};

        ZEngine::Core::Containers::Array<ZEngine::Rendering::Meshes::MeshMaterial>                   GPUMeshMaterials        = {};

        ZEngine::Core::Containers::HashMap<uuids::uuid, AssetHandle>                                 UUIDToHandle            = {};
        ZEngine::Core::Containers::HashMap<AssetHandle, uuids::uuid>                                 HandleToUUID            = {};
        ZEngine::Core::Containers::HashMap<uuids::uuid, uuids::uuid>                                 MeshToNodeHierarchy     = {};
        ZEngine::Core::Containers::HashMap<uuids::uuid, uuids::uuid>                                 NodeHierarchyToMesh     = {};

        ZEngine::Core::Containers::HashMap<uuids::uuid, ZEngine::Rendering::Textures::TextureHandle> UUIDToTextureHandle     = {};

        ZEngine::Hardwares::StorageBufferSetHandle                                                   MaterialBufferHandle    = {};

        std::mutex                                                                                   Mut;
        std::condition_variable                                                                      Cond;
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetImporterOutput>                            PendingAssetFiles           = {};

        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetMesh>                                      PendingAssetMeshes          = {};
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetNodeHierarchy>                             PendingAssetNodeHierarchies = {};
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetMaterial>                                  PendingAssetMaterials       = {};
        ZEngine::Helpers::ThreadSafeQueue<ZEngine::Core::Containers::Array<Importers::AssetTexture>> PendingAssetTextures        = {};

        ZEngine::Hardwares::VulkanDevice*                                                            Device                      = nullptr;
        ZEngine::Rendering::Renderers::AsyncResourceLoader*                                          ResourceLoader              = nullptr;

        Importers::AssetMesh*                                                                        GetMeshAsset(const uuids::uuid& id);
        Importers::AssetNodeHierarchy*                                                               GetMeshNodeHierarchy(const uuids::uuid& id);
        AssetHandle                                                                                  GetMeshNodeHierarchyHandle(const uuids::uuid& id);
        AssetHandle                                                                                  GetMaterialHandleFromUUID(const uuids::uuid& material_uuid);

        static AssetManager*                                                                         Instance();

        static AssetHandle                                                                           CreateHandle(uint32_t, AssetType);
        static uint32_t                                                                              ReadAssetHandleIndex(AssetHandle);
        static AssetType                                                                             ReadAssetHandleType(AssetHandle);

        static void                                                                                  Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, ZEngine::Hardwares::VulkanDevice* device, ZEngine::Rendering::Renderers::AsyncResourceLoader* async_loader, cstring working_space_path);
        static void                                                                                  Run();
        static void                                                                                  Shutdown();

        static bool                                                                                  IsLoadingAsset();
        static AssetHandle                                                                           RegisterAsset(AssetType type, const uuids::uuid& uid, uint32_t asset_id);

        static void                                                                                  LoadAssetFile(const Importers::AssetImporterOutput& file);

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
} // namespace Tetragrama::Managers