#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/Profiling/MemoryProfiler.h>
#include <ZEngine/Profiling/Profiling.h>
#include <chrono>
#include <cstdio>

namespace ZEngine::Profiling
{
    // clang-format off
    Core::Containers::Array<MemoryProfiler::TrackedArena> MemoryProfiler::s_arenas = {};
    // clang-format on

    void MemoryProfiler::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "MemoryProfiler::Initialize: arena must not be null")
        s_arenas.init(arena, 16);
    }

    void MemoryProfiler::TrackArena(cstring name, Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(name != nullptr, "MemoryProfiler::TrackArena: name must not be null")
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "MemoryProfiler::TrackArena: arena must not be null")
        if (s_arenas.capacity() == 0)
            return; // Initialize() not yet called — skip (e.g. unit-test arenas)
        s_arenas.push({name, arena});
    }

    void MemoryProfiler::Update()
    {
        using namespace std::chrono;
        int64_t now_ns = static_cast<int64_t>(steady_clock::now().time_since_epoch().count());

        for (auto& tracked : s_arenas)
        {
            uint64_t current = tracked.Arena->m_current_offset;
            uint64_t total   = tracked.Arena->m_total_size;

            if (current > tracked.PeakOffset)
            {
                tracked.PeakOffset = current;
            }

            char name_buf[128] = {};
            snprintf(name_buf, sizeof(name_buf), "Arena/%s", tracked.Name);
            ZENGINE_PROFILE_VALUE(name_buf, static_cast<float>(current));

            if (total > 0 && current >= total)
            {
                ZENGINE_VALIDATE_ASSERT(false, "MemoryProfiler: arena is full — increase budget")
            }
            else if (total > 0 && current > static_cast<uint64_t>(0.8f * static_cast<float>(total)))
            {
                bool cooldown_expired = !tracked.HasWarned || (now_ns - tracked.LastWarningTimeNs) > 60'000'000'000LL;
                if (cooldown_expired)
                {
                    ZENGINE_CORE_WARN("Arena '{}': watermark at {:.0f}% ({} / {} bytes)", tracked.Name, 100.0f * static_cast<float>(current) / static_cast<float>(total), current, total);
                    tracked.LastWarningTimeNs = now_ns;
                    tracked.HasWarned         = true;
                }
            }
        }
    }

    void MemoryProfiler::GetStats(Core::Containers::Array<ArenaStats>& out)
    {
        out.clear();
        for (auto& tracked : s_arenas)
        {
            out.push({
                tracked.Name,
                tracked.Arena->m_current_offset,
                tracked.PeakOffset,
                tracked.Arena->m_total_size,
            });
        }
    }

    void MemoryProfiler::RecordAlloc(const void* /*ptr*/, uint64_t /*size*/) {}

    void MemoryProfiler::RecordFree(const void* /*ptr*/) {}

    void MemoryProfiler::ResetPeaks()
    {
        for (auto& tracked : s_arenas)
        {
            tracked.PeakOffset = 0;
        }
    }

} // namespace ZEngine::Profiling
