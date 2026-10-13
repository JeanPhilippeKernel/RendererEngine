#include <ZEngine/ECS/Tools/GizmoMath.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace ZEngine::ECS::Tools
{
    namespace
    {
        [[nodiscard]] bool IsFinite(const Vec3f& value)
        {
            return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
        }

        [[nodiscard]] bool TryNormalize(Vec3f value, Vec3f& out_value, float minimum_length)
        {
            if (!IsFinite(value) || !std::isfinite(minimum_length) || minimum_length <= 0.f)
                return false;
            const float length = value.magnitude();
            if (!std::isfinite(length) || length <= minimum_length)
                return false;
            out_value = value / length;
            return true;
        }
    } // namespace

    bool TryIntersectRayPlane(const GizmoRay& ray, const GizmoPlane& plane, Vec3f& out_point, float parallel_epsilon)
    {
        Vec3f direction = {};
        Vec3f normal    = {};
        if (!TryNormalize(ray.Direction, direction, parallel_epsilon) || !TryNormalize(plane.Normal, normal, parallel_epsilon) || !IsFinite(ray.Origin) || !IsFinite(plane.Point))
            return false;

        const float denominator = dot(direction, normal);
        if (!std::isfinite(denominator) || std::fabs(denominator) <= parallel_epsilon)
            return false;

        const float distance = dot(plane.Point - ray.Origin, normal) / denominator;
        if (!std::isfinite(distance) || distance < 0.f)
            return false;

        out_point = ray.Origin + direction * distance;
        return IsFinite(out_point);
    }

    bool TryClosestRayAxisParameter(const GizmoRay& ray, Vec3f axis_origin, Vec3f axis_direction, float& out_axis_parameter, float parallel_epsilon)
    {
        Vec3f direction = {};
        Vec3f axis      = {};
        if (!TryNormalize(ray.Direction, direction, parallel_epsilon) || !TryNormalize(axis_direction, axis, parallel_epsilon) || !IsFinite(ray.Origin) || !IsFinite(axis_origin))
            return false;

        const Vec3f from_axis_to_ray = ray.Origin - axis_origin;
        const float direction_axis   = dot(direction, axis);
        const float denominator      = 1.f - direction_axis * direction_axis;
        if (!std::isfinite(denominator) || std::fabs(denominator) <= parallel_epsilon)
            return false;

        const float ray_distance   = (direction_axis * dot(axis, from_axis_to_ray) - dot(direction, from_axis_to_ray)) / denominator;
        const float axis_parameter = (dot(axis, from_axis_to_ray) - direction_axis * dot(direction, from_axis_to_ray)) / denominator;
        if (!std::isfinite(ray_distance) || !std::isfinite(axis_parameter) || ray_distance < 0.f)
            return false;

        out_axis_parameter = axis_parameter;
        return true;
    }

    bool TrySignedAxisAngle(Vec3f axis, Vec3f drag_start, Vec3f current, float& out_radians, float minimum_radius)
    {
        Vec3f normalized_axis = {};
        if (!TryNormalize(axis, normalized_axis, minimum_radius))
            return false;

        // Project on the rotation plane in case a caller supplied ray-plane points with
        // small numeric movement along the normal.
        drag_start              -= normalized_axis * dot(drag_start, normalized_axis);
        current                 -= normalized_axis * dot(current, normalized_axis);
        Vec3f start_direction    = {};
        Vec3f current_direction  = {};
        if (!TryNormalize(drag_start, start_direction, minimum_radius) || !TryNormalize(current, current_direction, minimum_radius))
            return false;

        out_radians = std::atan2(dot(normalized_axis, cross3d(start_direction, current_direction)), dot(start_direction, current_direction));
        return std::isfinite(out_radians);
    }

    float SnapGizmoScalar(float value, float increment, bool enabled)
    {
        if (!enabled || !std::isfinite(value) || !std::isfinite(increment) || increment <= 0.f)
            return value;
        return std::round(value / increment) * increment;
    }

    bool TryGizmoScaleRatio(float captured_distance, float current_distance, float& out_ratio, float minimum_distance)
    {
        if (!std::isfinite(captured_distance) || !std::isfinite(current_distance) || !std::isfinite(minimum_distance) || minimum_distance <= 0.f || std::fabs(captured_distance) <= minimum_distance)
            return false;

        const float ratio = current_distance / captured_distance;
        if (!std::isfinite(ratio) || ratio <= minimum_distance)
            return false;

        out_ratio = ratio;
        return true;
    }

    bool TryInvertAffineTransform(const Mat4f& transform, Mat4f& out_inverse, float determinant_epsilon)
    {
        if (!std::isfinite(determinant_epsilon) || determinant_epsilon <= 0.f)
            return false;
        for (size_t row = 0; row < 4; ++row)
            for (size_t column = 0; column < 4; ++column)
                if (!std::isfinite(transform(row, column)))
                    return false;

        if (std::fabs(transform(3, 0)) > determinant_epsilon || std::fabs(transform(3, 1)) > determinant_epsilon || std::fabs(transform(3, 2)) > determinant_epsilon || std::fabs(transform(3, 3) - 1.f) > determinant_epsilon)
            return false;

        const float a00 = transform(0, 0), a01 = transform(0, 1), a02 = transform(0, 2);
        const float a10 = transform(1, 0), a11 = transform(1, 1), a12 = transform(1, 2);
        const float a20 = transform(2, 0), a21 = transform(2, 1), a22 = transform(2, 2);
        const float determinant = a00 * (a11 * a22 - a12 * a21) - a01 * (a10 * a22 - a12 * a20) + a02 * (a10 * a21 - a11 * a20);
        if (!std::isfinite(determinant) || std::fabs(determinant) <= determinant_epsilon)
            return false;

        const float reciprocal  = 1.f / determinant;
        Mat4f       inverse     = {};
        inverse(0, 0)           = (a11 * a22 - a12 * a21) * reciprocal;
        inverse(0, 1)           = (a02 * a21 - a01 * a22) * reciprocal;
        inverse(0, 2)           = (a01 * a12 - a02 * a11) * reciprocal;
        inverse(1, 0)           = (a12 * a20 - a10 * a22) * reciprocal;
        inverse(1, 1)           = (a00 * a22 - a02 * a20) * reciprocal;
        inverse(1, 2)           = (a02 * a10 - a00 * a12) * reciprocal;
        inverse(2, 0)           = (a10 * a21 - a11 * a20) * reciprocal;
        inverse(2, 1)           = (a01 * a20 - a00 * a21) * reciprocal;
        inverse(2, 2)           = (a00 * a11 - a01 * a10) * reciprocal;

        const Vec3f translation = {transform(0, 3), transform(1, 3), transform(2, 3)};
        inverse(0, 3)           = -(inverse(0, 0) * translation.x + inverse(0, 1) * translation.y + inverse(0, 2) * translation.z);
        inverse(1, 3)           = -(inverse(1, 0) * translation.x + inverse(1, 1) * translation.y + inverse(1, 2) * translation.z);
        inverse(2, 3)           = -(inverse(2, 0) * translation.x + inverse(2, 1) * translation.y + inverse(2, 2) * translation.z);
        inverse(3, 3)           = 1.f;

        out_inverse             = inverse;
        return true;
    }

    bool TryWorldToLocalGizmoTransform(const Mat4f& desired_world, const Mat4f* parent_world, GizmoLocalTransform& out_local)
    {
        Mat4f local = desired_world;
        if (parent_world)
        {
            Mat4f inverse_parent = {};
            if (!TryInvertAffineTransform(*parent_world, inverse_parent))
                return false;
            local = inverse_parent * desired_world;
        }

        GizmoLocalTransform candidate = {};
        if (!TryDecomposeTransformTRS(local, candidate.Position, candidate.Rotation, candidate.Scale))
            return false;

        out_local = candidate;
        return true;
    }
} // namespace ZEngine::ECS::Tools
