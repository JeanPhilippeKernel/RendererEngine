#pragma once
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>

namespace ZEngine::ECS::Tools
{
    /// @brief A normalized world-space cursor ray used by editor manipulators.
    struct GizmoRay
    {
        Core::Maths::Vec3f Origin    = {};
        Core::Maths::Vec3f Direction = {};
    };

    /// @brief A world-space plane expressed by a point and a normalized normal.
    struct GizmoPlane
    {
        Core::Maths::Vec3f Point  = {};
        Core::Maths::Vec3f Normal = {};
    };

    /// @brief Authored local transform values produced by a gizmo preview.
    /// @details The type deliberately mirrors TransformComponent while keeping the interaction
    ///          kernel independent from mutable ECS state.
    struct GizmoLocalTransform
    {
        Core::Maths::Vec3f Position = {};
        Core::Maths::Vec3f Rotation = {};
        Core::Maths::Vec3f Scale    = {1.f, 1.f, 1.f};
    };

    /// @brief Intersects a forward cursor ray with a constraint plane.
    /// @returns False for invalid input, a near-parallel ray, or an intersection behind the ray.
    ///          @p out_point is left untouched on failure.
    [[nodiscard]] bool  TryIntersectRayPlane(const GizmoRay& ray, const GizmoPlane& plane, Core::Maths::Vec3f& out_point, float parallel_epsilon = 1.0e-5f);

    /// @brief Finds the closest point on an axis line to a forward cursor ray.
    /// @returns False when the two lines are near parallel or input is invalid. The returned
    ///          scalar is measured in world units along the normalized axis from @p axis_origin.
    [[nodiscard]] bool  TryClosestRayAxisParameter(const GizmoRay& ray, Core::Maths::Vec3f axis_origin, Core::Maths::Vec3f axis_direction, float& out_axis_parameter, float parallel_epsilon = 1.0e-5f);

    /// @brief Computes the signed angle around @p axis from @p drag_start to @p current.
    /// @returns False when either radial vector is degenerate. Positive follows the right-hand
    ///          rule around the normalized axis.
    [[nodiscard]] bool  TrySignedAxisAngle(Core::Maths::Vec3f axis, Core::Maths::Vec3f drag_start, Core::Maths::Vec3f current, float& out_radians, float minimum_radius = 1.0e-5f);

    /// @brief Snaps a finite scalar to the nearest captured increment.
    /// @details Invalid/disabled increments return the input unchanged. Values exactly between
    ///          increments use std::round's deterministic away-from-zero behavior.
    [[nodiscard]] float SnapGizmoScalar(float value, float increment, bool enabled);

    /// @brief Returns the stable scale ratio between captured and current radial distances.
    [[nodiscard]] bool  TryGizmoScaleRatio(float captured_distance, float current_distance, float& out_ratio, float minimum_distance = 1.0e-5f);

    /// @brief Safely inverts a finite affine matrix without asserting on a singular parent.
    /// @returns false for projective, non-finite, or singular matrices. @p out_inverse is
    ///          untouched on failure.
    [[nodiscard]] bool  TryInvertAffineTransform(const Core::Maths::Mat4f& transform, Core::Maths::Mat4f& out_inverse, float determinant_epsilon = 1.0e-7f);

    /// @brief Converts a desired world transform into the strict authored local TRS model.
    /// @param parent_world Null for a root target; otherwise the captured non-selected parent.
    /// @returns False if the parent cannot be inverted or the local result would introduce shear.
    ///          @p out_local is untouched on failure.
    [[nodiscard]] bool  TryWorldToLocalGizmoTransform(const Core::Maths::Mat4f& desired_world, const Core::Maths::Mat4f* parent_world, GizmoLocalTransform& out_local);
} // namespace ZEngine::ECS::Tools
