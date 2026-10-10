#pragma once
#include <ZEngine/ZEngineDef.h>
#include <chrono>
#include <cstdint>
#include <thread>

namespace ZEngine::Timing
{
    // Limits presentation rate when vsync is disabled.
    // Sleeps the remainder of the frame budget then spin-waits the last 500 µs
    // for sub-millisecond accuracy.
    struct FrameRateCap
    {
        static constexpr int kDefaultMaxFps = 300;

        explicit FrameRateCap(int max_fps = kDefaultMaxFps) noexcept
        {
            SetMaxFps(max_fps);
        }

        void SetMaxFps(int max_fps) noexcept
        {
            ZENGINE_VALIDATE_ASSERT(max_fps > 0, "FrameRateCap::SetMaxFps: max_fps must be positive")
            m_frame_budget_ns = 1'000'000'000LL / max_fps;
        }

        void MarkFrameStart() noexcept
        {
            m_frame_start = std::chrono::high_resolution_clock::now();
        }

        // Call at the end of the frame. Sleeps/spins to hold the cap.
        // No-op if the frame already exceeded its budget.
        void WaitForFrameBudget() noexcept
        {
            using namespace std::chrono;
            constexpr int64_t kSpinThresholdNs = 500'000LL; // spin last 0.5 ms

            auto              deadline         = m_frame_start + nanoseconds(m_frame_budget_ns);
            auto              now              = high_resolution_clock::now();
            int64_t           remaining_ns     = duration_cast<nanoseconds>(deadline - now).count();

            if (remaining_ns <= 0)
                return;

            if (remaining_ns > kSpinThresholdNs)
                std::this_thread::sleep_for(nanoseconds(remaining_ns - kSpinThresholdNs));

            while (high_resolution_clock::now() < deadline)
                std::this_thread::yield();
        }

    private:
        std::chrono::high_resolution_clock::time_point m_frame_start;
        int64_t                                        m_frame_budget_ns{1'000'000'000LL / kDefaultMaxFps};
    };
} // namespace ZEngine::Timing
