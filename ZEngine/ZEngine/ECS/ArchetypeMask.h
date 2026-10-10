#pragma once
#include <ZEngine/ECS/ComponentTypeID.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::ECS
{
    using ArchetypeMask                               = uint64_t;

    // v1 cap: 64 component types. v2 path: replace uint64_t with std::bitset<128>.
    inline constexpr uint32_t ARCHETYPE_MASK_CAPACITY = 64;

    inline ArchetypeMask      MaskBit(ComponentTypeID id)
    {
        ZENGINE_VALIDATE_ASSERT(id != UINT32_MAX, "MaskBit: ComponentTypeID is UINT32_MAX — uninitialized or invalid type ID")
        ZENGINE_VALIDATE_ASSERT(id < ARCHETYPE_MASK_CAPACITY, "MaskBit: component type ID exceeds ArchetypeMask v1 capacity (64)")
        return uint64_t(1) << id;
    }

    inline bool MaskHas(ArchetypeMask mask, ComponentTypeID id)
    {
        return (mask >> id) & 1;
    }

    inline bool MaskMatches(ArchetypeMask mask, ArchetypeMask required)
    {
        return (mask & required) == required;
    }
} // namespace ZEngine::ECS
