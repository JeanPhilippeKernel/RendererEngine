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

        // Mesh UUID → NodeHierarchy slot — O(1) lookup replacing the old linear scan.
        Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t>                           MeshToHierarchySlot     = {};
        // Populated only when asset data is actually ingested — not by VFSScanner
        // pre-registration (which sets SlotHandle=0, causing GetAsset to return a
        // false match for any material at slot 0).
        Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t>                           UUIDToMaterialSlot      = {};

        // (255, 20, 147) fallback handle used when a texture file cannot be resolved.
        Rendering::Textures::TextureHandle                                                  FallbackTextureHandle   = {};

        // Recursive so IngestMaterial can call IngestTexture while holding the lock.
        mutable std::recursive_mutex                                                        IngestMutex;

        Hardwares::VulkanDevice*                                                            Device   = nullptr;
        ::ZEngine::Core::VFS::AssetRegistry*                                                Registry = nullptr;

        Importers::AssetMesh*                                                               GetMeshAsset(const uuids::uuid& id);
        Importers::AssetNodeHierarchy*                                                      GetMeshNodeHierarchy(const uuids::uuid& mesh_id);
        AssetHandle                                                                         GetMeshNodeHierarchyHandle(const uuids::uuid& id);

        static AssetManager*                                                                Instance();
        static AssetHandle                                                                  CreateHandle(uint32_t index, AssetType type);
        static void                                                                         InitFallbackTexture(); // call after RRM is assigned to Device
        static uint32_t                                                                     ReadAssetHandleIndex(AssetHandle h);
        static AssetType                                                                    ReadAssetHandleType(AssetHandle h);

        static void                                                                         Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path);
        static void                                                                         Shutdown();

        static AssetHandle                                                                  RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path = {}, const Core::VFS::MetaFileData& meta = {});

        // Returns true if uuid is already registered — used by Ingest* for deduplication.
        static bool                                                                         IsRegistered(const uuids::uuid& id);

        // Direct ingest — called from ImportCoordinator thread after AssetCodec cooks the file.
        // Each method deduplicates (no-ops if uuid is already registered), copies data into
        // arena-backed flat buffers, and calls RegisterAsset. Thread-safe via IngestMutex.
        static void                                                                         IngestMesh(Importers::AssetMesh&& mesh, Importers::AssetNodeHierarchy&& hierarchy);
        static Rendering::Textures::TextureHandle                                           IngestTexture(const uuids::uuid& uuid, const Core::Containers::String& path);
        static void                                                                         IngestTextures(Core::Containers::Array<Importers::AssetTexture>&& textures);
        static void                                                                         IngestMaterial(Importers::AssetMaterial&& material);

        static uuids::uuid                                                                  GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name);

        // Reload all .zemesh and .zematerial assets already registered by the VFSScanner
        // but not yet ingested (second launch / project reopen). Safe to call every scan.
        static void                                                                         ReloadFromDisk(Core::Memory::ArenaAllocator* scratch);

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
