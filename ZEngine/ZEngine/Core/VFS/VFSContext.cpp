#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/LoggerDefinition.h>

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
    }

    void VFSContext::InitWatcher(const char* project_root_native, VFSDirectoryCache* cache, VFSScanner* scanner)
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

        m_directory_cache   = cache;
        m_scanner           = scanner;

        const size_t length = Helpers::secure_strlen(project_root_native);
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
        m_file_watcher->Initialize(m_arena);

        const WatchHandle root_handle = m_file_watcher->Watch(m_project_root_native, /*recursive=*/true, [this](const VFSWatchEvent& ev) {
            const bool         full_rescan = (ev.Kind == WatchEventKind::Overflow);

            VFSResult<VFSPath> path        = full_rescan ? VFSPath::FromNative(m_project_root_native) : VFSPath::FromNative(ev.Path);
            if (path.Failed())
            {
                return;
            }

            const VFSPath target = (ev.IsDirectory || full_rescan) ? path.Value() : path.Value().Parent();

            if (m_directory_cache)
            {
                m_directory_cache->Invalidate(target);
                if (ev.Kind == WatchEventKind::Renamed && ev.OldPath[0] != '\0')
                {
                    VFSResult<VFSPath> old_path = VFSPath::FromNative(ev.OldPath);
                    if (old_path.Succeeded())
                    {
                        m_directory_cache->Invalidate(ev.IsDirectory ? old_path.Value() : old_path.Value().Parent());
                    }
                }
            }

            if (m_scanner && m_directory_cache && !m_scanner->IsScanning())
            {
                m_scanner->Scan(this, target, m_directory_cache);
            }
        });

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

    void VFSContext::Tick()
    {
        if (m_file_watcher)
        {
            m_file_watcher->Tick();
        }
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
        m_directory_cache = nullptr;
        m_scanner         = nullptr;
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
