#pragma once
#include <ZEngine/Core/Container/Array.h>
#include <ZEngine/Core/Container/Strings.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <iostream>
#include <unordered_map>

namespace Tetragrama::Helpers
{
    void SerializeStringData(std::ostream&, ZEngine::Core::Container::StringView);
    void SerializeStringArrayData(std::ostream&, ZEngine::Core::Container::ArrayView<ZEngine::Core::Container::String>);
    void SerializeMapData(std::ostream&, const std::unordered_map<uint32_t, uint32_t>&);

    void DeserializeStringData(ZEngine::Core::Memory::ArenaAllocator*, std::istream& in, ZEngine::Core::Container::String& data);
    void DeserializeStringArrayData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, ZEngine::Core::Container::Array<ZEngine::Core::Container::String>&);
    void DeserializeMapData(ZEngine::Core::Memory::ArenaAllocator*, std::istream&, std::unordered_map<uint32_t, uint32_t>&);
} // namespace Tetragrama::Helpers