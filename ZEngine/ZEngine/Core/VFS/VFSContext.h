#pragma once
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <ZEngine/Core/VFS/VFSFileWatcher.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <mutex>

namespace ZEngine
{
    namespace Core
    {
        namespace VFS
        {
            struct AssetRegistry;
        }
    } // namespace Core
} // namespace ZEngine
namespace ZEngine
{
    namespace Importers
    {
        class ImportCoordinator;
    }
} // namespace ZEngine

namespace ZEngine::Core::VFS
{
    struct VFSScanner;
    struct VFSContextTestAccess;

    using FileChangeListener = void (*)(void* context, const VFSPath& path, WatchEventKind kind);

    struct VFSContext : IVFSContext
    {
        void                                                    Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity = 16);

        // Start the platform file watcher for the project root.
        // registry and coordinator are optional — pass nullptr if not yet available.
        // When a file is modified: registry->OnAssetModified + coordinator->Enqueue(Immediate)
        // When a file is deleted:  registry->OnAssetDeleted
        // When a file is renamed:  registry->OnAssetRenamed
        void                                                    InitWatcher(const char* project_root_native, VFSDirectoryCache* cache, VFSScanner* scanner, AssetRegistry* registry = nullptr, Importers::ImportCoordinator* coordinator = nullptr, FileChangeListener file_change_listener = nullptr, void* file_change_context = nullptr);

        // Scan the whole project into the registry/directory cache. Call once, after the
        // project's own backend is mounted — InitWatcher's file watcher only reacts to
        // changes from that point forward, it doesn't enumerate what's already on disk.
        void                                                    ScanProject();

        // Pump the watcher — call once per frame from MainThreadRun.
        void                                                    Tick();

        void                                                    ShutdownWatcher();

        [[nodiscard]] VFSImportPublication                      BeginImportPublication() override;
        void                                                    RecordImportPublicationArtifact(VFSImportPublication publication, const VFSPath& path) override;
        void                                                    EndImportPublication(VFSImportPublication publication) override;

        [[nodiscard]] VFSResult<IVFSFile*>                      Open(const VFSPath& absolute_path, VFSOpenFlags flags) override;
        void                                                    Close(IVFSFile* file) override;
        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena) override;
        [[nodiscard]] VFSResult<VFSFileStat>                    Stat(const VFSPath& absolute_path) override;
        [[nodiscard]] VFSResult<bool>                           Exists(const VFSPath& absolute_path) override;
        [[nodiscard]] VFSResult<void>                           Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority) override;
        [[nodiscard]] VFSResult<void>                           Unmount(const VFSPath& logical_root) override;
        [[nodiscard]] VFSResult<void>                           CreateDir(const VFSPath& absolute_path) override;
        [[nodiscard]] VFSResult<void>                           Remove(const VFSPath& absolute_path) override;
        [[nodiscard]] VFSResult<void>                           RemoveAll(const VFSPath& absolute_path) override;
        [[nodiscard]] VFSResult<void>                           Rename(const VFSPath& src, const VFSPath& dst) override;
        void                                                    Shutdown() override;

    private:
        friend struct VFSContextTestAccess;

        struct ImportPublicationState
        {
            VFSImportPublication             Publication      = {};
            uint64_t                         StartedAtNs      = 0;
            uint32_t                         Depth            = 0;
            bool                             RequiresScan     = false;
            Containers::Array<VFSPath>       Artifacts        = {};
            Containers::Array<VFSPath>       DirtyDirectories = {};
            Containers::Array<VFSWatchEvent> DeferredEvents   = {};
        };

        struct CompletedImportPublication
        {
            uint64_t                   StartedAtNs   = 0;
            uint64_t                   SettleUntilNs = 0;
            bool                       RequiresScan  = false;
            Containers::Array<VFSPath> Artifacts     = {};
        };

        [[nodiscard]] VFSResult<ResolveResult> ResolveWritable(const VFSPath& path) const;
        [[nodiscard]] VFSResult<VFSPath>       ToRelativeVFSPath(cstring native) const;
        [[nodiscard]] bool                     IsPublicationEvent(uint64_t started_at_ns, const Containers::Array<VFSPath>& artifacts, const VFSWatchEvent& event) const;
        [[nodiscard]] bool                     IsPublicationArtifact(const Containers::Array<VFSPath>& artifacts, const VFSPath& path) const;
        void                                   HandleWatchEvent(const VFSWatchEvent& event);
        void                                   ProcessWatchEvent(const VFSWatchEvent& event);
        void                                   FlushDeferredImportScan();

        VFSMountTable                          m_mount_table = {};
        Memory::ArenaAllocator*                m_arena       = nullptr;
        mutable std::mutex                     m_arena_mutex;

        IVFSPlatformWatcher*                   m_platform_watcher                                                  = nullptr;
        VFSFileWatcher*                        m_file_watcher                                                      = nullptr;
        VFSDirectoryCache*                     m_directory_cache                                                   = nullptr;
        VFSScanner*                            m_scanner                                                           = nullptr;
        AssetRegistry*                         m_registry                                                          = nullptr;
        Importers::ImportCoordinator*          m_coordinator                                                       = nullptr;
        FileChangeListener                     m_file_change_listener                                              = nullptr;
        void*                                  m_file_change_context                                               = nullptr;
        char                                   m_project_root_native[MAX_FILE_PATH_COUNT]                          = {};

        // Native watchers observe filesystem changes asynchronously. Keep each
        // completed publication long enough for the raw event and its debounce
        // window to drain before a single reconciliation scan is allowed through.
        static constexpr uint64_t              IMPORT_PUBLICATION_SETTLE_NS                                        = 500'000'000ULL;
        static constexpr uint32_t              COMPLETED_IMPORT_PUBLICATION_COUNT                                  = 8;

        ImportPublicationState                 m_active_import_publication                                         = {};
        CompletedImportPublication             m_completed_import_publications[COMPLETED_IMPORT_PUBLICATION_COUNT] = {};
        uint32_t                               m_next_completed_import_publication                                 = 0;
        uint64_t                               m_next_import_publication_id                                        = 1;
    };

} // namespace ZEngine::Core::VFS
