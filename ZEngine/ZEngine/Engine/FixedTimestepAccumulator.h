#pragma once
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>
#include <chrono>
#include <cstdint>

namespace ZEngine::Timing
{
    struct FixedTimestepAccumulatorConfig
    {
        float FixedDt          = 1.0f / 60.0f; // 16.67 ms
        int   MaxStepsPerFrame = 5;            // spiral-of-death guard
    };

    // Drives fixed-timestep simulation using the "fix your timestep" pattern.
    // Feed raw frame delta with Accumulate(), then loop on ShouldStep()/ConsumeStep().
    // Alpha() returns the interpolation factor for the renderer after the loop exits.
    struct FixedTimestepAccumulator
    {
        explicit FixedTimestepAccumulator(const FixedTimestepAccumulatorConfig& config = {}) noexcept : m_config(config) {}

        void Accumulate(float raw_dt) noexcept
        {
            const float max_dt  = m_config.FixedDt * static_cast<float>(m_config.MaxStepsPerFrame);
            m_accumulator      += raw_dt;
            if (m_accumulator > max_dt)
            {
                ZENGINE_CORE_WARN("FixedTimestepAccumulator: spiral-of-death guard triggered")
                m_accumulator = max_dt;
            }
        }

        [[nodiscard]] bool ShouldStep() const noexcept
        {
            return m_accumulator >= m_config.FixedDt;
        }

        void ConsumeStep() noexcept
        {
            ZENGINE_VALIDATE_ASSERT(ShouldStep(), "ConsumeStep called when no step is pending")
            m_accumulator -= m_config.FixedDt;
            ++m_step_count;
        }

        // Interpolation factor in [0, 1). Pass to renderer after the step loop.
        [[nodiscard]] float Alpha() const noexcept
        {
            return m_accumulator / m_config.FixedDt;
        }

        [[nodiscard]] float FixedDt() const noexcept
        {
            return m_config.FixedDt;
        }
        [[nodiscard]] uint64_t StepCount() const noexcept
        {
            return m_step_count;
        }

        // Reset after scene load or unpause to avoid a simulation burst.
        void Reset() noexcept
        {
            m_accumulator = 0.f;
        }

    private:
        FixedTimestepAccumulatorConfig m_config;
        float                          m_accumulator{0.f};
        uint64_t                       m_step_count{0};
    };
} // namespace ZEngine::Timing
