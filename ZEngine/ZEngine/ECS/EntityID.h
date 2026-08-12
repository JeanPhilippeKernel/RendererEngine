#pragma once
#include <cstdint>

namespace ZEngine::ECS
{
    struct EntityID
    {
        uint32_t Index      = 0;
        uint32_t Generation = 0;

        bool     IsValid() const
        {
            return Generation != 0;
        }
        bool operator==(const EntityID&) const = default;
        bool operator!=(const EntityID&) const = default;
    };

    inline constexpr EntityID INVALID_ENTITY = {0, 0};
} // namespace ZEngine::ECS
