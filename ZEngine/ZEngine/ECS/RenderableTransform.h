#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ECS/EntityID.h>

namespace ZEngine::ECS
{
    // Per-entity interpolated transform written by Scene::FillRenderableTransforms
    // and consumed by the renderer each frame.
    struct RenderableTransform
    {
        EntityID           Entity   = INVALID_ENTITY;
        Core::Maths::Vec3f Position = {0.f, 0.f, 0.f};
        Core::Maths::Vec3f Rotation = {0.f, 0.f, 0.f};
        Core::Maths::Vec3f Scale    = {1.f, 1.f, 1.f};
    };
} // namespace ZEngine::ECS
