#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <cstdint>

namespace ZEngine::Rendering::Scenes
{
    struct RenderScene;

    /// @brief Result of a closest-hit scene ray query.
    /// @details A miss is explicit. Its Distance is the requested maximum distance,
    /// so callers that only need a bounded distance retain a useful value.
    struct SceneRaycastHit
    {
        bool     Hit        = false;
        float    Distance   = 0.0f;
        uint32_t InstanceId = 0;
    };

    /// @brief World-space bounding sphere used by the scene-query broad phase.
    struct SceneRaycastBounds
    {
        Core::Maths::Vec3f Center     = {};
        float              Radius     = 0.0f;
        uint32_t           InstanceId = 0;
    };

    /// @brief Converts a mesh-local bounding sphere to world space.
    /// @details The radius uses the largest basis scale, conservatively handling
    /// non-uniform scale. This helper does not allocate.
    [[nodiscard]] SceneRaycastBounds TransformBounds(Core::Maths::Vec3f local_center, float local_radius, const Core::Maths::Mat4f& transform, uint32_t instance_id);

    /// @brief Returns the closest positive ray/sphere hit in a supplied bounds set.
    /// @details Direction is normalized internally. The query does not allocate.
    [[nodiscard]] SceneRaycastHit    RaycastBounds(Core::Containers::ArrayView<const SceneRaycastBounds> bounds, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance);

    /// @brief Returns the closest transformed mesh-bound hit in a render scene.
    /// @details Instance iteration is seqlock-protected and mesh bounds are read
    /// under AssetManager's ingest lock. This is safe on the editor update thread
    /// and has no allocations on the camera update path.
    [[nodiscard]] SceneRaycastHit    RaycastSceneBounds(const RenderScene& scene, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance);

    /// @brief Retrieves one selected instance's transformed mesh bounds.
    /// @details The output is only modified on success. Instance access uses the
    /// scene seqlock and mesh bounds are copied under AssetManager's ingest lock.
    /// This query does not allocate.
    [[nodiscard]] bool               TryGetSceneInstanceBounds(const RenderScene& scene, uint32_t instance_id, SceneRaycastBounds& out_bounds);

    /// @brief Retrieves one conservative sphere enclosing every frameable scene mesh.
    /// @details The output is only modified on success. Instance access uses the
    /// scene seqlock and mesh bounds are copied under AssetManager's ingest lock.
    /// This query does not allocate.
    [[nodiscard]] bool               TryGetSceneBounds(const RenderScene& scene, SceneRaycastBounds& out_bounds);
} // namespace ZEngine::Rendering::Scenes
