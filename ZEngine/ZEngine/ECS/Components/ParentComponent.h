#pragma once
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    // Links an entity to its parent in the scene hierarchy.
    // Entities without this component are roots — their WorldTransform equals their local transform.
    // HierarchySystem propagates parent WorldTransform × local matrix each frame.
    struct ParentComponent
    {
        EntityID Parent = INVALID_ENTITY;
    };

    static_assert(sizeof(ParentComponent) <= 8, "ParentComponent exceeds expected size");
} // namespace ZEngine::ECS::Components
