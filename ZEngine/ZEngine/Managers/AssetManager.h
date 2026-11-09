#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/AssetTypes.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetCacheLease.h>
#include <ZEngine/Managers/AssetTypes.h>
#include <ZEngine/Rendering/Meshes/Mesh.h>
#include <atomic>
#include <mutex>

namespace ZEngine::Managers
{
    struct AssetManager
    {
        Core::Memory::ArenaAllocator*                                                       Arena                   = nullptr;
        cstring                                                                             CurrentWorkingSpacePath = "";

        // TLSF slab for growing containers and individually reclaimable mesh payloads.
        // Realloc extends in-place when the following block is free — eliminates
        // the dead-block accumulation from arena-backed grows (~20 MB per 100 sessions).
        static constexpr size_t                                                             CONTAINER_SLAB_BYTES    = 256 * 1024 * 1024; // 256 MB
        Core::Memory::TLSFSlab                                                              ContainerSlab           = {};

        // CPU-side import buffers — owned by the import pipeline.
        Core::Containers::Array<Importers::AssetNodeHierarchy>                              NodeHierarchies         = {};
        Core::Containers::Array<Importers::AssetMesh>                                       Meshes                  = {};
        Core::Containers::Array<Importers::AssetMaterial>                                   Materials               = {};
        Core::Containers::Array<Importers::AssetTexture>                                    Textures                = {};

        // GPU-side material data — mirrored from Materials after upload.
        Core::Containers::Array<Rendering::Meshes::MeshMaterial>                            GPUMeshMaterials        = {};

        // GPU texture handle map — needed to resolve material → texture handles at upload time.
        Core::Containers::UnorderedHashMap<uuids::uuid, Rendering::Textures::TextureHandle> UUIDToTextureHandle     = {};

        struct MeshBinding
        {
            uint32_t MeshSlot      = 0;
            uint32_t HierarchySlot = 0;
        };
        // Stable CPU slots outlive a deleted source/registry record: live actors
        // may still use them. Reimport replaces this pair, never resolves it via
        // a scanner placeholder's packed handle.
        Core::Containers::UnorderedHashMap<uuids::uuid, MeshBinding> MeshBindings                      = {};
        // Populated only when asset data is actually ingested — not by VFSScanner
        // pre-registration (which sets SlotHandle=0, causing GetAsset to return a
        // false match for any material at slot 0).
        Core::Containers::UnorderedHashMap<uuids::uuid, uint32_t>    UUIDToMaterialSlot                = {};

        using AssetPayloadAllocation                                                                   = Managers::AssetPayloadAllocation;
        // One immutable payload per Meshes slot, for both ingestion paths. Arena
        // views stay at stable addresses while the descriptor arrays can grow.
        Core::Containers::Array<AssetPayloadAllocation*> MeshCacheAllocations                          = {};
        Core::Containers::Array<AssetPayloadAllocation*> MaterialCacheAllocations                      = {};
        Core::Containers::Array<AssetPayloadAllocation*> TextureCacheAllocations                       = {};
        std::atomic<uint32_t>                            CpuAssetOwnerPins[AssetCacheLease::SlotCount] = {};
        // Bounded maintenance workspace, serialized by MeshLoadMutex. Avoids a
        // large worker stack or unaccounted heap when sorting eviction candidates.
        uint32_t                                         CpuEvictionOrder[AssetCacheLease::SlotCount]  = {};
        uint64_t                                         CpuCacheClock                                 = 0;
        enum class CpuCacheRetention : uint8_t
        {
            Retain,
            ZeroRetention
        };
        // Zero-retention maintenance runs on explicit worker loads, not frames.
        std::atomic<CpuCacheRetention>               CpuRetention          = CpuCacheRetention::Retain;

        // (255, 20, 147) fallback handle used when a texture file cannot be resolved.
        Rendering::Textures::TextureHandle           FallbackTextureHandle = {};

        // Recursive so IngestMaterial can call IngestTexture while holding the lock.
        mutable std::recursive_mutex                 IngestMutex;
        // At most one checked decode workspace per cache. A competing explicit
        // load fails retryably; rendering and ordinary cached queries never take it.
        std::mutex                                   MeshLoadMutex;

        // Pending texture releases — written from any thread via ReleaseTexture, drained by
        // FlushTextureReleases on the render thread.
        // One release per possible cache key, deduplicated under the cache lock.
        // Texture admission never grows the 8,192-slot UUID index.
        static constexpr uint32_t                    MAX_PENDING_TEXTURE_RELEASES                         = 8192;
        uuids::uuid                                  PendingTextureReleases[MAX_PENDING_TEXTURE_RELEASES] = {};
        uint32_t                                     PendingTextureReleaseCount                           = 0;
        std::mutex                                   PendingTextureReleaseMutex;

        Hardwares::VulkanDevice*                     Device   = nullptr;
        ::ZEngine::Core::VFS::AssetRegistry*         Registry = nullptr;

        /// Borrowed CPU data: caller holds IngestMutex until all result use ends.
        /// Prefer the copied/checked helpers for editor bounds and load queries.
        Importers::AssetMesh*                        GetMeshAsset(const uuids::uuid& id);
        /// @brief Copies one loaded mesh's local-space bounds under the ingest lock.
        /// @details Safe for editor scene queries while an importer may update mesh data.
        [[nodiscard]] bool                           TryGetMeshBounds(const uuids::uuid& id, Core::Maths::Vec3f& out_center, float& out_radius);
        /// Tries to copy a loaded mesh's runtime AABB without waiting on ingestion. Degenerate
        /// boxes are valid. Failure leaves both outputs unchanged; CPU availability
        /// does not imply GPU residency. No mesh-buffer pointers escape the lock.
        [[nodiscard]] bool                           TryGetMeshAABB(const uuids::uuid& id, Core::Maths::Vec3f& out_min, Core::Maths::Vec3f& out_max);
        /// Copies the complete CPU draw count under the ingest lock. Scanner-only
        /// registrations, mismatched slots and incomplete submesh ranges fail closed.
        [[nodiscard]] bool                           TryGetMeshDrawCount(const uuids::uuid& id, uint32_t* out_draws);
        /// Explicit worker/load-time operation, never per-frame. Bounded decoding
        /// and staging occur outside IngestMutex. Reuses complete CPU bindings;
        /// never hot-reloads a live one. Failure publishes no partial mesh pair.
        [[nodiscard]] Core::VFS::VFSResult<uint32_t> LoadMeshAsset(Core::VFS::IVFSContext& vfs, const uuids::uuid& uuid, const Importers::AssetCodec::MeshAssetReadLimits& limits = {}, const std::atomic<bool>* cancelled = nullptr, AssetCacheLease* lease = nullptr);
        /// Retains the current mesh/material/texture CPU closure under one lock.
        /// Initialize the lease in its admitted owner arena before calling.
        [[nodiscard]] bool                           RetainMeshAssets(const uuids::uuid& uuid, AssetCacheLease& lease);
        [[nodiscard]] AssetPayloadPin                PinMeshPayload(const uuids::uuid& uuid);
        /// Explicit worker/quiescent maintenance, never a per-frame scan. Keeps
        /// stable UUID slots and GPU descriptors; frees only unleased CPU blocks.
        /// Cache lock is try-only: competing ingestion retries on a later call.
        [[nodiscard]] size_t                         TrimCpuCache(size_t target_bytes = SIZE_MAX);
        Importers::AssetNodeHierarchy*               GetMeshNodeHierarchy(const uuids::uuid& mesh_id);
        AssetHandle                                  GetMeshNodeHierarchyHandle(const uuids::uuid& id);

        static AssetManager*                         Instance();
        static AssetHandle                           CreateHandle(uint32_t index, AssetType type);
        static void                                  InitFallbackTexture(); // call after RRM is assigned to Device
        static uint32_t                              ReadAssetHandleIndex(AssetHandle h);
        static AssetType                             ReadAssetHandleType(AssetHandle h);

        static void                                  Initialize(Core::Memory::ArenaAllocator* arena, Hardwares::VulkanDevice* device, cstring working_space_path);
        static void                                  Shutdown();

        static AssetHandle                           RegisterAsset(AssetType type, const uuids::uuid& uuid, uint32_t slot_index, const Core::VFS::VFSPath& path = {}, const Core::VFS::MetaFileData& meta = {});

        // Returns true if uuid is already registered — used by Ingest* for deduplication.
        static bool                                  IsRegistered(const uuids::uuid& id);

        // Direct ingest is serialized by IngestMutex. Mesh replacement preserves
        // stable CPU slots and fails without modifying them on registry conflicts.
        // Async callers pass the pre-read snapshot to reject obsolete decodes.
        static bool                                  IngestMesh(Importers::AssetMesh&& mesh, Importers::AssetNodeHierarchy&& hierarchy, const Core::VFS::AssetRecordSnapshot* expected_record = nullptr);
        static Rendering::Textures::TextureHandle    IngestTexture(const uuids::uuid& uuid, const Core::Containers::String& path);
        static void                                  IngestTextures(Core::Containers::Array<Importers::AssetTexture>&& textures);
        static bool                                  IngestMaterial(Importers::AssetMaterial&& material);

        /// @brief Single-UUID counterpart to ReloadFromDisk's material block — resolves,
        ///        deserializes, and ingests one material (and its textures) by UUID.
        /// @return True only when the UUID is already resident or was fully ingested.
        static bool                                  IngestMaterialFromUUID(Core::Memory::ArenaAllocator* scratch, const uuids::uuid& material_uuid);

        /// @brief Thread-safe lookup of a texture's current handle by UUID.
        static Rendering::Textures::TextureHandle    FindTextureHandle(const uuids::uuid& uuid);

        /// Render-thread checked publication. Updates the loaded descriptor and every
        /// authored material reference together; pending deletion wins over reload.
        /// retire_previous is false for shared handles (including the global fallback).
        static bool                                  TryReplaceTexture(const uuids::uuid& uuid, Rendering::Textures::TextureHandle expected, Rendering::Textures::TextureHandle replacement, bool* retire_previous);

        /// @brief Resolve a material's texture map field to a bindless index.
        /// @details UUID lookup first, else ingest from path, else INVALID_MAP_HANDLE.
        static uint32_t                              ResolveTextureMapIndex(const uuids::uuid& id, const Core::Containers::String& path);

        /// @brief Thread-safe enqueue: patches every referencing material to the sentinel
        ///        once FlushTextureReleases drains it.
        static void                                  ReleaseTexture(const uuids::uuid& uuid);

        /// @brief Render-thread drain of ReleaseTexture's queue.
        static void                                  FlushTextureReleases();

        static uuids::uuid                           GetOrCreateUUID(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& asset_path, const char* importer_name);

        // Reload all .zemesh and .zematerial assets already registered by the VFSScanner
        // but not yet ingested (second launch / project reopen). Safe to call every scan.
        static void                                  ReloadFromDisk(Core::Memory::ArenaAllocator* scratch);

    private:
        static bool                                  TryIngestMaterial(const Importers::AssetMaterial& material, const Core::VFS::AssetRecordSnapshot* expected, bool required);
        static bool                                  ResolveMaterialTextures(const Importers::AssetMaterial& material, Rendering::Meshes::MeshMaterial& gpu_material, bool required);
        static Rendering::Textures::TextureHandle    IngestTextureInternal(const uuids::uuid& uuid, const Core::Containers::String& path, bool required, bool reload);
        /// Caller owns MeshLoadMutex and IngestMutex. Optionally protects the
        /// unpublished load's material/texture closure during pressure recovery.
        [[nodiscard]] size_t                         TrimCpuCacheLocked(const Importers::AssetMesh* protected_source = nullptr, size_t target_bytes = SIZE_MAX);
        [[nodiscard]] uint64_t                       NextCpuCacheUse() noexcept;
        /// Caller must hold IngestMutex. Kept separate so checked loading can
        /// recheck a competing completed binding without re-entering the lock.
        [[nodiscard]] bool                           TryGetMeshDrawCountLocked(const uuids::uuid& id, uint32_t* out_draws);
        /// Caller holds IngestMutex. Publishes complete staged data and its two
        /// registry handles as one checked transaction; takes storage on success.
        [[nodiscard]] Core::VFS::VFSResult<uint32_t> PublishMesh(const Core::VFS::AssetRecordSnapshot& expected, Importers::AssetMesh&& mesh, Importers::AssetNodeHierarchy&& hierarchy, AssetPayloadAllocation* storage);

    public:
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
        auto* manager = Instance();
        return manager ? manager->GetMeshAsset(id) : nullptr;
    }

    template <>
    inline Importers::AssetMaterial* AssetManager::GetAsset<Importers::AssetMaterial, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
        {
            return nullptr;
        }
        const auto* rec = Instance()->Registry->FindByUUID(id);
        // VFSScanner pre-registers every .zematerial UUID with SlotHandle=0 before any
        // ingest happens — IsLoaded() tells a real ingest apart from that placeholder.
        if (!rec || !rec->IsLoaded())
        {
            return nullptr;
        }
        return GetAsset<Importers::AssetMaterial, AssetHandle>(rec->SlotHandle);
    }

    template <>
    inline Importers::AssetTexture* AssetManager::GetAsset<Importers::AssetTexture, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
        {
            return nullptr;
        }
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
        {
            return nullptr;
        }
        return GetAsset<Importers::AssetTexture, AssetHandle>(rec->SlotHandle);
    }

    template <>
    inline Importers::AssetNodeHierarchy* AssetManager::GetAsset<Importers::AssetNodeHierarchy, uuids::uuid>(uuids::uuid id)
    {
        if (!Instance()->Registry)
        {
            return nullptr;
        }
        const auto* rec = Instance()->Registry->FindByUUID(id);
        if (!rec)
        {
            return nullptr;
        }
        return GetAsset<Importers::AssetNodeHierarchy, AssetHandle>(rec->SlotHandle);
    }

} // namespace ZEngine::Managers
