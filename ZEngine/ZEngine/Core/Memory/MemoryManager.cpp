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

        m_page_size = 0;
#ifdef _WIN32
        SYSTEM_INFO sys_info;
        GetSystemInfo(&sys_info);
        m_page_size = sys_info.dwPageSize;
#elif defined(__linux__) || defined(__APPLE__)
        m_page_size = sysconf(_SC_PAGESIZE);
#endif

        // A configured application consists of independently reserved named owners.
        // This avoids a mandatory 8 GiB VMA at startup on Linux where RLIMIT_AS counts
        // PROT_NONE reservations. The no-profile mode remains useful for small unit
        // tests and callers that deliberately want one general-purpose arena.
        m_uses_independent_owners = config.TotalCapacity() > 0;
        if (!m_uses_independent_owners)
            MainArena.Initialize(buffer_size, m_page_size, "MainArena");

        if (Budget.Bootstrap.SizeBytes > 0)
            CreateBudgetedArena(Budget.Bootstrap, &BootstrapArena);
    }

    void MemoryManager::CreateBudgetedArena(const SubArenaConfig& config, ArenaAllocator* result)
    {
        ZENGINE_VALIDATE_ASSERT(config.SizeBytes > 0, "MemoryManager::CreateBudgetedArena: SizeBytes must be > 0")
        ZENGINE_VALIDATE_ASSERT(result != nullptr, "MemoryManager::CreateBudgetedArena: out must not be null")

        if (m_uses_independent_owners)
        {
            result->Initialize(config.SizeBytes, m_page_size, config.Name);
            RegisterOwnedArena(result);
        }
        else
        {
            MainArena.CreateSubArena(config.SizeBytes, result, config.Name);
        }

#if ZENGINE_PROFILING
        Profiling::MemoryProfiler::TrackArena(config.Name, result);
#endif
    }

    void MemoryManager::Shutdown()
    {
        while (m_owned_arena_count > 0)
        {
            ArenaAllocator* arena               = m_owned_arenas[--m_owned_arena_count];
            m_owned_arenas[m_owned_arena_count] = nullptr;
            arena->Shutdown();
        }
        MainArena.Shutdown();
        m_uses_independent_owners = false;
        m_page_size               = 0;
    }

    void MemoryManager::RegisterOwnedArena(ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(m_owned_arena_count < MaxOwnedArenas, "MemoryManager::RegisterOwnedArena: too many independent arena owners")
        m_owned_arenas[m_owned_arena_count++] = arena;
    }
} // namespace ZEngine::Core::Memory
