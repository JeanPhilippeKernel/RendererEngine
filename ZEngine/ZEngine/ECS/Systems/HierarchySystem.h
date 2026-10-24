#pragma once
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS::Systems
{
    // Propagates local→world transforms top-down each frame.
    //
    // Cached, iterative parent-before-child order: O(V + E), with no recursion or
    // per-frame allocations. Transformless groups inherit their parent frame.
    // Invalid links/cycles use finite local fallback poses and invalid edit frames;
    // authored hierarchy/TRS are never silently repaired. Every matrix lane updates.
    //
    // Must run before SyncECSToRenderScene and SyncECSToLights.
    void SyncHierarchy(Scene& scene);
} // namespace ZEngine::ECS::Systems
