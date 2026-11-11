#pragma once
#include <cstdint>

namespace ZEngine::Profiling
{
    struct ThreadCpuTime
    {
        uint64_t Nanoseconds = 0;
        bool     Valid       = false;
    };

    // Calling-thread CPU time, not process CPU time. Unsupported/failed clocks
    // return an invalid sample rather than presenting an invented zero as data.
    ThreadCpuTime ReadThreadCpuTime() noexcept;

    struct ThreadExecutionTimes
    {
        uint64_t                    CpuNanoseconds    = 0;
        uint64_t                    NonCpuNanoseconds = 0;
        bool                        Valid             = false;

        // CPU reads must be nested inside the wall interval on the same thread.
        // Non-CPU time includes blocking, descheduling and clock-read overhead;
        // it is not by itself
        // proof of OS preemption or of a loader-caused scheduling delay.
        static ThreadExecutionTimes Between(ThreadCpuTime begin, ThreadCpuTime end, uint64_t wall_nanoseconds) noexcept
        {
            if (!begin.Valid || !end.Valid || end.Nanoseconds < begin.Nanoseconds)
            {
                return {};
            }
            const uint64_t cpu_nanoseconds = end.Nanoseconds - begin.Nanoseconds;
            if (cpu_nanoseconds > wall_nanoseconds)
            {
                return {};
            }
            return {cpu_nanoseconds, wall_nanoseconds - cpu_nanoseconds, true};
        }
    };
} // namespace ZEngine::Profiling
