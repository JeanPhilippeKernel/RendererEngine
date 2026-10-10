#pragma once
#include <ZEngine/Core/VFS/VFSWatchEvent.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    using RawEventCallback                     = void (*)(void* ctx, const VFSWatchEvent&);
    using WatchHandle                          = uint32_t;
    constexpr WatchHandle INVALID_WATCH_HANDLE = 0xFFFF'FFFFu;

    class IVFSPlatformWatcher
    {
    public:
        virtual ~IVFSPlatformWatcher()                                        = default;

        virtual WatchHandle AddWatch(const char* native_path, bool recursive) = 0;
        virtual void        RemoveWatch(WatchHandle handle)                   = 0;

        virtual void        Poll(RawEventCallback cb, void* ctx)              = 0;

        virtual void        StartThread()                                     = 0;
        virtual void        StopThread()                                      = 0;
    };
} // namespace ZEngine::Core::VFS
