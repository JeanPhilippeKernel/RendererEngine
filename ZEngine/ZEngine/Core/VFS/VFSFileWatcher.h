#pragma once
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSPlatformWatcher.h>
#include <ZEngine/Core/VFS/VFSWatchEvent.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <chrono>
#include <functional>
#include <mutex>

namespace ZEngine::Core::VFS
{
    using WatchCallback = std::function<void(const VFSWatchEvent&)>;

    struct VFSWatchPathKey
    {
        char Data[MAX_FILE_PATH_COUNT] = {};

        VFSWatchPathKey()              = default;

        explicit VFSWatchPathKey(const char* path)
        {
            Helpers::secure_memset(Data, 0, sizeof(Data), sizeof(Data));

            size_t length = Helpers::secure_strlen(path);
            if (length >= MAX_FILE_PATH_COUNT)
            {
                length = MAX_FILE_PATH_COUNT - 1;
            }
            if (length > 0)
            {
                Helpers::secure_strncpy(Data, sizeof(Data), path, length);
            }
        }

        bool operator==(const VFSWatchPathKey& other) const
        {
            return Helpers::secure_memcmp(Data, sizeof(Data), other.Data, sizeof(other.Data), sizeof(Data)) == 0;
        }
    };

    struct DebounceEntry
    {
        VFSWatchEvent                         Event    = {};
        std::chrono::steady_clock::time_point LastSeen = {};
        WatchHandle                           Owner    = INVALID_WATCH_HANDLE;
    };

    struct VFSFileWatcher
    {
        explicit VFSFileWatcher(IVFSPlatformWatcher* platform, std::chrono::milliseconds debounce_window = std::chrono::milliseconds{80});
        ~VFSFileWatcher();

        VFSFileWatcher(const VFSFileWatcher&)            = delete;
        VFSFileWatcher& operator=(const VFSFileWatcher&) = delete;

        void            Initialize(Memory::ArenaAllocator* arena, size_t pending_capacity = 64);

        WatchHandle     Watch(const char* native_path, bool recursive, WatchCallback cb);
        void            Unwatch(WatchHandle handle);

        void            Tick();

        size_t          PendingCount() const;

    private:
        struct WatchEntry
        {
            char          Root[MAX_FILE_PATH_COUNT] = {};
            size_t        RootLength                = 0;
            WatchCallback Callback                  = {};
        };

        void                                                         OnRawEvent(const VFSWatchEvent& ev);
        void                                                         EmitReady(std::chrono::steady_clock::time_point now);
        WatchHandle                                                  ResolveOwner(const char* native_path) const;

        IVFSPlatformWatcher*                                         m_platform = nullptr;
        std::chrono::milliseconds                                    m_window   = std::chrono::milliseconds{80};
        Memory::ArenaAllocator*                                      m_arena    = nullptr;
        Memory::ArenaAllocator                                       m_local_arena;

        mutable std::mutex                                           m_mutex;
        Containers::UnorderedHashMap<VFSWatchPathKey, DebounceEntry> m_pending;
        Containers::UnorderedHashMap<WatchHandle, WatchEntry>        m_callbacks;
    };

} // namespace ZEngine::Core::VFS
