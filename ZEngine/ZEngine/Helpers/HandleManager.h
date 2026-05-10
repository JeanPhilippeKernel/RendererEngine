#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <MemoryOperations.h>
#include <ZEngineDef.h>

#define INVALID_HANDLE_INDEX -1

namespace ZEngine::Helpers
{
    template <typename T>
    struct Handle;

    template <typename T>
    class HandleManager;

    template <typename T>
    struct Handle
    {
        uint64_t Index      = UINT64_MAX;
        uint64_t Generation = 0u;

        bool     Valid() const
        {
            return Index != UINT64_MAX;
        }

        operator bool() const
        {
            return Valid();
        }
    };

    template <typename T>
    class HandleManager
    {
        union FreeHead
        {
            struct
            {
                uint32_t index;
                uint32_t tag;
            };
            uint64_t raw;
        };
        static_assert(sizeof(FreeHead) == 8, "FreeHead must be 8 bytes");

        uint32_t                                        m_count       = 0;
        PaddedAtomic<uint32_t>                          m_head        = {.value = 0};
        PaddedAtomic<uint64_t>                          m_live_count  = {.value = 0};
        PaddedAtomic<uint64_t>                          m_free_head   = {}; // FreeHead packed

        Core::Containers::Array<T>                      m_memory      = {};
        Core::Containers::Array<uint32_t>               m_next        = {}; // free-list next pointers
        Core::Containers::Array<PaddedAtomic<uint64_t>> m_generations = {};

    public:
        void Initialize(Core::Memory::ArenaAllocator* arena, uint32_t count = 0)
        {
            m_count = count;
            m_memory.init(arena, count, count);
            m_next.init(arena, count, count);
            m_generations.init(arena, count, count);

            for (uint32_t i = 0; i < count; ++i)
            {
                m_generations[i].value.store(0, std::memory_order_relaxed);
            }

            FreeHead empty{UINT32_MAX, 0};
            m_free_head.value.store(empty.raw, std::memory_order_relaxed);
            m_head.value.store(0, std::memory_order_relaxed);
            m_live_count.value.store(0, std::memory_order_relaxed);
        }

        T& operator[](const Handle<T>& handle)
        {
            ZENGINE_VALIDATE_ASSERT(IsLive(handle), "Trying to access an invalid handle")
            return *(&m_memory[handle.Index]);
        }

        Handle<T> Create()
        {
            Handle<T> handle = {};
            uint64_t  index  = TryPopFree();

            if (index == UINT64_MAX)
            {
                index = m_head.value.fetch_add(1, std::memory_order_acq_rel);
                if (index >= m_count)
                {
                    m_head.value.fetch_sub(1, std::memory_order_acq_rel);
                    return {};
                }
            }
            else
            {
                uint64_t gen = m_generations[index].value.load(std::memory_order_acquire);
                ZENGINE_VALIDATE_ASSERT((gen & 1) == 1, "Popped a slot that wasn't freed")

                m_generations[index].value.store(gen + 1, std::memory_order_release);
            }

            handle.Index      = index;
            handle.Generation = m_generations[index].value.load(std::memory_order_acquire);
            m_live_count.value.fetch_add(1, std::memory_order_acq_rel);
            return handle;
        }

        T* Access(const Handle<T>& handle)
        {
            T* ptr = nullptr;
            if (IsLive(handle))
            {
                ptr = &m_memory[handle.Index];
            }
            return ptr;
        }

        Handle<T> Add(const T& value)
        {
            Handle<T> handle = Create();

            if (handle)
            {
                T* ptr = &m_memory[handle.Index];
                *ptr   = value;
            }

            return handle;
        }

        Handle<T> Add(T&& value)
        {
            Handle<T> handle = Create();

            if (handle)
            {
                T* ptr = &m_memory[handle.Index];
                *ptr   = std::move(value);
            }

            return handle;
        }

        Handle<T> ToHandle(uint64_t index)
        {
            Handle<T> handle{};
            ZENGINE_VALIDATE_ASSERT(index != UINT64_MAX && index < m_count, "Handle Index is invalid")

            uint64_t gen = m_generations[index].value.load(std::memory_order_acquire);
            if ((gen & 1) == 0 && index < m_head.value.load(std::memory_order_acquire))
            {
                handle.Index      = index;
                handle.Generation = gen;
            }

            return handle;
        }

        void Update(Handle<T>& handle, T& data)
        {
            ZENGINE_VALIDATE_ASSERT(IsLive(handle), "Handle Index is invalid")

            T* ptr = &m_memory[handle.Index];
            *ptr   = data;
        }

        void Update(Handle<T>& handle, T&& data)
        {
            ZENGINE_VALIDATE_ASSERT(IsLive(handle), "Handle Index is invalid")

            T* ptr = &m_memory[handle.Index];
            *ptr   = std::move(data);
        }

        bool IsLive(const Handle<T>& handle) const
        {
            if (!handle || handle.Index >= m_count)
            {
                return false;
            }

            uint64_t gen = m_generations[handle.Index].value.load(std::memory_order_acquire);
            return (gen & 1) == 0 && gen == handle.Generation;
        }

        void Remove(Handle<T>& handle)
        {
            if (!IsLive(handle))
            {
                return;
            }

            uint64_t gen = m_generations[handle.Index].value.load(std::memory_order_acquire);
            ZENGINE_VALIDATE_ASSERT((gen & 1) == 0, "Trying to remove a handle that is already freed")
            m_generations[handle.Index].value.store(gen + 1, std::memory_order_release);

            PushFree(handle.Index);
            m_live_count.value.fetch_sub(1, std::memory_order_acq_rel);
            handle = Handle<T>{};
        }

        size_t Size() const
        {
            return m_live_count.value.load(std::memory_order_acquire);
        }

        uint32_t Head() const
        {
            return m_head.value.load(std::memory_order_acquire);
        }

        uint32_t Capacity() const
        {
            return m_count;
        }

        void PushFree(uint64_t index)
        {
            FreeHead expected, desired;
            expected.raw = m_free_head.value.load(std::memory_order_acquire);
            do
            {
                m_next[index] = expected.index;
                desired.index = static_cast<uint32_t>(index);
                desired.tag   = expected.tag + 1;
            } while (!m_free_head.value.compare_exchange_weak(expected.raw, desired.raw, std::memory_order_release, std::memory_order_relaxed));
        }

        uint64_t TryPopFree()
        {
            FreeHead expected, desired;
            expected.raw = m_free_head.value.load(std::memory_order_acquire);
            do
            {
                if (expected.index == UINT32_MAX)
                {
                    return UINT64_MAX; // No free slots
                }
                desired.index = m_next[expected.index];
                desired.tag   = expected.tag + 1;
            } while (!m_free_head.value.compare_exchange_weak(expected.raw, desired.raw, std::memory_order_acquire, std::memory_order_relaxed));

            return expected.index;
        }

        void Dispose() {}
    };
} // namespace ZEngine::Helpers
