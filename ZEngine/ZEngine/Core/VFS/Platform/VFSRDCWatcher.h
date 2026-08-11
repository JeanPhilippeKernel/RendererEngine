#pragma once
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSPlatformWatcher.h>
#include <ZEngine/ZEngineDef.h>
#include <windows.h>
#include <mutex>
#include <thread>

namespace ZEngine::Core::VFS
{
    class VFSRDCWatcher final : public IVFSPlatformWatcher
    {
    public:
        VFSRDCWatcher();
        ~VFSRDCWatcher() override;

        VFSRDCWatcher(const VFSRDCWatcher&)            = delete;
        VFSRDCWatcher& operator=(const VFSRDCWatcher&) = delete;

        void           Initialize(Memory::ArenaAllocator* arena, size_t capacity = 64);

        WatchHandle    AddWatch(const char* native_path, bool recursive) override;
        void           RemoveWatch(WatchHandle handle) override;
        void           Poll(RawEventCallback cb, void* ctx) override;
        void           StartThread() override;
        void           StopThread() override;

        bool           IsValid() const
        {
            return m_iocp != nullptr && m_arena != nullptr;
        }

        size_t WatchCount() const;

    private:
        struct WatchEntry
        {
            OVERLAPPED  Overlapped                = {};
            HANDLE      DirHandle                 = INVALID_HANDLE_VALUE;
            WatchHandle Handle                    = INVALID_WATCH_HANDLE;
            bool        Recursive                 = false;
            char        Root[MAX_FILE_PATH_COUNT] = {};
            alignas(DWORD) BYTE Buffer[ZKilo(32)] = {};
        };

        bool                                                   ReissueRead(WatchEntry* entry);
        void                                                   ProcessNotifications(WatchEntry* entry, DWORD bytes);
        void                                                   DrainCompletions(DWORD timeout_ms);
        void                                                   ThreadMain();
        void                                                   PushEvent(const VFSWatchEvent& ev);

        static VFSWatchEvent                                   MakeEvent(const char* path, WatchEventKind kind);
        static void                                            AppendWideName(char* out, size_t out_size, const char* root, const wchar_t* name, DWORD name_bytes);
        static size_t                                          ClampedLength(const char* text);

        Memory::ArenaAllocator*                                m_arena = nullptr;
        HANDLE                                                 m_iocp  = nullptr;

        mutable std::mutex                                     m_watch_mutex;
        Containers::UnorderedHashMap<WatchHandle, WatchEntry*> m_entries;
        WatchHandle                                            m_next_handle = 1;

        std::mutex                                             m_queue_mutex;
        Containers::Array<VFSWatchEvent>                       m_queue;

        Containers::Array<VFSWatchEvent>                       m_batch;
        Containers::Array<VFSWatchEvent>                       m_drain;

        std::thread                                            m_thread;
        PaddedAtomic<bool>                                     m_running = {};
    };

} // namespace ZEngine::Core::VFS
#endif // _WIN32
