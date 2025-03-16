#pragma once
#include <Allocator.h>
#include <type_traits>

// using span ?

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Container
{

    template <typename T>
    struct Array
    {
        using value_type      = T;
        using size_type       = size_t;
        using reference       = T&;
        using const_reference = const T&;
        using pointer         = T*;
        using const_pointer   = const T*;
        using iterator        = T*;
        using const_iterator  = const T*;

        Array(Memory::ArenaAllocator& allocator, size_type initial_capacity, size_type initial_size) : m_allocator(allocator), m_size(initial_size), m_capacity(0), m_data(nullptr)
        {
            reserve(initial_capacity);
        }

        const_reference operator[](size_type index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            return m_data[index];
        }

        iterator begin()
        {
            return m_data;
        }

        const_iterator begin() const
        {
            return m_data;
        }

        iterator end()
        {
            return m_data + m_size;
        }

        const_iterator end() const
        {
            return m_data + m_size;
        }

        reference front()
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            return m_data[0];
        }

        const_reference front() const
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            return m_data[0];
        }

        reference back()
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            return m_data[m_size - 1];
        }

        const_reference back() const
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            return m_data[m_size - 1];
        }

        pointer data()
        {
            return m_data;
        }

        const_pointer data() const
        {
            return m_data;
        }

        bool empty() const
        {
            return m_size == 0;
        }

        size_type size() const
        {
            return m_size;
        }

        size_type capacity() const
        {
            return m_capacity;
        }

        void clear()
        {
            for (size_type i = 0; i < m_size; ++i)
            {
                m_data[i].~T();
            }
            m_size = 0;
        }

        void push(const T& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            m_data[m_size] = value;
            ++m_size;
        }

        void push(T&& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            m_data[m_size] = std::move(value);
            ++m_size;
        }

        reference push_use(const T& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            m_data[m_size] = value;
            ++m_size;
            return back();
        }

        void pop()
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            --m_size;
            m_data[m_size].~T();
        }

        ~Array()
        {
            clear();
            m_data     = nullptr;
            m_capacity = 0;
        }

        void reserve(size_type new_capacity)
        {
            if (new_capacity <= m_capacity)
            {
                return;
            }

            size_t old_alloc_size = m_capacity * sizeof(T);
            size_t new_alloc_size = new_capacity * sizeof(T);

            m_data                = static_cast<pointer>(m_allocator.Reallocate(m_data, old_alloc_size, new_alloc_size, alignof(value_type)));
            m_capacity            = new_capacity;
        }

        Memory::ArenaAllocator& m_allocator;
        size_type               m_size;
        size_type               m_capacity;
        pointer                 m_data;
    };

    template <typename T>
    struct ArrayView
    {
        ArrayView(T* data, size_t size) : m_data(data), m_size(size) {}

        void set(T* data, size_t size)
        {
            m_data = data;
            m_size = size;
        }

        T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            return m_data[index];
        }
        const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            return m_data[index];
        }

        T*     m_data;
        size_t m_size;
    };
} // namespace ZEngine::Core::Container