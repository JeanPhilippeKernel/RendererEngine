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
