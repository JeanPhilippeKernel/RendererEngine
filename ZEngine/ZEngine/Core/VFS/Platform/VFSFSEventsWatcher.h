#pragma once
#if defined(__APPLE__)
#include <CoreServices/CoreServices.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSPlatformWatcher.h>
#include <ZEngine/ZEngineDef.h>
#include <mutex>
#include <thread>

namespace ZEngine::Core::VFS
{
    class VFSFSEventsWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSFSEventsWatcher();
        ~VFSFSEventsWatcher() override;

        VFSFSEventsWatcher(const VFSFSEventsWatcher&)            = delete;
        VFSFSEventsWatcher& operator=(const VFSFSEventsWatcher&) = delete;

        void                Initialize(Memory::ArenaAllocator* arena, size_t capacity = 64);

        WatchHandle         AddWatch(const char* native_path, bool recursive) override;
        void                RemoveWatch(WatchHandle handle) override;
        void                Poll(RawEventCallback cb, void* ctx) override;
        void                StartThread() override;
        void                StopThread() override;

        bool                IsValid() const
        {
            return m_arena != nullptr;
        }

        size_t WatchCount() const;

    private:
        struct WatchEntry
        {
            char Path[MAX_FILE_PATH_COUNT] = {};
            bool Recursive                 = false;
        };

        static void                                           FSEventsCallback(ConstFSEventStreamRef stream, void* context, size_t count, void* paths, const FSEventStreamEventFlags* flags, const FSEventStreamEventId* ids);
        void                                                  RebuildStream();
        void                                                  TeardownStream();
        void                                                  PushEvent(const VFSWatchEvent& ev);

        static VFSWatchEvent                                  MakeEvent(const char* path, WatchEventKind kind, bool is_directory);
        static size_t                                         ClampedLength(const char* text);

        Memory::ArenaAllocator*                               m_arena = nullptr;

        mutable std::mutex                                    m_watch_mutex;
        Containers::UnorderedHashMap<WatchHandle, WatchEntry> m_watches;
        WatchHandle                                           m_next_handle    = 1;

        FSEventStreamRef                                      m_stream         = nullptr;
        bool                                                  m_stream_started = false;
        CFRunLoopRef                                          m_run_loop       = nullptr;
        std::thread                                           m_thread;
        PaddedAtomic<bool>                                    m_running = {};

        std::mutex                                            m_queue_mutex;
        Containers::Array<VFSWatchEvent>                      m_queue;
        Containers::Array<VFSWatchEvent>                      m_drain;
    };

} // namespace ZEngine::Core::VFS
#endif // __APPLE__
