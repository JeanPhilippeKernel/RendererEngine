#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/VFSFileWatcher.h>
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::Core::VFS
{

    VFSFileWatcher::VFSFileWatcher(IVFSPlatformWatcher* platform, std::chrono::milliseconds debounce_window) : m_platform(platform), m_window(debounce_window) {}

    VFSFileWatcher::~VFSFileWatcher()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_platform)
        {
            for (const auto& [handle, entry] : m_callbacks)
            {
                m_platform->RemoveWatch(handle);
            }
        }
        m_callbacks.clear();
        m_pending.clear();
    }

    void VFSFileWatcher::Initialize(Memory::ArenaAllocator* arena, size_t pending_capacity)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_arena = arena;
        m_pending.init(arena, pending_capacity);
        m_callbacks.init(arena, pending_capacity);

        // Dedicated sub arena for per tick temporaries
        arena->CreateSubArena(ZKilo(512), &m_local_arena);
    }

    WatchHandle VFSFileWatcher::Watch(const char* native_path, bool recursive, WatchCallback cb)
    {
        if (!m_platform || !native_path || native_path[0] == '\0')
        {
            return INVALID_WATCH_HANDLE;
        }

        const WatchHandle handle = m_platform->AddWatch(native_path, recursive);
        if (handle == INVALID_WATCH_HANDLE)
        {
            return INVALID_WATCH_HANDLE;
        }

        WatchEntry entry;
        entry.RootLength = Helpers::secure_strlen(native_path);
        if (entry.RootLength >= MAX_FILE_PATH_COUNT)
        {
            entry.RootLength = MAX_FILE_PATH_COUNT - 1;
        }
        Helpers::secure_memcpy(entry.Root, MAX_FILE_PATH_COUNT, native_path, entry.RootLength);
        entry.Root[entry.RootLength] = '\0';

        while (entry.RootLength > 1 && entry.Root[entry.RootLength - 1] == '/')
        {
            entry.Root[--entry.RootLength] = '\0';
        }
        entry.Callback = std::move(cb);

        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks[handle] = std::move(entry);
        return handle;
    }

    void VFSFileWatcher::Unwatch(WatchHandle handle)
    {
        if (handle == INVALID_WATCH_HANDLE)
        {
            return;
        }

        if (m_platform)
        {
            m_platform->RemoveWatch(handle);
        }

        std::lock_guard<std::mutex> lock(m_mutex);
        m_callbacks.remove(handle);

        for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
        {
            if ((*it).second.Owner == handle)
            {
                m_pending.remove((*it).first);
            }
        }
    }

    void VFSFileWatcher::Tick()
    {
        if (m_platform)
        {
            m_platform->Poll([](void* ctx, const VFSWatchEvent& ev) { reinterpret_cast<VFSFileWatcher*>(ctx)->OnRawEvent(ev); }, this);
        }

        EmitReady(std::chrono::steady_clock::now());
    }

    void VFSFileWatcher::OnRawEvent(const VFSWatchEvent& ev)
    {
        std::lock_guard<std::mutex> lock(m_mutex);

        const bool                  is_overflow = (ev.Kind == WatchEventKind::Overflow);
        const WatchHandle           owner       = is_overflow ? INVALID_WATCH_HANDLE : ResolveOwner(ev.Path);
        if (!is_overflow && owner == INVALID_WATCH_HANDLE)
        {
            return;
        }

        DebounceEntry& entry = m_pending[VFSWatchPathKey{ev.Path}];
        entry.Event          = ev;
        entry.Owner          = owner;
        entry.LastSeen       = std::chrono::steady_clock::now();
    }

    void VFSFileWatcher::EmitReady(std::chrono::steady_clock::time_point now)
    {
        auto                             scratch = ZGetScratch(&m_local_arena);

        Containers::Array<DebounceEntry> ready;
        ready.init(scratch.Arena, 16);

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            for (auto it = m_pending.begin(); it != m_pending.end(); ++it)
            {
                const DebounceEntry& entry = (*it).second;
                if ((now - entry.LastSeen) >= m_window)
                {
                    ready.push(entry);
                    m_pending.remove((*it).first);
                }
            }
        }

        Containers::Array<WatchCallback> targets;
        targets.init(scratch.Arena, 4);

        for (const DebounceEntry& entry : ready)
        {
            targets.clear();
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (entry.Event.Kind == WatchEventKind::Overflow)
                {
                    for (const auto& [handle, watch] : m_callbacks)
                    {
                        targets.push(watch.Callback);
                    }
                }
                else if (const WatchEntry* watch = m_callbacks.find(entry.Owner))
                {
                    targets.push(watch->Callback);
                }
            }

            for (const WatchCallback& cb : targets)
            {
                if (cb)
                {
                    cb(entry.Event);
                }
            }
        }

        ZReleaseScratch(scratch);
    }

    WatchHandle VFSFileWatcher::ResolveOwner(const char* native_path) const
    {
        if (!native_path || native_path[0] == '\0')
        {
            return INVALID_WATCH_HANDLE;
        }

        const size_t path_length = Helpers::secure_strlen(native_path);
        WatchHandle  best        = INVALID_WATCH_HANDLE;
        size_t       best_length = 0;

        for (const auto& [handle, entry] : m_callbacks)
        {
            if (entry.RootLength > path_length || Helpers::secure_memcmp(native_path, path_length, entry.Root, entry.RootLength, entry.RootLength) != 0)
            {
                continue;
            }
            const bool boundary_ok = (path_length == entry.RootLength) || (native_path[entry.RootLength] == '/') || (entry.RootLength == 1 && entry.Root[0] == '/');
            if (boundary_ok && (best == INVALID_WATCH_HANDLE || entry.RootLength > best_length))
            {
                best        = handle;
                best_length = entry.RootLength;
            }
        }
        return best;
    }

    size_t VFSFileWatcher::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_pending.size();
    }

} // namespace ZEngine::Core::VFS
