#pragma once
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <cstdint>
#include <new>

namespace ZEngine::Core::Containers
{
    // Lock-free bounded ring buffer — single producer, single consumer.
    // N must be a power of 2. Head owned by producer, tail by consumer.
    // Both are cache-line padded to prevent false sharing.
    template <typename T, uint32_t N>
    struct SPSCQueue
    {
        static_assert((N & (N - 1)) == 0, "SPSCQueue capacity must be power of 2");
        static constexpr uint32_t CAPACITY = N;
        static constexpr uint32_t MASK     = N - 1;

        SPSCQueue()
        {
            for (uint32_t i = 0; i < N; ++i)
                new (&m_buffer[i]) T{};
            m_head.value.store(0, std::memory_order_relaxed);
            m_tail.value.store(0, std::memory_order_relaxed);
        }

        // Producer thread only. Returns false if full.
        bool push(const T& item)
        {
            uint32_t head = m_head.value.load(std::memory_order_relaxed);
            uint32_t next = (head + 1) & MASK;
            if (next == m_tail.value.load(std::memory_order_acquire))
                return false;
            m_buffer[head] = item;
            m_head.value.store(next, std::memory_order_release);
            return true;
        }

        // Consumer thread only. Returns false if empty.
        bool pop(T& out)
        {
            uint32_t tail = m_tail.value.load(std::memory_order_relaxed);
            if (tail == m_head.value.load(std::memory_order_acquire))
                return false;
            out = m_buffer[tail];
            m_tail.value.store((tail + 1) & MASK, std::memory_order_release);
            return true;
        }

        bool empty() const
        {
            return m_tail.value.load(std::memory_order_acquire) == m_head.value.load(std::memory_order_acquire);
        }

    private:
        PaddedAtomic<uint32_t> m_head{};
        PaddedAtomic<uint32_t> m_tail{};
        T                      m_buffer[N];
    };

} // namespace ZEngine::Core::Containers
