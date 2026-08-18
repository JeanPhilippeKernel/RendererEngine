#pragma once
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <cstdint>
#include <new>

namespace ZEngine::Core::Containers
{
    // Lock-free bounded ring buffer — multiple producers, single consumer.
    // N must be a power of 2.
    //
    // Each slot carries a sequence counter that encodes its lifecycle:
    //
    //   seq == pos          slot is empty; a producer may claim it
    //   seq == pos + 1      data written; consumer may read it
    //   seq == pos + N      consumed; slot is free for position pos+N
    //
    // Producers compete via CAS on m_write to claim a unique slot index.
    // The consumer advances m_read privately — no contention with producers.
    // Re-pushing from the consumer thread is safe: the consumer is just
    // another producer from the queue's perspective.
    //
    // seq is PaddedAtomic so each slot's sequence counter sits on its own
    // cache line — producers writing to adjacent slots don't cause false sharing.
    template <typename T, uint32_t N>
    struct MPSCQueue
    {
        static_assert((N & (N - 1)) == 0, "MPSCQueue capacity must be power of 2");
        static constexpr uint32_t CAPACITY = N;
        static constexpr uint32_t MASK     = N - 1;

        struct Slot
        {
            T                      data{};
            PaddedAtomic<uint32_t> seq{};
        };

        MPSCQueue()
        {
            for (uint32_t i = 0; i < N; ++i)
            {
                new (&m_slots[i]) Slot{};
                m_slots[i].seq.value.store(i, std::memory_order_relaxed);
            }
            m_write.value.store(0, std::memory_order_relaxed);
            m_read = 0;
        }

        // Any thread. Returns false if full.
        bool push(const T& item)
        {
            uint32_t pos;
            Slot*    slot;
            for (;;)
            {
                pos          = m_write.value.load(std::memory_order_relaxed);
                slot         = &m_slots[pos & MASK];

                int32_t diff = static_cast<int32_t>(slot->seq.value.load(std::memory_order_acquire)) - static_cast<int32_t>(pos);

                if (diff == 0)
                {
                    // Slot is free — claim it.
                    if (m_write.value.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed))
                        break;
                }
                else if (diff < 0)
                {
                    return false; // queue is full
                }
                // diff > 0: another producer already claimed this slot, retry.
            }
            slot->data = item;
            slot->seq.value.store(pos + 1, std::memory_order_release); // publish
            return true;
        }

        // Consumer thread only. Returns false if empty.
        bool pop(T& out)
        {
            Slot* slot = &m_slots[m_read & MASK];
            if (slot->seq.value.load(std::memory_order_acquire) != m_read + 1)
                return false; // not published yet
            out = slot->data;
            slot->seq.value.store(m_read + N, std::memory_order_release); // free slot
            ++m_read;
            return true;
        }

        bool empty() const
        {
            const Slot* slot = &m_slots[m_read & MASK];
            return slot->seq.value.load(std::memory_order_acquire) != m_read + 1;
        }

    private:
        // Slot already carries a PaddedAtomic seq, so alignof(Slot) == CACHE_LINE_SIZE.
        // The array is correctly aligned without an explicit alignas.
        Slot                   m_slots[N];

        // m_write and m_read are on separate cache lines:
        // PaddedAtomic<uint32_t> occupies exactly one cache line, so m_read
        // starts at the next cache-line boundary — no explicit alignas needed.
        PaddedAtomic<uint32_t> m_write{};
        uint32_t               m_read{0}; // consumer-private
    };

} // namespace ZEngine::Core::Containers
