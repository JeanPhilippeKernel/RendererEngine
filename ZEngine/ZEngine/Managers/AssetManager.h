#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetTypes.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <mutex>

namespace ZEngine::Managers
{
    struct AssetManager
    {
        Core::Memory::ArenaAllocator                                                        Arena                   = {};
        cstring                                                                             CurrentWorkingSpacePath = "";

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

        // Mutex guards direct-ingest methods called from import threads.
        mutable std::mutex                                                                  IngestMutex;

        Hardwares::VulkanDevice*                                                            Device   = nullptr;
        ::ZEngine::Core::VFS::AssetRegistry*                                                Registry = nullptr;

        // CPU buffer accessors
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
        static void                                                                         Shutdown();

        static AssetHandle                                                                  RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path = {}, const Core::VFS::MetaFileData& meta = {});

        // Direct ingest — called from ImportCoordinator thread after AssetCodec cooks the file.
        // Each method copies the data into the arena-backed flat buffers and calls RegisterAsset.
        // Thread-safe via IngestMutex.
        static void                                                                         IngestMesh(Importers::AssetMesh&& mesh, Importers::AssetNodeHierarchy&& hierarchy);
        static void                                                                         IngestTextures(Core::Containers::Array<Importers::AssetTexture>&& textures);
        static void                                                                         IngestMaterial(Importers::AssetMaterial&& material);

        static uuids::uuid                                                                  GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name);

        template <typename T, typename K>
        static T* GetAsset(K key)
        {
            return nullptr;
        }
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
