#pragma once
#include <Allocator.h>
#include <MemoryOperations.h>

namespace ZEngine::Core::Container
{
    struct String
    {
        using size_type       = size_t;
        using value_type      = char;
        using pointer         = char*;
        using const_pointer   = const char*;
        using reference       = char&;
        using const_reference = const char&;
        using iterator        = char*;
        using const_iterator  = const char*;

        String() : m_allocator(nullptr), m_size(0), m_capacity(0), m_data(nullptr) {}

        void init(Memory::ArenaAllocator* allocator, size_type initial_capacity = 16)
        {
            m_allocator = allocator;
            m_capacity = m_size = 0;
            m_data              = nullptr;
            reserve(initial_capacity);
            m_data[0] = '\0';
        }

        void init(Memory::ArenaAllocator* allocator, const char* str)
        {
            m_allocator = allocator;
            m_capacity = m_size = 0;
            m_data              = nullptr;

            if (str)
            {
                size_type len = Helpers::secure_strlen(str);
                reserve(len + 1);
                Helpers::secure_memcpy(m_data, m_capacity, str, len);
                m_size         = len;
                m_data[m_size] = '\0';
            }
            else
            {
                reserve(16);
                m_data[0] = '\0';
            }
        }

        ~String()
        {
            m_data     = nullptr;
            m_size     = 0;
            m_capacity = 0;
        }

        void append(const char* str)
        {
            if (!str)
                return;

            size_type len = Helpers::secure_strlen(str);
            if (len == 0)
                return;

            size_type new_size = m_size + len;
            if (new_size + 1 > m_capacity)
            {
                reserve(new_size + 1);
            }

            Helpers::secure_memcpy(m_data + m_size, m_capacity, str, len);
            m_size         = new_size;
            m_data[m_size] = '\0';
        }

        void append(const String& other)
        {
            append(other.c_str());
        }

        void append(char c)
        {
            if (m_size + 2 > m_capacity)
            {
                reserve((m_size + 2) * 2);
            }

            m_data[m_size] = c;
            m_size++;
            m_data[m_size] = '\0';
        }

        reference operator[](size_type index)
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range");
            return m_data[index];
        }

        const_reference operator[](size_type index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range");
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

        bool empty() const
        {
            return m_size == 0;
        }

        size_type size() const
        {
            return m_size;
        }

        size_type length() const
        {
            return m_size;
        }

        size_type capacity() const
        {
            return m_capacity;
        }

        const char* c_str() const
        {
            return m_data;
        }

        void clear()
        {
            if (m_data)
            {
                m_size    = 0;
                m_data[0] = '\0';
            }
        }

        void reserve(size_type new_capacity)
        {
            if (new_capacity <= m_capacity)
            {
                return;
            }

            size_t old_alloc_size = m_capacity * sizeof(char);
            size_t new_alloc_size = new_capacity * sizeof(char);

            m_data                = static_cast<pointer>(ZENGINE_RESIZE_MEMORY(m_allocator, m_data, old_alloc_size, new_alloc_size, ZENGINE_TYPE_ALIGNMENT(value_type)));
            m_capacity            = new_capacity;
        }

        Memory::ArenaAllocator* m_allocator;
        size_type               m_size;
        size_type               m_capacity;
        pointer                 m_data;
    };

    struct StringView
    {
        using size_type = size_t;

        StringView() : m_data(nullptr), m_size(0) {}

        StringView(const char* str) : m_data(str), m_size(str ? Helpers::secure_strlen(str) : 0) {}

        StringView(const char* str, size_type size) : m_data(str), m_size(size) {}

        StringView(const String& str) : m_data(str.c_str()), m_size(str.size()) {}

        void set(const char* str)
        {
            m_data = str;
            m_size = str ? Helpers::secure_strlen(str) : 0;
        }

        void set(const char* str, size_type size)
        {
            m_data = str;
            m_size = size;
        }

        void set(const String& str)
        {
            m_data = str.c_str();
            m_size = str.size();
        }

        char operator[](size_type index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < m_size, "Index out of range");
            return m_data[index];
        }

        const char* begin() const
        {
            return m_data;
        }

        const char* end() const
        {
            return m_data + m_size;
        }

        bool empty() const
        {
            return m_size == 0 || m_data == nullptr;
        }

        size_type size() const
        {
            return m_size;
        }

        size_type length() const
        {
            return m_size;
        }

        const char* data() const
        {
            return m_data;
        }

        const char* m_data;
        size_type   m_size;
    };

} // namespace ZEngine::Core::Container