#pragma once
#include <atomic>
#include <cstdint>
#include <limits>

namespace ZEngine::Rendering::Scenes
{
    /// Observability only: accepted graphics submission is not presentation or
    /// GPU completion. One render writer; bounded, coherent cross-thread reads.
    class SceneSubmissionProgress final
    {
    public:
        struct Snapshot
        {
            uint64_t Generation       = 0;
            uint64_t Count            = 0;
            uint64_t FirstTimestampNs = 0;
            uint32_t DrawCount        = 0;
        };

        /// Called only after Vulkan accepted a complete scene frame. A rejected
        /// graph, aborted acquire or failed submission must not advance this.
        void Record(uint64_t generation, uint64_t timestamp_ns, uint32_t draw_count = 0) noexcept
        {
            const auto previous = m_generation.load();
            if (!generation || generation < previous)
            {
                return;
            }
            // Sequentially consistent atomic fields make both sides of the
            // sequence check ordered; non-atomic seqlock payloads would race.
            m_sequence.fetch_add(1);
            m_draw_count.store(draw_count);
            if (generation != previous)
            {
                m_generation.store(generation);
                m_count.store(1);
                m_first_timestamp.store(timestamp_ns);
            }
            else if (m_count.load() != std::numeric_limits<uint64_t>::max())
            {
                m_count.fetch_add(1);
            }
            m_sequence.fetch_add(1);
        }

        /// Preserves output under contention; callers retry on a later update.
        [[nodiscard]] bool TryRead(Snapshot* output) const noexcept
        {
            if (!output)
            {
                return false;
            }
            for (uint32_t attempt = 0; attempt < 8; ++attempt)
            {
                const auto before = m_sequence.load();
                if (before & 1)
                {
                    continue;
                }
                const Snapshot snapshot = {m_generation.load(), m_count.load(), m_first_timestamp.load(), m_draw_count.load()};
                if (before == m_sequence.load())
                {
                    *output = snapshot;
                    return true;
                }
            }
            return false;
        }

    private:
        std::atomic<uint64_t> m_sequence        = 0;
        std::atomic<uint64_t> m_generation      = 0;
        std::atomic<uint64_t> m_count           = 0;
        std::atomic<uint64_t> m_first_timestamp = 0;
        std::atomic<uint32_t> m_draw_count      = 0;
    };
} // namespace ZEngine::Rendering::Scenes
