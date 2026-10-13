#include <ZEngine/Rendering/Scenes/SceneRayQuery.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Scenes;

TEST(SceneRayQueryTest, ReturnsTheClosestPositiveBoundsHit)
{
    const SceneRaycastBounds bounds[] = {
        {.Center = {0.0f, 0.0f, -8.0f}, .Radius = 2.0f, .InstanceId = 10},
        {.Center = {0.0f, 0.0f, -3.0f}, .Radius = 0.5f, .InstanceId = 20},
        { .Center = {0.0f, 0.0f, 2.0f}, .Radius = 1.0f, .InstanceId = 30},
    };

    const SceneRaycastHit hit = RaycastBounds({bounds, 3}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 100.0f);

    EXPECT_TRUE(hit.Hit);
    EXPECT_EQ(hit.InstanceId, 20u);
    EXPECT_NEAR(hit.Distance, 2.5f, 1e-6f);
}

TEST(SceneRayQueryTest, MissIsExplicitAndRetainsTheRequestedMaximumDistance)
{
    const SceneRaycastBounds bounds[] = {
        {.Center = {0.0f, 0.0f, -5.0f}, .Radius = 1.0f, .InstanceId = 10},
    };

    const SceneRaycastHit hit = RaycastBounds({bounds, 1}, {0.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 12.0f);

    EXPECT_FALSE(hit.Hit);
    EXPECT_EQ(hit.InstanceId, 0u);
    EXPECT_FLOAT_EQ(hit.Distance, 12.0f);
}

TEST(SceneRayQueryTest, StartsInsideBoundsAtThePositiveExitDistance)
{
    const SceneRaycastBounds bounds[] = {
        {.Center = {0.0f, 0.0f, 0.0f}, .Radius = 2.0f, .InstanceId = 7},
    };

    const SceneRaycastHit hit = RaycastBounds({bounds, 1}, {0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 12.0f);

    EXPECT_TRUE(hit.Hit);
    EXPECT_EQ(hit.InstanceId, 7u);
    EXPECT_NEAR(hit.Distance, 2.0f, 1e-6f);
}

TEST(SceneRayQueryTest, NormalizesItsInputDirection)
{
    const SceneRaycastBounds bounds[] = {
        {.Center = {0.0f, 0.0f, -9.0f}, .Radius = 1.0f, .InstanceId = 4},
    };

    const SceneRaycastHit hit = RaycastBounds({bounds, 1}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -4.0f}, 12.0f);

    EXPECT_TRUE(hit.Hit);
    EXPECT_NEAR(hit.Distance, 8.0f, 1e-6f);
}

TEST(SceneRayQueryTest, TransformsLocalBoundsWithTranslationAndLargestScale)
{
    Mat4f transform                 = Identity<Mat4f>();
    transform(0, 0)                 = 2.0f;
    transform(1, 1)                 = 0.5f;
    transform(2, 2)                 = 3.0f;
    transform(0, 3)                 = 10.0f;
    transform(1, 3)                 = -4.0f;
    transform(2, 3)                 = 2.0f;

    const SceneRaycastBounds bounds = TransformBounds({1.0f, 2.0f, -1.0f}, 4.0f, transform, 42);

    EXPECT_EQ(bounds.InstanceId, 42u);
    EXPECT_NEAR(bounds.Center.x, 12.0f, 1e-6f);
    EXPECT_NEAR(bounds.Center.y, -3.0f, 1e-6f);
    EXPECT_NEAR(bounds.Center.z, -1.0f, 1e-6f);
    EXPECT_NEAR(bounds.Radius, 12.0f, 1e-6f);
}

TEST(SceneRayQueryTest, IgnoresInvalidBounds)
{
    const SceneRaycastBounds bounds[] = {
        {.Center = {std::numeric_limits<float>::quiet_NaN(), 0.0f, -2.0f}, .Radius = 1.0f, .InstanceId = 1},
        {                                   .Center = {0.0f, 0.0f, -5.0f}, .Radius = 1.0f, .InstanceId = 2},
    };

    const SceneRaycastHit hit = RaycastBounds({bounds, 2}, {0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, -1.0f}, 10.0f);

    EXPECT_TRUE(hit.Hit);
    EXPECT_EQ(hit.InstanceId, 2u);
    EXPECT_NEAR(hit.Distance, 4.0f, 1e-6f);
}

TEST(SceneRayQueryTest, IntersectsIndexedMeshTrianglesInsteadOfTheirBoundingVolume)
{
    // The ray passes through this triangle's enclosing sphere but outside the
    // triangle itself. Bounds-only selection would produce a false positive.
    const float vertices[] = {
        -1.f, 1.f, -4.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, 1.f, -4.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 2.f, -4.f, 0.f, 0.f, 1.f, 0.f, 0.f,
    };
    const uint32_t indices[] = {0, 1, 2};
    float          distance  = 17.f;
    EXPECT_FALSE(RaycastIndexedTriangles({vertices, 24}, {indices, 3}, Identity<Mat4f>(), {0.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, 20.f, distance));
    EXPECT_FLOAT_EQ(distance, 17.f);
}

TEST(SceneRayQueryTest, ReturnsTheNearestTransformedMeshTriangle)
{
    const float vertices[] = {
        -1.f, -1.f, -2.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, -1.f, -2.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, -2.f, 0.f, 0.f, 1.f, 0.f, 0.f, -1.f, -1.f, -5.f, 0.f, 0.f, 1.f, 0.f, 0.f, 1.f, -1.f, -5.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, -5.f, 0.f, 0.f, 1.f, 0.f, 0.f,
    };
    const uint32_t indices[] = {0, 1, 2, 3, 4, 5};
    Mat4f          transform = Identity<Mat4f>();
    transform(0, 3)          = 2.f;

    float distance           = 0.f;
    ASSERT_TRUE(RaycastIndexedTriangles({vertices, 48}, {indices, 6}, transform, {2.f, 0.f, 0.f}, {0.f, 0.f, -2.f}, 20.f, distance));
    EXPECT_NEAR(distance, 2.f, 1.0e-5f);
}
