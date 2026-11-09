#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetIndex.h>
#include <ZEngine/Core/VFS/Registry/DependencyGraph.h>
#include <mutex>
#include <optional>
#include <span>

namespace ZEngine::Core::VFS
{
    class AssetRegistry
    {
    public:
        using AssetReadyCallback   = void (*)(void*, const uuids::uuid&, Managers::AssetHandle);
        using AssetStaleCallback   = void (*)(void*, const uuids::uuid&);
        using AssetRemovedCallback = void (*)(void*, const uuids::uuid&, Managers::AssetType);

        AssetRegistry()            = default;
        ~AssetRegistry()           = default;

        // arena: persistent arena. scratch_size: byte size of BFS scratch sub-arena.
        void               Initialize(Core::Memory::ArenaAllocator* arena, uint64_t scratch_size = 65536);

        // Registration
        RegisterResult     Register(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta);

        RegisterResult     RegisterLoaded(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta, Managers::AssetHandle slot_handle);
        [[nodiscard]] bool TryReadRecord(const uuids::uuid& uuid, AssetRecordSnapshot* out) const;
        [[nodiscard]] bool TryReadRecordByPath(const Core::VFS::VFSPath& path, AssetRecordSnapshot* out) const;
        [[nodiscard]] bool TryReadSlot(const uuids::uuid& uuid, Managers::AssetType type, Managers::AssetHandle* out) const;
        /// No callbacks are fired until the cache owner has published complete CPU data
        /// and calls SetState(Loaded). Failure changes neither record.
        [[nodiscard]] bool TryBindLoadedMesh(const AssetRecordSnapshot& expected, const uuids::uuid& hierarchy, Managers::AssetHandle mesh_slot, Managers::AssetHandle hierarchy_slot, const AssetRecordSnapshot* retained_hierarchy = nullptr);
        // Snapshot-guarded, allocation/callback-free binding. Caller commits CPU
        // data before announcing readiness through SetState.
        [[nodiscard]] bool TryBindLoadedAsset(const AssetRecordSnapshot& expected, Managers::AssetHandle slot);

        bool               Remove(const uuids::uuid& uuid);
        bool               Remove(Helpers::Handle<AssetRecord> handle);

        // Dependency management
        bool               AddDependency(const uuids::uuid& dependent, const uuids::uuid& dependency);
        bool               RemoveDependency(const uuids::uuid& dependent, const uuids::uuid& dependency);

        // State transitions
        bool               SetState(const uuids::uuid& uuid, AssetState new_state);
        bool               UpdateMeta(const uuids::uuid& uuid, const Core::VFS::MetaFileData& new_meta, AssetState new_state = AssetState::Loaded);

        // Lookup
        AssetRecord*       FindByUUID(const uuids::uuid& uuid);
        const AssetRecord* FindByUUID(const uuids::uuid& uuid) const;
        AssetRecord*       FindByPath(const Core::VFS::VFSPath& path);
        const AssetRecord* FindByPath(const Core::VFS::VFSPath& path) const;
        AssetRecord*       Access(Helpers::Handle<AssetRecord> handle);

        // Query
        struct QueryFilter
        {
            std::optional<Managers::AssetType> Type     = std::nullopt;
            const char*                        NameLike = nullptr;
            const char*                        Ext      = nullptr;
            std::optional<AssetState>          State    = std::nullopt;
        };

        struct QueryResult
        {
            Core::Containers::Array<Helpers::Handle<AssetRecord>> Handles;
            uint32_t                                              Count = 0;
        };

        QueryResult            Query(const QueryFilter& filter, Core::Memory::ArenaAllocator* arena) const;

        // Hot-reload callback: void(*)(void* ctx, span<const uuid> cascade)
        // Fired when any asset transitions to Loaded (first-time or re-import).
        void                   SetHotReloadCallback(void* ctx, void (*cb)(void*, std::span<const uuids::uuid>));

        // RRM callbacks — separate from the hot-reload callback so RRM can distinguish
        // first-time upload (OnReady) from hot-reload swap (OnStale).
        //
        // OnReady:  fired when SetState transitions an asset to Loaded.
        //           Signature: void(void* ctx, const uuids::uuid& uuid, Managers::AssetHandle slot_handle)
        // OnStale:  fired when OnAssetModified marks an asset Stale.
        //           Signature: void(void* ctx, const uuids::uuid& uuid)
        void                   SetOnReadyCallback(void* ctx, AssetReadyCallback cb);
        void                   SetOnStaleCallback(void* ctx, AssetStaleCallback cb);

        /// @brief Register a callback fired once per cascade UUID from OnAssetDeleted,
        ///        before each record is erased (so it can still read the record's Type).
        void                   SetOnRemovedCallback(void* ctx, AssetRemovedCallback cb);

        /// @brief Adds an observer without replacing the RRM's primary callback.
        /// @details Observer lifecycle is serialized with delivery. Removing an observer
        ///          waits for concurrent delivery; callbacks must return promptly.
        [[nodiscard]] bool     AddOnReadyListener(void* ctx, AssetReadyCallback cb);
        [[nodiscard]] bool     AddOnStaleListener(void* ctx, AssetStaleCallback cb);
        [[nodiscard]] bool     AddOnRemovedListener(void* ctx, AssetRemovedCallback cb);
        bool                   RemoveOnReadyListener(void* ctx, AssetReadyCallback cb);
        bool                   RemoveOnStaleListener(void* ctx, AssetStaleCallback cb);
        bool                   RemoveOnRemovedListener(void* ctx, AssetRemovedCallback cb);

        void                   OnAssetModified(const Core::VFS::VFSPath& path);
        void                   OnAssetDeleted(const Core::VFS::VFSPath& path);

        /// @brief UUID-based equivalent of OnAssetModified's stale-marking, for callers
        ///        (e.g. AssetManager::IngestMesh re-ingesting an in-memory asset) that
        ///        have no VFSPath to look up. No dependency cascade — fires OnStale for
        ///        this UUID only.
        void                   MarkStale(const uuids::uuid& uuid);
        void                   OnAssetRenamed(const Core::VFS::VFSPath& old_path, const Core::VFS::VFSPath& new_path);

        // Called per-file from the scanner's ScanComplete callback.
        void                   OnScanFileDiscovered(Core::VFS::IVFSContext& ctx, const Core::VFS::VFSPath& path, Managers::AssetType type);

        uint32_t               RecordCount() const;
        uint32_t               EdgeCount() const;
        const DependencyGraph& GetGraph() const
        {
            return m_graph;
        }

        // Write DOT-format graph to out_buf. Returns bytes written (0 if buffer too small).
        uint32_t                   DumpGraphDOT(char* out_buf, uint32_t out_len) const;

        static Managers::AssetType InferTypeFromExtension(const Core::VFS::VFSPath& path);
        static bool                ExtensionMatches(const Core::VFS::VFSPath& path, const char* ext);

    private:
        AssetIndex      m_index                                  = {};
        DependencyGraph m_graph                                  = {};
        void*           m_reload_cb_ctx                          = nullptr;
        void (*m_reload_cb)(void*, std::span<const uuids::uuid>) = nullptr;

        void*                     m_ready_cb_ctx                 = nullptr;
        AssetReadyCallback        m_ready_cb                     = nullptr;
        void*                     m_stale_cb_ctx                 = nullptr;
        AssetStaleCallback        m_stale_cb                     = nullptr;
        void*                     m_removed_cb_ctx               = nullptr;
        AssetRemovedCallback      m_removed_cb                   = nullptr;

        static constexpr uint32_t kMaxEventListeners             = 4;
        struct ReadyListener
        {
            void*              Context  = nullptr;
            AssetReadyCallback Callback = nullptr;
        };
        struct StaleListener
        {
            void*              Context  = nullptr;
            AssetStaleCallback Callback = nullptr;
        };
        struct RemovedListener
        {
            void*                Context  = nullptr;
            AssetRemovedCallback Callback = nullptr;
        };
        ReadyListener                m_ready_listeners[kMaxEventListeners]   = {};
        StaleListener                m_stale_listeners[kMaxEventListeners]   = {};
        RemovedListener              m_removed_listeners[kMaxEventListeners] = {};
        std::recursive_mutex         m_listener_mutex                        = {};

        void                         NotifyReadyListeners(const uuids::uuid& uuid, Managers::AssetHandle handle);
        void                         NotifyStaleListeners(const uuids::uuid& uuid);
        void                         NotifyRemovedListeners(const uuids::uuid& uuid, Managers::AssetType type);

        Core::Memory::ArenaAllocator m_scratch = {};

        bool                         PassesFilter(const AssetRecord& rec, const QueryFilter& f) const;
    };

} // namespace ZEngine::Core::VFS
