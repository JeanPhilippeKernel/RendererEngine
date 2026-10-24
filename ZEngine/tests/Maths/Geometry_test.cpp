#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <array>
#include <cmath>
#include <limits>
#include <random>

using namespace ZEngine::Core::Maths;
namespace G = ZEngine::Core::Maths::Geometry;
using namespace ZEngine::Rendering::Cameras;

TEST(GeometryBoxTest, EightBitIndexedCornersPreserveRotationReflectionNonuniformScaleAndShear)
{
    auto world   = ComposeTransformMatrix({7.f, -8.f, 9.f}, {.3f, -.7f, 1.1f}, {-2.f, 3.f, .5f});
    world(0, 1) += .6f;
    const Vec3f   minimum(-4.f, 2.f, -3.f), maximum(6.f, 5.f, 9.f);
    G::BoxCorners corners = {};
    ASSERT_TRUE(G::TransformBox(minimum, maximum, world, &corners));
    for (uint32_t corner = 0; corner < 8; ++corner)
        for (uint32_t row = 0; row < 3; ++row)
        {
            double expected = world(row, 3);
            for (uint32_t axis = 0; axis < 3; ++axis)
                expected += double(world(row, axis)) * (corner & (1u << axis) ? maximum[axis] : minimum[axis]);
            EXPECT_FLOAT_EQ(corners[corner][row], float(expected));
            EXPECT_EQ(corners[corner].w, 0.f);
        }
}

TEST(GeometryBoxTest, PointLinePlanarAndSingularAffineBoxesAreValid)
{
    for (const Vec3f maximum : {Vec3f(1.f, 2.f, 3.f), Vec3f(4.f, 2.f, 3.f), Vec3f(4.f, 5.f, 3.f)})
    {
        G::BoxCorners output = {};
        ASSERT_TRUE(G::TransformBox({1.f, 2.f, 3.f}, maximum, Identity<Mat4f>(), &output));
        EXPECT_EQ(output[0].x, 1.f);
        EXPECT_EQ(output[7].x, maximum.x);
    }
    auto world           = Identity<Mat4f>();
    world(0, 0)          = 0.f;
    G::BoxCorners output = {};
    ASSERT_TRUE(G::TransformBox({-1.f, -2.f, -3.f}, {1.f, 2.f, 3.f}, world, &output));
    for (const auto& point : output)
        EXPECT_EQ(point.x, 0.f);
}

TEST(GeometryBoxTest, InvalidBoundsMatrixOverflowAndNullPreserveTheCompleteOutput)
{
    G::BoxCorners output = {};
    for (auto& point : output)
        point = {11.f, 12.f, 13.f};
    auto reject = [&](const Vec3f& minimum, const Vec3f& maximum, const Mat4f& world) {
        EXPECT_FALSE(G::TransformBox(minimum, maximum, world, &output));
        for (const auto& point : output)
        {
            EXPECT_EQ(point.x, 11.f);
            EXPECT_EQ(point.y, 12.f);
            EXPECT_EQ(point.z, 13.f);
        }
    };
    reject({1.f, 2.f, 3.f}, {0.f, 4.f, 5.f}, Identity<Mat4f>());
    reject({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f}, {1.f, 1.f, 1.f}, Identity<Mat4f>());
    auto world  = Identity<Mat4f>();
    world(3, 0) = 1.f;
    reject({}, {1.f, 1.f, 1.f}, world);
    world       = Identity<Mat4f>();
    world(1, 2) = std::numeric_limits<float>::infinity();
    reject({}, {1.f, 1.f, 1.f}, world);
    world       = Identity<Mat4f>();
    world(0, 0) = std::numeric_limits<float>::max();
    reject({}, {2.f, 1.f, 1.f}, world);
    EXPECT_FALSE(G::TransformBox({}, {1.f, 1.f, 1.f}, Identity<Mat4f>(), nullptr));
}

namespace
{
    constexpr float   Tolerance      = 1e-4f;
    constexpr float   ShearTolerance = 1e-3f;
    const G::Viewport Viewport       = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    G::ProjectionFrame Frame(bool orthographic = false, bool reverse_z = false, bool infinite_far = false)
    {
        CameraSetting settings      = {};
        settings.FOV                = 60.f;
        settings.NearPlane          = 0.1f;
        settings.FarPlane           = 1000.f;
        settings.OrthographicHeight = 10.f;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetViewportSize(800.f, 600.f);
        camera.SetPosition({0.f, 0.f, 10.f});
        camera.SetOrientation(0.f, 0.f);
        if (orthographic)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto captured = camera.CaptureFrameData();
        if (infinite_far)
        {
            captured.Projection(2, 2) = -1.f;
            captured.Projection(2, 3) = -settings.NearPlane;
        }
        if (reverse_z)
            for (uint32_t col = 0; col < 4; ++col)
                captured.Projection(2, col) = captured.Projection(3, col) - captured.Projection(2, col);
        G::ProjectionFrame result = {};
        EXPECT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse_z, &result));
        return result;
    }
    void Near(const Vec3f& a, const Vec3f& b, float tolerance = Tolerance)
    {
        EXPECT_NEAR(a.x, b.x, tolerance);
        EXPECT_NEAR(a.y, b.y, tolerance);
        EXPECT_NEAR(a.z, b.z, tolerance);
    }
    void Near(const Mat4f& a, const Mat4f& b, float tolerance = Tolerance)
    {
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t col = 0; col < 4; ++col)
                EXPECT_NEAR(a(row, col), b(row, col), tolerance) << row << "," << col;
    }
    Mat4f Compose(const G::LocalTRS& value)
    {
        return ComposeTransformMatrix(value.Position, value.Rotation, value.Scale);
    }
} // namespace

TEST(GeometryTest, RecordsHaveDeliberateDefaults)
{
    G::Ray ray = {};
    Near(ray.Origin, {});
    Near(ray.Direction, {0.f, 0.f, -1.f});
    G::ProjectionFrame frame = {};
    EXPECT_FALSE(frame.Valid);
    EXPECT_FALSE(frame.ReverseZ);
    EXPECT_EQ(frame.ProjectionYScale, 0.f);
    Near(frame.ViewProjection, {});
    G::LocalTRS trs = {};
    Near(trs.Position, {});
    Near(trs.Rotation, {});
    Near(trs.Scale, {1.f, 1.f, 1.f});
}

TEST(GeometryTest, RealEngineCameraProjectsPositiveXRightAndPositiveYUp)
{
    for (bool orthographic : {false, true})
    {
        const auto        frame  = Frame(orthographic);
        G::ProjectedPoint center = {}, right = {}, up = {};
        ASSERT_TRUE(G::Project({}, frame, Viewport, &center));
        ASSERT_TRUE(G::Project({1.f, 0.f, 0.f}, frame, Viewport, &right));
        ASSERT_TRUE(G::Project({0.f, 1.f, 0.f}, frame, Viewport, &up));
        EXPECT_NEAR(center.Screen.x, 437.f, Tolerance);
        EXPECT_NEAR(center.Screen.y, 361.f, Tolerance);
        EXPECT_GT(right.Screen.x, center.Screen.x);
        EXPECT_LT(up.Screen.y, center.Screen.y);
        EXPECT_GT(center.Depth, 0.f);
        EXPECT_LT(center.Depth, 1.f);
    }
}

TEST(GeometryTest, ProjectionRoundTripsAtAllViewportCornersAndCenter)
{
    const std::array<Vec2f, 5> points = {
        {{37.f, 61.f}, {837.f, 61.f}, {37.f, 661.f}, {837.f, 661.f}, {437.f, 361.f}}
    };
    for (bool orthographic : {false, true})
        for (bool reverse_z : {false, true})
        {
            const auto frame = Frame(orthographic, reverse_z);
            for (const auto& screen : points)
            {
                Vec3f world = {};
                ASSERT_TRUE(G::UnprojectAtDepth(screen, 0.25f, frame, Viewport, &world));
                G::ProjectedPoint projected = {};
                ASSERT_TRUE(G::Project(world, frame, Viewport, &projected));
                EXPECT_NEAR(projected.Screen.x, screen.x, 0.005f);
                EXPECT_NEAR(projected.Screen.y, screen.y, 0.005f);
                EXPECT_NEAR(projected.Depth, 0.25f, 1e-5f);
            }
        }
}

TEST(GeometryTest, PerspectiveRaysUseCameraOriginAndPassThroughTheirScreenPoints)
{
    for (bool reverse_z : {false, true})
    {
        const auto frame = Frame(false, reverse_z);
        for (const Vec2f screen : {Vec2f(437.f, 361.f), Vec2f(37.f, 61.f), Vec2f(837.f, 661.f)})
        {
            G::Ray ray = {};
            ASSERT_TRUE(G::UnprojectRay(screen, frame, Viewport, &ray));
            Near(ray.Origin, frame.Position);
            EXPECT_NEAR(dot(ray.Direction, ray.Direction), 1.f, Tolerance);
            EXPECT_LT(ray.Direction.z, 0.f);
            G::ProjectedPoint projected = {};
            ASSERT_TRUE(G::Project(ray.Origin + ray.Direction * 5.f, frame, Viewport, &projected));
            EXPECT_NEAR(projected.Screen.x, screen.x, 0.01f);
            EXPECT_NEAR(projected.Screen.y, screen.y, 0.01f);
        }
    }
}

TEST(GeometryTest, PerspectiveRayDirectionIsStableAcrossSmallNearPlanesAndTranslatedCameras)
{
    for (const Vec3f position : {Vec3f(7.f, 5.f, 10.f), Vec3f(700.f, 500.f, 1000.f)})
        for (float near_plane : {.1f, .01f, .001f})
            for (float far_plane : {1000.f, 1000000.f})
                for (bool reverse : {false, true})
                {
                    SCOPED_TRACE(testing::Message() << position.x << " near=" << near_plane << " far=" << far_plane << " reverse=" << reverse);
                    CameraSetting settings = {};
                    settings.NearPlane     = near_plane;
                    settings.FarPlane      = far_plane;
                    FlyCamera camera(4.f / 3.f, settings);
                    camera.SetPosition(position);
                    camera.SetOrientation(25.f, -35.f);
                    auto source = camera.CaptureFrameData();
                    if (reverse)
                        for (uint32_t column = 0; column < 4; ++column)
                            source.Projection(2, column) = source.Projection(3, column) - source.Projection(2, column);
                    G::ProjectionFrame frame = {};
                    ASSERT_TRUE(G::MakeProjectionFrame(source.View, source.Projection, source.Position, G::ProjectionKind::Perspective, reverse, &frame));
                    for (const Vec2f screen : {Vec2f(437.f, 361.f), Vec2f(37.f, 61.f), Vec2f(1037.f, -139.f)})
                    {
                        // Independent camera-basis reference; no inverse VP or
                        // reconstructed near-world point enters this calculation.
                        const double x           = (2.0 * (double(screen.x) - Viewport.Origin.x) / Viewport.Size.x - 1.0) / source.Projection(0, 0);
                        const double y           = (2.0 * (double(screen.y) - Viewport.Origin.y) / Viewport.Size.y - 1.0) / source.Projection(1, 1);
                        double       expected[3] = {};
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            expected[axis] = x * source.View(0, axis) + y * source.View(1, axis) - source.View(2, axis);
                        const double length = std::hypot(expected[0], expected[1], expected[2]);
                        G::Ray       ray    = {};
                        ASSERT_TRUE(G::UnprojectRay(screen, frame, Viewport, &ray));
                        Near(ray.Origin, source.Position, 0.f);
                        for (uint32_t axis = 0; axis < 3; ++axis)
                            EXPECT_NEAR(ray.Direction[axis], expected[axis] / length, 5e-7);
                    }
                }
}

TEST(GeometryTest, PerspectiveRaysRespectOffCenterAndMirroredProjection)
{
    for (float mirror : {-1.f, 1.f})
        for (bool reverse : {false, true})
        {
            FlyCamera camera(4.f / 3.f, {});
            camera.SetPosition({7.f, 5.f, 10.f});
            camera.SetOrientation(25.f, -35.f);
            auto source              = camera.CaptureFrameData();
            source.Projection(0, 0) *= mirror;
            source.Projection(0, 2)  = .35f;
            source.Projection(1, 2)  = -.2f;
            if (reverse)
                for (uint32_t column = 0; column < 4; ++column)
                    source.Projection(2, column) = source.Projection(3, column) - source.Projection(2, column);
            G::ProjectionFrame frame = {};
            ASSERT_TRUE(G::MakeProjectionFrame(source.View, source.Projection, source.Position, G::ProjectionKind::Perspective, reverse, &frame));
            for (const Vec2f screen : {Vec2f(437.f, 361.f), Vec2f(37.f, 61.f), Vec2f(837.f, 661.f)})
            {
                const double x           = (2.0 * (double(screen.x) - Viewport.Origin.x) / Viewport.Size.x - 1.0 + source.Projection(0, 2)) / source.Projection(0, 0);
                const double y           = (2.0 * (double(screen.y) - Viewport.Origin.y) / Viewport.Size.y - 1.0 + source.Projection(1, 2)) / source.Projection(1, 1);
                double       expected[3] = {};
                for (uint32_t axis = 0; axis < 3; ++axis)
                    expected[axis] = x * source.View(0, axis) + y * source.View(1, axis) - source.View(2, axis);
                const double length = std::hypot(expected[0], expected[1], expected[2]);
                G::Ray       ray    = {};
                ASSERT_TRUE(G::UnprojectRay(screen, frame, Viewport, &ray));
                for (uint32_t axis = 0; axis < 3; ++axis)
                    EXPECT_NEAR(ray.Direction[axis], expected[axis] / length, 5e-7);
            }
        }
}

TEST(GeometryTest, MalformedPerspectiveRayInputsPreserveTheCompleteOutput)
{
    for (uint32_t invalid = 0; invalid < 5; ++invalid)
    {
        auto frame = Frame();
        if (invalid == 0)
            frame.Position.x = std::numeric_limits<float>::infinity();
        else if (invalid == 1)
            frame.Forward = {};
        else if (invalid == 2)
            frame.ViewProjection(0, 0) = std::numeric_limits<float>::quiet_NaN();
        else if (invalid == 3)
            for (uint32_t column = 0; column < 4; ++column)
                frame.ViewProjection(0, column) = 0.f;
        else
            frame.Kind = static_cast<G::ProjectionKind>(255);
        G::Ray ray = {
            .Origin = {11.f, 12.f, 13.f},
              .Direction = {21.f, 22.f, 23.f}
        };
        EXPECT_FALSE(G::UnprojectRay({437.f, 361.f}, frame, Viewport, &ray)) << invalid;
        Near(ray.Origin, {11.f, 12.f, 13.f}, 0.f);
        Near(ray.Direction, {21.f, 22.f, 23.f}, 0.f);
    }
}

TEST(GeometryTest, OrthographicRaysUseSeparateNearPlaneOriginsAndParallelDirections)
{
    for (bool reverse_z : {false, true})
    {
        const auto frame  = Frame(true, reverse_z);
        G::Ray     center = {}, corner = {};
        ASSERT_TRUE(G::UnprojectRay({437.f, 361.f}, frame, Viewport, &center));
        ASSERT_TRUE(G::UnprojectRay({837.f, 61.f}, frame, Viewport, &corner));
        Near(center.Direction, {0.f, 0.f, -1.f});
        Near(corner.Direction, center.Direction);
        EXPECT_NEAR(center.Origin.z, 9.9f, Tolerance);
        // Switching projection preserves apparent scale at the camera's default
        // 10-unit orbit distance, rather than retaining OrthographicHeight.
        EXPECT_NEAR(corner.Origin.y, 10.f * std::tan(radians(30.f)), Tolerance);
        EXPECT_GT(corner.Origin.x, center.Origin.x);
    }
}

TEST(GeometryTest, InfiniteFarPerspectiveRaysWorkWithBothDepthConventions)
{
    for (bool reverse_z : {false, true})
    {
        const auto frame = Frame(false, reverse_z, true);
        G::Ray     ray   = {};
        ASSERT_TRUE(G::UnprojectRay({437.f, 361.f}, frame, Viewport, &ray));
        Near(ray.Direction, {0.f, 0.f, -1.f});
        Vec3f near = {};
        ASSERT_TRUE(G::UnprojectAtDepth({437.f, 361.f}, reverse_z ? 1.f : 0.f, frame, Viewport, &near));
        EXPECT_NEAR(near.z, 9.9f, Tolerance);
    }
}

TEST(GeometryTest, ReverseZChangesDepthButNotScreenPosition)
{
    const auto        standard = Frame();
    const auto        reversed = Frame(false, true);
    G::ProjectedPoint a = {}, b = {};
    ASSERT_TRUE(G::Project({1.f, 2.f, 0.f}, standard, Viewport, &a));
    ASSERT_TRUE(G::Project({1.f, 2.f, 0.f}, reversed, Viewport, &b));
    EXPECT_NEAR(a.Screen.x, b.Screen.x, Tolerance);
    EXPECT_NEAR(a.Screen.y, b.Screen.y, Tolerance);
    EXPECT_NEAR(a.Depth + b.Depth, 1.f, Tolerance);
}

TEST(GeometryTest, ProjectionUsesLogicalViewportSizeWithoutClampingOffscreenPoints)
{
    const auto        frame = Frame();
    G::ProjectedPoint a = {}, b = {};
    ASSERT_TRUE(
        G::Project(
            {
            1.f, 2.f, 0.f
    },
            frame,
            {{}, {800.f, 600.f}},
            &a));
    ASSERT_TRUE(
        G::Project(
            {
            1.f, 2.f, 0.f
    },
            frame,
            {{}, {1600.f, 1200.f}},
            &b));
    EXPECT_NEAR(b.Screen.x, a.Screen.x * 2.f, Tolerance);
    EXPECT_NEAR(b.Screen.y, a.Screen.y * 2.f, Tolerance);
    ASSERT_TRUE(G::Project({100.f, 0.f, 0.f}, frame, Viewport, &a));
    EXPECT_GT(a.Screen.x, Viewport.Origin.x + Viewport.Size.x);
}

TEST(GeometryTest, BehindCameraAndDegenerateViewportPreserveOutput)
{
    const auto        frame = Frame();
    G::ProjectedPoint point = {
        .Screen = {23.f, 31.f},
          .Depth = 17.f
    };
    EXPECT_FALSE(G::Project({0.f, 0.f, 20.f}, frame, Viewport, &point));
    EXPECT_FALSE(G::Project({}, frame, {}, &point));
    EXPECT_EQ(point.Screen.x, 23.f);
    EXPECT_EQ(point.Depth, 17.f);
    G::Ray ray = {
        .Origin = {1.f, 2.f, 3.f}
    };
    EXPECT_FALSE(
        G::UnprojectRay(
            {
    },
            frame,
            {{}, {-1.f, 1.f}},
            &ray));
    Near(ray.Origin, {1.f, 2.f, 3.f});
}

TEST(GeometryTest, InvalidCameraCannotProduceAProjectionFrame)
{
    G::ProjectionFrame frame = {
        .Position = {9.f, 8.f, 7.f}
    };
    EXPECT_FALSE(G::MakeProjectionFrame(Identity<Mat4f>(), {}, {}, G::ProjectionKind::Perspective, false, &frame));
    EXPECT_FALSE(frame.Valid);
    Near(frame.Position, {9.f, 8.f, 7.f});
    Mat4f invalid = Identity<Mat4f>();
    invalid(0, 0) = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(G::MakeProjectionFrame(invalid, Identity<Mat4f>(), {}, G::ProjectionKind::Perspective, false, &frame));
    EXPECT_FALSE(G::MakeProjectionFrame(Identity<Mat4f>(), Identity<Mat4f>(), {}, static_cast<G::ProjectionKind>(42), false, &frame));
}

TEST(GeometryTest, PerspectiveWorldPerPixelDependsOnDepthAndViewportHeight)
{
    const auto frame = Frame();
    float      near = 0.f, far = 0.f, tall = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(frame, {}, 600.f, &near));
    ASSERT_TRUE(G::WorldPerPixel(frame, {0.f, 0.f, -10.f}, 600.f, &far));
    ASSERT_TRUE(G::WorldPerPixel(frame, {}, 1200.f, &tall));
    EXPECT_NEAR(far, near * 2.f, Tolerance);
    EXPECT_NEAR(tall, near * 0.5f, Tolerance);
    G::ProjectedPoint origin = {}, tip = {};
    ASSERT_TRUE(G::Project({}, frame, Viewport, &origin));
    ASSERT_TRUE(G::Project({120.f * near, 0.f, 0.f}, frame, Viewport, &tip));
    EXPECT_NEAR(tip.Screen.x - origin.Screen.x, 120.f, Tolerance);
}

TEST(GeometryTest, OrthographicWorldPerPixelIsIndependentOfDepth)
{
    const auto frame = Frame(true);
    float      near = 0.f, far = 0.f;
    ASSERT_TRUE(G::WorldPerPixel(frame, {}, 600.f, &near));
    ASSERT_TRUE(G::WorldPerPixel(frame, {0.f, 0.f, -100.f}, 600.f, &far));
    EXPECT_NEAR(near, (20.f * std::tan(radians(30.f))) / 600.f, Tolerance);
    EXPECT_EQ(near, far);
}

TEST(GeometryTest, InvalidWorldPerPixelPreservesPreviousValue)
{
    float result = 19.f;
    EXPECT_FALSE(G::WorldPerPixel({}, {}, 600.f, &result));
    EXPECT_FALSE(G::WorldPerPixel(Frame(), {0.f, 0.f, 20.f}, 600.f, &result));
    EXPECT_FALSE(G::WorldPerPixel(Frame(), {}, 0.f, &result));
    EXPECT_EQ(result, 19.f);
}

TEST(GeometryTest, CheckedInverseHandlesPivotingAndProjectiveMatrices)
{
    const Mat4f matrix(0.f, 2.f, 0.f, 3.f, 4.f, 0.f, 0.f, 1.f, 0.f, 0.f, 5.f, 6.f, 0.f, 0.f, 0.f, 1.f);
    Mat4f       inverse = {};
    ASSERT_TRUE(G::TryInverse(matrix, &inverse));
    Near(matrix * inverse, Identity<Mat4f>());
    const auto frame = Frame();
    ASSERT_TRUE(G::TryInverse(frame.ViewProjection, &inverse));
    Near(frame.ViewProjection * inverse, Identity<Mat4f>());
}

TEST(GeometryTest, CheckedInverseRejectsSingularNonfiniteAndOverflowWithoutAsserting)
{
    Mat4f result = Identity<Mat4f>();
    EXPECT_FALSE(G::TryInverse({}, &result));
    Near(result, Identity<Mat4f>());
    auto matrix  = Identity<Mat4f>();
    matrix(2, 2) = 0.f;
    EXPECT_FALSE(G::TryInverse(matrix, &result));
    matrix(2, 2) = std::numeric_limits<float>::denorm_min();
    EXPECT_FALSE(G::TryInverse(matrix, &result));
    matrix(2, 2) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(G::TryInverse(matrix, &result));
    Near(result, Identity<Mat4f>());
}

TEST(GeometryTest, ClosestAxisPointNormalizesDirectionsAndMeasuresWorldLengths)
{
    G::AxisPoint point = {};
    ASSERT_TRUE(
        G::ClosestPointOnAxis(
            {
            {3.f, 2.f, 10.f},
            {0.f, 0.f, -7.f}
    },
            {},
            {5.f, 0.f, 0.f},
            1e-4f,
            &point));
    EXPECT_NEAR(point.AxisParameter, 3.f, Tolerance);
    EXPECT_NEAR(point.RayParameter, 10.f, Tolerance);
    Near(point.Position, {3.f, 0.f, 0.f});
}

TEST(GeometryTest, AxisParallelGuardRetainsThePreviousPreview)
{
    G::AxisPoint point = {.AxisParameter = 11.f};
    EXPECT_FALSE(
        G::ClosestPointOnAxis(
            {
            {},
            {1.f, 0.f, 0.f}
    },
            {},
            {1.f, 0.f, 0.f},
            1e-4f,
            &point));
    EXPECT_FALSE(
        G::ClosestPointOnAxis(
            {
            {},
            {1.f, 0.001f, 0.f}
    },
            {},
            {1.f, 0.f, 0.f},
            1e-4f,
            &point));
    EXPECT_FALSE(G::ClosestPointOnAxis({{}, {}}, {}, {1.f, 0.f, 0.f}, 1e-4f, &point));
    EXPECT_FALSE(
        G::ClosestPointOnAxis(
            {
            {0.f, 0.f, 1.f},
            {0.f, 0.f, 1.f}
    },
            {},
            {1.f, 0.f, 0.f},
            1e-4f,
            &point));
    EXPECT_EQ(point.AxisParameter, 11.f);
}

TEST(GeometryTest, RayPlaneIntersectsInEitherNormalDirection)
{
    for (float sign : {-1.f, 1.f})
    {
        G::PlaneHit hit = {};
        ASSERT_TRUE(
            G::RayPlane(
                {
                {2.f, 3.f, 10.f},
                {0.f, 0.f, -2.f}
        },
                {},
                {0.f, 0.f, sign * 5.f},
                1e-3f,
                &hit));
        EXPECT_NEAR(hit.Distance, 10.f, Tolerance);
        Near(hit.Position, {2.f, 3.f, 0.f});
    }
}

TEST(GeometryTest, RayPlaneRejectsParallelBackwardAndZeroNormal)
{
    G::PlaneHit hit = {.Distance = 13.f};
    EXPECT_FALSE(
        G::RayPlane(
            {
            {0.f, 0.f, 1.f},
            {1.f, 0.f, 0.f}
    },
            {},
            {0.f, 0.f, 1.f},
            1e-3f,
            &hit));
    EXPECT_FALSE(
        G::RayPlane(
            {
            {0.f, 0.f,      1.f},
            {1.f, 0.f, -0.0001f}
    },
            {},
            {0.f, 0.f, 1.f},
            1e-3f,
            &hit));
    EXPECT_FALSE(
        G::RayPlane(
            {
            {0.f, 0.f, 1.f},
            {0.f, 0.f, 1.f}
    },
            {},
            {0.f, 0.f, 1.f},
            1e-3f,
            &hit));
    EXPECT_FALSE(G::RayPlane({}, {}, {}, 1e-3f, &hit));
    EXPECT_EQ(hit.Distance, 13.f);
}

TEST(GeometryTest, PlaneAnglesFollowTheRightHandRuleAndIgnoreNormalOffsets)
{
    float angle = 0.f;
    ASSERT_TRUE(G::AngleOnPlane({2.f, 0.f, 5.f}, {0.f, 3.f, -9.f}, {0.f, 0.f, 2.f}, &angle));
    EXPECT_NEAR(angle, HALF_PI<float>, Tolerance);
    ASSERT_TRUE(G::AngleOnPlane({1.f, 0.f, 0.f}, {0.f, -1.f, 0.f}, {0.f, 0.f, 1.f}, &angle));
    EXPECT_NEAR(angle, -HALF_PI<float>, Tolerance);
    EXPECT_FALSE(G::AngleOnPlane({}, {1.f, 0.f, 0.f}, {0.f, 0.f, 1.f}, &angle));
}

TEST(GeometryTest, PlaneAnglesRejectParallelVectorsWithoutNormalizingRoundoff)
{
    float angle = 19.f;
    for (const Vec3f normal : {Vec3f(1.f, 2.f, 3.f), Vec3f(-7.f, 11.f, 5.f)})
    {
        for (float scale : {1e-20f, 1.f, 1e20f})
        {
            EXPECT_FALSE(G::AngleOnPlane(normal * scale, {1.f, 0.f, 0.f}, normal, &angle));
            EXPECT_FALSE(G::AngleOnPlane({1.f, 0.f, 0.f}, normal * scale, normal, &angle));
            EXPECT_EQ(angle, 19.f);
        }
    }
}

TEST(GeometryTest, RotationUnwrapCrossesBothPiBoundariesAndMultipleTurns)
{
    float angle = 0.f;
    ASSERT_TRUE(G::UnwrapAngle(radians(-179.f), radians(179.f), radians(179.f), &angle));
    EXPECT_NEAR(degrees(angle), 181.f, 0.001f);
    ASSERT_TRUE(G::UnwrapAngle(radians(179.f), radians(-179.f), radians(-539.f), &angle));
    EXPECT_NEAR(degrees(angle), -541.f, 0.001f);
}

TEST(GeometryTest, TangentFallbackUsesCapturedTangentAndRingRadius)
{
    float angle = 0.f;
    ASSERT_TRUE(G::TangentAngle({100.f, 500.f}, {2.f, 0.f}, 100.f, &angle));
    EXPECT_NEAR(angle, 1.f, Tolerance);
    EXPECT_FALSE(G::TangentAngle({}, {}, 100.f, &angle));
    EXPECT_FALSE(G::TangentAngle({}, {1.f, 0.f}, 0.f, &angle));
    EXPECT_EQ(angle, 1.f);
}

TEST(GeometryTest, SegmentDistanceClampsEndpointsAndHandlesCollapsedSegments)
{
    float squared = 0.f;
    ASSERT_TRUE(G::DistanceToSegmentSquared({5.f, 3.f}, {}, {10.f, 0.f}, &squared));
    EXPECT_NEAR(squared, 9.f, Tolerance);
    ASSERT_TRUE(G::DistanceToSegmentSquared({-4.f, 3.f}, {}, {10.f, 0.f}, &squared));
    EXPECT_NEAR(squared, 25.f, Tolerance);
    ASSERT_TRUE(G::DistanceToSegmentSquared({4.f, 3.f}, {}, {}, &squared));
    EXPECT_NEAR(squared, 25.f, Tolerance);
}

TEST(GeometryTest, SnappingSupportsSignedCoordinatesAndTinySteps)
{
    float result = 0.f;
    ASSERT_TRUE(G::Snap(0.26f, 0.1f, &result));
    EXPECT_NEAR(result, 0.3f, Tolerance);
    ASSERT_TRUE(G::Snap(-0.25f, 0.5f, &result));
    EXPECT_EQ(result, -0.5f);
    ASSERT_TRUE(G::Snap(std::numeric_limits<float>::max(), std::numeric_limits<float>::denorm_min(), &result));
    EXPECT_EQ(result, std::numeric_limits<float>::max());
}

TEST(GeometryTest, GroupSnappingUsesOneDeltaAndPreservesSpacing)
{
    float delta = 0.f;
    ASSERT_TRUE(G::SnapDelta(0.3f, 0.44f, 0.5f, &delta));
    EXPECT_NEAR(0.3f + delta, 0.5f, Tolerance);
    const float second = 1.7f;
    EXPECT_NEAR((second + delta) - (0.3f + delta), second - 0.3f, Tolerance);
}

TEST(GeometryTest, InvalidSnapRetainsPreviousValue)
{
    float value = 7.f;
    EXPECT_FALSE(G::Snap(1.f, 0.f, &value));
    EXPECT_FALSE(G::Snap(1.f, -1.f, &value));
    EXPECT_FALSE(G::Snap(std::numeric_limits<float>::infinity(), 1.f, &value));
    EXPECT_FALSE(G::SnapDelta(1.f, 2.f, 0.f, &value));
    EXPECT_EQ(value, 7.f);
}

TEST(GeometryTest, AxisScaleRatioClampsWithoutFlippingAuthoredSigns)
{
    float ratio = 0.f;
    ASSERT_TRUE(G::AxisScaleRatio(4.f, 2.f, 1.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 2.f);
    EXPECT_EQ(-3.f * ratio, -6.f);
    ASSERT_TRUE(G::AxisScaleRatio(4.f, -2.f, 1.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 0.001f);
    ASSERT_TRUE(G::AxisScaleRatio(-4.f, -2.f, 1.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 2.f);
    ASSERT_TRUE(G::AxisScaleRatio(0.25f, 0.f, 0.5f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 0.5f);
}

TEST(GeometryTest, UniformScaleIsNeutralAtStartAndExponentialAlongTheDiagonal)
{
    float ratio = 0.f;
    ASSERT_TRUE(G::UniformScaleRatio({}, 120.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 1.f);
    ASSERT_TRUE(G::UniformScaleRatio({120.f, -120.f}, 120.f, 0.001f, &ratio));
    EXPECT_NEAR(ratio, std::exp(std::sqrt(2.f)), Tolerance);
    ASSERT_TRUE(G::UniformScaleRatio({-100000.f, 100000.f}, 120.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 0.001f);
    EXPECT_FALSE(G::UniformScaleRatio({100000.f, -100000.f}, 120.f, 0.001f, &ratio));
    EXPECT_EQ(ratio, 0.001f);
}

TEST(GeometryTest, ClosestEulerRetainsTheNearestEquivalentSolutionAcrossTurns)
{
    const Vec3f rotation(0.3f, 2.f, -0.4f);
    const Vec3f reference = rotation + Vec3f(TWO_PI<float>, -TWO_PI<float>, TWO_PI<float>);
    Vec3f       result    = {};
    ASSERT_TRUE(G::ClosestEuler(ComposeTransformMatrix({}, rotation, {1.f, 1.f, 1.f}), reference, ShearTolerance, &result));
    Near(result, reference);
}

TEST(GeometryTest, ClosestEulerHandlesGimbalLockWithoutAnUnnecessaryHalfTurn)
{
    for (float sign : {-1.f, 1.f})
        for (float offset : {-0.00001f, 0.f, 0.00001f})
        {
            const Vec3f rotation(0.7f, sign * HALF_PI<float> + offset, -0.3f);
            const auto  matrix = ComposeTransformMatrix({}, rotation, {1.f, 1.f, 1.f});
            Vec3f       result = {};
            ASSERT_TRUE(G::ClosestEuler(matrix, rotation, ShearTolerance, &result));
            Near(ComposeTransformMatrix({}, result, {1.f, 1.f, 1.f}), matrix);
            Near(result, rotation, 0.001f);
        }
}

TEST(GeometryTest, NonuniformSignedScaleDecomposesUsingColumnsNotRowLengths)
{
    const G::LocalTRS base = {
        { 3.f,  -2.f,   7.f},
        {0.7f, -0.4f,  1.2f},
        {-2.f,   3.f, 0.25f}
    };
    G::LocalTRS result = {};
    ASSERT_TRUE(G::DecomposeTRS(Compose(base), base, ShearTolerance, &result));
    Near(result.Position, base.Position);
    Near(result.Scale, base.Scale);
    Near(result.Rotation, base.Rotation);
    Near(Compose(result), Compose(base));
}

TEST(GeometryTest, DecompositionRoundTripsTenThousandSignedTransforms)
{
    std::mt19937                          generator(1771);
    std::uniform_real_distribution<float> angles(-8.f * PI<float>, 8.f * PI<float>);
    std::uniform_real_distribution<float> values(0.05f, 10.f);
    for (uint32_t iteration = 0; iteration < 10000; ++iteration)
    {
        const G::LocalTRS base = {
            {                               values(generator),                                values(generator),                                values(generator)},
            {                               angles(generator),                                angles(generator),                                angles(generator)},
            {values(generator) * (iteration & 1 ? -1.f : 1.f), values(generator) * (iteration & 2 ? -1.f : 1.f), values(generator) * (iteration & 4 ? -1.f : 1.f)}
        };
        G::LocalTRS result = {};
        ASSERT_TRUE(G::DecomposeTRS(Compose(base), base, ShearTolerance, &result)) << iteration;
        Near(Compose(result), Compose(base), ShearTolerance);
        for (uint32_t axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::signbit(result.Scale[axis]), std::signbit(base.Scale[axis])) << iteration;
    }
}

TEST(GeometryTest, ParentLocalRoundTripHandlesRotatedNonuniformNegativeParents)
{
    const G::LocalTRS child = {
        { 1.f,   2.f,  3.f},
        {0.2f, -0.3f, 0.4f},
        {-2.f,  0.3f,  4.f}
    };
    const auto parent  = ComposeTransformMatrix({10.f, 20.f, 30.f}, {0.4f, -0.2f, 0.8f}, {-3.f, 0.5f, 2.f});
    Mat4f      inverse = {};
    ASSERT_TRUE(G::TryInverse(parent, &inverse));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::LocalFromWorld(inverse, parent * Compose(child), child, ShearTolerance, &result));
    Near(Compose(result), Compose(child), ShearTolerance);
    Near(result.Scale, child.Scale);
}

TEST(GeometryTest, DecompositionRejectsShearAndSingularityWithoutPartialOutput)
{
    const G::LocalTRS sentinel = {
        {7.f, 8.f, 9.f},
        {1.f, 2.f, 3.f},
        {4.f, 5.f, 6.f}
    };
    G::LocalTRS output = sentinel;
    auto        matrix = Identity<Mat4f>();
    matrix(0, 1)       = 0.1f;
    EXPECT_FALSE(G::DecomposeTRS(matrix, {}, ShearTolerance, &output));
    matrix       = Identity<Mat4f>();
    matrix(1, 1) = 0.f;
    EXPECT_FALSE(G::DecomposeTRS(matrix, {}, ShearTolerance, &output));
    EXPECT_FALSE(G::LocalFromWorld({}, Identity<Mat4f>(), {}, ShearTolerance, &output));
    Near(output.Position, sentinel.Position);
    Near(output.Rotation, sentinel.Rotation);
    Near(output.Scale, sentinel.Scale);
}

TEST(GeometryTest, ParentInducedShearRejectsTheEntireCandidate)
{
    const auto parent  = ComposeTransformMatrix({}, {}, {3.f, 1.f, 1.f});
    const auto delta   = ComposeTransformMatrix({}, {0.f, 0.f, 0.4f}, {1.f, 1.f, 1.f});
    Mat4f      inverse = {};
    ASSERT_TRUE(G::TryInverse(parent, &inverse));
    G::LocalTRS output = {};
    EXPECT_FALSE(G::LocalFromWorld(inverse, delta * parent, {}, ShearTolerance, &output));
    Near(output.Scale, {1.f, 1.f, 1.f});
}

TEST(GeometryTest, NearlyZeroButNonsingularScaleDoesNotAssert)
{
    const G::LocalTRS base = {
        {},
        {0.3f, 0.4f, 0.5f},
        {-1e-8f, 2.f, 3.f}
    };
    G::LocalTRS result = {};
    ASSERT_TRUE(G::DecomposeTRS(Compose(base), base, ShearTolerance, &result));
    EXPECT_LT(result.Scale.x, 0.f);
    Near(Compose(result), Compose(base));
}

TEST(GeometryTest, NonfiniteInputsAndNullOutputsAreRejected)
{
    const float nan    = std::numeric_limits<float>::quiet_NaN();
    float       scalar = 43.f;
    EXPECT_FALSE(G::Snap(nan, 1.f, &scalar));
    EXPECT_FALSE(G::UnwrapAngle(nan, 0.f, 0.f, &scalar));
    EXPECT_FALSE(G::TangentAngle({nan, 0.f}, {1.f, 0.f}, 1.f, &scalar));
    EXPECT_FALSE(G::DistanceToSegmentSquared({nan, 0.f}, {}, {}, &scalar));
    EXPECT_FALSE(G::AxisScaleRatio(nan, 1.f, 1.f, 0.001f, &scalar));
    EXPECT_FALSE(G::UniformScaleRatio({}, 0.f, 0.001f, &scalar));
    EXPECT_EQ(scalar, 43.f);
    EXPECT_FALSE(G::Snap(1.f, 1.f, nullptr));
    EXPECT_FALSE(G::TryInverse(Identity<Mat4f>(), nullptr));
    EXPECT_FALSE(G::Project({}, Frame(), Viewport, nullptr));
    EXPECT_FALSE(G::UnprojectRay({}, Frame(), Viewport, nullptr));
    EXPECT_FALSE(G::DecomposeTRS(Identity<Mat4f>(), {}, ShearTolerance, nullptr));
}
