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

        Vec(size_type initial_capacity, Memory::ArenaAllocator& allocator) : m_allocator(allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            Reserve(initial_capacity);
        }

        Vec(std::initializer_list<T> init, Memory::ArenaAllocator& allocator) : m_allocator(allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            Reserve(init.size());
            for (const auto& item : init)
            {
                PushBack(item);
            }
        }

        Vec(const Vec& other) : m_allocator(other.m_allocator), m_size(0), m_capacity(0), m_data(nullptr)
        {
            Reserve(other.m_size);
            for (size_type i = 0; i < other.m_size; ++i)
            {
                PushBack(other.m_data[i]);
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
                Clear();
                Reserve(other.Size());

                for (size_type i = 0; i < other.Size(); i++)
                {
                    PushBack(other.m_data[i]);
                }
            }
            return *this;
        }

        Vec operator=(Vec&& other) noexcept
        {
            if (this != &other)
            {
                Clear();

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

        reference At(size_type index)
        {
            assert(index <= m_size && "Index out of bounds");
            return m_data[index];
        }

        const_reference At(size_type index) const
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

        iterator Begin()
        {
            return m_data;
        }

        const_iterator Begin() const
        {
            return m_data;
        }

        iterator End()
        {
            return m_data + m_size;
        }

        const_iterator End() const
        {
            return m_data + m_size;
        }

        reference Front()
        {
            return m_data[0];
        }

        const_reference Front() const
        {
            return m_data[0];
        }

        reference Back()
        {
            return m_data[m_size - 1];
        }

        const_reference Back() const
        {
            return m_data[m_size - 1];
        }

        pointer Data()
        {
            return m_data;
        }

        const_pointer Data() const
        {
            return m_data;
        }

        bool Empty() const
        {
            return m_size == 0;
        }

        size_type Size() const
        {
            return m_size;
        }

        size_type Capacity() const
        {
            return m_capacity;
        }

        void Clear()
        {
            for (size_type i = 0; i < m_size; ++i)
            {
                m_data[i].~T();
            }
            m_size = 0;
        }

        void PushBack(const T& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                Reserve(new_capacity);
            }
            new (m_data + m_size) T(value);
            ++m_size;
        }

        void PushBack(T&& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                Reserve(new_capacity);
            }
            new (m_data + m_size) T(std::move(value));
            ++m_size;
        }

        template <typename... Args>
        reference EmplaceBack(Args&&... args)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                Reserve(new_capacity);
            }
            new (m_data + m_size) T(std::forward<Args>(args)...);
            return m_data[m_size++];
        }

        void PopBack()
        {
            if (m_size > 0)
            {
                --m_size;
                m_data[m_size].~T();
            }
        }

        void Resize(size_type count)
        {
            Resize(count, T());
        }

        void Resize(size_type count, const T& value)
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
                Reserve(count);
                for (size_type i = m_size; i < count; ++i)
                {
                    new (m_data + i) T(value);
                }
            }
            m_size = count;
        }

        ~Vec()
        {
            Clear();
            m_data     = nullptr;
            m_capacity = 0;
        }

        void Reserve(size_type new_capacity)
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