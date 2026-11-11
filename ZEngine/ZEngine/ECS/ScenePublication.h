#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstdint>
#include <limits>
#include <thread>

namespace ZEngine::ECS
{
    /// Main-thread activation authority. The host switches prepared bindings
    /// between TryBegin and Publish without callbacks, allocation or failure.
    /// Keep the transition alive through CPU retirement to deny reentrant loads.
    /// This is not an asynchronous load/result coordinator or a GPU fence.
    class ScenePublication final
    {
    public:
        class Transition final
        {
        public:
            Transition() = default;
            ~Transition()
            {
                if (m_owner)
                {
                    ZENGINE_VALIDATE_ASSERT(m_owner->m_thread == std::this_thread::get_id(), "Scene transition left its owner thread")
                    m_owner->m_busy = false;
                }
            }
            Transition(const Transition&)            = delete;
            Transition& operator=(const Transition&) = delete;

            /// No-fail commit point; exactly once after all prepared views switch.
            void        Publish() noexcept
            {
                ZENGINE_VALIDATE_ASSERT(m_owner && m_owner->m_thread == std::this_thread::get_id() && !m_published, "Invalid scene publication transition")
                ++m_owner->m_generation;
                m_published = true;
            }

        private:
            friend class ScenePublication;
            ScenePublication* m_owner     = nullptr;
            bool              m_published = false;
        };

        ScenePublication()                                    = default;
        ScenePublication(const ScenePublication&)             = delete;
        ScenePublication&  operator=(const ScenePublication&) = delete;

        [[nodiscard]] bool Idle() const noexcept
        {
            return m_thread == std::this_thread::get_id() && !m_busy;
        }
        /// Main-thread reads only; generation is not a cross-thread publication primitive.
        [[nodiscard]] uint64_t Generation() const noexcept
        {
            return m_generation;
        }
        [[nodiscard]] bool TryBegin(uint64_t expected_generation, Transition* out) noexcept
        {
            if (!Idle() || !out || out->m_owner || expected_generation != m_generation || m_generation == std::numeric_limits<uint64_t>::max())
            {
                return false;
            }
            m_busy       = true;
            out->m_owner = this;
            return true;
        }

    private:
        const std::thread::id m_thread     = std::this_thread::get_id();
        uint64_t              m_generation = 1;
        bool                  m_busy       = false;
    };
} // namespace ZEngine::ECS
