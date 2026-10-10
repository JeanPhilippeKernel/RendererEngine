#include <ZEngine/Core/Memory/MemoryManager.h>
#include <cstdio>
#include <limits>

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
    namespace
    {
        struct BudgetSlot
        {
            cstring  FieldName;
            cstring  OwnerName;
            uint64_t SizeBytes;
        };

        constexpr size_t BudgetSlotCount = 18;

        void             CollectBudgetSlots(const MemoryBudgetConfig& config, BudgetSlot (&slots)[BudgetSlotCount])
        {
            slots[0]  = {"Bootstrap", config.Bootstrap.Name, config.Bootstrap.SizeBytes};
            slots[1]  = {"AudioEngine", config.AudioEngine.Name, config.AudioEngine.SizeBytes};
            slots[2]  = {"AnimationManager", config.AnimationManager.Name, config.AnimationManager.SizeBytes};
            slots[3]  = {"AssetManager", config.AssetManager.Name, config.AssetManager.SizeBytes};
            slots[4]  = {"ECSScene", config.ECSScene.Name, config.ECSScene.SizeBytes};
            slots[5]  = {"Logging", config.Logging.Name, config.Logging.SizeBytes};
            slots[6]  = {"VirtualFS", config.VirtualFS.Name, config.VirtualFS.SizeBytes};
            slots[7]  = {"VulkanDevice", config.VulkanDevice.Name, config.VulkanDevice.SizeBytes};
            slots[8]  = {"ImportPipeline", config.ImportPipeline.Name, config.ImportPipeline.SizeBytes};
            slots[9]  = {"UIContext", config.UIContext.Name, config.UIContext.SizeBytes};
            slots[10] = {"EditorContext", config.EditorContext.Name, config.EditorContext.SizeBytes};
            slots[11] = {"EditorSceneLoadA", config.EditorSceneLoadA.Name, config.EditorSceneLoadA.SizeBytes};
            slots[12] = {"EditorSceneLoadB", config.EditorSceneLoadB.Name, config.EditorSceneLoadB.SizeBytes};
            slots[13] = {"Swapchain", config.Swapchain.Name, config.Swapchain.SizeBytes};
            slots[14] = {"ShaderCache", config.ShaderCache.Name, config.ShaderCache.SizeBytes};
            slots[15] = {"Serializer", config.Serializer.Name, config.Serializer.SizeBytes};
            slots[16] = {"Network", config.Network.Name, config.Network.SizeBytes};
            slots[17] = {"Input", config.Input.Name, config.Input.SizeBytes};
        }

        uint64_t SumBudgetSlots(const BudgetSlot (&slots)[BudgetSlotCount])
        {
            uint64_t total = 0;
            for (const BudgetSlot& slot : slots)
            {
                if (slot.SizeBytes > std::numeric_limits<uint64_t>::max() - total)
                    return std::numeric_limits<uint64_t>::max();
                total += slot.SizeBytes;
            }
            return total;
        }
    } // namespace

    uint64_t MemoryBudgetConfig::TotalCapacity() const
    {
        BudgetSlot slots[BudgetSlotCount] = {};
        CollectBudgetSlots(*this, slots);
        return SumBudgetSlots(slots);
    }

    bool MemoryBudgetConfig::Validate(uint64_t total_available_bytes) const
    {
        BudgetSlot slots[BudgetSlotCount] = {};
        CollectBudgetSlots(*this, slots);

        const uint64_t capacity = SumBudgetSlots(slots);
        if (capacity <= total_available_bytes)
            return true;

        // MemoryManager is initialized before the logger. Keep this diagnostic
        // allocation-free and send it directly to stderr as well as the assertion
        // path, so startup crashes always identify the owner that needs resizing.
        char       diagnostic[2048] = {};
        size_t     written          = 0;
        const auto append           = [&diagnostic, &written](const char* format, auto... args) {
            if (written >= sizeof(diagnostic) - 1)
                return;

            const int result = std::snprintf(diagnostic + written, sizeof(diagnostic) - written, format, args...);
            if (result <= 0)
                return;

            const size_t appended   = static_cast<size_t>(result);
            const size_t remaining  = sizeof(diagnostic) - written;
            written                += appended < remaining ? appended : remaining - 1;
        };

        append("MemoryBudgetConfig::Validate: configured capacity %llu MiB (%llu bytes) exceeds limit %llu MiB (%llu bytes) by %llu MiB (%llu bytes)\\nConfigured owners:\\n", capacity / ZMega(1ULL), capacity, total_available_bytes / ZMega(1ULL), total_available_bytes, (capacity - total_available_bytes) / ZMega(1ULL), capacity - total_available_bytes);
        for (const BudgetSlot& slot : slots)
        {
            append("  %s [%s]: %llu MiB (%llu bytes)\\n", slot.FieldName, slot.OwnerName ? slot.OwnerName : "unnamed", slot.SizeBytes / ZMega(1ULL), slot.SizeBytes);
        }

        std::fputs(diagnostic, stderr);
        std::fflush(stderr);

#if defined(NDEBUG) || defined(ZENGINE_RELWITHDEBINFO) || defined(ZENGINE_RELEASE)
        ::ZEngine::CrashHandlers::CrashHandler::OnAssertionFailure(__FILE__, __LINE__, diagnostic);
#else
        // Keep the runtime-generated text out of the assertion macro: fmt requires
        // a compile-time format string in debug builds.
        ZENGINE_CORE_CRITICAL("{}", diagnostic)
        ZENGINE_DEBUG_BREAK()
#endif
        return false;
    }

    void MemoryManager::Initialize(uint64_t buffer_size, const MemoryBudgetConfig& config)
    {
        Budget = config;
        (void) config.Validate(buffer_size);

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
