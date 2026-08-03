#pragma once
#include <ZEngine/Core/Memory/Allocator.h>

using namespace ZEngine::Core::Memory;

namespace ZEngine::Core::Containers
{

    template <typename T>
    struct InitializerList
    {
        using value_type      = T;
        using size_type       = size_t;
        using reference       = T&;
        using const_reference = const T&;
        using iterator        = T*;
        using const_iterator  = const T*;
        using pointer         = T*;
        using const_pointer   = const T*;

        InitializerList(pointer data, size_type size) : m_data(data), m_size(size) {}

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

        size_type m_size;
        pointer   m_data;
    };

    template <typename T, typename... Args>
    InitializerList<T> make_initializer_list(Memory::ArenaAllocator* allocator, T first, Args... args)
    {
        size_t count  = sizeof...(args) + 1;

        T*     buffer = static_cast<T*>(ZAlloc(allocator, count * sizeof(T), ZAlignof(T)));

        buffer[0]     = first;

        size_t i      = 1;
        ((buffer[i++] = static_cast<T>(args)), ...);

        return InitializerList<T>(buffer, count);
    }

} // namespace ZEngine::Core::Containers