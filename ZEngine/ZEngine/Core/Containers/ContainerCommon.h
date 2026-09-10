#pragma once
#include <ZEngine/Helpers/MemoryOperations.h>
#include <rapidhash.h>
#include <cstdint>

namespace ZEngine::Core::Containers
{
    enum class EntryState : uint8_t
    {
        Empty    = 0,
        Occupied = 1,
        Deleted  = 2,
    };

    inline uint64_t hash_compute(const char* str)
    {
        return rapidhash(str, Helpers::secure_strlen(str));
    }
} // namespace ZEngine::Core::Containers
