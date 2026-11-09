#include <ZEngine/Profiling/ThreadCpuTime.h>
#include <ctime>

namespace ZEngine::Profiling
{
    ThreadCpuTime ReadThreadCpuTime() noexcept
    {
#if defined(CLOCK_THREAD_CPUTIME_ID)
        timespec value = {};
        if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) == 0 && value.tv_sec >= 0 && value.tv_nsec >= 0 && value.tv_nsec < 1'000'000'000)
        {
            return {static_cast<uint64_t>(value.tv_sec) * 1'000'000'000ULL + static_cast<uint64_t>(value.tv_nsec), true};
        }
#endif
        // In particular, do not substitute coarse Windows scheduler-accounting
        // ticks or process-wide CPU time for a sub-millisecond thread clock.
        return {};
    }
} // namespace ZEngine::Profiling
