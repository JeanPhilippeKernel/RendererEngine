#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetIndex.h>
#include <ZEngine/Core/VFS/Registry/DependencyGraph.h>
#include <optional>
#include <span>

namespace ZEngine::Core::VFS
{
    class AssetRegistry
    {
    public:
        AssetRegistry()  = default;
        ~AssetRegistry() = default;

        // arena: persistent arena. scratch_size: byte size of BFS scratch sub-arena.
        void               Initialize(Core::Memory::ArenaAllocator* arena, uint64_t scratch_size = 65536);

        // Registration
        RegisterResult     Register(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta);

        RegisterResult     RegisterLoaded(const uuids::uuid& uuid, Managers::AssetType type, const Core::VFS::VFSPath& path, const Core::VFS::MetaFileData& meta, Managers::AssetHandle slot_handle);

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
        void                   SetHotReloadCallback(void* ctx, void (*cb)(void*, std::span<const uuids::uuid>));

        void                   OnAssetModified(const Core::VFS::VFSPath& path);
        void                   OnAssetDeleted(const Core::VFS::VFSPath& path);
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
        Core::Memory::ArenaAllocator m_scratch                   = {};

        bool                         PassesFilter(const AssetRecord& rec, const QueryFilter& f) const;
    };

} // namespace ZEngine::Core::VFS
