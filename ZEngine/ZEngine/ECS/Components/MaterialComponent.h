#pragma once
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>

namespace ZEngine::ECS::Components
{
    // Overrides the default material on the Actor's mesh for this instance.
    // When absent, the mesh's baked material UUIDs are used instead.
    struct MaterialComponent
    {
        uuids::uuid MaterialUUID = {};
    };

    static_assert(sizeof(MaterialComponent) <= 16, "MaterialComponent exceeds expected size");
    static_assert(alignof(MaterialComponent) <= 16, "MaterialComponent misaligned");
} // namespace ZEngine::ECS::Components
