#pragma once
#include <algorithm>
#include <chrono>
#include <cstdint>

namespace ZEngine::Timing
{
    // Measures wall-clock time between frames.
    // Clamps raw delta to 250 ms so debugger breakpoints or OS sleeps do not
    // inject a catastrophically large delta into the simulation accumulator.
    // Provides a rolling 8-sample smoothed delta for camera lerp and HUD display.
    struct FrameTimer
    {
        static constexpr float kMaxRawDelta      = 0.25f; // 250 ms = 4 FPS minimum
        static constexpr int   kSmoothingSamples = 8;

        FrameTimer() noexcept
        {
            m_last = std::chrono::high_resolution_clock::now();
            for (int i = 0; i < kSmoothingSamples; ++i)
                m_samples[i] = 1.0f / 60.0f;
        }

        // Call at the beginning of each frame iteration.
        void Begin() noexcept
        {
            m_frame_begin = std::chrono::high_resolution_clock::now();
        }

        // Call after Begin(). Returns the raw clamped delta since the previous End() call.
        float End() noexcept
        {
            using namespace std::chrono;
            auto  now                 = high_resolution_clock::now();
            float raw_dt              = duration<float>(now - m_last).count();
            m_last                    = now;

            raw_dt                    = std::min(raw_dt, kMaxRawDelta);

            m_samples[m_sample_index] = raw_dt;
            m_sample_index            = (m_sample_index + 1) % kSmoothingSamples;
            float sum                 = 0.f;
            for (int i = 0; i < kSmoothingSamples; ++i)
                sum += m_samples[i];
            m_smoothed_dt = sum / static_cast<float>(kSmoothingSamples);

            return raw_dt;
        }

        // Smoothed delta for UI / camera lerp — not for the simulation accumulator.
        [[nodiscard]] float SmoothedDelta() const noexcept
        {
            return m_smoothed_dt;
        }

        // Raw clamped delta from the most recent End() call.
        [[nodiscard]] float RawDelta() const noexcept
        {
            return m_samples[(m_sample_index + kSmoothingSamples - 1) % kSmoothingSamples];
        }

    private:
        std::chrono::high_resolution_clock::time_point m_last;
        std::chrono::high_resolution_clock::time_point m_frame_begin;
        float                                          m_samples[kSmoothingSamples]{};
        float                                          m_smoothed_dt{1.0f / 60.0f};
        int                                            m_sample_index{0};
    };
} // namespace ZEngine::Timing
