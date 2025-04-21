#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <MemoryOperations.h>
#include <ZEngineDef.h>
#include <shared_mutex>

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
        uint32_t Index = UINT32_MAX;

        bool     Valid() const
        {
            return Index != UINT32_MAX;
        }

        operator bool() const
        {
            return this->Valid();
        }
    };

    template <typename T>
    class HandleManager
    {
        uint32_t                         m_count           = 0;
        uint32_t                         m_head            = 0;
        uint32_t                         m_free_slot_index = 0;
        Core::Containers::Array<T>       m_memory          = {};
        Core::Containers::Array<uint8_t> m_free_slot       = {};

        mutable std::shared_mutex        m_mutex;

    public:
        void Initialize(Core::Memory::ArenaAllocator* arena, uint32_t count = 0)
        {
            m_memory.init(arena, count, count);
            m_free_slot.init(arena, count, count);
            m_count           = count;
            m_head            = 0;
            m_free_slot_index = 0;
        }

        T& operator[](const Handle<T>& handle)
        {
            return *Access(handle);
        }

        Handle<T> Create()
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle = {};

            if (m_free_slot_index > 0 && m_free_slot_index >= m_head)
            {
                m_head            = 0;
                m_free_slot_index = 0;
            }

            if (m_free_slot_index > 0)
            {
                handle.Index = m_free_slot[--m_free_slot_index];
            }
            else if (m_head < m_count)
            {
                handle.Index = m_head++;
            }
            return handle;
        }

        T* Access(const Handle<T>& handle)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            T*                                  ptr = nullptr;

            if (handle && (handle.Index < m_count))
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

        Handle<T> ToHandle(uint32_t index)
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            Handle<T>                           handle{};
            ZENGINE_VALIDATE_ASSERT(index != UINT32_MAX && index < m_count, "Handle Index is invalid")

            if (!(index >= m_head))
            {
                handle.Index = index;
            }

            return handle;
        }

        void Update(Handle<T>& handle, T& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            ZENGINE_VALIDATE_ASSERT((handle) && handle.Index < m_count, "Handle Index is invalid")

            T* ptr = &m_memory[handle.Index];
            *ptr   = data;
        }

        void Update(Handle<T>& handle, T&& data)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            ZENGINE_VALIDATE_ASSERT((handle) && handle.Index < m_count, "Handle Index is invalid")

            T* ptr = &m_memory[handle.Index];
            *ptr   = std::move(data);
        }

        bool CanRemove()
        {
            return !(m_free_slot_index >= m_head);
        }

        void Remove(Handle<T>& handle)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);
            if (!handle || !(handle.Index < m_count))
            {
                return;
            }

            if (!CanRemove())
            {
                return;
            }

            m_free_slot[m_free_slot_index++] = handle.Index;
            handle                           = Handle<T>{};
        }

        size_t Size() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_count;
        }

        uint32_t Head() const
        {
            std::shared_lock<std::shared_mutex> lock(m_mutex);
            return m_head;
        }

        void Dispose() {}
    };
} // namespace ZEngine::Helpers
