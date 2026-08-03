#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>

namespace Tetragrama::Helpers
{
    void KMPbuildTable(const char* pattern, ZEngine::Core::Containers::ArrayView<int> lps);
    bool KMPSearch(ZEngine::Core::Memory::ArenaAllocator* arena, const char* text, const char* pattern);
} // namespace Tetragrama::Helpers