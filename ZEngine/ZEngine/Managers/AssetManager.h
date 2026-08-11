#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Helpers/ThreadSafeQueue.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetTypes.h>
#include <condition_variable>
#include <mutex>

namespace ZEngine::Managers
{

    struct AssetManager
    {
        // Flat slot index into the corresponding Array<T> buffer.
        // Type is encoded in the high 4 bits, index in the low 28 bits.

        Core::Memory::ArenaAllocator                                                        Arena                   = {};
        Core::Memory::ArenaAllocator                                                        ThreadLocalArena        = {};

        cstring                                                                             CurrentWorkingSpacePath = "";

        std::atomic_bool                                                                    IsLoading               = false;
        std::atomic_bool                                                                    RequestShutdown         = false;

        // CPU-side import buffers — owned by the import pipeline.
        Core::Containers::Array<Importers::AssetNodeHierarchy>                              NodeHierarchies         = {};
        Core::Containers::Array<Importers::AssetMesh>                                       Meshes                  = {};
        Core::Containers::Array<Importers::AssetMaterial>                                   Materials               = {};
        Core::Containers::Array<Importers::AssetTexture>                                    Textures                = {};

        // GPU-side material data — mirrored from Materials after upload.
        Core::Containers::Array<Rendering::Meshes::MeshMaterial>                            GPUMeshMaterials        = {};

        // GPU texture handle map — needed to resolve material → texture handles at upload time.
        Core::Containers::UnorderedHashMap<uuids::uuid, Rendering::Textures::TextureHandle> UUIDToTextureHandle     = {};

        Hardwares::StorageBufferSetHandle                                                   MaterialBufferHandle    = {};

        std::mutex                                                                          Mut;
        std::condition_variable                                                             Cond;

        Helpers::ThreadSafeQueue<Importers::AssetImporterOutput>                            PendingAssetFiles           = {};
        Helpers::ThreadSafeQueue<Importers::AssetMesh>                                      PendingAssetMeshes          = {};
        Helpers::ThreadSafeQueue<Importers::AssetNodeHierarchy>                             PendingAssetNodeHierarchies = {};
        Helpers::ThreadSafeQueue<Importers::AssetMaterial>                                  PendingAssetMaterials       = {};
        Helpers::ThreadSafeQueue<Core::Containers::Array<Importers::AssetTexture>>          PendingAssetTextures        = {};

        Hardwares::VulkanDevice*                                                            Device                      = nullptr;
        ::ZEngine::Core::VFS::AssetRegistry*                                                Registry                    = nullptr;

        // CPU buffer accessors — index via flat slot stored in AssetRecord::SlotHandle.
        Importers::AssetMesh*                                                               GetMeshAsset(const uuids::uuid& id);
        Importers::AssetNodeHierarchy*                                                      GetMeshNodeHierarchy(const uuids::uuid& id);
        AssetHandle                                                                         GetMeshNodeHierarchyHandle(const uuids::uuid& id);
        AssetHandle                                                                         GetMaterialHandleFromUUID(const uuids::uuid& material_uuid);

        Importers::AssetTexture*                                                            LoadTextureFileAsAsset(cstring file, bool absolute);

        static AssetManager*                                                                Instance();

        static AssetHandle                                                                  CreateHandle(uint32_t index, AssetType type);
        static uint32_t                                                                     ReadAssetHandleIndex(AssetHandle h);
        static AssetType                                                                    ReadAssetHandleType(AssetHandle h);

        static void                                                                         Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path);
        static void                                                                         Run();
        static void                                                                         Shutdown();

        static bool                                                                         IsLoadingAsset();

        // Register a newly imported asset into both the flat buffer and the registry.

        static AssetHandle                                                                  RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path = {}, const Core::VFS::MetaFileData& meta = {});

        static void                                                                         LoadAssetFile(const Importers::AssetImporterOutput& file);

        static uuids::uuid                                                                  GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name);

        template <typename T, typename K>
        static T* GetAsset(K key)
        {
            return nullptr;
        }

    private:
        void __Run();
    };

    template <>
    inline Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, AssetHandle>(AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        return index < Instance()->Meshes.size() ? &Instance()->Meshes[index] : nullptr;
    }

    template <>
    inline Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, AssetHandle>(AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        return index < Instance()->Materials.size() ? &Instance()->Materials[index] : nullptr;
    }

    template <>
    inline Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, AssetHandle>(AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        return index < Instance()->Textures.size() ? &Instance()->Textures[index] : nullptr;
    }

    template <>
    inline Importers::AssetNodeHierarchy* AssetManager::GetAsset<Importers::AssetNodeHierarchy, AssetHandle>(AssetHandle key)
    {
        uint32_t index = ReadAssetHandleIndex(key);
        return index < Instance()->NodeHierarchies.size() ? &Instance()->NodeHierarchies[index] : nullptr;
    }

    template <>
    inline Importers::AssetMesh* AssetManager::GetAsset<Importers::AssetMesh, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
            return nullptr;
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
            return nullptr;
        return GetAsset<Importers::AssetMesh, AssetHandle>(rec->SlotHandle);
    }

    template <>
    inline Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
            return nullptr;
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
            return nullptr;
        return GetAsset<Importers::AssetMaterial, AssetHandle>(rec->SlotHandle);
    }

    template <>
    inline Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
            return nullptr;
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
            return nullptr;
        return GetAsset<Importers::AssetTexture, AssetHandle>(rec->SlotHandle);
    }

    template <>
    inline Importers::AssetNodeHierarchy* AssetManager::GetAsset<Importers::AssetNodeHierarchy, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
            return nullptr;
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
            return nullptr;
        return GetAsset<Importers::AssetNodeHierarchy, AssetHandle>(rec->SlotHandle);
    }

} // namespace ZEngine::Managers
