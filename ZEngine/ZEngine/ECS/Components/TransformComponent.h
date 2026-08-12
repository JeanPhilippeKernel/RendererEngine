#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    // Plain-data transform. No methods, no computed matrix.
    // Separate from Rendering::Components::TransformComponent which has methods and a Mat4f.
    struct TransformComponent
    {
        Core::Maths::Vec3f Position         = {0.f, 0.f, 0.f};
        Core::Maths::Vec3f Rotation         = {0.f, 0.f, 0.f}; // radians (pitch, yaw, roll)
        Core::Maths::Vec3f Scale            = {1.f, 1.f, 1.f};

        // Previous-frame position for fixed-timestep interpolation (game-loop.md)
        Core::Maths::Vec3f PreviousPosition = {0.f, 0.f, 0.f};
    };

    static_assert(sizeof(TransformComponent) <= 64, "TransformComponent exceeds cache line");
    static_assert(alignof(TransformComponent) <= 16, "TransformComponent misaligned");
} // namespace ZEngine::ECS::Components
