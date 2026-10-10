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
        uuids::uuid MeshUUID         = {};
        uint32_t    RenderInstanceId = UINT32_MAX; // index into RenderScene::Instances; UINT32_MAX = not registered
    };

    static_assert(sizeof(MeshComponent) <= 32, "MeshComponent exceeds expected size");
    static_assert(alignof(MeshComponent) <= 16, "MeshComponent misaligned");
} // namespace ZEngine::ECS::Components
