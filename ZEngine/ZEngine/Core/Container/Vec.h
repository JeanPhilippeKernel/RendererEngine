#pragma once
#include <Allocator.h>
#include <Logging/LoggerDefinition.h>
#include <type_traits>

// using span ?

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Container
{

    template <typename T>
    class Vec
    {
    public:
        using value_type      = T;
        using size_type       = size_t;
        using difference_type = ptrdiff_t;
        using reference       = T&;
        using const_reference = const T&;
        using pointer         = T*;
        using const_pointer   = const T*;
        using iterator        = T*;
        using const_iterator  = const T*;

        Vec(Memory::ArenaAllocator& allocator) : m_allocator(allocator), m_size(0), m_capacity(0), m_data(nullptr) {}

        Vec(Memory::ArenaAllocator& allocator, size_type initial_capacity) : m_allocator(allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            reserve(initial_capacity);
        }

        Vec(Memory::ArenaAllocator& allocator, std::initializer_list<T> init) : m_allocator(allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            reserve(init.size());
            for (const auto& item : init)
            {
                pushback(item);
            }
        }

        Vec(const Vec& other) : m_allocator(other.m_allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            reserve(other.m_size);
            for (size_type i = 0; i < other.m_size; ++i)
            {
                pushback(other.m_data[i]);
            }
        }

        Vec(Vec&& other) noexcept : m_allocator(other.m_allocator), m_size(other.m_size), m_capacity(other.m_capacity), m_data(other.m_data)
        {
            other.m_size     = 0;
            other.m_capacity = 0;
            other.m_data     = nullptr;
        }

        Vec operator=(const Vec& other)
        {
            if (this != &other)
            {
                clear();
                reserve(other.size());

                for (size_type i = 0; i < other.size(); i++)
                {
                    pushback(other.m_data[i]);
                }
            }
            return *this;
        }

        Vec operator=(Vec&& other) noexcept
        {
            if (this != &other)
            {
                clear();

                m_allocator      = other.m_allocator;
                m_size           = other.m_size;
                m_capacity       = other.m_capacity;
                m_data           = other.m_data;

                other.m_size     = 0;
                other.m_capacity = 0;
                other.m_data     = nullptr;
            }

            return *this;
        }

        reference at(size_type index)
        {
            assert(index <= m_size && "Index out of bounds");
            return m_data[index];
        }

        const_reference at(size_type index) const
        {
            assert(index <= m_size && "Index out of bounds");
            return m_data[index];
        }

        reference operator[](size_type index)
        {
            return m_data[index];
        }

        const_reference operator[](size_type index) const
        {
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
            return m_data[0];
        }

        const_reference front() const
        {
            return m_data[0];
        }

        reference back()
        {
            return m_data[m_size - 1];
        }

        const_reference back() const
        {
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

        void pushback(const T& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            new (m_data + m_size) T(value);
            ++m_size;
        }

        void pushback(T&& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            new (m_data + m_size) T(std::move(value));
            ++m_size;
        }

        template <typename... Args>
        reference emplaceBack(Args&&... args)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            new (m_data + m_size) T(std::forward<Args>(args)...);
            return m_data[m_size++];
        }

        void popback()
        {
            if (m_size > 0)
            {
                --m_size;
                m_data[m_size].~T();
            }
        }

        void resize(size_type count)
        {
            resize(count, T());
        }

        void resize(size_type count, const T& value)
        {
            if (count < m_size)
            {
                for (size_type i = count; i < m_size; ++i)
                {
                    m_data[i].~T();
                }
            }
            else if (count > m_size)
            {
                reserve(count);
                for (size_type i = m_size; i < count; ++i)
                {
                    new (m_data + i) T(value);
                }
            }
            m_size = count;
        }

        ~Vec()
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

            size_t  old_alloc_size = m_capacity * sizeof(T);
            size_t  new_alloc_size = new_capacity * sizeof(T);
            pointer new_data       = nullptr;

            if constexpr (std::is_trivially_move_constructible_v<value_type> && std::is_trivially_destructible_v<value_type>)
            {
                new_data = static_cast<pointer>(m_allocator.Reallocate(m_data, old_alloc_size, new_alloc_size, alignof(value_type)));
            }
            else
            {
                new_data = static_cast<pointer>(m_allocator.Allocate(new_alloc_size, alignof(value_type)));
                for (size_type i = 0; i < m_size; i++)
                {
                    new (new_data + i) T(std::move(m_data[i]));
                    m_data[i].~T();
                }
            }

            if (new_data == nullptr)
            {
                // How to handle this better??
                ZENGINE_CORE_CRITICAL("Bad Alloc")
            }
            m_data     = new_data;
            m_capacity = new_capacity;
        }

    private:
        Memory::ArenaAllocator& m_allocator;
        size_type               m_size;
        size_type               m_capacity;
        pointer                 m_data;
    };
} // namespace ZEngine::Core::Container