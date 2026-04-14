#pragma once
#include <Core/Containers/Array.h>
#include <Core/Containers/Strings.h>
#include <Core/Containers/UnorderedHashMap.h>
#include <Core/Memory/Allocator.h>
#include <ZEngineDef.h>
#include <iostream>
#include <type_traits>

namespace ZEngine::Helpers
{
    template <typename T>
    static void WriteBinary(std::ostream& writer, const T& data)
    {
        writer.write(reinterpret_cast<const char*>(&data), sizeof(T));
    }

    template <typename T, typename = std::enable_if_t<std::is_same_v<T, const char*> || std::is_same_v<T, ZEngine::Core::Containers::String>>>
    static void WriteBinaryString(std::ostream& writer, const T& str)
    {
        if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)

        {
            size_t count = str.size();
            writer.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
            if (count > 0)
            {
                writer.write(str.c_str(), count + 1);
            }
        }
        else if constexpr (std::is_same_v<T, const char*>)

        {
            size_t count = ZEngine::Helpers::secure_strlen(str);
            writer.write(reinterpret_cast<const char*>(&count), sizeof(size_t));

            if (count > 0)
            {
                writer.write(str, count);
            }
        }
    }

    template <typename T>
    static void WriteBinaryArray(std::ostream& writer, ZEngine::Core::Containers::ArrayView<T> arr)
    {
        size_t count = arr.size();
        writer.write(reinterpret_cast<const char*>(&count), sizeof(size_t));

        if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)

        {
            for (unsigned i = 0; i < count; ++i)
            {
                WriteBinaryString(writer, arr[i]);
            }
        }

        else
        {
            if (count > 0)
            {
                writer.write(reinterpret_cast<const char*>(arr.data()), sizeof(T) * count);
            }
        }
    }

    template <class T, class U>
    static void WriteBinaryHashMap(std::ostream& writer, ZEngine::Core::Containers::UnorderedHashMap<T, U>& map)
    {
        size_t size = map.size();
        WriteBinary(writer, size);

        for (const auto& [key, val] : map)
        {
            WriteBinary(writer, key);
            WriteBinary(writer, val);
        }
    }

    template <typename T>
    static void ReadBinary(std::istream& in, T& value)
    {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
    }

    static void ReadBinaryString(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, ZEngine::Core::Containers::String& str)
    {
        size_t size                    = 0;
        char   buf[DEFAULT_STR_BUFFER] = {0};
        in.read(reinterpret_cast<char*>(&size), sizeof(size_t));

        if (size > 0)
        {
            in.read(buf, size + 1);
            str.init(arena, buf);
        }
    }

    static void ReadBinaryCString(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, char* str)
    {
        size_t size = 0;
        in.read(reinterpret_cast<char*>(&size), sizeof(size_t));

        if (size > 0)
        {
            in.read(str, size);
        }
    }

    template <typename T>
    static void ReadBinaryArray(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, ZEngine::Core::Containers::Array<T>& arr)
    {
        size_t size = 0;
        ReadBinary(in, size);
        arr.init(arena, size, size);

        if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)

        {
            for (unsigned i = 0; i < size; ++i)
            {
                auto& str = arr[i];
                ReadBinaryString(arena, in, str);
            }
        }
        else
        {
            if (size > 0)
            {
                in.read(reinterpret_cast<char*>(arr.data()), sizeof(T) * size);
            }
        }
    }

    template <class T, class U>
    static void ReadHashMap(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, ZEngine::Core::Containers::UnorderedHashMap<T, U>& map)
    {
        size_t size = 0;
        ReadBinary(in, size);
        map.init(arena, (size > 32) ? size : 32);

        for (uint32_t i = 0; i < size; ++i)
        {
            T key;
            U val;

            if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)
            {
                ReadBinaryString(arena, in, key);
            }
            else
            {
                ReadBinary(in, key);
            }

            if constexpr (std::is_same_v<U, ZEngine::Core::Containers::String>)
            {
                ReadBinaryString(arena, in, val);
            }
            else
            {
                ReadBinary(in, val);
            }
            map.insert(key, val);
        }
    }
} // namespace ZEngine::Helpers