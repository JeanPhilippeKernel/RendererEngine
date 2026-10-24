#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

using namespace ZEngine::Core::Maths;
namespace G = ZEngine::Core::Maths::Geometry;

namespace
{
    const G::Viewport Bounds = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    G::ProjectionFrame Projection(bool ortho = false, bool reverse = false, bool oblique = false, float far_plane = 10000.f)
    {
        using namespace ZEngine::Rendering::Cameras;
        CameraSetting settings = {};
        settings.FarPlane      = far_plane;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetPosition(oblique ? Vec3f(7.f, 5.f, 10.f) : Vec3f(0.f, 0.f, 10.f));
        camera.SetOrientation(oblique ? 25.f : 0.f, oblique ? -35.f : 0.f);
        if (ortho)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto frame = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t column = 0; column < 4; ++column)
                frame.Projection(2, column) = frame.Projection(3, column) - frame.Projection(2, column);
        G::ProjectionFrame result = {};
        EXPECT_TRUE(G::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, ortho ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &result));
        return result;
    }

    Vec2f Screen(const G::ProjectionFrame& frame, const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        EXPECT_TRUE(G::Project(point, frame, Bounds, &projected));
        return projected.Screen;
    }

    G::ScaleCapture Capture(const G::ProjectionFrame& frame, const Vec3f& axis = {1.f, 0.f, 0.f}, float start = 2.f, bool uniform = false)
    {
        return {.Projection = frame, .Bounds = Bounds, .Pointer = Screen(frame, axis * start), .Axis = axis, .HandleLength = 2.f, .MinimumGrabFraction = .25f, .AxisMinDenominator = .0001f, .UniformPixelsPerEfold = 120.f, .MinimumRatio = .001f, .Uniform = uniform};
    }

    void Exact(const Vec3f& actual, const Vec3f& expected)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
            EXPECT_EQ(std::bit_cast<uint32_t>(actual[axis]), std::bit_cast<uint32_t>(expected[axis]));
    }

    void Exact(const G::LocalTRS& actual, const G::LocalTRS& expected)
    {
        Exact(actual.Position, expected.Position);
        Exact(actual.Rotation, expected.Rotation);
        Exact(actual.Scale, expected.Scale);
    }
} // namespace

TEST(ScaleMathTest, DefaultRecordsAreNotLiveMappingsOrTargets)
{
    G::ScaleReference mapping = {};
    G::ScaleTarget    target  = {};
    G::LocalTRS       output  = {
        .Position = {77.f, 88.f, 99.f}
    };
    const auto before = output;
    float      ratio  = 77.f;
    EXPECT_FALSE(G::CaptureScale({}, &mapping));
    EXPECT_FALSE(G::SampleScale(mapping, {}, 1.f, false, 0.f, &ratio));
    EXPECT_FALSE(G::ScaleLocal(target, {1.f, 1.f, 1.f}, &output));
    EXPECT_EQ(ratio, 77.f);
    Exact(output, before);
}

TEST(GroupScaleMathTest, DefaultAndInvalidFramesPreserveCallerOutput)
{
    G::ScaleFrame     frame  = {};
    G::RotationTarget target = {};
    G::LocalTRS       output = {
        .Position = {77.f, 88.f, 99.f}
    };
    EXPECT_FALSE(G::ScaleInFrame(target, frame, {2.f, 2.f, 2.f}, &output));
    EXPECT_EQ(output.Position.x, 77.f);
    for (uint32_t bad = 0; bad < 5; ++bad)
    {
        auto  basis  = Identity<Mat4f>();
        float volume = .0001f;
        if (bad == 0)
            basis(0, 0) = 0.f;
        if (bad == 1)
            basis(0, 3) = 1.f;
        if (bad == 2)
            basis(0, 0) = std::numeric_limits<float>::infinity();
        if (bad == 3)
            volume = -1.f;
        if (bad == 4)
            volume = 1.f;
        EXPECT_FALSE(G::CaptureScaleFrame(basis, volume, &frame));
        EXPECT_FALSE(frame.Valid);
    }
    EXPECT_FALSE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, nullptr));
}

TEST(GroupScaleMathTest, CapturedBasisNormalizesWithoutDroppingReflectionsOrSkew)
{
    auto basis          = Identity<Mat4f>();
    basis(0, 0)         = -4.f;
    basis(0, 1)         = 2.f;
    basis(1, 1)         = 2.f;
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(basis, .0001f, &frame));
    EXPECT_EQ(frame.Basis(0, 0), -1.f);
    EXPECT_NEAR(frame.Basis(0, 1), std::numbers::sqrt2_v<float> / 2.f, .000001f);
    EXPECT_GT(frame.Basis(0, 1), 0.f); // No Gram-Schmidt substitution.
    const auto identity = frame.Basis * frame.InverseBasis;
    for (uint32_t row = 0; row < 4; ++row)
        for (uint32_t column = 0; column < 4; ++column)
            EXPECT_NEAR(identity(row, column), row == column ? 1.f : 0.f, .000001f);
}

TEST(GroupScaleMathTest, OneImmutableScaleDeltaIsReusedAcrossRootsAndInvalidSamplesPreserveIt)
{
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, &frame));
    G::ScaleDelta delta = {};
    EXPECT_FALSE(G::MakeScaleDelta({}, {2.f, 1.f, 1.f}, &delta));
    ASSERT_TRUE(G::MakeScaleDelta(frame, {2.f, 1.f, 1.f}, &delta));
    EXPECT_DOUBLE_EQ(delta.Coefficients[0], 1.0);
    EXPECT_DOUBLE_EQ(delta.Coefficients[4], 0.0);
    for (uint32_t i = 0; i < 100; ++i)
    {
        G::RotationTarget target = {};
        ASSERT_TRUE(
            G::CaptureRotationTarget(
                Identity<Mat4f>(),
                {
                {float(i), 0.f, 0.f}
        },
                {},
                .001f,
                &target));
        G::LocalTRS output = {};
        ASSERT_TRUE(G::ScaleInFrame(target, delta, &output));
        EXPECT_FLOAT_EQ(output.Position.x, float(i) * 2.f);
        EXPECT_FLOAT_EQ(output.Scale.x, 2.f);
    }
    EXPECT_FALSE(G::MakeScaleDelta(frame, {0.f, 1.f, 1.f}, &delta));
    EXPECT_TRUE(delta.Valid);
    EXPECT_DOUBLE_EQ(delta.Coefficients[0], 1.0);
}

TEST(GroupScaleMathTest, ActiveFrameMatchesLocalScalingUnderRotatedReflectedAndNonuniformParents)
{
    const auto        parent = ComposeTransformMatrix({1.f, -2.f, .5f}, {.2f, -.3f, .4f}, {-2.f, .75f, 1.5f});
    const G::LocalTRS before = {
        { .5f, -.7f, .2f},
        { .2f,  .1f, .3f},
        {-2.f,  0.f, .5f}
    };
    const Vec3f       pivot  = {.2f, .3f, .4f};
    G::RotationTarget shared = {};
    G::ScaleTarget    single = {};
    ASSERT_TRUE(G::CaptureRotationTarget(parent, before, pivot, .001f, &shared));
    ASSERT_TRUE(G::CaptureScaleTarget(parent, before, pivot, .001f, &single));
    auto basis = shared.StartFrame;
    for (uint32_t row = 0; row < 3; ++row)
        basis(row, 3) = 0.f;
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(basis, .0001f, &frame));
    for (const Vec3f ratios : {Vec3f(2.f, 1.f, 1.f), Vec3f(1.f, .5f, 1.f), Vec3f(1.f, 1.f, 1.5f)})
    {
        G::LocalTRS expected = {}, actual = {};
        ASSERT_TRUE(G::ScaleLocal(single, ratios, &expected));
        ASSERT_TRUE(G::ScaleInFrame(shared, frame, ratios, &actual));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(actual.Position[axis], expected.Position[axis], .0001f);
            EXPECT_NEAR(actual.Rotation[axis], expected.Rotation[axis], .0001f);
            EXPECT_NEAR(actual.Scale[axis], expected.Scale[axis], .0001f);
        }
    }
}

TEST(GroupScaleMathTest, DifferentRootOrientationsUseTheSharedAxisAndPivot)
{
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, &frame));
    const G::LocalTRS before = {
        {3.f,  4.f,                             0.f},
        {0.f,  0.f, std::numbers::pi_v<float> / 2.f},
        {1.f, -2.f,                             3.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {1.f, 1.f, 0.f}, .001f, &target));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::ScaleInFrame(target, frame, {2.f, 1.f, 1.f}, &result));
    EXPECT_NEAR(result.Position.x, 5.f, .0001f);
    EXPECT_NEAR(result.Position.y, 4.f, .0001f);
    EXPECT_NEAR(result.Scale.x, 1.f, .0001f);
    EXPECT_NEAR(result.Scale.y, -4.f, .0001f);
    EXPECT_NEAR(result.Scale.z, 3.f, .0001f);
}

TEST(GroupScaleMathTest, NonrepresentableAxisShearRejectsButUniformPreservesExactAuthoredRotation)
{
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, &frame));
    const G::LocalTRS before = {
        { 3.f, 4.f, 0.f},
        { .2f, .1f, .7f},
        {-2.f, 3.f, .5f}
    };
    for (const auto parent : {Identity<Mat4f>(), ComposeTransformMatrix({}, {.1f, .2f, .3f}, {-2.f, .7f, 1.5f})})
    {
        G::RotationTarget target = {};
        ASSERT_TRUE(G::CaptureRotationTarget(parent, before, {1.f, 1.f, 0.f}, .001f, &target));
        G::LocalTRS output = {
            .Position = {77.f, 88.f, 99.f}
        };
        EXPECT_FALSE(G::ScaleInFrame(target, frame, {2.f, 1.f, 1.f}, &output));
        EXPECT_EQ(output.Position.x, 77.f);
        ASSERT_TRUE(G::ScaleInFrame(target, frame, {2.f, 2.f, 2.f}, &output));
        Exact(output.Rotation, before.Rotation);
        for (uint32_t axis = 0; axis < 3; ++axis)
            EXPECT_FLOAT_EQ(output.Scale[axis], before.Scale[axis] * 2.f);
        ASSERT_TRUE(G::ScaleInFrame(target, frame, {1.f, 1.f, 1.f}, &output));
        Exact(output, before);
    }
}

TEST(GroupScaleMathTest, ZeroNearZeroNegativeScalesAndNoOpRemainExact)
{
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, &frame));
    const G::LocalTRS before = {
        { 3.f,    4.f, 0.f},
        { .2f,   -.4f, .7f},
        {-0.f, -1e-6f, 3.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {}, .001f, &target));
    G::LocalTRS output = {};
    ASSERT_TRUE(G::ScaleInFrame(target, frame, {1.5f, 1.5f, 1.5f}, &output));
    EXPECT_TRUE(std::signbit(output.Scale.x));
    EXPECT_FLOAT_EQ(output.Scale.x, 0.f);
    EXPECT_FLOAT_EQ(output.Scale.y, -1.5e-6f);
    Exact(output.Rotation, before.Rotation);
    ASSERT_TRUE(G::ScaleInFrame(target, frame, {1.f, 1.f, 1.f}, &output));
    Exact(output, before);
}

TEST(GroupScaleMathTest, LostSmallDisplacementsAndScaleUnderflowCannotPublishAPlausibleWrongPose)
{
    G::ScaleFrame frame = {};
    ASSERT_TRUE(G::CaptureScaleFrame(Identity<Mat4f>(), .0001f, &frame));
    const float       origin = 1e12f;
    G::RotationTarget target = {};
    ASSERT_TRUE(
        G::CaptureRotationTarget(
            Identity<Mat4f>(),
            {
            {origin, 0.f, 0.f}
    },
            {std::nextafter(origin, 0.f), 0.f, 0.f},
            .001f,
            &target));
    G::LocalTRS output = {
        .Position = {77.f, 88.f, 99.f}
    };
    EXPECT_FALSE(G::ScaleInFrame(target, frame, {std::nextafter(1.f, 2.f), 1.f, 1.f}, &output));
    EXPECT_EQ(output.Position.x, 77.f);
    ASSERT_TRUE(
        G::CaptureRotationTarget(
            Identity<Mat4f>(),
            {
            .Scale = {std::numeric_limits<float>::denorm_min(), 1.f, 1.f}
    },
            {},
            .001f,
            &target));
    EXPECT_FALSE(G::ScaleInFrame(target, frame, {.5f, .5f, .5f}, &output));
    for (const auto ratios : {Vec3f(0.f, 1.f, 1.f), Vec3f(-1.f, 1.f, 1.f), Vec3f(std::numeric_limits<float>::infinity(), 1.f, 1.f)})
        EXPECT_FALSE(G::ScaleInFrame(target, frame, ratios, &output));
    EXPECT_EQ(output.Position.x, 77.f);
}

TEST(ScaleMathTest, EveryAxisUsesItsCapturedRatioAcrossProjectionsDepthConventionsAndSigns)
{
    for (bool ortho : {false, true})
        for (bool reverse : {false, true})
            for (uint32_t axis = 0; axis < 3; ++axis)
                for (float sign : {-1.f, 1.f})
                {
                    SCOPED_TRACE(testing::Message() << "ortho=" << ortho << " reverse=" << reverse << " axis=" << axis << " sign=" << sign);
                    const auto frame          = Projection(ortho, reverse, true);
                    Vec3f      direction      = {};
                    direction[axis]           = sign;
                    const auto        capture = Capture(frame, direction);
                    G::ScaleReference mapping = {};
                    ASSERT_TRUE(G::CaptureScale(capture, &mapping));
                    for (float distance : {1.f, 2.f, 3.f, 4.f})
                    {
                        float ratio = 0.f;
                        ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, direction * distance), 1.f, false, 0.f, &ratio));
                        EXPECT_NEAR(ratio, distance / 2.f, .0001f);
                    }
                }
}

TEST(ScaleMathTest, GuardedAxisDenominatorPreservesGrabOffsetAndAnExactNeutralPress)
{
    const auto frame = Projection();
    for (float start : {-.05f, .05f})
    {
        auto capture              = Capture(frame, {1.f, 0.f, 0.f}, start);
        capture.HandleLength      = 4.f;
        G::ScaleReference mapping = {};
        ASSERT_TRUE(G::CaptureScale(capture, &mapping));
        EXPECT_FLOAT_EQ(std::abs(mapping.Denominator), 1.f);
        float ratio = 0.f;
        ASSERT_TRUE(G::SampleScale(mapping, capture.Pointer, 1.f, true, .3f, &ratio));
        EXPECT_FLOAT_EQ(ratio, 1.f);
        ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, {start + .01f, 0.f, 0.f}), 1.f, false, 0.f, &ratio));
        EXPECT_NEAR(ratio, start > 0.f ? 1.01f : .99f, .00001f);
        ASSERT_TRUE(G::SampleScale(mapping, capture.Pointer, .1f, true, .3f, &ratio));
        EXPECT_FLOAT_EQ(ratio, 1.f);
    }
}

TEST(ScaleMathTest, LongOrthographicDepthRangesDoNotShiftRayAxisRatiosUnderReverseZ)
{
    for (float far_plane : {1000.f, 10000.f, 1000000.f})
        for (bool reverse : {false, true})
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const auto frame          = Projection(true, reverse, true, far_plane);
                Vec3f      direction      = {};
                direction[axis]           = 1.f;
                G::ScaleReference mapping = {};
                ASSERT_TRUE(G::CaptureScale(Capture(frame, direction), &mapping));
                float ratio = 0.f;
                ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, direction * 4.f), 1.f, false, 0.f, &ratio));
                EXPECT_NEAR(ratio, 2.f, .00002f) << "far=" << far_plane << " reverse=" << reverse << " axis=" << axis;
            }
}

TEST(ScaleMathTest, FineControlScalesPointerMotionBeforePerspectiveUnprojection)
{
    const auto        frame = Projection(false, false, true);
    const Vec3f       axis(0.f, 0.f, 1.f);
    const auto        capture = Capture(frame, axis);
    G::ScaleReference mapping = {};
    ASSERT_TRUE(G::CaptureScale(capture, &mapping));
    const auto pointer      = Screen(frame, axis * 4.f);
    const auto fine_pointer = capture.Pointer + (pointer - capture.Pointer) * .1f;
    float      fine = 0.f, expected = 0.f;
    ASSERT_TRUE(G::SampleScale(mapping, pointer, .1f, false, 0.f, &fine));
    ASSERT_TRUE(G::SampleScale(mapping, fine_pointer, 1.f, false, 0.f, &expected));
    EXPECT_NEAR(fine, expected, .00001f);
}

TEST(ScaleMathTest, AxisRatiosSnapThenClampWithoutCrossingZeroOrFlippingSign)
{
    const auto        frame   = Projection();
    G::ScaleReference mapping = {};
    ASSERT_TRUE(G::CaptureScale(Capture(frame), &mapping));
    float ratio = 0.f;
    ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, {2.74f, 0.f, 0.f}), 1.f, true, .25f, &ratio));
    EXPECT_FLOAT_EQ(ratio, 1.25f);
    ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, {-3.f, 0.f, 0.f}), 1.f, true, .25f, &ratio));
    EXPECT_FLOAT_EQ(ratio, .001f);
}

TEST(ScaleMathTest, UniformRatioIsExponentialAndSnapPrecedesTheMinimumClamp)
{
    for (bool ortho : {false, true})
    {
        const auto        frame   = Projection(ortho);
        const auto        capture = Capture(frame, {}, 0.f, true);
        G::ScaleReference mapping = {};
        ASSERT_TRUE(G::CaptureScale(capture, &mapping));
        float ratio = 0.f;
        ASSERT_TRUE(G::SampleScale(mapping, capture.Pointer + Vec2f(120.f, -120.f), 1.f, false, 0.f, &ratio));
        EXPECT_NEAR(ratio, std::exp(std::numbers::sqrt2), .00001);
        ASSERT_TRUE(G::SampleScale(mapping, capture.Pointer + Vec2f(-100000.f, 100000.f), 1.f, true, .0007f, &ratio));
        EXPECT_FLOAT_EQ(ratio, .001f);
        ASSERT_TRUE(G::SampleScale(mapping, capture.Pointer, 1.f, true, .3f, &ratio));
        EXPECT_FLOAT_EQ(ratio, 1.f);
        EXPECT_FALSE(G::SampleScale(mapping, capture.Pointer + Vec2f(100000.f, -100000.f), 1.f, false, 0.f, &ratio));
        EXPECT_FLOAT_EQ(ratio, 1.f);
    }
}

TEST(ScaleMathTest, InvalidCapturePoliciesPreserveTheExistingReference)
{
    const auto valid = Capture(Projection());
    for (uint32_t invalid = 0; invalid < 15; ++invalid)
    {
        auto capture = valid;
        switch (invalid)
        {
            case 0:
                capture.Projection.Valid = false;
                break;
            case 1:
                capture.Bounds.Size.x = 0.f;
                break;
            case 2:
                capture.Pointer.y = std::numeric_limits<float>::quiet_NaN();
                break;
            case 3:
                capture.Pivot.z = 20.f;
                break;
            case 4:
                capture.Axis = {};
                break;
            case 5:
                capture.HandleLength = 0.f;
                break;
            case 6:
                capture.MinimumGrabFraction = 0.f;
                break;
            case 7:
                capture.MinimumGrabFraction = 2.f;
                break;
            case 8:
                capture.AxisMinDenominator = 0.f;
                break;
            case 9:
                capture.MinimumRatio = 0.f;
                break;
            case 10:
                capture.MinimumRatio = 2.f;
                break;
            case 11:
                capture.HandleLength = std::numeric_limits<float>::infinity();
                break;
            case 12:
                capture.Uniform               = true;
                capture.UniformPixelsPerEfold = 0.f;
                break;
            case 13:
                capture.Uniform               = true;
                capture.UniformPixelsPerEfold = std::numeric_limits<float>::quiet_NaN();
                break;
            case 14:
                capture.Axis    = {0.f, 0.f, 1.f};
                capture.Pointer = Screen(capture.Projection, {});
                break;
        }
        G::ScaleReference output = {.Denominator = 77.f, .Valid = true};
        EXPECT_FALSE(G::CaptureScale(capture, &output)) << invalid;
        EXPECT_TRUE(output.Valid);
        EXPECT_FLOAT_EQ(output.Denominator, 77.f);
    }
    EXPECT_FALSE(G::CaptureScale(valid, nullptr));
}

TEST(ScaleMathTest, InvalidOrParallelSamplesPreserveOutputAndLaterMotionCanRecover)
{
    const auto        frame   = Projection();
    G::ScaleReference mapping = {};
    ASSERT_TRUE(G::CaptureScale(Capture(frame), &mapping));
    float ratio = 77.f;
    EXPECT_FALSE(G::SampleScale(mapping, {1.e10f, 361.f}, 1.f, false, 0.f, &ratio));
    EXPECT_FALSE(G::SampleScale(mapping, {std::numeric_limits<float>::quiet_NaN(), 0.f}, 1.f, false, 0.f, &ratio));
    EXPECT_FALSE(G::SampleScale(mapping, {}, 0.f, false, 0.f, &ratio));
    EXPECT_FALSE(G::SampleScale(mapping, {}, 1.f, true, 0.f, &ratio));
    EXPECT_FLOAT_EQ(ratio, 77.f);
    ASSERT_TRUE(G::SampleScale(mapping, Screen(frame, {3.f, 0.f, 0.f}), 1.f, false, 0.f, &ratio));
    EXPECT_NEAR(ratio, 1.5f, .00001f);
    EXPECT_FALSE(G::SampleScale(mapping, {}, 1.f, false, 0.f, nullptr));
}

TEST(ScaleMathTest, SelfPivotPreservesExactPositionRotationSignedScalesAndWholeNoops)
{
    const G::LocalTRS before = {
        {-0.f,  2.f, -0.f},
        { .2f, -.3f,  .4f},
        {-2.f,  0.f, -0.f}
    };
    const auto     parent = ComposeTransformMatrix({2.f, -3.f, 4.f}, {-.3f, .2f, .5f}, {-2.f, 3.f, 4.f});
    const auto     world  = parent * ComposeTransformMatrix(before.Position, before.Rotation, before.Scale);
    G::ScaleTarget target = {};
    ASSERT_TRUE(G::CaptureScaleTarget(parent, before, {world(0, 3), world(1, 3), world(2, 3)}, .001f, &target));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::ScaleLocal(target, {1.f, 1.f, 1.f}, &result));
    Exact(result, before);
    ASSERT_TRUE(G::ScaleLocal(target, {2.f, .5f, 3.f}, &result));
    Exact(result.Position, before.Position);
    Exact(result.Rotation, before.Rotation);
    Exact(result.Scale, {-4.f, 0.f, -0.f});
    Exact(target.Placement.Before, before);
}

TEST(ScaleMathTest, CustomPivotUsesCapturedLocalAxesUnderReflectedNonuniformParents)
{
    std::mt19937                          random(0x5ca1e);
    std::uniform_real_distribution<float> angle(-3.f, 3.f), position(-5.f, 5.f), scale(.2f, 3.f);
    for (uint32_t iteration = 0; iteration < 300; ++iteration)
    {
        const auto        parent = ComposeTransformMatrix({position(random), position(random), position(random)}, {angle(random), angle(random), angle(random)}, {-scale(random), scale(random), scale(random)});
        const G::LocalTRS before = {
            {position(random),                    position(random), position(random)},
            {   angle(random),                       angle(random),    angle(random)},
            {  -scale(random), iteration % 5 ? scale(random) : 0.f,    scale(random)}
        };
        const auto     frame = parent * ComposeTransformMatrix(before.Position, before.Rotation, {1.f, 1.f, 1.f});
        const Vec3f    offset(.5f, -.7f, 1.2f);
        const auto     point  = frame * Vec4f(offset.x, offset.y, offset.z, 1.f);
        G::ScaleTarget target = {};
        ASSERT_TRUE(G::CaptureScaleTarget(parent, before, {point.x, point.y, point.z}, .001f, &target)) << iteration;
        const Vec3f ratios(scale(random), scale(random), scale(random));
        G::LocalTRS result = {};
        ASSERT_TRUE(G::ScaleLocal(target, ratios, &result)) << iteration;
        Exact(result.Rotation, before.Rotation);
        const auto actual = parent * ComposeTransformMatrix(result.Position, result.Rotation, result.Scale);
        const auto start  = parent * ComposeTransformMatrix(before.Position, before.Rotation, before.Scale);
        for (uint32_t row = 0; row < 3; ++row)
        {
            double expected = start(row, 3);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                expected += double(frame(row, axis)) * offset[axis] * (1.0 - ratios[axis]);
                EXPECT_NEAR(actual(row, axis), double(start(row, axis)) * ratios[axis], .001);
            }
            EXPECT_NEAR(actual(row, 3), expected, .001);
        }
    }
}

TEST(ScaleMathTest, SingularInvalidOverflowAndUnderflowCannotReplaceAnExistingPose)
{
    G::ScaleTarget target = {.Valid = true};
    EXPECT_FALSE(G::CaptureScaleTarget({}, {}, {}, .001f, &target));
    EXPECT_TRUE(target.Valid);
    EXPECT_FALSE(G::CaptureScaleTarget(Identity<Mat4f>(), {}, {}, -.001f, &target));
    EXPECT_FALSE(G::CaptureScaleTarget(Identity<Mat4f>(), {}, {std::numeric_limits<float>::infinity(), 0.f, 0.f}, .001f, &target));
    EXPECT_FALSE(G::CaptureScaleTarget(Identity<Mat4f>(), {}, {}, .001f, nullptr));
    ASSERT_TRUE(
        G::CaptureScaleTarget(
            Identity<Mat4f>(),
            {
            .Scale = {2.f, 3.f, 4.f}
    },
            {},
            .001f,
            &target));
    G::LocalTRS output = {
        .Position = {77.f, 88.f, 99.f}
    };
    const auto before = output;
    for (const Vec3f ratios : {Vec3f(0.f, 1.f, 1.f), Vec3f(-1.f, 1.f, 1.f), Vec3f(std::numeric_limits<float>::max(), 1.f, 1.f), Vec3f(1.f, std::numeric_limits<float>::quiet_NaN(), 1.f)})
    {
        EXPECT_FALSE(G::ScaleLocal(target, ratios, &output));
        Exact(output, before);
    }
    ASSERT_TRUE(
        G::CaptureScaleTarget(
            Identity<Mat4f>(),
            {
            .Scale = {std::numeric_limits<float>::denorm_min(), 1.f, 1.f}
    },
            {},
            .001f,
            &target));
    EXPECT_FALSE(G::ScaleLocal(target, {.001f, 1.f, 1.f}, &output));
    Exact(output, before);
    EXPECT_FALSE(G::ScaleLocal(target, {1.f, 1.f, 1.f}, nullptr));
}
