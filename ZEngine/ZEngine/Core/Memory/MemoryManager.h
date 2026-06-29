#pragma once
#ifdef _WIN32
// clang-format off
#include <windows.h>
// clang-format on
#include <sysinfoapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

#include <Core/Memory/Allocator.h>

namespace ZEngine::Core::Memory
{
    struct MemoryConfiguration
    {
        uint64_t BufferSize = ZGiga(2ull);
    };

    struct MemoryManager
    {
        void Initialize(const MemoryConfiguration& config)
        {
            unsigned long page_size = 0ul;

            // Get the system page size
            // This is platform-specific, so we need to handle it differently for Windows and Linux/macOS
            // 4KB is the default page size, but it isn't guaranteed.
            // Linux on ARM64 has a 16KB page size, macOS on ARM64 has a 16KB page size, and Windows on ARM64 has a 4KB page size.
#ifdef _WIN32
            SYSTEM_INFO sys_info;
            GetSystemInfo(&sys_info);
            page_size = sys_info.dwPageSize;
#elif defined(__linux__) || defined(__APPLE__)
            page_size = sysconf(_SC_PAGESIZE);
#endif
            MainArena.Initialize(config.BufferSize, page_size);
        }

        void Shutdown()
        {
            MainArena.Shutdown();
        }

        ArenaAllocator MainArena = {};
    };
} // namespace ZEngine::Core::Memory
