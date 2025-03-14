#pragma once
#include <Allocator.h>
#include <MemoryOperations.h>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Container
{
    class String
    {
    public:
        using size_type       = size_t;
        using pointer         = char*;
        using const_pointer   = const char*;
        using iterator        = char*;
        using const_iterator  = const char*;
        using reference       = char&;
        using const_reference = const char&;

        explicit String(Memory::ArenaAllocator* arena) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(arena) {}

        String(Memory::ArenaAllocator* arena, const char* str) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(arena)
        {
            if (str)
            {
                size_t len = Helpers::secure_strlen(str);
                reserve(len + 1);
                Helpers::secure_memcpy(m_data, m_capacity, str, len);
                m_data[len] = '\0';
                m_size      = len;
            }
        }

        String(const String& other) : m_data(nullptr), m_size(0), m_capacity(0), m_arena(other.m_arena)
        {
            if (other.m_size > 0)
            {
                reserve(other.m_size + 1);
                Helpers::secure_memcpy(m_data, m_capacity, other.m_data, other.m_size + 1);
                m_size = other.m_size;
            }
        }

        String(String&& other) noexcept : m_data(other.m_data), m_size(other.m_size), m_capacity(other.m_capacity), m_arena(other.m_arena)
        {
            other.m_data     = nullptr;
            other.m_size     = 0;
            other.m_capacity = 0;
        }

        ~String()
        {
            m_data     = nullptr;
            m_size     = 0;
            m_capacity = 0;
        }

        String& operator=(const String& other)
        {
            if (this != &other)
            {
                m_arena = other.m_arena;
                if (other.m_size > 0)
                {
                    reserve(other.m_size + 1);
                    Helpers::secure_memcpy(m_data, m_capacity, other.m_data, other.m_size + 1);
                    m_size = other.m_size;
                }
                else
                {
                    m_size = 0;
                    if (m_data)
                    {
                        m_data[0] = '\0';
                    }
                }
            }
            return *this;
        }

        String& operator=(String&& other) noexcept
        {
            if (this != &other)
            {
                m_data           = other.m_data;
                m_size           = other.m_size;
                m_capacity       = other.m_capacity;
                m_arena          = other.m_arena;

                other.m_data     = nullptr;
                other.m_size     = 0;
                other.m_capacity = 0;
            }
            return *this;
        }

        String& operator=(const char* str)
        {
            clear();
            if (str)
            {
                size_t len = Helpers::secure_strlen(str);
                reserve(len + 1);
                Helpers::secure_memcpy(m_data, m_capacity, str, len + 1);
                m_size = len;
            }
            return *this;
        }

        String& append(const String& other)
        {
            if (other.m_size > 0)
            {
                size_t new_size = m_size + other.m_size;
                reserve(new_size + 1);
                Helpers::secure_memcpy(m_data + m_size, m_capacity, other.m_data, other.m_size + 1);
                m_size = new_size;
            }
            return *this;
        }

        String& append(const char* str)
        {
            if (str)
            {
                size_t len = Helpers::secure_strlen(str);
                if (len > 0)
                {
                    size_t new_size = m_size + len;
                    reserve(new_size + 1);
                    Helpers::secure_memcpy(m_data + m_size, m_capacity, str, len + 1);
                    m_size = new_size;
                }
            }
            return *this;
        }

        String& append(char c)
        {
            reserve(m_size + 2);
            m_data[m_size]     = c;
            m_data[m_size + 1] = '\0';
            m_size++;
            return *this;
        }

        String& operator+=(const String& other)
        {
            return append(other);
        }
        String& operator+=(const char* str)
        {
            return append(str);
        }
        String& operator+=(char c)
        {
            return append(c);
        }

        String substring(size_t start, size_t length) const
        {
            String result(m_arena);
            if (start < m_size)
            {
                length = (start + length > m_size) ? (m_size - start) : length;
                result.reserve(length + 1);
                Helpers::secure_memcpy(result.m_data, m_capacity, m_data + start, length);
                result.m_data[length] = '\0';
                result.m_size         = length;
            }
            return result;
        }

        void clear()
        {
            if (m_data)
            {
                m_data[0] = '\0';
            }
            m_size = 0;
        }

        void reserve(size_t capacity)
        {
            if (capacity > m_capacity && m_arena)
            {
                char* new_data = static_cast<char*>(m_arena->Allocate(capacity * sizeof(char)));

                if (m_data && m_size > 0)
                {
                    Helpers::secure_memcpy(new_data, m_capacity, m_data, m_size + 1);
                }
                else
                {
                    new_data[0] = '\0';
                }

                m_data     = new_data;
                m_capacity = capacity;
            }
        }

        void SetArenaAllocator(Memory::ArenaAllocator* arena)
        {
            m_arena = arena;
        }

        reference operator[](size_t index)
        {
            return m_data[index];
        }

        const_reference operator[](size_t index) const
        {
            return m_data[index];
        }

        bool operator==(const String& other) const
        {
            if (m_size != other.m_size)
                return false;
            if (m_size == 0)
                return true;
            return Helpers::secure_memcmp(m_data, m_capacity, other.m_data, other.m_size, m_size) == 0;
        }

        bool operator!=(const String& other) const
        {
            return !(*this == other);
        }

        const_pointer cstr() const
        {
            return m_data ? m_data : "";
        }

        bool isempty() const
        {
            return m_size == 0;
        }
        size_t length() const
        {
            return m_size;
        }

        size_t capacity() const
        {
            return m_capacity;
        }

    private:
        char*                   m_data;
        size_type               m_size;
        size_type               m_capacity;
        Memory::ArenaAllocator* m_arena;
    };
} // namespace ZEngine::Core::Container