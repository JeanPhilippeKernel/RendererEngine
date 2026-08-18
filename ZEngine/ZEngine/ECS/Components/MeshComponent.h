#pragma once
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>

namespace ZEngine::ECS::Components
{
    // Links an Actor to a cooked mesh artifact in the AssetManager.
    // The UUID is the stable identity assigned at import time and stored
    // in the .meta sidecar — it never changes across reimports.
    struct MeshComponent
    {
        uuids::uuid MeshUUID = {};
    };

    static_assert(sizeof(MeshComponent) <= 16, "MeshComponent exceeds expected size");
    static_assert(alignof(MeshComponent) <= 16, "MeshComponent misaligned");
} // namespace ZEngine::ECS::Components
