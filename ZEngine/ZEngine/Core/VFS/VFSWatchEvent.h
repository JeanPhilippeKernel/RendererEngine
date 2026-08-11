#pragma once
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::Core::VFS
{
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
    };
} // namespace ZEngine::Core::VFS
