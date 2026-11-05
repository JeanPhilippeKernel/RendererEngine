#include <ZEngine/Core/Memory/NativePages.h>
#include <limits>
#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__) || defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace ZEngine::Core::Memory
{
    size_t NativePageSize() noexcept
    {
        size_t page = 0;
#if defined(_WIN32)
        SYSTEM_INFO info = {};
        GetSystemInfo(&info);
        page = info.dwPageSize;
#elif defined(__APPLE__) || defined(__linux__)
        const auto native = sysconf(_SC_PAGESIZE);
        if (native > 0)
            page = static_cast<size_t>(native);
#endif
        return page && !(page & (page - 1)) ? page : 0;
    }

    size_t NativePageFootprint(size_t bytes, size_t page_size) noexcept
    {
        if (!bytes || !page_size || (page_size & (page_size - 1)) || bytes > std::numeric_limits<size_t>::max() - (page_size - 1))
            return 0;
        return (bytes + page_size - 1) & ~(page_size - 1);
    }

    void* AllocateNativePages(size_t page_bytes) noexcept
    {
        const auto page = NativePageSize();
        if (!page || !page_bytes || page_bytes % page)
            return nullptr;
#if defined(_WIN32)
        return VirtualAlloc(nullptr, page_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#elif defined(__APPLE__) || defined(__linux__)
        void* mapping = mmap(nullptr, page_bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        return mapping == MAP_FAILED ? nullptr : mapping;
#else
        return nullptr;
#endif
    }

    bool FreeNativePages(void* mapping, size_t page_bytes) noexcept
    {
        const auto page = NativePageSize();
        if (!page || !mapping || !page_bytes || page_bytes % page || reinterpret_cast<uintptr_t>(mapping) % page)
            return false;
#if defined(_WIN32)
        return VirtualFree(mapping, 0, MEM_RELEASE) != 0;
#elif defined(__APPLE__) || defined(__linux__)
        return munmap(mapping, page_bytes) == 0;
#else
        return false;
#endif
    }
} // namespace ZEngine::Core::Memory
