#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>
#include <atomic>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace
{
    bool IsFinite(const Vec3f& value)
    {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    }

    bool NormalizeRayDirection(Vec3f direction, Vec3f& out_direction)
    {
        const float length = direction.magnitude();
        if (!std::isfinite(length) || length <= 0.00001f)
            return false;

        out_direction = direction / length;
        return true;
    }

    bool IntersectSphere(const Vec3f& origin, const Vec3f& direction, const ZEngine::Rendering::Scenes::SceneRaycastBounds& bounds, float max_distance, float& out_distance)
    {
        if (!IsFinite(origin) || !IsFinite(direction) || !IsFinite(bounds.Center) || !std::isfinite(bounds.Radius) || bounds.Radius <= 0.0f)
            return false;

        const Vec3f offset       = origin - bounds.Center;
        const float projection   = dot(offset, direction);
        const float discriminant = projection * projection - (dot(offset, offset) - bounds.Radius * bounds.Radius);
        if (!std::isfinite(discriminant) || discriminant < 0.0f)
            return false;

        const float root          = std::sqrt(discriminant);
        const float near_distance = -projection - root;
        const float far_distance  = -projection + root;
        const float distance      = near_distance > 0.0f ? near_distance : far_distance;
        if (!std::isfinite(distance) || distance <= 0.0f || distance > max_distance)
            return false;

        out_distance = distance;
        return true;
    }

    float MaxColumnScale(const ZEngine::Core::Maths::Mat4f& transform)
    {
        const float x = Vec3f(transform(0, 0), transform(1, 0), transform(2, 0)).magnitude();
        const float y = Vec3f(transform(0, 1), transform(1, 1), transform(2, 1)).magnitude();
        const float z = Vec3f(transform(0, 2), transform(1, 2), transform(2, 2)).magnitude();
        return x > y ? (x > z ? x : z) : (y > z ? y : z);
    }

    ZEngine::Core::Maths::Vec3f TransformPoint(const ZEngine::Core::Maths::Mat4f& transform, const ZEngine::Core::Maths::Vec3f& point)
    {
        return {
            transform(0, 0) * point.x + transform(0, 1) * point.y + transform(0, 2) * point.z + transform(0, 3),
            transform(1, 0) * point.x + transform(1, 1) * point.y + transform(1, 2) * point.z + transform(1, 3),
            transform(2, 0) * point.x + transform(2, 1) * point.y + transform(2, 2) * point.z + transform(2, 3),
        };
    }

    bool TryBuildWorldBounds(const ZEngine::Rendering::Scenes::MeshInstance& instance, ZEngine::Managers::AssetManager& assets, ZEngine::Rendering::Scenes::SceneRaycastBounds& out_bounds)
    {
        Vec3f local_center = {};
        float local_radius = 0.0f;
        if (!assets.TryGetMeshBounds(instance.MeshUUID, local_center, local_radius))
            return false;

        const auto bounds = ZEngine::Rendering::Scenes::TransformBounds(local_center, local_radius, instance.Transform, instance.Id);
        if (!IsFinite(bounds.Center) || !std::isfinite(bounds.Radius) || bounds.Radius <= 0.0f)
            return false;

        out_bounds = bounds;
        return true;
    }

    bool MergeBounds(const ZEngine::Rendering::Scenes::SceneRaycastBounds& candidate, ZEngine::Rendering::Scenes::SceneRaycastBounds& in_out_bounds)
    {
        const Vec3f offset   = candidate.Center - in_out_bounds.Center;
        const float distance = offset.magnitude();
        if (!std::isfinite(distance))
            return false;

        if (distance <= 0.00001f)
        {
            if (candidate.Radius > in_out_bounds.Radius)
                in_out_bounds = candidate;
            return true;
        }

        if (distance + candidate.Radius <= in_out_bounds.Radius)
            return true;
        if (distance + in_out_bounds.Radius <= candidate.Radius)
        {
            in_out_bounds = candidate;
            return true;
        }

        const float radius_delta  = (distance + candidate.Radius - in_out_bounds.Radius) * 0.5f;
        in_out_bounds.Center     += offset * (radius_delta / distance);
        in_out_bounds.Radius     += radius_delta;
        return IsFinite(in_out_bounds.Center) && std::isfinite(in_out_bounds.Radius);
    }
} // namespace

namespace ZEngine::Rendering::Scenes
{
    SceneRaycastBounds TransformBounds(Vec3f local_center, float local_radius, const Mat4f& transform, uint32_t instance_id)
    {
        return {
            .Center     = TransformPoint(transform, local_center),
            .Radius     = local_radius * MaxColumnScale(transform),
            .InstanceId = instance_id,
        };
    }

    SceneRaycastHit RaycastBounds(Core::Containers::ArrayView<const SceneRaycastBounds> bounds, Vec3f origin, Vec3f direction, float max_distance)
    {
        SceneRaycastHit result = {};
        result.Distance        = max_distance;
        if (!std::isfinite(max_distance) || max_distance <= 0.0f)
            return result;

        Vec3f normalized_direction = {};
        if (!NormalizeRayDirection(direction, normalized_direction))
            return result;

        for (size_t index = 0; index < bounds.size(); ++index)
        {
            float distance = 0.0f;
            if (!IntersectSphere(origin, normalized_direction, bounds[index], result.Distance, distance))
                continue;

            result.Hit        = true;
            result.Distance   = distance;
            result.InstanceId = bounds[index].InstanceId;
        }
        return result;
    }

    SceneRaycastHit RaycastSceneBounds(const RenderScene& scene, Vec3f origin, Vec3f direction, float max_distance)
    {
        SceneRaycastHit result = {};
        result.Distance        = max_distance;
        if (!std::isfinite(max_distance) || max_distance <= 0.0f)
            return result;

        auto* assets = Managers::AssetManager::Instance();
        if (!assets)
            return result;

        Vec3f normalized_direction = {};
        if (!NormalizeRayDirection(direction, normalized_direction))
            return result;

        while (true)
        {
            const uint64_t sequence_before = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before & 1u)
                continue;

            const MeshInstance* const instances      = scene.Instances.data();
            const size_t              instance_count = scene.Instances.size();
            SceneRaycastHit           closest        = {};
            closest.Distance                         = max_distance;

            for (size_t index = 0; index < instance_count; ++index)
            {
                SceneRaycastBounds bounds = {};
                if (!TryBuildWorldBounds(instances[index], *assets, bounds))
                    continue;

                float distance = 0.0f;
                if (!IntersectSphere(origin, normalized_direction, bounds, closest.Distance, distance))
                    continue;

                closest.Hit        = true;
                closest.Distance   = distance;
                closest.InstanceId = bounds.InstanceId;
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t sequence_after = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before == sequence_after)
                return closest;
        }
    }

    bool TryGetSceneInstanceBounds(const RenderScene& scene, uint32_t instance_id, SceneRaycastBounds& out_bounds)
    {
        auto* assets = Managers::AssetManager::Instance();
        if (!assets || instance_id == 0u)
            return false;

        while (true)
        {
            const uint64_t sequence_before = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before & 1u)
                continue;

            const MeshInstance* const instances      = scene.Instances.data();
            const size_t              instance_count = scene.Instances.size();
            MeshInstance              instance       = {};
            bool                      found          = false;
            for (size_t index = 0; index < instance_count; ++index)
            {
                if (instances[index].Id != instance_id)
                    continue;

                instance = instances[index];
                found    = true;
                break;
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t sequence_after = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before != sequence_after)
                continue;
            if (!found)
                return false;

            SceneRaycastBounds bounds = {};
            if (!TryBuildWorldBounds(instance, *assets, bounds))
                return false;

            out_bounds = bounds;
            return true;
        }
    }

    bool TryGetSceneBounds(const RenderScene& scene, SceneRaycastBounds& out_bounds)
    {
        auto* assets = Managers::AssetManager::Instance();
        if (!assets)
            return false;

        while (true)
        {
            const uint64_t sequence_before = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before & 1u)
                continue;

            const MeshInstance* const instances      = scene.Instances.data();
            const size_t              instance_count = scene.Instances.size();
            SceneRaycastBounds        combined       = {};
            bool                      found          = false;
            bool                      valid          = true;
            for (size_t index = 0; index < instance_count; ++index)
            {
                SceneRaycastBounds bounds = {};
                if (!TryBuildWorldBounds(instances[index], *assets, bounds))
                    continue;

                if (!found)
                {
                    combined = bounds;
                    found    = true;
                }
                else if (!MergeBounds(bounds, combined))
                {
                    valid = false;
                    break;
                }
            }

            std::atomic_thread_fence(std::memory_order_acquire);
            const uint64_t sequence_after = scene.m_seq.value.load(std::memory_order_acquire);
            if (sequence_before != sequence_after)
                continue;
            if (!found || !valid)
                return false;

            out_bounds = combined;
            return true;
        }
    }
} // namespace ZEngine::Rendering::Scenes
