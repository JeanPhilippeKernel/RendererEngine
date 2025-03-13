#pragma once
#include <MemoryOperations.h>

namespace ZEngine::Core::Container
{
    class StringView
    {

    public:
        using size_type = size_t;
        using pointer   = const char*;
        using iterator  = const char*;
        using reference = const char&;

        StringView() noexcept : m_data(""), m_size(0) {}

        StringView(const char* data) : m_data(data), m_size(len(data)) {}

        static constexpr size_type len(char const* str)
        {
            char const* it = str;
            while (*it != 0)
            {
                it++;
            }
            return it - str;
        }

        StringView(const StringView& other) : m_data(other.m_data), m_size(other.m_size) {}

        constexpr iterator begin() const noexcept
        {
            return m_data;
        }

        constexpr iterator end() const noexcept
        {
            return m_data + m_size;
        }

        constexpr pointer data() const noexcept
        {
            return m_data;
        }

        constexpr size_type size() const noexcept
        {
            return m_size;
        }

        constexpr bool empty() const noexcept
        {
            return m_size == 0;
        }

        constexpr reference operator[](size_type pos) const
        {
            return m_data[pos];
        }

        constexpr reference at(size_type pos) const
        {
            // work on this
            return m_data[pos];
        }

        constexpr bool operator==(const StringView& other) const
        {
            if (m_size != other.size())
            {
                return false;
            }

            return Helpers::secure_memcmp(m_data, m_size, other.m_data, other.m_size, m_size) == 0;
        }

        bool operator!=(const StringView& other) const noexcept
        {
            return !(*this == other);
        }

    private:
        const char* m_data = "";
        size_type   m_size;
    };
} // namespace ZEngine::Core::Container