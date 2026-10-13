#include <ZEngine/ECS/Tools/GizmoMath.h>
#include <gtest/gtest.h>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS::Tools;

TEST(GizmoMathTest, IntersectsForwardRayWithConstraintPlane)
{
    const GizmoRay ray = {
        {0.f,  2.f, 0.f},
        {0.f, -1.f, 0.f}
    };
    const GizmoPlane plane = {
        {0.f, 0.f, 0.f},
        {0.f, 1.f, 0.f}
    };
    Vec3f point = {};
    ASSERT_TRUE(TryIntersectRayPlane(ray, plane, point));
    EXPECT_FLOAT_EQ(point.x, 0.f);
    EXPECT_FLOAT_EQ(point.y, 0.f);
    EXPECT_FLOAT_EQ(point.z, 0.f);
}

TEST(GizmoMathTest, RejectsParallelAndBackwardPlaneIntersections)
{
    Vec3f point = {1.f, 2.f, 3.f};
    EXPECT_FALSE(TryIntersectRayPlane(
        {
        {0.f, 1.f, 0.f},
        {1.f, 0.f, 0.f}
    },
        {{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
        point));
    EXPECT_FLOAT_EQ(point.x, 1.f);
    EXPECT_FLOAT_EQ(point.y, 2.f);
    EXPECT_FLOAT_EQ(point.z, 3.f);
    EXPECT_FALSE(TryIntersectRayPlane(
        {
        {0.f, -1.f, 0.f},
        {0.f, -1.f, 0.f}
    },
        {{0.f, 0.f, 0.f}, {0.f, 1.f, 0.f}},
        point));
}

TEST(GizmoMathTest, ComputesClosestAxisParameterFromCursorRay)
{
    float parameter = 0.f;
    ASSERT_TRUE(TryClosestRayAxisParameter(
        {
        {3.f, 2.f,  5.f},
        {0.f, 0.f, -1.f}
    },
        {0.f, 0.f, 0.f},
        {1.f, 0.f, 0.f},
        parameter));
    EXPECT_NEAR(parameter, 3.f, 1.0e-5f);
    EXPECT_FALSE(TryClosestRayAxisParameter(
        {
        {0.f, 2.f, 0.f},
        {1.f, 0.f, 0.f}
    },
        {},
        {1.f, 0.f, 0.f},
        parameter));
}

TEST(GizmoMathTest, ComputesSignedAxisAngleAndStableScaleRatio)
{
    float angle = 0.f;
    ASSERT_TRUE(TrySignedAxisAngle({0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, angle));
    EXPECT_NEAR(angle, 1.5707963f, 1.0e-5f);

    float ratio = 0.f;
    ASSERT_TRUE(TryGizmoScaleRatio(2.f, 3.f, ratio));
    EXPECT_NEAR(ratio, 1.5f, 1.0e-6f);
    EXPECT_FALSE(TryGizmoScaleRatio(0.f, 3.f, ratio));
}

TEST(GizmoMathTest, SnapsOnlyWithAValidEnabledIncrement)
{
    EXPECT_NEAR(SnapGizmoScalar(1.24f, 0.5f, true), 1.0f, 1.0e-6f);
    EXPECT_NEAR(SnapGizmoScalar(-1.26f, 0.5f, true), -1.5f, 1.0e-6f);
    EXPECT_NEAR(SnapGizmoScalar(1.24f, 0.f, true), 1.24f, 1.0e-6f);
    EXPECT_NEAR(SnapGizmoScalar(1.24f, 0.5f, false), 1.24f, 1.0e-6f);
}

TEST(GizmoMathTest, ConvertsWorldTransformToStrictParentRelativeTRS)
{
    const Mat4f         parent         = ComposeTransformMatrix({2.f, 1.f, -3.f}, {0.1f, -0.25f, 0.2f}, {1.5f, 1.5f, 1.5f});
    const Mat4f         expected_local = ComposeTransformMatrix({3.f, 4.f, 5.f}, {-0.1f, 0.5f, 0.25f}, {2.f, 0.5f, 1.f});
    const Mat4f         desired_world  = parent * expected_local;

    GizmoLocalTransform result         = {};
    ASSERT_TRUE(TryWorldToLocalGizmoTransform(desired_world, &parent, result));
    const Mat4f reconstructed = ComposeTransformMatrix(result.Position, result.Rotation, result.Scale);
    for (size_t row = 0; row < 4; ++row)
        for (size_t column = 0; column < 4; ++column)
            EXPECT_NEAR(reconstructed(row, column), expected_local(row, column), 1.0e-4f);
}

TEST(GizmoMathTest, RejectsWorldTransformThatWouldBecomeLocalShear)
{
    const Mat4f         parent       = ComposeTransformMatrix({}, {}, {2.f, 1.f, 1.f});
    const Mat4f         world_rotate = ComposeTransformMatrix({}, {0.f, 0.f, 0.5f}, {1.f, 1.f, 1.f});

    GizmoLocalTransform result       = {
        .Position = {7.f, 8.f, 9.f},
        .Rotation = {1.f, 2.f, 3.f},
        .Scale    = {4.f, 5.f, 6.f},
    };
    EXPECT_FALSE(TryWorldToLocalGizmoTransform(world_rotate * parent, &parent, result));
    EXPECT_FLOAT_EQ(result.Position.x, 7.f);
    EXPECT_FLOAT_EQ(result.Position.y, 8.f);
    EXPECT_FLOAT_EQ(result.Position.z, 9.f);
}
