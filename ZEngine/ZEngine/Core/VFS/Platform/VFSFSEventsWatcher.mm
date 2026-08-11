#include <ZEngine/Core/VFS/Platform/VFSFSEventsWatcher.h>
#if defined(__APPLE__)
#include <ZEngine/Helpers/MemoryOperations.h>
#include <sys/stat.h>

namespace ZEngine::Core::VFS
{

    static constexpr CFAbsoluteTime STREAM_LATENCY_SECONDS = 0.05;

    size_t                   VFSFSEventsWatcher::ClampedLength(const char* text)
    {
        const size_t length = Helpers::secure_strlen(text);
        return length < MAX_FILE_PATH_COUNT ? length : MAX_FILE_PATH_COUNT - 1;
    }

    VFSWatchEvent VFSFSEventsWatcher::MakeEvent(const char* path, WatchEventKind kind, bool is_directory)
    {
        VFSWatchEvent ev{};
        Helpers::secure_strncpy(ev.Path, sizeof(ev.Path), path, ClampedLength(path));
        ev.Kind        = kind;
        ev.IsDirectory = is_directory;
        return ev;
    }

    VFSFSEventsWatcher::VFSFSEventsWatcher() = default;

    VFSFSEventsWatcher::~VFSFSEventsWatcher()
    {
        StopThread();

        std::lock_guard<std::mutex> lock(m_watch_mutex);
        TeardownStream();
    }

    void VFSFSEventsWatcher::Initialize(Memory::ArenaAllocator* arena, size_t capacity)
    {
        m_arena = arena;
        m_watches.init(arena, capacity);
        m_queue.init(arena, capacity);
        m_drain.init(arena, capacity);
    }

    void VFSFSEventsWatcher::PushEvent(const VFSWatchEvent& ev)
    {
        std::lock_guard<std::mutex> lock(m_queue_mutex);
        m_queue.push(ev);
    }

    void VFSFSEventsWatcher::FSEventsCallback(ConstFSEventStreamRef, void* context, size_t count, void* paths, const FSEventStreamEventFlags* flags, const FSEventStreamEventId*)
    {
        VFSFSEventsWatcher* self       = static_cast<VFSFSEventsWatcher*>(context);
        const char**        path_array = static_cast<const char**>(paths);

        for (size_t i = 0; i < count; ++i)
        {
            const FSEventStreamEventFlags flag = flags[i];

            if (flag & (kFSEventStreamEventFlagUserDropped | kFSEventStreamEventFlagKernelDropped | kFSEventStreamEventFlagMustScanSubDirs))
            {
                VFSWatchEvent overflow{};
                overflow.Kind = WatchEventKind::Overflow;
                self->PushEvent(overflow);
                continue;
            }

            const bool     is_directory = (flag & kFSEventStreamEventFlagItemIsDir) != 0;

            struct stat    st           = {};
            WatchEventKind kind;
            if (stat(path_array[i], &st) != 0)
            {
                kind = WatchEventKind::Deleted;
            }
            else if (flag & kFSEventStreamEventFlagItemCreated)
            {
                kind = WatchEventKind::Created;
            }
            else
            {
                kind = WatchEventKind::Modified;
            }

            self->PushEvent(MakeEvent(path_array[i], kind, is_directory));
        }
    }

    void VFSFSEventsWatcher::TeardownStream()
    {
        if (!m_stream)
        {
            return;
        }

        if (m_stream_started)
        {
            FSEventStreamStop(m_stream);
            FSEventStreamInvalidate(m_stream);
            m_stream_started = false;
        }
        FSEventStreamRelease(m_stream);
        m_stream = nullptr;
    }

    void VFSFSEventsWatcher::RebuildStream()
    {
        TeardownStream();

        if (m_watches.size() == 0)
        {
            return;
        }

        CFMutableArrayRef path_list = CFArrayCreateMutable(nullptr, 0, &kCFTypeArrayCallBacks);
        for (auto it = m_watches.begin(); it != m_watches.end(); ++it)
        {
            CFStringRef path = CFStringCreateWithCString(nullptr, (*it).second.Path, kCFStringEncodingUTF8);
            CFArrayAppendValue(path_list, path);
            CFRelease(path);
        }

        FSEventStreamContext context = {0, this, nullptr, nullptr, nullptr};
        m_stream                     = FSEventStreamCreate(nullptr, &FSEventsCallback, &context, path_list, kFSEventStreamEventIdSinceNow, STREAM_LATENCY_SECONDS,
                                                           kFSEventStreamCreateFlagFileEvents | kFSEventStreamCreateFlagNoDefer);
        CFRelease(path_list);

        if (m_stream && m_run_loop)
        {
            FSEventStreamScheduleWithRunLoop(m_stream, m_run_loop, kCFRunLoopDefaultMode);
            FSEventStreamStart(m_stream);
            m_stream_started = true;
        }
    }

    WatchHandle VFSFSEventsWatcher::AddWatch(const char* native_path, bool recursive)
    {
        if (!IsValid() || !native_path || native_path[0] == '\0')
        {
            return INVALID_WATCH_HANDLE;
        }

        std::lock_guard<std::mutex> lock(m_watch_mutex);

        const WatchHandle           handle = m_next_handle++;
        WatchEntry                  entry;
        Helpers::secure_strncpy(entry.Path, sizeof(entry.Path), native_path, ClampedLength(native_path));
        entry.Recursive   = recursive;

        m_watches[handle] = entry;

        if (m_run_loop)
        {
            CFRunLoopRef run_loop = m_run_loop;
            CFRunLoopPerformBlock(run_loop, kCFRunLoopDefaultMode, ^{
                std::lock_guard<std::mutex> rebuild_lock(m_watch_mutex);
                RebuildStream();
            });
            CFRunLoopWakeUp(run_loop);
        }
        return handle;
    }

    void VFSFSEventsWatcher::RemoveWatch(WatchHandle handle)
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);

        if (!m_watches.find(handle))
        {
            return;
        }

        m_watches.remove(handle);

        if (m_run_loop)
        {
            CFRunLoopRef run_loop = m_run_loop;
            CFRunLoopPerformBlock(run_loop, kCFRunLoopDefaultMode, ^{
                std::lock_guard<std::mutex> rebuild_lock(m_watch_mutex);
                RebuildStream();
            });
            CFRunLoopWakeUp(run_loop);
        }
    }

    void VFSFSEventsWatcher::Poll(RawEventCallback cb, void* ctx)
    {
        if (!m_arena)
        {
            return;
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

    void VFSFSEventsWatcher::StartThread()
    {
        if (!IsValid() || m_running.value.exchange(true, std::memory_order_acq_rel))
        {
            return;
        }

        m_thread = std::thread([this] {
            m_run_loop = CFRunLoopGetCurrent();
            {
                std::lock_guard<std::mutex> lock(m_watch_mutex);
                RebuildStream();
            }
            CFRunLoopRun();
        });
    }

    void VFSFSEventsWatcher::StopThread()
    {
        if (!m_running.value.exchange(false, std::memory_order_acq_rel))
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(m_watch_mutex);
            if (m_run_loop)
            {
                CFRunLoopStop(m_run_loop);
            }
        }
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        std::lock_guard<std::mutex> lock(m_watch_mutex);
        m_run_loop = nullptr;
    }

    size_t VFSFSEventsWatcher::WatchCount() const
    {
        std::lock_guard<std::mutex> lock(m_watch_mutex);
        return m_watches.size();
    }

} // namespace ZEngine::Core::VFS
#endif // __APPLE__
