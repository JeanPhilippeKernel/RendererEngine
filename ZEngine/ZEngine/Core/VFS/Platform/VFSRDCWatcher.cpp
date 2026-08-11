#include <ZEngine/Core/VFS/Platform/VFSRDCWatcher.h>
#if defined(_WIN32)
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::Core::VFS
{
    static constexpr DWORD     NOTIFY_FILTER       = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
    static constexpr ULONG_PTR COMPLETION_KEY_STOP = 1;

    size_t                     VFSRDCWatcher::ClampedLength(const char* text)
    {
        const size_t length = Helpers::secure_strlen(text);
        return length < MAX_FILE_PATH_COUNT ? length : MAX_FILE_PATH_COUNT - 1;
    }

    void VFSRDCWatcher::AppendWideName(char* out, size_t out_size, const char* root, const wchar_t* name, DWORD name_bytes)
    {
        char      narrow[MAX_FILE_PATH_COUNT] = {};

        const int wide_length                 = static_cast<int>(name_bytes / sizeof(wchar_t));
        const int written                     = WideCharToMultiByte(CP_UTF8, 0, name, wide_length, narrow, sizeof(narrow) - 1, nullptr, nullptr);
        narrow[written > 0 ? written : 0]     = '\0';

        for (char* c = narrow; *c != '\0'; ++c)
        {
            if (*c == '\\')
            {
                *c = '/';
            }
        }

        if (!out || out_size == 0)
        {
            return;
        }

        const size_t root_length   = Helpers::secure_strlen(root);
        const bool   has_separator = (root_length > 0 && (root[root_length - 1] == '/' || root[root_length - 1] == '\\'));

        out[0]                     = '\0';
        Helpers::secure_strncpy(out, out_size, root, root_length < out_size ? root_length : out_size - 1);

        size_t pos = Helpers::secure_strlen(out);
        if (!has_separator && pos + 1 < out_size)
        {
            out[pos++] = '/';
            out[pos]   = '\0';
        }

        if (pos < out_size - 1)
        {
            const size_t remaining   = out_size - pos - 1;
            const size_t name_length = Helpers::secure_strlen(narrow);
            Helpers::secure_strncpy(out + pos, out_size - pos, narrow, name_length < remaining ? name_length : remaining);
        }
    }

    VFSWatchEvent VFSRDCWatcher::MakeEvent(const char* path, WatchEventKind kind)
    {
        VFSWatchEvent ev{};
        Helpers::secure_strncpy(ev.Path, sizeof(ev.Path), path, ClampedLength(path));
        ev.Kind = kind;
        return ev;
    }

    VFSRDCWatcher::VFSRDCWatcher()
    {
        m_iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    }

    VFSRDCWatcher::~VFSRDCWatcher()
    {
        StopThread();

        std::lock_guard<std::mutex> lock(m_watch_mutex);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it)
        {
            WatchEntry* entry = (*it).second;
            if (entry && entry->DirHandle != INVALID_HANDLE_VALUE)
            {
                CancelIoEx(entry->DirHandle, &entry->Overlapped);
                CloseHandle(entry->DirHandle);
                entry->DirHandle = INVALID_HANDLE_VALUE;
            }
        }
        m_entries.clear();

        if (m_iocp)
        {
            CloseHandle(m_iocp);
            m_iocp = nullptr;
        }
    }

    void VFSRDCWatcher::Initialize(Memory::ArenaAllocator* arena, size_t capacity)
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);

        m_arena = arena;
        m_entries.init(arena, capacity);
        m_queue.init(arena, capacity);
        m_batch.init(arena, capacity);
        m_drain.init(arena, capacity);
    }

    bool VFSRDCWatcher::ReissueRead(WatchEntry* entry)
    {
        Helpers::secure_memset(&entry->Overlapped, 0, sizeof(entry->Overlapped), sizeof(entry->Overlapped));
        return ReadDirectoryChangesW(entry->DirHandle, entry->Buffer, sizeof(entry->Buffer), entry->Recursive ? TRUE : FALSE, NOTIFY_FILTER, nullptr, &entry->Overlapped, nullptr) != FALSE;
    }

    WatchHandle VFSRDCWatcher::AddWatch(const char* native_path, bool recursive)
    {
        if (!IsValid() || !native_path || native_path[0] == '\0')
        {
            return INVALID_WATCH_HANDLE;
        }

        const HANDLE dir = CreateFileA(native_path, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
        if (dir == INVALID_HANDLE_VALUE)
        {
            return INVALID_WATCH_HANDLE;
        }

        std::lock_guard<std::mutex> lock(m_watch_mutex);

        void*                       storage = ZAlloc(m_arena, sizeof(WatchEntry), ZAlignof(WatchEntry));
        if (!storage)
        {
            CloseHandle(dir);
            return INVALID_WATCH_HANDLE;
        }

        WatchEntry*       entry  = new (storage) WatchEntry();
        const WatchHandle handle = m_next_handle++;
        entry->DirHandle         = dir;
        entry->Handle            = handle;
        entry->Recursive         = recursive;
        Helpers::secure_strncpy(entry->Root, sizeof(entry->Root), native_path, ClampedLength(native_path));

        if (!CreateIoCompletionPort(dir, m_iocp, reinterpret_cast<ULONG_PTR>(entry), 0) || !ReissueRead(entry))
        {
            CloseHandle(dir);
            entry->DirHandle = INVALID_HANDLE_VALUE;
            return INVALID_WATCH_HANDLE;
        }

        m_entries[handle] = entry;
        return handle;
    }

    void VFSRDCWatcher::RemoveWatch(WatchHandle handle)
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);

        WatchEntry**                slot = m_entries.find(handle);
        if (!slot || !*slot)
        {
            return;
        }

        WatchEntry* entry = *slot;
        CancelIoEx(entry->DirHandle, &entry->Overlapped);
        CloseHandle(entry->DirHandle);
        entry->DirHandle = INVALID_HANDLE_VALUE;

        m_entries.remove(handle);
    }

    void VFSRDCWatcher::ProcessNotifications(WatchEntry* entry, DWORD bytes)
    {
        if (bytes == 0)
        {
            VFSWatchEvent overflow{};
            overflow.Kind = WatchEventKind::Overflow;
            PushEvent(overflow);
            return;
        }

        m_batch.clear();

        char  pending_rename[MAX_FILE_PATH_COUNT] = {};
        bool  has_pending_rename                  = false;

        DWORD offset                              = 0;
        for (;;)
        {
            const FILE_NOTIFY_INFORMATION* info                      = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(entry->Buffer + offset);

            char                           path[MAX_FILE_PATH_COUNT] = {};
            AppendWideName(path, sizeof(path), entry->Root, info->FileName, info->FileNameLength);

            switch (info->Action)
            {
                case FILE_ACTION_ADDED:
                    m_batch.push(MakeEvent(path, WatchEventKind::Created));
                    break;
                case FILE_ACTION_REMOVED:
                    m_batch.push(MakeEvent(path, WatchEventKind::Deleted));
                    break;
                case FILE_ACTION_MODIFIED:
                    m_batch.push(MakeEvent(path, WatchEventKind::Modified));
                    break;
                case FILE_ACTION_RENAMED_OLD_NAME:
                    Helpers::secure_strcpy(pending_rename, sizeof(pending_rename), path);
                    has_pending_rename = true;
                    break;
                case FILE_ACTION_RENAMED_NEW_NAME:
                {
                    VFSWatchEvent renamed = MakeEvent(path, has_pending_rename ? WatchEventKind::Renamed : WatchEventKind::Created);
                    if (has_pending_rename)
                    {
                        Helpers::secure_strcpy(renamed.OldPath, sizeof(renamed.OldPath), pending_rename);
                        has_pending_rename = false;
                    }
                    m_batch.push(renamed);
                    break;
                }
                default:
                    break;
            }

            if (info->NextEntryOffset == 0)
            {
                break;
            }
            offset += info->NextEntryOffset;
        }

        if (m_batch.size() > 0)
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            for (size_t i = 0; i < m_batch.size(); ++i)
            {
                m_queue.push(m_batch[i]);
            }
        }
        m_batch.clear();
    }

    void VFSRDCWatcher::PushEvent(const VFSWatchEvent& ev)
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.push(ev);
    }

    void VFSRDCWatcher::DrainCompletions(DWORD timeout_ms)
    {
        for (;;)
        {
            DWORD       bytes      = 0;
            ULONG_PTR   key        = 0;
            OVERLAPPED* overlapped = nullptr;

            if (!GetQueuedCompletionStatus(m_iocp, &bytes, &key, &overlapped, timeout_ms))
            {
                return;
            }
            if (key == COMPLETION_KEY_STOP)
            {
                return;
            }

            WatchEntry* entry = reinterpret_cast<WatchEntry*>(key);
            if (!entry)
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_watch_mutex);
                WatchEntry**                slot = m_entries.find(entry->Handle);
                if (!slot || *slot != entry)
                {
                    continue;
                }
            }

            ProcessNotifications(entry, bytes);
            ReissueRead(entry);

            timeout_ms = 0;
        }
    }

    void VFSRDCWatcher::Poll(RawEventCallback cb, void* ctx)
    {
        if (!m_arena)
        {
            return;
        }

        if (!m_running.value.load(std::memory_order_acquire))
        {
            DrainCompletions(0);
        }
        m_drain.clear();
        {
            std::lock_guard<std::mutex> lock(m_queue_mutex);
            for (size_t i = 0; i < m_queue.size(); ++i)
            {
                m_drain.push(m_queue[i]);
            }
            m_queue.clear();
        }

        for (size_t i = 0; i < m_drain.size(); ++i)
        {
            cb(ctx, m_drain[i]);
        }
    }

    void VFSRDCWatcher::ThreadMain()
    {
        while (m_running.value.load(std::memory_order_acquire))
        {
            DrainCompletions(INFINITE);
        }
    }

    void VFSRDCWatcher::StartThread()
    {
        if (!IsValid() || m_running.value.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        m_thread = std::thread([this] { ThreadMain(); });
    }

    void VFSRDCWatcher::StopThread()
    {
        if (!m_running.value.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        PostQueuedCompletionStatus(m_iocp, 0, COMPLETION_KEY_STOP, nullptr);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    size_t VFSRDCWatcher::WatchCount() const
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);
        return m_entries.size();
    }

} // namespace ZEngine::Core::VFS
#endif // _WIN32
