#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>
#include <atomic>
#include <cmath>
#include <mutex>

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

    bool IntersectSphere(const Vec3f& origin, const Vec3f& direction, const ZEngine::Rendering::Scenes::SceneRaycastBounds& bounds, float max_distance, float& out_distance, bool broad_phase = false)
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
        if (!std::isfinite(far_distance) || far_distance <= 0.0f)
            return false;
        // Broad phase tests interval overlap, not the next sphere surface. A
        // ray starting inside can hit a triangle before the sphere's exit.
        const float distance = near_distance > 0.0f ? near_distance : (broad_phase ? 0.0f : far_distance);
        if (!std::isfinite(distance) || distance > max_distance)
            return false;

        out_distance = distance;
        return true;
    }

    bool IntersectTriangle(const Vec3f& origin, const Vec3f& direction, const Vec3f& a, const Vec3f& b, const Vec3f& c, float max_distance, float& out_distance)
    {
        if (!IsFinite(a) || !IsFinite(b) || !IsFinite(c))
            return false;

        const Vec3f edge_ab     = b - a;
        const Vec3f edge_ac     = c - a;
        const Vec3f p           = cross3d(direction, edge_ac);
        const float determinant = dot(edge_ab, p);
        if (!std::isfinite(determinant) || std::fabs(determinant) <= 1.0e-7f)
            return false;

        const float reciprocal = 1.f / determinant;
        const Vec3f from_a     = origin - a;
        const float u          = dot(from_a, p) * reciprocal;
        if (!std::isfinite(u) || u < 0.f || u > 1.f)
            return false;

        const Vec3f q = cross3d(from_a, edge_ab);
        const float v = dot(direction, q) * reciprocal;
        if (!std::isfinite(v) || v < 0.f || u + v > 1.f)
            return false;

        const float distance = dot(edge_ac, q) * reciprocal;
        if (!std::isfinite(distance) || distance <= 1.0e-5f || distance > max_distance)
            return false;

        out_distance = distance;
        return true;
    }

    float ConservativeBoundsScale(const ZEngine::Core::Maths::Mat4f& transform)
    {
        const Vec3f x  = {transform(0, 0), transform(1, 0), transform(2, 0)};
        const Vec3f y  = {transform(0, 1), transform(1, 1), transform(2, 1)};
        const Vec3f z  = {transform(0, 2), transform(1, 2), transform(2, 2)};
        const float xy = std::fabs(dot(x, y));
        const float xz = std::fabs(dot(x, z));
        const float yz = std::fabs(dot(y, z));
        // The maximum absolute row sum of A^T A bounds its largest eigenvalue.
        // Unlike the largest column length, this remains conservative for shear
        // introduced by a rotated child beneath a nonuniformly scaled parent.
        const float sx = dot(x, x) + xy + xz;
        const float sy = dot(y, y) + xy + yz;
        const float sz = dot(z, z) + xz + yz;
        return std::sqrt(std::fmax(sx, std::fmax(sy, sz)));
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
            .Radius     = local_radius * ConservativeBoundsScale(transform),
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

    bool RaycastIndexedTriangles(Core::Containers::ArrayView<const float> vertices, Core::Containers::ArrayView<const uint32_t> indices, const Mat4f& transform, Vec3f origin, Vec3f direction, float max_distance, float& out_distance)
    {
        if (vertices.size() < 24 || vertices.size() % 8 != 0 || indices.size() < 3 || !std::isfinite(max_distance) || max_distance <= 0.f || !IsFinite(origin))
            return false;

        Vec3f normalized_direction = {};
        if (!NormalizeRayDirection(direction, normalized_direction))
            return false;

        const uint32_t vertex_count = static_cast<uint32_t>(vertices.size() / 8);
        float          closest      = max_distance;
        bool           hit          = false;
        for (size_t index = 0; index + 2 < indices.size(); index += 3)
        {
            const uint32_t triangle[3] = {indices[index], indices[index + 1], indices[index + 2]};
            if (triangle[0] >= vertex_count || triangle[1] >= vertex_count || triangle[2] >= vertex_count)
                continue;

            const Vec3f local[3] = {
                {vertices[static_cast<size_t>(triangle[0]) * 8], vertices[static_cast<size_t>(triangle[0]) * 8 + 1], vertices[static_cast<size_t>(triangle[0]) * 8 + 2]},
                {vertices[static_cast<size_t>(triangle[1]) * 8], vertices[static_cast<size_t>(triangle[1]) * 8 + 1], vertices[static_cast<size_t>(triangle[1]) * 8 + 2]},
                {vertices[static_cast<size_t>(triangle[2]) * 8], vertices[static_cast<size_t>(triangle[2]) * 8 + 1], vertices[static_cast<size_t>(triangle[2]) * 8 + 2]},
            };
            const Vec3f a        = TransformPoint(transform, local[0]);
            const Vec3f b        = TransformPoint(transform, local[1]);
            const Vec3f c        = TransformPoint(transform, local[2]);
            float       distance = 0.f;
            if (!IntersectTriangle(origin, normalized_direction, a, b, c, closest, distance))
                continue;

            closest = distance;
            hit     = true;
        }

        if (!hit)
            return false;
        out_distance = closest;
        return true;
    }

    bool RaycastMeshGeometry(const Importers::AssetMesh& mesh, const Mat4f& transform, Vec3f origin, Vec3f direction, float max_distance, float& out_distance)
    {
        if (mesh.SubMeshes.empty())
            return RaycastIndexedTriangles({mesh.Vertices.data(), mesh.Vertices.size()}, {mesh.Indices.data(), mesh.Indices.size()}, transform, origin, direction, max_distance, out_distance);

        if (mesh.Vertices.size() % 8 != 0 || !std::isfinite(max_distance) || max_distance <= 0.f)
            return false;

        const size_t vertex_count = mesh.Vertices.size() / 8;
        float        closest      = max_distance;
        bool         hit          = false;
        for (const auto& submesh : mesh.SubMeshes)
        {
            if (submesh.VertexOffset > vertex_count || submesh.VertexCount > vertex_count - submesh.VertexOffset || submesh.IndexOffset > mesh.Indices.size() || submesh.IndexCount > mesh.Indices.size() - submesh.IndexOffset || submesh.VertexCount < 3 || submesh.IndexCount < 3)
                continue;

            float distance = 0.f;
            if (RaycastIndexedTriangles({mesh.Vertices.data() + size_t(submesh.VertexOffset) * 8, size_t(submesh.VertexCount) * 8}, {mesh.Indices.data() + submesh.IndexOffset, submesh.IndexCount}, transform, origin, direction, closest, distance))
            {
                closest = distance;
                hit     = true;
            }
        }
        if (hit)
            out_distance = closest;
        return hit;
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

    SceneRaycastHit RaycastSceneGeometry(const RenderScene& scene, Vec3f origin, Vec3f direction, float max_distance)
    {
        SceneRaycastHit result = {};
        result.Distance        = max_distance;
        if (!std::isfinite(max_distance) || max_distance <= 0.f)
            return result;

        auto* assets = Managers::AssetManager::Instance();
        if (!assets)
            return result;

        Vec3f normalized_direction = {};
        if (!NormalizeRayDirection(direction, normalized_direction))
            return result;

        // Mesh storage can be replaced during a reimport. Hold the same lock
        // used by ingestion across this click-only query so vertex/index views
        // remain valid without copying them into a transient container.
        std::lock_guard lock(assets->IngestMutex);
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
                const MeshInstance&         instance = instances[index];
                Importers::AssetMesh* const mesh     = assets->GetMeshAsset(instance.MeshUUID);
                if (!mesh || mesh->BoundsRadius <= 0.f)
                    continue;

                const SceneRaycastBounds bounds         = TransformBounds(mesh->BoundsCenter, mesh->BoundsRadius, instance.Transform, instance.Id);
                float                    broad_distance = 0.f;
                if (!IntersectSphere(origin, normalized_direction, bounds, closest.Distance, broad_distance, true))
                    continue;

                float distance = 0.f;
                if (!RaycastMeshGeometry(*mesh, instance.Transform, origin, normalized_direction, closest.Distance, distance))
                    continue;

                closest.Hit        = true;
                closest.Distance   = distance;
                closest.InstanceId = instance.Id;
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
