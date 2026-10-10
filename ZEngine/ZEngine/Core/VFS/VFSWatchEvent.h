#pragma once
#include <ZEngine/ZEngineDef.h>
#include <chrono>

namespace ZEngine::Core::VFS
{
    [[nodiscard]] inline uint64_t VFSWatchTimestampNowNanoseconds()
    {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
    }

    enum class WatchEventKind : uint8_t
    {
        Created  = 0,
        Modified = 1,
        Deleted  = 2,
        Renamed  = 3,
        Overflow = 4,
    };

    struct VFSWatchEvent
    {
        char           Path[MAX_FILE_PATH_COUNT]    = {};
        char           OldPath[MAX_FILE_PATH_COUNT] = {};
        WatchEventKind Kind                         = WatchEventKind::Created;
        bool           IsDirectory                  = false;
        // Captured by the platform watcher before VFSFileWatcher debounces the event.
        uint64_t       ObservedAtNanoseconds        = 0;
    };
} // namespace ZEngine::Core::VFS
