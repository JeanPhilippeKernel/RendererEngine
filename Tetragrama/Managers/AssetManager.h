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
        using AssetHandle                                                                                             = uint32_t;

        ZEngine::Core::Memory::ArenaAllocator                                                        Arena            = {};
        ZEngine::Core::Memory::ArenaAllocator                                                        ThreadLocalArena = {};

        std::atomic_bool                                                                             IsLoading        = false;
        std::atomic_bool                                                                             RequestShutdown  = false;

        ZEngine::Core::Containers::Array<Importers::AssetNodeHierarchy>                              NodeHierarchies  = {};
        ZEngine::Core::Containers::Array<Importers::AssetMesh>                                       Meshes           = {};
        ZEngine::Core::Containers::Array<Importers::AssetMaterial>                                   Materials        = {};
        ZEngine::Core::Containers::Array<Importers::AssetTexture>                                    Textures         = {};

        ZEngine::Core::Containers::HashMap<uuids::uuid, AssetHandle>                                 UUIDToHandle     = {};
        ZEngine::Core::Containers::HashMap<AssetHandle, uuids::uuid>                                 HandleToUUID     = {};

        std::mutex                                                                                   Mut;
        std::condition_variable                                                                      Cond;
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetImporterOutput>                            PendingAssetFiles           = {};

        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetMesh>                                      PendingAssetMeshes          = {};
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetNodeHierarchy>                             PendingAssetNodeHierarchies = {};
        ZEngine::Helpers::ThreadSafeQueue<Importers::AssetMaterial>                                  PendingAssetMaterials       = {};
        ZEngine::Helpers::ThreadSafeQueue<ZEngine::Core::Containers::Array<Importers::AssetTexture>> PendingAssetTextures        = {};

        static AssetManager*                                                                         Instance();

        static inline AssetHandle                                                                    CreateHandle(uint32_t, AssetType);
        static inline uint32_t                                                                       ReadAssetHandleIndex(AssetHandle);
        static inline AssetType                                                                      ReadAssetHandleType(AssetHandle);

        static void                                                                                  Initialize(ZEngine::Core::Memory::ArenaAllocator* arena);
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
    inline static Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, AssetManager::AssetHandle>(AssetManager::AssetHandle)
    {
    }

    template <>
    inline static Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, AssetManager::AssetHandle>(AssetManager::AssetHandle)
    {
    }

    template <>
    inline static Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, AssetManager::AssetHandle>(AssetManager::AssetHandle)
    {
    }

    template <>
    inline static Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, uuids::uuid>(uuids::uuid)
    {
    }

    template <>
    inline static Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, uuids::uuid>(uuids::uuid)
    {
    }

    template <>
    inline static Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, uuids::uuid>(uuids::uuid)
    {
    }
} // namespace Tetragrama::Managers