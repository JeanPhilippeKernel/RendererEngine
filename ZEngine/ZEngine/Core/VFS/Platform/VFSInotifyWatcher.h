#pragma once
#if defined(__linux__)
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSPlatformWatcher.h>
#include <ZEngine/ZEngineDef.h>
#include <mutex>
#include <thread>

namespace ZEngine::Core::VFS
{
    class VFSInotifyWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSInotifyWatcher();
        ~VFSInotifyWatcher() override;

        VFSInotifyWatcher(const VFSInotifyWatcher&)            = delete;
        VFSInotifyWatcher& operator=(const VFSInotifyWatcher&) = delete;

        void               Initialize(Memory::ArenaAllocator* arena, size_t capacity = 64);

        WatchHandle        AddWatch(const char* native_path, bool recursive) override;
        void               RemoveWatch(WatchHandle handle) override;
        void               Poll(RawEventCallback cb, void* ctx) override;
        void               StartThread() override;
        void               StopThread() override;

        bool               IsValid() const
        {
            return m_inotify_fd >= 0 && m_arena != nullptr;
        }

        size_t DescriptorCount() const;

    private:
        struct WatchDescriptor
        {
            char        Path[MAX_FILE_PATH_COUNT] = {};
            WatchHandle Owner                     = INVALID_WATCH_HANDLE;
        };

        struct WatchRoot
        {
            char                   Root[MAX_FILE_PATH_COUNT] = {};
            bool                   Recursive                 = false;
            Containers::Array<int> Descriptors               = {};
        };

        struct PendingMove
        {
            uint32_t      Cookie = 0;
            VFSWatchEvent Event  = {};
        };

        int                                                  AddDirectory(WatchHandle handle, const char* directory);
        void                                                 AddSubtree(WatchHandle handle, const char* directory);
        void                                                 DropDescriptor(int wd);

        void                                                 ReadPendingEvents();
        void                                                 TranslateBuffer(const char* buffer, size_t length);
        void                                                 ThreadMain();
        void                                                 PushEvent(const VFSWatchEvent& ev);

        static VFSWatchEvent                                 MakeEvent(const char* path, WatchEventKind kind, bool is_directory);
        static void                                          JoinPath(char* out, size_t out_size, const char* directory, const char* name);
        static size_t                                        ClampedLength(const char* text);

        Memory::ArenaAllocator*                              m_arena        = nullptr;

        int                                                  m_inotify_fd   = -1;
        int                                                  m_wake_pipe[2] = {-1, -1};

        mutable std::mutex                                   m_watch_mutex;
        Containers::UnorderedHashMap<int, WatchDescriptor>   m_wd_to_entry;
        Containers::UnorderedHashMap<WatchHandle, WatchRoot> m_roots;
        WatchHandle                                          m_next_handle = 1;

        std::mutex                                           m_queue_mutex;
        Containers::Array<VFSWatchEvent>                     m_queue;
        Containers::Array<VFSWatchEvent>                     m_batch;
        Containers::Array<PendingMove>                       m_pending_moves;
        Containers::Array<VFSWatchEvent>                     m_drain;

        std::thread                                          m_thread;
        PaddedAtomic<bool>                                   m_running = {};
    };

} // namespace ZEngine::Core::VFS
#endif // __linux__
