#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <cstring>

#if defined(__APPLE__)
#include <ZEngine/Core/VFS/Platform/VFSFSEventsWatcher.h>
#elif defined(__linux__)
#include <ZEngine/Core/VFS/Platform/VFSInotifyWatcher.h>
#elif defined(_WIN32)
#include <ZEngine/Core/VFS/Platform/VFSRDCWatcher.h>
#endif

namespace ZEngine::Core::VFS
{
    void VFSContext::Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity)
    {
        m_arena = arena;
        m_mount_table.Initialize(m_arena, mount_table_capacity);

        m_active_import_publication.Artifacts.init(m_arena, 16);
        m_active_import_publication.DirtyDirectories.init(m_arena, 8);
        m_active_import_publication.DeferredEvents.init(m_arena, 32);
        for (uint32_t i = 0; i < COMPLETED_IMPORT_PUBLICATION_COUNT; ++i)
            m_completed_import_publications[i].Artifacts.init(m_arena, 16);
    }

    void VFSContext::InitWatcher(const char* project_root_native, VFSDirectoryCache* cache, VFSScanner* scanner, AssetRegistry* registry, Importers::ImportCoordinator* coordinator, FileChangeListener file_change_listener, void* file_change_context)
    {
        if (!m_arena)
        {
            ZENGINE_LOG_VFS_ERR("InitWatcher: arena is null");
            return;
        }
        if (!project_root_native || project_root_native[0] == '\0')
        {
            ZENGINE_LOG_VFS_ERR("InitWatcher: project_root_native is null or empty");
            return;
        }

        m_directory_cache      = cache;
        m_scanner              = scanner;
        m_registry             = registry;
        m_coordinator          = coordinator;
        m_file_change_listener = file_change_listener;
        m_file_change_context  = file_change_context;

        const size_t length    = Helpers::secure_strlen(project_root_native);
        Helpers::secure_strncpy(m_project_root_native, sizeof(m_project_root_native), project_root_native, length < MAX_FILE_PATH_COUNT ? length : MAX_FILE_PATH_COUNT - 1);

#if defined(__APPLE__)
        {
            void* storage = ZAlloc(m_arena, sizeof(VFSFSEventsWatcher), ZAlignof(VFSFSEventsWatcher));
            if (!storage)
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: arena allocation failed for VFSFSEventsWatcher");
                return;
            }
            auto* fsevents = new (storage) VFSFSEventsWatcher();
            fsevents->Initialize(m_arena);
            if (!fsevents->IsValid())
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: VFSFSEventsWatcher failed to initialize");
                fsevents->~VFSFSEventsWatcher();
                return;
            }
            m_platform_watcher = fsevents;
        }
#elif defined(__linux__)
        {
            void* storage = ZAlloc(m_arena, sizeof(VFSInotifyWatcher), ZAlignof(VFSInotifyWatcher));
            if (!storage)
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: arena allocation failed for VFSInotifyWatcher");
                return;
            }
            auto* inotify = new (storage) VFSInotifyWatcher();
            inotify->Initialize(m_arena);
            if (!inotify->IsValid())
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: VFSInotifyWatcher failed to initialize");
                inotify->~VFSInotifyWatcher();
                return;
            }
            m_platform_watcher = inotify;
        }
#elif defined(_WIN32)
        {
            void* storage = ZAlloc(m_arena, sizeof(VFSRDCWatcher), ZAlignof(VFSRDCWatcher));
            if (!storage)
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: arena allocation failed for VFSRDCWatcher");
                return;
            }
            auto* rdc = new (storage) VFSRDCWatcher();
            rdc->Initialize(m_arena);
            if (!rdc->IsValid())
            {
                ZENGINE_LOG_VFS_ERR("InitWatcher: VFSRDCWatcher failed to initialize");
                rdc->~VFSRDCWatcher();
                return;
            }
            m_platform_watcher = rdc;
        }
#else
        ZENGINE_LOG_VFS_ERR("InitWatcher: unsupported platform");
        return;
#endif

        void* fw_storage = ZAlloc(m_arena, sizeof(VFSFileWatcher), ZAlignof(VFSFileWatcher));
        if (!fw_storage)
        {
            ZENGINE_LOG_VFS_ERR("InitWatcher: arena allocation failed for VFSFileWatcher");
            m_platform_watcher->~IVFSPlatformWatcher();
            m_platform_watcher = nullptr;
            return;
        }
        m_file_watcher = new (fw_storage) VFSFileWatcher(m_platform_watcher);
        // Default capacity (64) is too small for a single material-heavy import —
        // each material now also gets an explicit .meta write (#762), and a
        // multi-material GLB (mesh + N materials + N textures) can generate more
        // simultaneous pending debounce entries than that before any flush.
        m_file_watcher->Initialize(m_arena, 256);

        const WatchHandle root_handle = m_file_watcher->Watch(m_project_root_native, /*recursive=*/true, [this](const VFSWatchEvent& event) { HandleWatchEvent(event); });

        if (root_handle == INVALID_WATCH_HANDLE)
        {
            ZENGINE_LOG_VFS_ERR("InitWatcher: failed to watch root path '{}'", m_project_root_native);
            m_file_watcher->~VFSFileWatcher();
            m_file_watcher = nullptr;
            m_platform_watcher->~IVFSPlatformWatcher();
            m_platform_watcher = nullptr;
            return;
        }

        m_platform_watcher->StartThread();
    }

    VFSImportPublication VFSContext::BeginImportPublication()
    {
        if (m_active_import_publication.Publication.IsValid())
        {
            ++m_active_import_publication.Depth;
            return m_active_import_publication.Publication;
        }

        m_active_import_publication.Publication  = {.Id = m_next_import_publication_id++};
        m_active_import_publication.StartedAtNs  = VFSWatchTimestampNowNanoseconds();
        m_active_import_publication.Depth        = 1;
        m_active_import_publication.RequiresScan = false;
        m_active_import_publication.Artifacts.clear();
        m_active_import_publication.DirtyDirectories.clear();
        m_active_import_publication.DeferredEvents.clear();
        return m_active_import_publication.Publication;
    }

    void VFSContext::RecordImportPublicationArtifact(VFSImportPublication publication, const VFSPath& path)
    {
        if (!publication.IsValid() || publication.Id != m_active_import_publication.Publication.Id || !path.IsValid())
            return;

        for (size_t i = 0; i < m_active_import_publication.Artifacts.size(); ++i)
            if (m_active_import_publication.Artifacts[i] == path)
                return;

        m_active_import_publication.Artifacts.push(path);

        const VFSPath parent = path.Parent();
        for (size_t i = 0; i < m_active_import_publication.DirtyDirectories.size(); ++i)
            if (m_active_import_publication.DirtyDirectories[i] == parent)
                return;
        m_active_import_publication.DirtyDirectories.push(parent);
    }

    void VFSContext::EndImportPublication(VFSImportPublication publication)
    {
        if (!publication.IsValid() || publication.Id != m_active_import_publication.Publication.Id)
            return;
        if (m_active_import_publication.Depth > 1)
        {
            --m_active_import_publication.Depth;
            return;
        }

        const uint64_t ended_at_ns = VFSWatchTimestampNowNanoseconds();

        // Events arriving during a publication are held until all artifact paths are
        // known. Generated paths are coalesced into the deferred scan; unrelated
        // paths retain the normal registry/coordinator behavior.
        for (size_t i = 0; i < m_active_import_publication.DeferredEvents.size(); ++i)
        {
            const VFSWatchEvent& event = m_active_import_publication.DeferredEvents[i];
            if (event.Kind == WatchEventKind::Overflow || IsPublicationEvent(m_active_import_publication.StartedAtNs, m_active_import_publication.Artifacts, event))
            {
                m_active_import_publication.RequiresScan = true;
                continue;
            }
            ProcessWatchEvent(event);
        }
        m_active_import_publication.DeferredEvents.clear();

        if (!m_active_import_publication.Artifacts.empty())
        {
            for (size_t i = 0; i < m_active_import_publication.DirtyDirectories.size(); ++i)
                if (m_directory_cache)
                    m_directory_cache->Invalidate(m_active_import_publication.DirtyDirectories[i]);
            m_active_import_publication.RequiresScan = true;
        }

        // Retain the exact output set until watcher delivery has settled. A raw
        // inotify/FSEvents event may arrive after the importer returns, so using
        // EndImportPublication's timestamp as a causal boundary is not reliable.
        CompletedImportPublication& completed = m_completed_import_publications[m_next_completed_import_publication];
        completed.StartedAtNs                 = m_active_import_publication.StartedAtNs;
        completed.SettleUntilNs               = ended_at_ns + IMPORT_PUBLICATION_SETTLE_NS;
        completed.RequiresScan                = m_active_import_publication.RequiresScan;
        completed.Artifacts.clear();
        for (size_t i = 0; i < m_active_import_publication.Artifacts.size(); ++i)
            completed.Artifacts.push(m_active_import_publication.Artifacts[i]);
        m_next_completed_import_publication      = (m_next_completed_import_publication + 1) % COMPLETED_IMPORT_PUBLICATION_COUNT;

        m_active_import_publication.Publication  = {};
        m_active_import_publication.StartedAtNs  = 0;
        m_active_import_publication.Depth        = 0;
        m_active_import_publication.RequiresScan = false;
        m_active_import_publication.Artifacts.clear();
        m_active_import_publication.DirtyDirectories.clear();
    }

    VFSResult<VFSPath> VFSContext::ToRelativeVFSPath(cstring native) const
    {
        if (!native)
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);

        const size_t root_len   = Helpers::secure_strlen(m_project_root_native);
        const size_t native_len = Helpers::secure_strlen(native);
        if (native_len >= root_len && strncmp(native, m_project_root_native, root_len) == 0 && (native[root_len] == '\0' || native[root_len] == PLATFORM_OS_BACKSLASH))
            return VFSPath::Parse(native[root_len] != '\0' ? native + root_len : "/");
        return VFSPath::FromNative(native);
    }

    bool VFSContext::IsPublicationArtifact(const Containers::Array<VFSPath>& artifacts, const VFSPath& path) const
    {
        for (size_t i = 0; i < artifacts.size(); ++i)
        {
            const VFSPath& artifact = artifacts[i];
            if (artifact == path)
                return true;
            // Metadata is stored beside its registered artifact as <artifact>.meta.
            if (path.Length() == artifact.Length() + 5 && strncmp(path.CStr(), artifact.CStr(), artifact.Length()) == 0 && strncmp(path.CStr() + artifact.Length(), ".meta", 5) == 0)
                return true;
        }
        return false;
    }

    bool VFSContext::IsPublicationEvent(uint64_t started_at_ns, const Containers::Array<VFSPath>& artifacts, const VFSWatchEvent& event) const
    {
        if (event.Kind == WatchEventKind::Overflow || event.ObservedAtNanoseconds == 0 || event.ObservedAtNanoseconds < started_at_ns)
            return false;

        VFSResult<VFSPath> path = ToRelativeVFSPath(event.Path);
        return path.Succeeded() && IsPublicationArtifact(artifacts, path.Value());
    }

    void VFSContext::HandleWatchEvent(const VFSWatchEvent& event)
    {
        if (m_active_import_publication.Publication.IsValid() && event.ObservedAtNanoseconds >= m_active_import_publication.StartedAtNs)
        {
            m_active_import_publication.DeferredEvents.push(event);
            return;
        }

        for (uint32_t i = 0; i < COMPLETED_IMPORT_PUBLICATION_COUNT; ++i)
        {
            CompletedImportPublication& completed = m_completed_import_publications[i];
            if (completed.SettleUntilNs != 0 && VFSWatchTimestampNowNanoseconds() <= completed.SettleUntilNs && IsPublicationEvent(completed.StartedAtNs, completed.Artifacts, event))
            {
                completed.RequiresScan = true;
                return;
            }
        }

        ProcessWatchEvent(event);
    }

    void VFSContext::ProcessWatchEvent(const VFSWatchEvent& event)
    {
        const bool         full_rescan = (event.Kind == WatchEventKind::Overflow);
        VFSResult<VFSPath> path        = full_rescan ? ToRelativeVFSPath(m_project_root_native) : ToRelativeVFSPath(event.Path);
        if (path.Failed())
            return;

        const VFSPath target    = (event.IsDirectory || full_rescan) ? path.Value() : path.Value().Parent();
        const VFSPath file_path = path.Value();

        if (m_directory_cache)
        {
            m_directory_cache->Invalidate(target);
            if (event.Kind == WatchEventKind::Renamed && event.OldPath[0] != '\0')
            {
                VFSResult<VFSPath> old_path = ToRelativeVFSPath(event.OldPath);
                if (old_path.Succeeded())
                    m_directory_cache->Invalidate(event.IsDirectory ? old_path.Value() : old_path.Value().Parent());
            }
        }

        auto is_temporary = [](const VFSPath& path) -> bool {
            const VFSPathComponent extension = path.Extension();
            return extension.Data && extension.Length == 4 && extension.Data[0] == '.' && extension.Data[1] == 't' && extension.Data[2] == 'm' && extension.Data[3] == 'p';
        };
        auto is_meta = [](const VFSPath& path) -> bool {
            const VFSPathComponent extension = path.Extension();
            return extension.Data && extension.Length == 5 && extension.Data[0] == '.' && extension.Data[1] == 'm' && extension.Data[2] == 'e' && extension.Data[3] == 't' && extension.Data[4] == 'a';
        };

        if (!event.IsDirectory && !full_rescan && is_temporary(file_path))
            return;
        if (!event.IsDirectory && !full_rescan && !is_meta(file_path))
        {
            switch (event.Kind)
            {
                case WatchEventKind::Modified:
                    if (m_registry)
                        m_registry->OnAssetModified(file_path);
                    if (m_coordinator)
                        m_coordinator->Enqueue(file_path, Importers::ImportPriority::Immediate);
                    break;

                case WatchEventKind::Deleted:
                    if (m_registry)
                        m_registry->OnAssetDeleted(file_path);
                    break;

                case WatchEventKind::Renamed:
                    if (m_registry && event.OldPath[0] != '\0')
                    {
                        VFSResult<VFSPath> old_path = ToRelativeVFSPath(event.OldPath);
                        if (old_path.Succeeded())
                            m_registry->OnAssetRenamed(old_path.Value(), file_path);
                    }
                    break;

                default:
                    break;
            }

            if (m_file_change_listener)
                m_file_change_listener(m_file_change_context, file_path, event.Kind);
        }

        if (m_scanner && m_directory_cache && !m_scanner->IsScanning())
            m_scanner->Scan(this, target, m_directory_cache);
    }

    void VFSContext::FlushDeferredImportScan()
    {
        if (!m_scanner || !m_directory_cache)
            return;

        const uint64_t now_ns      = VFSWatchTimestampNowNanoseconds();
        bool           should_scan = false;
        for (uint32_t i = 0; i < COMPLETED_IMPORT_PUBLICATION_COUNT; ++i)
        {
            const CompletedImportPublication& completed = m_completed_import_publications[i];
            if (completed.SettleUntilNs != 0 && completed.RequiresScan && now_ns >= completed.SettleUntilNs)
            {
                should_scan = true;
                break;
            }
        }
        if (!should_scan || m_scanner->IsScanning())
            return;

        // Start one scan for every completed publication whose watcher grace period
        // elapsed. The scan observes the final filesystem state, including a real
        // external edit that happened during the grace period.
        ScanProject();
        for (uint32_t i = 0; i < COMPLETED_IMPORT_PUBLICATION_COUNT; ++i)
        {
            CompletedImportPublication& completed = m_completed_import_publications[i];
            if (completed.SettleUntilNs != 0 && now_ns >= completed.SettleUntilNs)
            {
                completed.StartedAtNs   = 0;
                completed.SettleUntilNs = 0;
                completed.RequiresScan  = false;
                completed.Artifacts.clear();
            }
        }
    }

    void VFSContext::ScanProject()
    {
        if (m_scanner && m_directory_cache)
            m_scanner->Scan(this, VFSPath::Root(), m_directory_cache);
    }

    void VFSContext::Tick()
    {
        if (m_file_watcher)
        {
            m_file_watcher->Tick();
        }
        FlushDeferredImportScan();
    }

    void VFSContext::ShutdownWatcher()
    {
        if (m_platform_watcher)
        {
            m_platform_watcher->StopThread();
        }
        if (m_file_watcher)
        {
            m_file_watcher->~VFSFileWatcher();
            m_file_watcher = nullptr;
        }
        if (m_platform_watcher)
        {
            m_platform_watcher->~IVFSPlatformWatcher();
            m_platform_watcher = nullptr;
        }
        m_directory_cache                       = nullptr;
        m_scanner                               = nullptr;
        m_active_import_publication.Publication = {};
        m_active_import_publication.Artifacts.clear();
        m_active_import_publication.DirtyDirectories.clear();
        m_active_import_publication.DeferredEvents.clear();
        for (uint32_t i = 0; i < COMPLETED_IMPORT_PUBLICATION_COUNT; ++i)
        {
            m_completed_import_publications[i].StartedAtNs   = 0;
            m_completed_import_publications[i].SettleUntilNs = 0;
            m_completed_import_publications[i].RequiresScan  = false;
            m_completed_import_publications[i].Artifacts.clear();
        }
    }

    VFSResult<IVFSFile*> VFSContext::Open(const VFSPath& absolute_path, VFSOpenFlags flags)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<IVFSFile*>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return hit.Backend->Open(hit.RelativePath, flags);
    }

    void VFSContext::Close(IVFSFile* file)
    {
        if (!file)
        {
            return;
        }

        if (file->Owner)
        {
            file->Owner->Close(file);
        }
    }

    VFSResult<VFSFileStat> VFSContext::Stat(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<VFSFileStat>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return hit.Backend->Stat(hit.RelativePath);
    }

    VFSResult<bool> VFSContext::Exists(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<bool>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return VFSResult<bool>::Ok(hit.Backend->Exists(hit.RelativePath));
    }

    VFSResult<Containers::Array<VFSDirEntry>> VFSContext::List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena)
    {
        using ResultT = VFSResult<Containers::Array<VFSDirEntry>>;

        std::lock_guard<std::mutex>      arena_lock(m_arena_mutex);
        auto                             scratch = ZGetScratch(m_arena);

        Containers::Array<ResolveResult> matches;
        matches.init(scratch.Arena, 8);

        VFSResult<void> resolved = m_mount_table.ResolveAll(absolute_dir, matches);
        if (resolved.Failed())
        {
            ZReleaseScratch(scratch);
            return ResultT::Fail(resolved.Error());
        }
        if (matches.empty())
        {
            ZReleaseScratch(scratch);
            return ResultT::Fail(VFSError::NotFound);
        }

        Containers::Array<VFSDirEntry> merged;
        merged.init(out_arena, 16);

        for (size_t m = 0; m < matches.size(); ++m)
        {
            VFSResult<Containers::Array<VFSDirEntry>> sub = matches[m].Backend->List(scratch.Arena, matches[m].RelativePath);
            if (sub.Failed())
            {
                continue;
            }

            Containers::Array<VFSDirEntry>& entries = sub.Value();
            for (size_t e = 0; e < entries.size(); ++e)
            {
                VFSPathComponent name                   = entries[e].Path.Filename();

                char             filename[VFS_MAX_PATH] = {};
                Helpers::secure_memcpy(filename, sizeof(filename), name.Data, name.Length);
                filename[name.Length]    = '\0';

                VFSResult<VFSPath> child = absolute_dir.Append(filename);
                if (child.Failed())
                {
                    continue;
                }

                bool already_present = false;
                for (size_t k = 0; k < merged.size(); ++k)
                {
                    if (merged[k].Path == child.Value())
                    {
                        already_present = true;
                        break;
                    }
                }
                if (already_present)
                {
                    continue;
                }

                VFSDirEntry entry = entries[e];
                entry.Path        = child.Value();
                merged.push(entry);
            }
        }

        ZReleaseScratch(scratch);
        return ResultT::Ok(std::move(merged));
    }

    VFSResult<void> VFSContext::Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority)
    {
        return m_mount_table.Mount(backend, logical_root, priority);
    }

    VFSResult<void> VFSContext::Unmount(const VFSPath& logical_root)
    {
        return m_mount_table.Unmount(logical_root);
    }

    VFSResult<void> VFSContext::CreateDir(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> writable = ResolveWritable(absolute_path);
        if (writable.Failed())
        {
            return VFSResult<void>::Fail(writable.Error());
        }

        const ResolveResult& hit = writable.Value();
        return hit.Backend->CreateDir(hit.RelativePath);
    }

    VFSResult<void> VFSContext::Remove(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> writable = ResolveWritable(absolute_path);
        if (writable.Failed())
        {
            return VFSResult<void>::Fail(writable.Error());
        }

        const ResolveResult& hit = writable.Value();
        return hit.Backend->Remove(hit.RelativePath);
    }

    VFSResult<void> VFSContext::RemoveAll(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> writable = ResolveWritable(absolute_path);
        if (writable.Failed())
            return VFSResult<void>::Fail(writable.Error());
        const ResolveResult& hit = writable.Value();
        return hit.Backend->RemoveAll(hit.RelativePath);
    }

    VFSResult<void> VFSContext::Rename(const VFSPath& src, const VFSPath& dst)
    {
        VFSResult<ResolveResult> src_hit = ResolveWritable(src);
        if (src_hit.Failed())
        {
            return VFSResult<void>::Fail(src_hit.Error());
        }

        VFSResult<ResolveResult> dst_hit = ResolveWritable(dst);
        if (dst_hit.Failed())
        {
            return VFSResult<void>::Fail(dst_hit.Error());
        }

        if (src_hit.Value().Backend != dst_hit.Value().Backend)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        return src_hit.Value().Backend->Rename(src_hit.Value().RelativePath, dst_hit.Value().RelativePath);
    }

    void VFSContext::Shutdown()
    {
        ShutdownWatcher();
        m_mount_table.Clear();
        m_arena = nullptr;
    }

    VFSResult<ResolveResult> VFSContext::ResolveWritable(const VFSPath& path) const
    {
        std::lock_guard<std::mutex>      arena_lock(m_arena_mutex);
        auto                             scratch = ZGetScratch(m_arena);

        Containers::Array<ResolveResult> matches;
        matches.init(scratch.Arena, 8);

        VFSResult<void> resolved = m_mount_table.ResolveAll(path, matches);
        if (resolved.Failed())
        {
            ZReleaseScratch(scratch);
            return VFSResult<ResolveResult>::Fail(resolved.Error());
        }

        for (size_t i = 0; i < matches.size(); ++i)
        {
            if (HasCap(matches[i].Backend->Capabilities(), VFSBackendCaps::Write))
            {
                ResolveResult hit = matches[i];
                ZReleaseScratch(scratch);
                return VFSResult<ResolveResult>::Ok(hit);
            }
        }

        ZReleaseScratch(scratch);
        return VFSResult<ResolveResult>::Fail(VFSError::PermissionDenied);
    }

} // namespace ZEngine::Core::VFS
