#pragma once
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS::Systems
{
    // Propagates local→world transforms top-down each frame.
    //
    // Two passes per frame:
    //   Pass 1 — roots (no ParentComponent): WorldTransform = ComposeTransformMatrix(local)
    //   Pass 2 — children: WorldTransform = parent.WorldTransform × ComposeTransformMatrix(local)
    //            Repeated until stable to handle arbitrary hierarchy depth.
    //
    // Must run before SyncECSToRenderScene and SyncECSToLights.
    void SyncHierarchy(Scene& scene);
} // namespace ZEngine::ECS::Systems
