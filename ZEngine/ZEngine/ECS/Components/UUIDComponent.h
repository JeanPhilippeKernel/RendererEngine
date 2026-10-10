#pragma once
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>

namespace ZEngine::ECS::Components
{
    // Stable cross-session identity for scene serialization.
    // Assigned once when the Actor is created and persisted in the scene file.
    struct UUIDComponent
    {
        uuids::uuid Value = {};
    };

    static_assert(sizeof(UUIDComponent) <= 16, "UUIDComponent exceeds expected size");
    static_assert(alignof(UUIDComponent) <= 16, "UUIDComponent misaligned");
} // namespace ZEngine::ECS::Components
