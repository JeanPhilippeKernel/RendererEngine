#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Vec.h>
#include <cstdint>

namespace ZEngine::Importers
{
    struct AssetMesh;
}

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
    /// @details The radius conservatively bounds non-uniform scale and hierarchy
    /// shear using an upper bound on the linear transform's maximum stretch.
    /// This helper does not allocate.
    [[nodiscard]] SceneRaycastBounds TransformBounds(Core::Maths::Vec3f local_center, float local_radius, const Core::Maths::Mat4f& transform, uint32_t instance_id);

    /// @brief Returns the closest positive ray/sphere hit in a supplied bounds set.
    /// @details Direction is normalized internally. The query does not allocate.
    [[nodiscard]] SceneRaycastHit    RaycastBounds(Core::Containers::ArrayView<const SceneRaycastBounds> bounds, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance);

    /// @brief Returns the closest positive triangle hit in indexed engine mesh data.
    /// @details Vertices use the engine's eight-float layout (position xyz,
    /// normal xyz, uv xy). The function performs no allocation and leaves
    /// @p out_distance unchanged on failure. It is intended for exact editor
    /// selection after a cheap bounds broad phase.
    [[nodiscard]] bool               RaycastIndexedTriangles(Core::Containers::ArrayView<const float> vertices, Core::Containers::ArrayView<const uint32_t> indices, const Core::Maths::Mat4f& transform, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance, float& out_distance);

    /// @brief Queries a mesh with submesh-local indices, honoring each vertex/index range.
    /// @details Invalid ranges are skipped without reading outside asset storage. A mesh
    /// without submeshes is treated as one mesh-global indexed triangle list.
    [[nodiscard]] bool               RaycastMeshGeometry(const Importers::AssetMesh& mesh, const Core::Maths::Mat4f& transform, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance, float& out_distance);

    /// @brief Returns the closest transformed mesh-bound hit in a render scene.
    /// @details Instance iteration is seqlock-protected and mesh bounds are read
    /// under AssetManager's ingest lock. This is safe on the editor update thread
    /// and has no allocations on the camera update path.
    [[nodiscard]] SceneRaycastHit    RaycastSceneBounds(const RenderScene& scene, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance);

    /// @brief Returns the closest exact triangle hit in a render scene.
    /// @details Mesh asset access is protected against concurrent importing and
    /// scene traversal is seqlock-protected. This query runs only on explicit
    /// selection clicks; it does not allocate or stall the GPU.
    [[nodiscard]] SceneRaycastHit    RaycastSceneGeometry(const RenderScene& scene, Core::Maths::Vec3f origin, Core::Maths::Vec3f direction, float max_distance);

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
