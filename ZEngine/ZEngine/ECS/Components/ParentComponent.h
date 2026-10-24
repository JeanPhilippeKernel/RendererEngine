#pragma once
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    // Links an entity to its parent in the scene hierarchy.
    // An absent component or INVALID_ENTITY means a root. Use Scene::SetParent
    // for validated edits; transformless grouping entities inherit their ancestor frame.
    struct ParentComponent
    {
        EntityID Parent = INVALID_ENTITY;
    };

    static_assert(sizeof(ParentComponent) <= 8, "ParentComponent exceeds expected size");
} // namespace ZEngine::ECS::Components
