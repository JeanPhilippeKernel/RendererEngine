#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <iostream>
#include <unordered_map>

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream&, ZEngine::Core::Containers::StringView);
    void SerializeStringArrayData(std::ostream&, ZEngine::Core::Containers::ArrayView<ZEngine::Core::Containers::String>);
    void SerializeMapData(std::ostream&, const std::unordered_map<uint32_t, uint32_t>&);

    void DeserializeStringData(ZEngine::Core::Memory::ArenaAllocator*, std::istream& in, ZEngine::Core::Containers::String& data);
    void DeserializeStringArrayData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, ZEngine::Core::Containers::Array<ZEngine::Core::Containers::String>&);
    void DeserializeMapData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, std::unordered_map<uint32_t, uint32_t>&);
} // namespace Tetragrama::Helpers