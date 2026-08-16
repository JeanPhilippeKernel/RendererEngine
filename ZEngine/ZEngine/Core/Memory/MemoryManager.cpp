#include <ZEngine/Core/Memory/MemoryManager.h>

#ifdef _WIN32
// clang-format off
#include <windows.h>
// clang-format on
#include <sysinfoapi.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <unistd.h>
#endif

namespace ZEngine::Core::Memory
{
    void MemoryManager::Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config)
    {
        Budget = config;
        config.Validate(buffer_size);

        unsigned long page_size = 0ul;
#ifdef _WIN32
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        page_size = sys_info.dwPageSize;
#elif defined(__linux__) || defined(__APPLE__)
        page_size = sysconf(_SC_PAGESIZE);
#endif
        MainArena.Initialize(buffer_size, page_size);
    }

    void MemoryManager::CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result)
    {
        ZENGINE_VALIDATE_ASSERT(config.SizeBytes > 0, "MemoryManager::CreateBudgetedArena: SizeBytes must be > 0")
        ZENGINE_VALIDATE_ASSERT(result != nullptr, "MemoryManager::CreateBudgetedArena: out must not be null")

        MainArena.CreateSubArena(config.SizeBytes, result);

#if ZENGINE_PROFILING
        Profiling::MemoryProfiler::TrackArena(config.Name, result);
#endif
    }

    void MemoryManager::Shutdown()
    {
        MainArena.Shutdown();
    }
} // namespace ZEngine::Core::Memory
