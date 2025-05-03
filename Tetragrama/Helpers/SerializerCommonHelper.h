#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/HashMap.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngineDef.h>
#include <iostream>
#include <type_traits>

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream&, ZEngine::Core::Containers::StringView);
    void SerializeStringArrayData(std::ostream&, ZEngine::Core::Containers::ArrayView<ZEngine::Core::Containers::String>);
    void SerializeMapData(std::ostream&, ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>&);

    void DeserializeStringData(ZEngine::Core::Memory::ArenaAllocator*, std::istream& in, ZEngine::Core::Containers::String& data);
    void DeserializeStringArrayData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String>&);
    void DeserializeMapData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, ZEngine::Core::Containers::HashMap<uint32_t, uint32_t>&);

    template <typename T>
    void WriteBinary(std::ostream& writer, const T& data)
    {
        writer.write(reinterpret_cast<const char*>(&data), sizeof(T));
    }

    template <typename T, typename = std::enable_if_t<std::is_same_v<T, const char*> || std::is_same_v<T, ZEngine::Core::Containers::String>>>
    void WriteBinaryString(std::ostream& writer, const T& str)
    {
        if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)

        {
            size_t count = str.size();
            writer.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
            writer.write(str.c_str(), count + 1);
        }
        else if constexpr (std::is_same_v<T, const char*>)

        {
            size_t count = ZEngine::Helpers::secure_strlen(str);

            writer.write(reinterpret_cast<const char*>(&count), sizeof(size_t));
            writer.write(str, count + 1);
        }
    }

    template <typename T>
    void WriteBinaryArray(std::ostream& writer, ZEngine::Core::Containers::ArrayView<T> arr)
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
            writer.write(reinterpret_cast<const char*>(arr.data()), sizeof(T) * count);
        }
    }

    template <class T, class U>
    void WriteBinaryHashMap(std::ostream& writer, ZEngine::Core::Containers::HashMap<T, U>& map)
    {
        uint32_t size = static_cast<uint32_t>(map.size());
        WriteBinary(writer, size);

        auto view = map.view();
        for (const auto& [key, val] : view)
        {
            WriteBinary(writer, key);
            WriteBinary(writer, val);
        }
    }

    template <typename T>
    void ReadBinary(std::istream& in, T& value)
    {
        in.read(reinterpret_cast<char*>(&value), sizeof(T));
    }

    template <typename T, typename = std::enable_if_t<std::is_same_v<T, ZEngine::Core::Containers::String>>>
    void ReadBinaryString(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, T& str)
    {
        uint32_t size;
        ReadBinary(in, size);
        if constexpr (std::is_same_v<T, ZEngine::Core::Containers::String>)

        {
            char buf[DEFAULT_STR_BUFFER] = {0};
            in.read(reinterpret_cast<char*>(&size), sizeof(size_t));
            in.read(buf, size + 1);

            str.init(arena, buf);
        }
    }

    template <typename T>
    void ReadBinaryArray(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, ZEngine::Core::Containers::Array<T>& arr)
    {
        uint32_t size;
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
    void ReadHashMap(ZEngine::Core::Memory::ArenaAllocator* arena, std::istream& in, ZEngine::Core::Containers::HashMap<T, U>& map)
    {
        uint32_t size;
        ReadBinary(in, size);

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
} // namespace Tetragrama::Helpers