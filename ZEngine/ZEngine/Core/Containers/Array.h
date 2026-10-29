#pragma once
#include <ZEngine/Core/Containers/InitializerList.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Containers
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

        Array() : m_allocator(nullptr), m_slab(nullptr), m_size(0), m_capacity(0), m_data(nullptr) {}

        Array(const Array&)            = delete;
        Array& operator=(const Array&) = delete;

        Array(Array&& other) noexcept : m_allocator(other.m_allocator), m_slab(other.m_slab), m_size(other.m_size), m_capacity(other.m_capacity), m_data(other.m_data)
        {
            other.m_slab     = nullptr;
            other.m_size     = 0;
            other.m_capacity = 0;
            other.m_data     = nullptr;
        }

        Array& operator=(Array&& other) noexcept
        {
            if (this != &other)
            {
                m_allocator      = other.m_allocator;
                m_slab           = other.m_slab;
                m_size           = other.m_size;
                m_capacity       = other.m_capacity;
                m_data           = other.m_data;
                other.m_slab     = nullptr;
                other.m_size     = 0;
                other.m_capacity = 0;
                other.m_data     = nullptr;
            }
            return *this;
        }

        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity, size_type initial_size)
        {
            m_allocator = allocator;
            m_slab      = nullptr;
            m_size      = initial_size;
            m_capacity  = 0;
            m_data      = nullptr;
            reserve(std::max(initial_capacity, initial_size));
        }

        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity)
        {
            m_allocator = allocator;
            m_slab      = nullptr;
            m_size      = 0;
            m_capacity  = 0;
            m_data      = nullptr;
            reserve(initial_capacity);
        }

        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity, const InitializerList<T>& list)
        {
            init(allocator, std::max(initial_capacity, list.size()));
            for (const auto& item : list)
            {
                push(item);
            }
        }

        /// @brief Init backed by a TLSFSlab — Realloc extends in-place when possible,
        ///        eliminating dead-block accumulation from arena-backed grows.
        void init(Memory::TLSFSlab* slab, size_type initial_capacity)
        {
            m_allocator = nullptr;
            m_slab      = slab;
            m_size      = 0;
            m_capacity  = 0;
            m_data      = nullptr;
            reserve(initial_capacity);
        }

        void init(Memory::TLSFSlab* slab, size_type initial_capacity, size_type initial_size)
        {
            m_allocator = nullptr;
            m_slab      = slab;
            m_size      = initial_size;
            m_capacity  = 0;
            m_data      = nullptr;
            reserve(std::max(initial_capacity, initial_size));
        }

        const_reference operator[](size_type index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            return m_data[index];
        }

        reference operator[](size_t index)
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
            return m_size ? m_data + m_size : m_data;
        }

        const_iterator end() const
        {
            return m_size ? m_data + m_size : m_data;
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

        reference push_use(T&& value)
        {
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            m_data[m_size] = std::move(value);
            ++m_size;
            return back();
        }

        void insert(size_type index, const T& value)
        {
            ZENGINE_VALIDATE_ASSERT(index <= m_size, "Index out of range")
            if (m_size == m_capacity)
            {
                size_type new_capacity = m_capacity == 0 ? 4 : m_capacity * 2;
                reserve(new_capacity);
            }
            for (size_type i = m_size; i > index; --i)
            {
                m_data[i] = std::move(m_data[i - 1]);
            }
            m_data[index] = value;
            ++m_size;
        }

        // Remove the element at `index`, shifting later elements one slot left.
        void erase(size_type index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            for (size_type i = index; i + 1 < m_size; ++i)
            {
                m_data[i] = std::move(m_data[i + 1]);
            }
            --m_size;
        }

        void pop()
        {
            ZENGINE_VALIDATE_ASSERT(m_size > 0, "Index out of range")
            --m_size;
        }

        ~Array()
        {
            clear();
            if (m_slab && m_data)
            {
                m_slab->Free(m_data);
            }
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

            if (m_slab)
            {
                m_data = static_cast<pointer>(m_slab->Realloc(m_data, new_alloc_size));
            }
            else
                m_data = static_cast<pointer>(ZResize(m_allocator, m_data, old_alloc_size, new_alloc_size, ZAlignof<value_type>()));

            m_capacity = new_capacity;
        }

        /// Checked growth for bounded transactional callers. Failure keeps the
        /// previous pointer, size, capacity and initialized elements intact.
        [[nodiscard]] bool try_reserve(size_type new_capacity)
        {
            if (new_capacity <= m_capacity)
            {
                return true;
            }
            if (new_capacity > SIZE_MAX / sizeof(T) || (!m_slab && !m_allocator))
            {
                return false;
            }
            const size_t old_bytes = m_capacity * sizeof(T);
            const size_t new_bytes = new_capacity * sizeof(T);
            auto*        storage   = m_slab ? static_cast<pointer>(m_slab->TryRealloc(m_data, new_bytes)) : static_cast<pointer>(m_allocator->TryResize(m_data, old_bytes, new_bytes, ZAlignof<value_type>()));
            if (!storage)
            {
                return false;
            }
            m_data     = storage;
            m_capacity = new_capacity;
            return true;
        }

        /// Status-returning append for raw-relocatable records. Growing an Array
        /// of owning/nontrivial elements needs a different lifetime contract.
        [[nodiscard]] bool try_push(const T& value)
        {
            static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>);
            // Copy first: value may refer to an element invalidated by growth.
            const T copy = value;
            if (m_size == m_capacity)
            {
                if (m_capacity > SIZE_MAX / 2)
                {
                    return false;
                }
                if (!try_reserve(m_capacity ? m_capacity * 2 : 4))
                {
                    return false;
                }
            }
            new (m_data + m_size) T(copy);
            ++m_size;
            return true;
        }

        Memory::ArenaAllocator* m_allocator = nullptr;
        Memory::TLSFSlab*       m_slab      = nullptr;
        size_type               m_size      = 0;
        size_type               m_capacity  = 0;
        pointer                 m_data      = nullptr;
    };

    template <typename T>
    struct ArrayView
    {
        constexpr ArrayView() = default;
        constexpr ArrayView(T* data, size_t size) : m_data(data), m_size(size) {}

        template <typename U, size_t Count>
            requires std::is_convertible_v<U*, T*>
        constexpr ArrayView(U (&data)[Count]) : m_data(data), m_size(Count)
        {
        }

        template <typename U>
            requires std::is_convertible_v<U*, T*>
        constexpr ArrayView(const ArrayView<U>& other) : m_data(other.data()), m_size(other.size())
        {
        }

        template <typename Range>
            requires std::is_convertible_v<decltype(std::declval<Range&>().data()), T*>
        constexpr ArrayView(Range& range) : m_data(range.data()), m_size(range.size())
        {
        }

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

        T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range")
            return m_data[index];
        }

        size_t size() const
        {
            return m_size;
        }

        size_t size_bytes() const
        {
            return m_size * sizeof(T);
        }

        T* data() const
        {
            return m_data;
        }

        T* begin() const
        {
            return m_data;
        }
        T* end() const
        {
            return m_size ? m_data + m_size : m_data;
        }
        bool empty() const
        {
            return m_size == 0;
        }
        T& front() const
        {
            return (*this)[0];
        }
        T& back() const
        {
            return (*this)[m_size - 1];
        }
        ArrayView first(size_t count) const
        {
            ZENGINE_VALIDATE_ASSERT(count <= m_size, "ArrayView prefix out of range")
            return {m_data, count};
        }
        ArrayView subspan(size_t offset, size_t count = SIZE_MAX) const
        {
            ZENGINE_VALIDATE_ASSERT(offset <= m_size, "ArrayView offset out of range")
            if (count == SIZE_MAX)
            {
                count = m_size - offset;
            }
            ZENGINE_VALIDATE_ASSERT(count <= m_size - offset, "ArrayView slice out of range")
            return {offset ? m_data + offset : m_data, count};
        }

        T*     m_data = nullptr;
        size_t m_size = 0;
    };

    template <typename Range>
    ArrayView(Range&) -> ArrayView<std::remove_pointer_t<decltype(std::declval<Range&>().data())>>;

    template <typename T, size_t Count>
    ArrayView(T (&)[Count]) -> ArrayView<T>;

    template <typename T>
    ArrayView<const std::byte> AsBytes(ArrayView<T> values)
    {
        return {reinterpret_cast<const std::byte*>(values.data()), values.size_bytes()};
    }
} // namespace ZEngine::Core::Containers
