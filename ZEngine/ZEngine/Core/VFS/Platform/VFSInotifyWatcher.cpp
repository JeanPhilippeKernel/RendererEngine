#include <ZEngine/Core/VFS/Platform/VFSInotifyWatcher.h>
#if defined(__linux__)
#include <ZEngine/Helpers/MemoryOperations.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <filesystem>

namespace ZEngine::Core::VFS
{
    static constexpr uint32_t WATCH_MASK = IN_CREATE | IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF | IN_ONLYDIR;

    size_t                    VFSInotifyWatcher::ClampedLength(const char* text)
    {
        const size_t length = Helpers::secure_strlen(text);
        return length < MAX_FILE_PATH_COUNT ? length : MAX_FILE_PATH_COUNT - 1;
    }

    void VFSInotifyWatcher::JoinPath(char* out, size_t out_size, const char* directory, const char* name)
    {
        if (!out || out_size == 0)
        {
            return;
        }
        Helpers::secure_memset(out, 0, out_size, out_size);

        const size_t directory_length = Helpers::secure_strlen(directory);
        size_t       length           = directory_length < out_size - 1 ? directory_length : out_size - 1;
        if (length > 0)
        {
            Helpers::secure_strncpy(out, out_size, directory, length);
        }

        if (!name || name[0] == '\0')
        {
            return;
        }

        const bool has_separator = (length > 0 && out[length - 1] == '/');
        if (!has_separator && length + 1 < out_size)
        {
            out[length++] = '/';
            out[length]   = '\0';
        }

        const size_t name_length = Helpers::secure_strlen(name);
        const size_t room        = (out_size - 1) - length;
        const size_t copied      = name_length < room ? name_length : room;
        if (copied > 0)
        {
            Helpers::secure_strncpy(out + length, out_size - length, name, copied);
        }
    }

    VFSWatchEvent VFSInotifyWatcher::MakeEvent(const char* path, WatchEventKind kind, bool is_directory)
    {
        VFSWatchEvent ev{};
        Helpers::secure_strncpy(ev.Path, sizeof(ev.Path), path, ClampedLength(path));
        ev.Kind        = kind;
        ev.IsDirectory = is_directory;
        return ev;
    }

    VFSInotifyWatcher::VFSInotifyWatcher()
    {
        m_inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);

        if (pipe2(m_wake_pipe, O_NONBLOCK | O_CLOEXEC) != 0)
        {
            m_wake_pipe[0] = -1;
            m_wake_pipe[1] = -1;
        }
    }

    VFSInotifyWatcher::~VFSInotifyWatcher()
    {
        StopThread();

        if (m_inotify_fd >= 0)
        {
            close(m_inotify_fd);
            m_inotify_fd = -1;
        }
        for (int& fd : m_wake_pipe)
        {
            if (fd >= 0)
            {
                close(fd);
                fd = -1;
            }
        }
    }

    void VFSInotifyWatcher::Initialize(Memory::ArenaAllocator* arena, size_t capacity)
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);

        m_arena = arena;
        m_wd_to_entry.init(arena, capacity);
        m_roots.init(arena, capacity);

        m_queue.init(arena, capacity);
        m_batch.init(arena, capacity);
        m_drain.init(arena, capacity);
        m_pending_moves.init(arena, 8);
    }

    int VFSInotifyWatcher::AddDirectory(WatchHandle handle, const char* directory)
    {
        const int wd = inotify_add_watch(m_inotify_fd, directory, WATCH_MASK);
        if (wd < 0)
        {
            VFSWatchEvent overflow{};
            overflow.Kind = WatchEventKind::Overflow;
            PushEvent(overflow);
            return -1;
        }

        WatchDescriptor descriptor;
        Helpers::secure_strncpy(descriptor.Path, sizeof(descriptor.Path), directory, ClampedLength(directory));
        descriptor.Owner  = handle;
        m_wd_to_entry[wd] = descriptor;

        if (WatchRoot* root = m_roots.find(handle))
        {
            root->Descriptors.push(wd);
        }
        return wd;
    }

    void VFSInotifyWatcher::AddSubtree(WatchHandle handle, const char* directory)
    {
        std::error_code ec;
        auto            options = std::filesystem::directory_options::skip_permission_denied;
        for (auto it = std::filesystem::recursive_directory_iterator(directory, options, ec); it != std::filesystem::recursive_directory_iterator(); it.increment(ec))
        {
            if (ec)
            {
                break;
            }
            if (it->is_directory(ec) && !ec)
            {
                AddDirectory(handle, it->path().c_str());
            }
        }
    }

    WatchHandle VFSInotifyWatcher::AddWatch(const char* native_path, bool recursive)
    {
        if (!IsValid() || !native_path || native_path[0] == '\0')
        {
            return INVALID_WATCH_HANDLE;
        }

        std::lock_guard<std::mutex> lock(m_watch_mutex);

        const WatchHandle           handle = m_next_handle++;
        WatchRoot                   root;
        Helpers::secure_strncpy(root.Root, sizeof(root.Root), native_path, ClampedLength(native_path));
        root.Recursive = recursive;
        root.Descriptors.init(m_arena, 8);
        m_roots[handle] = std::move(root);

        if (AddDirectory(handle, native_path) < 0)
        {
            m_roots.remove(handle);
            return INVALID_WATCH_HANDLE;
        }

        if (recursive)
        {
            AddSubtree(handle, native_path);
        }
        return handle;
    }

    void VFSInotifyWatcher::RemoveWatch(WatchHandle handle)
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);

        WatchRoot*                  root = m_roots.find(handle);
        if (!root)
        {
            return;
        }

        for (size_t i = 0; i < root->Descriptors.size(); ++i)
        {
            const int wd = root->Descriptors[i];
            inotify_rm_watch(m_inotify_fd, wd);
            m_wd_to_entry.remove(wd);
        }
        m_roots.remove(handle);
    }

    void VFSInotifyWatcher::DropDescriptor(int wd)
    {
        WatchDescriptor* entry = m_wd_to_entry.find(wd);
        if (!entry)
        {
            return;
        }

        if (WatchRoot* root = m_roots.find(entry->Owner))
        {
            for (size_t i = 0; i < root->Descriptors.size(); ++i)
            {
                if (root->Descriptors[i] == wd)
                {
                    root->Descriptors.erase(i);
                    break;
                }
            }
        }
        m_wd_to_entry.remove(wd);
    }

    void VFSInotifyWatcher::PushEvent(const VFSWatchEvent& ev)
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.push(ev);
    }

    void VFSInotifyWatcher::TranslateBuffer(const char* buffer, size_t length)
    {
        m_batch.clear();
        m_pending_moves.clear();

        size_t offset = 0;
        while (offset + sizeof(inotify_event) <= length)
        {
            const inotify_event* raw  = reinterpret_cast<const inotify_event*>(buffer + offset);
            offset                   += sizeof(inotify_event) + raw->len;

            if (raw->mask & IN_Q_OVERFLOW)
            {
                VFSWatchEvent overflow{};
                overflow.Kind = WatchEventKind::Overflow;
                m_batch.push(overflow);
                continue;
            }

            if (raw->mask & IN_IGNORED)
            {
                std::lock_guard<std::mutex> lock(m_watch_mutex);
                DropDescriptor(raw->wd);
                continue;
            }

            char        path[MAX_FILE_PATH_COUNT];
            WatchHandle owner     = INVALID_WATCH_HANDLE;
            bool        recursive = false;
            {
                std::lock_guard<std::mutex> lock(m_watch_mutex);

                WatchDescriptor*            descriptor = m_wd_to_entry.find(raw->wd);
                if (!descriptor)
                {
                    continue;
                }

                owner                 = descriptor->Owner;
                const WatchRoot* root = m_roots.find(owner);
                recursive             = root && root->Recursive;

                JoinPath(path, sizeof(path), descriptor->Path, raw->len ? raw->name : "");
            }

            const bool is_directory = (raw->mask & IN_ISDIR) != 0;

            if (raw->mask & (IN_DELETE_SELF | IN_MOVE_SELF))
            {
                m_batch.push(MakeEvent(path, WatchEventKind::Deleted, true));
                continue;
            }

            if (raw->mask & IN_CREATE)
            {
                if (is_directory && recursive)
                {
                    std::lock_guard<std::mutex> lock(m_watch_mutex);
                    AddDirectory(owner, path);
                    AddSubtree(owner, path);
                }
                m_batch.push(MakeEvent(path, WatchEventKind::Created, is_directory));
            }
            else if (raw->mask & (IN_MODIFY | IN_CLOSE_WRITE | IN_ATTRIB))
            {
                m_batch.push(MakeEvent(path, WatchEventKind::Modified, is_directory));
            }
            else if (raw->mask & IN_DELETE)
            {
                m_batch.push(MakeEvent(path, WatchEventKind::Deleted, is_directory));
            }
            else if (raw->mask & IN_MOVED_FROM)
            {
                PendingMove move;
                move.Cookie = raw->cookie;
                move.Event  = MakeEvent(path, WatchEventKind::Deleted, is_directory);
                m_pending_moves.push(move);
            }
            else if (raw->mask & IN_MOVED_TO)
            {
                bool paired = false;
                for (size_t i = 0; i < m_pending_moves.size(); ++i)
                {
                    if (m_pending_moves[i].Cookie != raw->cookie)
                    {
                        continue;
                    }

                    VFSWatchEvent renamed = MakeEvent(path, WatchEventKind::Renamed, is_directory);
                    Helpers::secure_strcpy(renamed.OldPath, sizeof(renamed.OldPath), m_pending_moves[i].Event.Path);
                    m_batch.push(renamed);
                    m_pending_moves.erase(i);
                    paired = true;
                    break;
                }

                if (!paired)
                {
                    m_batch.push(MakeEvent(path, WatchEventKind::Created, is_directory));
                }

                if (is_directory && recursive)
                {
                    std::lock_guard<std::mutex> lock(m_watch_mutex);
                    AddDirectory(owner, path);
                    AddSubtree(owner, path);
                }
            }
        }

        for (size_t i = 0; i < m_pending_moves.size(); ++i)
        {
            m_batch.push(m_pending_moves[i].Event);
        }
        m_pending_moves.clear();

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

    void VFSInotifyWatcher::ReadPendingEvents()
    {
        if (m_inotify_fd < 0)
        {
            return;
        }

        alignas(inotify_event) char buffer[ZKilo(64)];
        for (;;)
        {
            const ssize_t bytes = read(m_inotify_fd, buffer, sizeof(buffer));
            if (bytes <= 0)
            {
                break;
            }
            TranslateBuffer(buffer, static_cast<size_t>(bytes));
        }
    }

    void VFSInotifyWatcher::Poll(RawEventCallback cb, void* ctx)
    {
        if (!m_arena)
        {
            return;
        }

        if (!m_running.value.load(std::memory_order_acquire))
        {
            ReadPendingEvents();
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

    void VFSInotifyWatcher::ThreadMain()
    {
        pollfd fds[2];
        fds[0].fd     = m_inotify_fd;
        fds[0].events = POLLIN;
        fds[1].fd     = m_wake_pipe[0];
        fds[1].events = POLLIN;

        while (m_running.value.load(std::memory_order_acquire))
        {
            const int ready = ::poll(fds, 2, -1);
            if (ready < 0)
            {
                continue;
            }

            if (fds[1].revents & POLLIN)
            {
                char discard[64];
                while (read(m_wake_pipe[0], discard, sizeof(discard)) > 0)
                {
                }
                break;
            }

            if (fds[0].revents & POLLIN)
            {
                ReadPendingEvents();
            }
        }
    }

    void VFSInotifyWatcher::StartThread()
    {
        if (!IsValid() || m_running.value.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }
        m_thread = std::thread([this] { ThreadMain(); });
    }

    void VFSInotifyWatcher::StopThread()
    {
        if (!m_running.value.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        if (m_wake_pipe[1] >= 0)
        {
            const char            wake    = 1;
            [[maybe_unused]] auto ignored = write(m_wake_pipe[1], &wake, 1);
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
    }

    size_t VFSInotifyWatcher::DescriptorCount() const
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);
        return m_wd_to_entry.size();
    }

} // namespace ZEngine::Core::VFS
#endif // __linux__
