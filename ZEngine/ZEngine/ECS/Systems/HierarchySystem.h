#pragma once
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS::Systems
{
    // Propagates local→world transforms top-down each frame.
    //
    // Two phases per frame:
    //   Pass 1 — roots (no usable ParentComponent): WorldTransform = ComposeTransformMatrix(local)
    //   Pass 2 — children: WorldTransform = parent.WorldTransform × ComposeTransformMatrix(local)
    //            Repeated until stable, bounded by the live-entity count, to handle
    //            arbitrary hierarchy depth without an unbounded cycle loop.
    //
    // Must run before SyncECSToRenderScene and SyncECSToLights.
    void SyncHierarchy(Scene& scene);
} // namespace ZEngine::ECS::Systems
