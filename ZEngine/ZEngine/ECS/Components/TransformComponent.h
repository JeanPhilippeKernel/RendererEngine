#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS::Components
{
    // Position, Rotation, Scale are LOCAL-space values edited by the user/gizmo.
    // WorldTransform is computed each frame by HierarchySystem and consumed by
    // TransformSyncSystem and LightSyncSystem — never set directly.
    struct TransformComponent
    {
        Core::Maths::Vec3f Position         = {0.f, 0.f, 0.f};
        Core::Maths::Vec3f Rotation         = {0.f, 0.f, 0.f}; // radians (pitch, yaw, roll)
        Core::Maths::Vec3f Scale            = {1.f, 1.f, 1.f};
        Core::Maths::Vec3f PreviousPosition = {0.f, 0.f, 0.f};
        Core::Maths::Mat4f WorldTransform   = Core::Maths::Identity<Core::Maths::Mat4f>();
    };

    static_assert(alignof(TransformComponent) <= 16, "TransformComponent misaligned");
} // namespace ZEngine::ECS::Components
