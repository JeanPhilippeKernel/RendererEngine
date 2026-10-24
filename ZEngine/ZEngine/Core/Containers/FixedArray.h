#pragma once
#include <ZEngine/ZEngineDef.h>
#include <cstddef>
#include <type_traits>

namespace ZEngine::Core::Containers
{
    /// Inline, value-initialized storage. No allocator, ownership transfer or
    /// hidden dynamic allocation; elements keep their normal C++ lifetimes.
    template <typename T, size_t Count>
    struct FixedArray
    {
        static_assert(Count > 0);
        T            Values[Count] = {};

        constexpr T* data()
        {
            return Values;
        }
        constexpr const T* data() const
        {
            return Values;
        }
        constexpr size_t size() const
        {
            return Count;
        }
        constexpr T* begin()
        {
            return Values;
        }
        constexpr const T* begin() const
        {
            return Values;
        }
        constexpr T* end()
        {
            return Values + Count;
        }
        constexpr const T* end() const
        {
            return Values + Count;
        }
        constexpr T& operator[](size_t index)
        {
            ZENGINE_VALIDATE_ASSERT(index < Count, "FixedArray index out of range")
            return Values[index];
        }
        constexpr const T& operator[](size_t index) const
        {
            ZENGINE_VALIDATE_ASSERT(index < Count, "FixedArray index out of range")
            return Values[index];
        }
        constexpr bool operator==(const FixedArray& other) const
        {
            for (size_t i = 0; i < Count; ++i)
            {
                if (!(Values[i] == other.Values[i]))
                {
                    return false;
                }
            }
            return true;
        }
    };

    template <typename T, typename... Rest>
    FixedArray(T, Rest...) -> FixedArray<T, 1 + sizeof...(Rest)>;
} // namespace ZEngine::Core::Containers
