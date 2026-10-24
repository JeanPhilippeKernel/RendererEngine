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
    constexpr double  Pi     = std::numbers::pi;
    const G::Viewport Bounds = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    G::ProjectionFrame Projection(bool orthographic = false, bool reverse = false, bool back = false, bool oblique = false)
    {
        using namespace ZEngine::Rendering::Cameras;
        CameraSetting settings = {};
        settings.FOV           = 60.f;
        settings.FarPlane      = 1000.f;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetPosition(oblique ? Vec3f(7.f, 5.f, 10.f) : Vec3f(0.f, 0.f, back ? -10.f : 10.f));
        camera.SetOrientation(oblique ? 25.f : 0.f, oblique ? -35.f : back ? 180.f : 0.f);
        if (orthographic)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto captured = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t column = 0; column < 4; ++column)
                captured.Projection(2, column) = captured.Projection(3, column) - captured.Projection(2, column);
        G::ProjectionFrame result = {};
        EXPECT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &result));
        return result;
    }

    Vec2f Screen(const G::ProjectionFrame& projection, const Vec3f& point)
    {
        G::ProjectedPoint projected = {};
        EXPECT_TRUE(G::Project(point, projection, Bounds, &projected));
        return projected.Screen;
    }

    G::RotationCapture Capture(const G::ProjectionFrame& projection, const Vec3f& axis = {0.f, 0.f, 1.f}, const Vec3f& radial = {1.f, 0.f, 0.f}, const Vec3f& pivot = {})
    {
        return {.Projection = projection, .Bounds = Bounds, .Pointer = Screen(projection, pivot + radial), .Pivot = pivot, .Axis = axis, .RingPoint = pivot + radial, .RadiusPx = 100.f, .TangentFallbackCosine = .15f, .PlaneMinCosine = .001f};
    }

    void Near(const Mat4f& actual, const Mat4f& expected, float error = .001f)
    {
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
                EXPECT_NEAR(actual(row, column), expected(row, column), error) << row << "," << column;
    }

    void Exact(const G::LocalTRS& actual, const G::LocalTRS& expected)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.Position[axis]), std::bit_cast<uint32_t>(expected.Position[axis]));
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.Rotation[axis]), std::bit_cast<uint32_t>(expected.Rotation[axis]));
            EXPECT_EQ(std::bit_cast<uint32_t>(actual.Scale[axis]), std::bit_cast<uint32_t>(expected.Scale[axis]));
        }
    }
} // namespace

TEST(RotationMathTest, DefaultRecordsCannotBeUsedAsLiveMappingsOrTargets)
{
    G::RotationCapture   capture   = {};
    G::RotationReference reference = {};
    G::RotationTarget    target    = {};
    G::RotationSample    sample    = {};
    G::LocalTRS          result    = {};
    EXPECT_FALSE(reference.Valid);
    EXPECT_FALSE(target.Valid);
    EXPECT_DOUBLE_EQ(sample.Progress.TotalAngle, 0.0);
    EXPECT_DOUBLE_EQ(sample.Angle, 0.0);
    EXPECT_FALSE(G::CaptureRotation(capture, &reference));
    EXPECT_FALSE(G::SampleRotation(reference, {}, {}, 1.f, false, 0.f, &sample));
    EXPECT_FALSE(G::RotateLocal(target, {0.f, 0.f, 1.f}, 1.0, &result));
    static_assert(sizeof(G::RotationReference) <= 384);
    static_assert(sizeof(G::RotationTarget) <= 320);
}

TEST(RotationMathTest, QuaternionWorldDeltaMatchesTheExistingMatrixDeltaAndKeepsLocalAxisComposition)
{
    const auto before = fromEulerAngles(.3f, -.7f, .5f);
    const auto frame  = quaternionToMat4(before);
    for (uint32_t axis = 0; axis < 3; ++axis)
        for (double angle : {-.7, .4, 2.9})
        {
            const Vec3f world_axis(frame(0, axis), frame(1, axis), frame(2, axis));
            Quatf       result = {};
            ASSERT_TRUE(G::RotateOrientation(before, world_axis, angle, &result));
            Vec3f local_axis = {};
            local_axis[axis] = 1.f;
            Near(quaternionToMat4(result), quaternionToMat4(before * fromAxisAngle(local_axis, float(angle))), .000002f);
            G::RotationDelta matrix = {};
            ASSERT_TRUE(G::MakeRotationDelta(world_axis, angle, &matrix));
            const auto actual = quaternionToMat4(result);
            for (uint32_t row = 0; row < 3; ++row)
                for (uint32_t column = 0; column < 3; ++column)
                {
                    double expected = 0.;
                    for (uint32_t k = 0; k < 3; ++k)
                        expected += matrix.Coefficients[row * 3 + k] * frame(k, column);
                    EXPECT_NEAR(actual(row, column), expected, .000002);
                }
            EXPECT_NEAR(result.dot(result), 1.f, .000001f);
        }
}

TEST(RotationMathTest, QuaternionIdentityIsExactAndInvalidInputsPreserveTheOutput)
{
    const Quatf before(-.2f, .3f, -.4f, -.8f);
    Quatf       result = {};
    for (double angle : {0., -0., 2. * Pi, -4. * Pi})
    {
        ASSERT_TRUE(G::RotateOrientation(before, {1.f, 2.f, 3.f}, angle, &result));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.x), std::bit_cast<uint32_t>(before.x));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.y), std::bit_cast<uint32_t>(before.y));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.z), std::bit_cast<uint32_t>(before.z));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.w), std::bit_cast<uint32_t>(before.w));
    }
    EXPECT_FALSE(G::RotateOrientation(before, {}, .5, &result));
    EXPECT_FALSE(G::RotateOrientation({0.f, 0.f, 0.f, 0.f}, {1.f, 0.f, 0.f}, .5, &result));
    EXPECT_FALSE(G::RotateOrientation({std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 1.f}, {1.f, 0.f, 0.f}, .5, &result));
    EXPECT_FALSE(G::RotateOrientation(before, {1.f, 0.f, 0.f}, std::numeric_limits<double>::infinity(), &result));
    EXPECT_FALSE(G::RotateOrientation(before, {1.f, 0.f, 0.f}, .5, nullptr));
    EXPECT_EQ(result.x, before.x);
    EXPECT_EQ(result.w, before.w);
    ASSERT_TRUE(G::RotateOrientation(before, {std::numeric_limits<float>::max(), 0.f, 0.f}, .5, &result));
    EXPECT_NEAR(result.dot(result), 1.f, .000001f);
}

TEST(RotationMathTest, EveryAxisTracksBothDirectionsAcrossMultipleTurnsAndProjectionDepthModes)
{
    for (bool orthographic : {false, true})
        for (bool reverse : {false, true})
            for (uint32_t component = 0; component < 3; ++component)
                for (double sign : {-1.0, 1.0})
                {
                    SCOPED_TRACE(testing::Message() << "orthographic=" << orthographic << " reverse=" << reverse << " axis=" << component << " sign=" << sign);
                    const auto  projection = Projection(orthographic, reverse, false, true);
                    const Vec3f pivot(.13f, -.27f, .11f);
                    float       world_per_pixel = 0.f;
                    ASSERT_TRUE(G::WorldPerPixel(projection, pivot, Bounds.Size.y, &world_per_pixel));
                    Vec3f axis = {}, radial = {};
                    axis[component]                = 3.f; // Axis magnitude must not affect angles.
                    radial[(component + 1) % 3]    = 100.f * world_per_pixel;
                    const auto           tangent   = cross3d(axis.normalize(), radial);
                    G::RotationReference reference = {};
                    ASSERT_TRUE(G::CaptureRotation(Capture(projection, axis, radial, pivot), &reference));
                    ASSERT_FALSE(reference.Tangent);
                    G::RotationProgress progress = {};
                    for (uint32_t step = 0; step <= 48; ++step)
                    {
                        const double      angle  = sign * step * Pi / 12.0;
                        G::RotationSample sample = {};
                        const auto        point  = pivot + radial * float(std::cos(angle)) + tangent * float(std::sin(angle));
                        ASSERT_TRUE(G::SampleRotation(reference, progress, Screen(projection, point), 1.f, false, 0.f, &sample));
                        EXPECT_NEAR(sample.Angle, angle, .0001);
                        progress = sample.Progress;
                    }
                }
}

TEST(RotationMathTest, RightHandedAnglesFollowTheCursorFromBothSidesOfTheRing)
{
    for (bool back : {false, true})
        for (bool orthographic : {false, true})
        {
            const auto projection = Projection(orthographic, false, back);
            for (float sign : {-1.f, 1.f})
            {
                G::RotationReference reference = {};
                ASSERT_TRUE(G::CaptureRotation(Capture(projection, {0.f, 0.f, sign}), &reference));
                G::RotationSample sample = {};
                ASSERT_TRUE(G::SampleRotation(reference, {}, Screen(projection, {0.f, 1.f, 0.f}), 1.f, false, 0.f, &sample));
                EXPECT_NEAR(sample.Angle, sign * Pi / 2.0, .00001);
            }
        }
}

TEST(RotationMathTest, EdgeOnFallbackUsesTheProjectedSignedTangentAndNeverChangesMapping)
{
    for (bool orthographic : {false, true})
        for (bool reverse : {false, true})
            for (float side : {-1.f, 1.f})
                for (float sign : {-1.f, 1.f})
                {
                    const auto           projection = Projection(orthographic, reverse);
                    const Vec3f          radial(0.f, 0.f, side), axis(sign, 0.f, 0.f);
                    const auto           capture   = Capture(projection, axis, radial);
                    G::RotationReference reference = {};
                    ASSERT_TRUE(G::CaptureRotation(capture, &reference));
                    ASSERT_TRUE(reference.Tangent);
                    const auto world_tangent = cross3d(axis, radial);
                    const auto projected     = (Screen(projection, radial + world_tangent * .001f) - Screen(projection, radial)).normalize();
                    EXPECT_NEAR(dot(reference.ScreenTangent, projected), 1.f, .00001f);
                    for (float pixels : {-1300.f, -100.f, 0.f, 100.f, 1300.f})
                    {
                        G::RotationSample sample = {};
                        ASSERT_TRUE(G::SampleRotation(reference, {}, capture.Pointer + reference.ScreenTangent * pixels, 1.f, false, 0.f, &sample));
                        EXPECT_NEAR(sample.Angle, pixels / 100.0, .00001);
                    }
                }
}

TEST(RotationMathTest, FallbackThresholdIsCapturedFromThePressRayRatherThanTheCameraForward)
{
    const auto projection = Projection();
    for (float cosine : {.149f, .151f})
    {
        const Vec3f axis(std::sqrt(1.f - cosine * cosine), 0.f, cosine);
        // A centre ray sees exactly the requested cosine. RingPoint is supplied
        // by the picked geometry when the ring plane cannot be intersected.
        auto        capture            = Capture(projection, axis, {0.f, 1.f, 0.f});
        capture.Pointer                = Screen(projection, {});
        G::RotationReference reference = {};
        if (cosine < .15f)
        {
            ASSERT_TRUE(G::CaptureRotation(capture, &reference));
            EXPECT_TRUE(reference.Tangent);
        }
        else
            EXPECT_FALSE(G::CaptureRotation(capture, &reference)); // Plane hit at pivot is undefined.
        capture.Pointer = Screen(projection, capture.RingPoint);
        ASSERT_TRUE(G::CaptureRotation(capture, &reference));
        EXPECT_EQ(reference.Tangent, cosine < .15f);
    }
}

TEST(RotationMathTest, SnappingAndFineModeDoNotQuantizeOrDiscardContinuousProgress)
{
    const auto           projection = Projection();
    G::RotationReference reference  = {};
    ASSERT_TRUE(G::CaptureRotation(Capture(projection), &reference));
    G::RotationProgress progress = {};
    G::RotationSample   sample   = {};
    ASSERT_TRUE(G::SampleRotation(reference, progress, reference.Pointer, 1.f, true, .25f, &sample));
    EXPECT_DOUBLE_EQ(sample.Angle, 0.0);
    for (double angle : {.3, .6, .9, 1.2})
    {
        const auto pointer = Screen(projection, {float(std::cos(angle)), float(std::sin(angle)), 0.f});
        ASSERT_TRUE(G::SampleRotation(reference, progress, pointer, 1.f, true, .25f, &sample));
        EXPECT_NEAR(sample.Angle, std::round(angle / .25) * .25, .00001);
        EXPECT_NEAR(sample.Progress.TotalAngle, angle, .00001);
        progress = sample.Progress;
        ASSERT_TRUE(G::SampleRotation(reference, progress, pointer, .1f, false, 0.f, &sample));
        EXPECT_NEAR(sample.Angle, angle * .1, .00001);
    }
    ASSERT_TRUE(G::SampleRotation(reference, progress, reference.Pointer, 1.f, true, .25f, &sample));
    EXPECT_DOUBLE_EQ(sample.Angle, 0.0);
}

TEST(RotationMathTest, InvalidAndCentralSamplesPreserveTheLastSampleAndProgress)
{
    const auto           projection = Projection();
    G::RotationReference reference  = {};
    ASSERT_TRUE(G::CaptureRotation(Capture(projection), &reference));
    G::RotationSample sample = {};
    ASSERT_TRUE(G::SampleRotation(reference, {}, Screen(projection, {0.f, 1.f, 0.f}), 1.f, false, 0.f, &sample));
    const auto last = sample;
    for (const Vec2f pointer : {Screen(projection, {}), Vec2f(std::numeric_limits<float>::quiet_NaN(), 0.f), Vec2f(0.f, std::numeric_limits<float>::infinity())})
    {
        EXPECT_FALSE(G::SampleRotation(reference, last.Progress, pointer, 1.f, false, 0.f, &sample));
        EXPECT_DOUBLE_EQ(sample.Angle, last.Angle);
        EXPECT_DOUBLE_EQ(sample.Progress.TotalAngle, last.Progress.TotalAngle);
        EXPECT_DOUBLE_EQ(sample.Progress.PreviousAngle, last.Progress.PreviousAngle);
    }
    EXPECT_FALSE(G::SampleRotation(reference, last.Progress, reference.Pointer, 0.f, false, 0.f, &sample));
    EXPECT_FALSE(G::SampleRotation(reference, last.Progress, reference.Pointer, 1.f, true, 0.f, &sample));
    EXPECT_FALSE(G::SampleRotation(reference, {.TotalAngle = std::numeric_limits<double>::infinity()}, reference.Pointer, 1.f, false, 0.f, &sample));
    ASSERT_TRUE(G::SampleRotation(reference, last.Progress, Screen(projection, {-1.f, 0.f, 0.f}), 1.f, false, 0.f, &sample));
    EXPECT_NEAR(std::abs(sample.Angle), Pi, .00001);
}

TEST(RotationMathTest, CaptureRejectsInvalidPoliciesAndUnprojectableOrDegenerateReferences)
{
    const auto           projection = Projection();
    const auto           valid      = Capture(projection);
    G::RotationReference output     = {.RadiusPx = 77.f};
    for (uint32_t failure = 0; failure < 11; ++failure)
    {
        auto input = valid;
        switch (failure)
        {
            case 0:
                input.Axis = {};
                break;
            case 1:
                input.Axis.x = std::numeric_limits<float>::quiet_NaN();
                break;
            case 2:
                input.RadiusPx = 0.f;
                break;
            case 3:
                input.TangentFallbackCosine = 0.f;
                break;
            case 4:
                input.TangentFallbackCosine = 1.f;
                break;
            case 5:
                input.PlaneMinCosine = input.TangentFallbackCosine;
                break;
            case 6:
                input.Bounds.Size.x = 0.f;
                break;
            case 7:
                input.Projection.Valid = false;
                break;
            case 8:
                input.Pointer.x = std::numeric_limits<float>::infinity();
                break;
            case 9:
                input.Axis      = {1.f, 0.f, 0.f};
                input.RingPoint = {};
                break;
            case 10:
                input.Pivot.z = 20.f;
                break;
        }
        EXPECT_FALSE(G::CaptureRotation(input, &output)) << failure;
        EXPECT_EQ(output.RadiusPx, 77.f);
    }
    EXPECT_FALSE(G::CaptureRotation(valid, nullptr));
}

TEST(RotationMathTest, LaterParallelRaysPreserveTheLastSampleWithoutSwitchingToTangent)
{
    const auto           projection = Projection();
    const Vec3f          axis(1.f, 0.f, .2f), radial(0.f, 1.f, 0.f);
    G::RotationReference reference = {};
    ASSERT_TRUE(G::CaptureRotation(Capture(projection, axis, radial), &reference));
    ASSERT_FALSE(reference.Tangent);
    const auto        tangent = cross3d(axis.normalize(), radial);
    G::RotationSample sample  = {};
    ASSERT_TRUE(G::SampleRotation(reference, {}, Screen(projection, radial * float(std::cos(.4)) + tangent * float(std::sin(.4))), 1.f, false, 0.f, &sample));
    const auto last    = sample;
    const auto horizon = Screen(projection, projection.Position + Vec3f(.2f, 0.f, -1.f));
    EXPECT_FALSE(G::SampleRotation(reference, last.Progress, horizon, 1.f, false, 0.f, &sample));
    EXPECT_DOUBLE_EQ(sample.Angle, last.Angle);
    EXPECT_DOUBLE_EQ(sample.Progress.TotalAngle, last.Progress.TotalAngle);
    ASSERT_TRUE(G::SampleRotation(reference, last.Progress, Screen(projection, radial * float(std::cos(.7)) + tangent * float(std::sin(.7))), 1.f, false, 0.f, &sample));
    EXPECT_NEAR(sample.Angle, .7, .00001);
    EXPECT_FALSE(reference.Tangent);
}

TEST(RotationMathTest, NoOpAndWholeTurnsReturnExactAuthoredValuesIncludingSignedZeroScale)
{
    const G::LocalTRS before = {
        { 2.f, -3.f,  4.f},
        { 7.f, -.3f, 12.f},
        {-0.f, -2.f,  3.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(ComposeTransformMatrix({3.f, 4.f, 5.f}, {.2f, .3f, .4f}, {-2.f, 3.f, 4.f}), before, {1.f, 2.f, 3.f}, .001f, &target));
    for (double angle : {0.0, 2.0 * Pi, -4.0 * Pi})
    {
        G::LocalTRS result = {};
        ASSERT_TRUE(G::RotateLocal(target, {0.f, 0.f, 1.f}, angle, &result));
        Exact(result, before);
    }
}

TEST(RotationMathTest, WorldRotationUsesRightHandedMotionAboutTheCapturedCustomPivot)
{
    const G::LocalTRS before = {
        {3.f, 2.f, 0.f},
        {},
        {-2.f, 3.f, .5f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {1.f, 2.f, 0.f}, .001f, &target));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::RotateLocal(target, {0.f, 0.f, 2.f}, Pi / 2.0, &result));
    EXPECT_NEAR(result.Position.x, 1.f, .00001f);
    EXPECT_NEAR(result.Position.y, 4.f, .00001f);
    EXPECT_NEAR(result.Rotation.z, -Pi / 2.0, .00001); // Engine's authored Euler convention.
    Near(ComposeTransformMatrix(result.Position, result.Rotation, result.Scale), ComposeTransformMatrix({1.f, 4.f, 0.f}, {0.f, 0.f, float(-Pi / 2.0)}, before.Scale));
}

TEST(RotationMathTest, ReflectedAndZeroAuthoredScalesRemainUsableWithoutDividingByScale)
{
    for (const Vec3f scale : {Vec3f(-2.f, 3.f, .5f), Vec3f(-0.f, -2.f, 0.f), Vec3f(0.f, 0.f, 0.f)})
    {
        const G::LocalTRS before = {
            {1.f,  2.f, 3.f},
            {.2f, -.3f, .4f},
            scale
        };
        G::RotationTarget target = {};
        ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, before.Position, .001f, &target));
        G::LocalTRS result = {};
        ASSERT_TRUE(G::RotateLocal(target, {0.f, 1.f, 0.f}, .7, &result));
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(result.Scale[axis], before.Scale[axis], .00001f);
            EXPECT_EQ(std::signbit(result.Scale[axis]), std::signbit(before.Scale[axis]));
        }
    }
}

TEST(RotationMathTest, NonUniformParentsRejectShearButAcceptRepresentableAxisPermutations)
{
    const auto        parent = ComposeTransformMatrix({4.f, 5.f, 6.f}, {}, {2.f, 1.f, 3.f});
    const G::LocalTRS before = {
        {},
        {},
        {-1.f, 2.f, 3.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(parent, before, {4.f, 5.f, 6.f}, .001f, &target));
    G::LocalTRS result = {
        .Position = {77.f, 0.f, 0.f}
    };
    const auto last = result;
    EXPECT_FALSE(G::RotateLocal(target, {0.f, 0.f, 1.f}, Pi / 4.0, &result));
    Exact(result, last);
    ASSERT_TRUE(G::RotateLocal(target, {0.f, 0.f, 1.f}, Pi / 2.0, &result));
    EXPECT_NEAR(result.Scale.x, -2.f, .00001f);
    EXPECT_NEAR(result.Scale.y, 1.f, .00001f);
    EXPECT_NEAR(result.Scale.z, 3.f, .00001f);
    const auto expected = ComposeTransformMatrix({4.f, 5.f, 6.f}, {0.f, 0.f, float(-Pi / 2.0)}, {-2.f, 2.f, 9.f});
    Near(parent * ComposeTransformMatrix(result.Position, result.Rotation, result.Scale), expected);
}

TEST(RotationMathTest, RotatedUniformParentsAndLocalAxesReconstructTheExpectedWorldFrame)
{
    std::mt19937                          random(0x7280);
    std::uniform_real_distribution<float> angles(-3.f, 3.f);
    for (uint32_t iteration = 0; iteration < 300; ++iteration)
    {
        const auto        parent = ComposeTransformMatrix({1.f, -2.f, 3.f}, {angles(random), angles(random), angles(random)}, {-2.f, 2.f, 2.f});
        const G::LocalTRS before = {
            {           .7f,            .4f,           -.3f},
            {angles(random), angles(random), angles(random)},
            {          -2.f,            3.f,            .5f}
        };
        const auto        start = parent * ComposeTransformMatrix(before.Position, before.Rotation, {1.f, 1.f, 1.f});
        const Vec3f       pivot(start(0, 3), start(1, 3), start(2, 3));
        const Vec3f       axis(start(0, 2), start(1, 2), start(2, 2));
        G::RotationTarget target = {};
        ASSERT_TRUE(G::CaptureRotationTarget(parent, before, pivot, .001f, &target));
        const double angle  = angles(random);
        G::LocalTRS  result = {};
        ASSERT_TRUE(G::RotateLocal(target, axis, angle, &result)) << iteration;
        for (uint32_t component = 0; component < 3; ++component)
            EXPECT_EQ(std::bit_cast<uint32_t>(result.Position[component]), std::bit_cast<uint32_t>(before.Position[component]));
        // A reflected parent reverses handedness: conjugating its reflected
        // world local-axis rotation produces the opposite authored local angle.
        const auto expected_local_rotation = ComposeTransformMatrix({}, before.Rotation, {1.f, 1.f, 1.f}) * ComposeTransformMatrix({}, {0.f, 0.f, float(angle)}, {1.f, 1.f, 1.f});
        auto       expected                = parent * expected_local_rotation;
        for (uint32_t row = 0; row < 3; ++row)
        {
            expected(row, 3) = pivot[row];
            for (uint32_t column = 0; column < 3; ++column)
                expected(row, column) *= before.Scale[column];
        }
        Near(parent * ComposeTransformMatrix(result.Position, result.Rotation, result.Scale), expected);
    }
}

TEST(RotationMathTest, EulerWriteBackStaysClosestToTheStartNearBothGimbalLocks)
{
    for (float sign : {-1.f, 1.f})
        for (float offset : {-.0001f, 0.f, .0001f})
        {
            const G::LocalTRS before = {
                {},
                {.4f, sign * float(Pi / 2.0) + offset, -.7f},
                {-2.f, 3.f, 4.f}
            };
            G::RotationTarget target = {};
            ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {}, .001f, &target));
            G::LocalTRS result = {};
            ASSERT_TRUE(G::RotateLocal(target, {0.f, 1.f, 0.f}, .00001, &result));
            for (uint32_t axis = 0; axis < 3; ++axis)
                EXPECT_LE(std::abs(double(result.Rotation[axis]) - before.Rotation[axis]), Pi);
            G::LocalTRS restored = {};
            ASSERT_TRUE(G::RotateLocal(target, {0.f, 1.f, 0.f}, 0.0, &restored));
            Exact(restored, before);
        }
}

TEST(RotationMathTest, SingularNonFiniteAndOverflowingTargetsCannotPublishOutput)
{
    const G::LocalTRS before = {
        {1.f, 2.f, 3.f},
        {},
        {1.f, 1.f, 1.f}
    };
    G::RotationTarget target = {.MaximumError = 77.f};
    EXPECT_FALSE(G::CaptureRotationTarget({}, before, {}, .001f, &target));
    EXPECT_EQ(target.MaximumError, 77.f);
    EXPECT_FALSE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {}, -1.f, &target));
    auto invalid       = before;
    invalid.Rotation.y = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(G::CaptureRotationTarget(Identity<Mat4f>(), invalid, {}, .001f, &target));
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {}, .001f, &target));
    G::LocalTRS result = {
        .Position = {77.f, 0.f, 0.f}
    };
    const auto last = result;
    EXPECT_FALSE(G::RotateLocal(target, {}, 1.0, &result));
    EXPECT_FALSE(G::RotateLocal(target, {0.f, 0.f, 1.f}, std::numeric_limits<double>::infinity(), &result));
    Exact(result, last);
    const auto huge = std::numeric_limits<float>::max();
    ASSERT_TRUE(
        G::CaptureRotationTarget(
            Identity<Mat4f>(),
            {
            {huge, huge, 0.f},
            {},
            {1.f, 1.f, 1.f}
    },
            {-huge, -huge, 0.f},
            .001f,
            &target));
    EXPECT_FALSE(G::RotateLocal(target, {0.f, 0.f, 1.f}, Pi, &result));
    Exact(result, last);
}

TEST(RotationMathTest, SharedDeltaRejectsInvalidInputsWithoutReplacingAcceptedCoefficientsOrOutput)
{
    G::RotationDelta delta = {};
    EXPECT_FALSE(delta.Valid);
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), {}, {}, .001f, &target));
    G::LocalTRS result = {
        .Position = {77.f, -0.f, 3.f}
    };
    const auto previous = result;
    EXPECT_FALSE(G::RotateLocal(target, delta, &result));
    Exact(result, previous);
    ASSERT_TRUE(G::MakeRotationDelta({1.f, 2.f, 3.f}, .7, &delta));
    const auto accepted = delta;
    EXPECT_FALSE(G::MakeRotationDelta({}, .7, &delta));
    EXPECT_FALSE(G::MakeRotationDelta({0.f, 0.f, 1.f}, std::numeric_limits<double>::infinity(), &delta));
    EXPECT_FALSE(G::MakeRotationDelta({0.f, std::numeric_limits<float>::quiet_NaN(), 1.f}, .7, &delta));
    EXPECT_FALSE(G::MakeRotationDelta({0.f, 0.f, 1.f}, .7, nullptr));
    for (size_t i = 0; i < delta.Coefficients.size(); ++i)
        EXPECT_DOUBLE_EQ(delta.Coefficients[i], accepted.Coefficients[i]);
    EXPECT_EQ(delta.Valid, accepted.Valid);
    EXPECT_EQ(delta.Identity, accepted.Identity);
    EXPECT_FALSE(G::RotateLocal({}, delta, &result));
    EXPECT_FALSE(G::RotateLocal(target, delta, nullptr));
    Exact(result, previous);
    static_assert(sizeof(G::RotationDelta) <= 80);
}

TEST(RotationMathTest, OneImmutableSharedDeltaMatchesIndividualRootsAcrossReflectedParentFrames)
{
    G::RotationDelta delta = {};
    const Vec3f      axis(1.f, 2.f, -3.f);
    ASSERT_TRUE(G::MakeRotationDelta(axis, -.7, &delta));
    const auto accepted = delta;
    for (uint32_t i = 0; i < 100; ++i)
    {
        SCOPED_TRACE(i);
        const auto        parent = ComposeTransformMatrix({float(i % 7), -2.f, 3.f}, {.2f, -.3f, float(i) * .01f}, {-2.f, 2.f, 2.f});
        const G::LocalTRS before = {
            {float(i % 5),                .7f, -.4f},
            {         .4f,    float(i) * .02f, -.3f},
            {        -1.f, i % 2 ? -0.f : 2.f,  .5f}
        };
        G::RotationTarget target = {};
        ASSERT_TRUE(G::CaptureRotationTarget(parent, before, {1.f, -2.f, 3.f}, .001f, &target));
        G::LocalTRS shared = {}, individual = {};
        ASSERT_TRUE(G::RotateLocal(target, delta, &shared));
        ASSERT_TRUE(G::RotateLocal(target, axis, -.7, &individual));
        Exact(shared, individual);
    }
    for (size_t i = 0; i < delta.Coefficients.size(); ++i)
        EXPECT_DOUBLE_EQ(delta.Coefficients[i], accepted.Coefficients[i]);
}

TEST(RotationMathTest, SharedCompleteTurnsRestoreExactAuthoredValuesWithoutSwallowingSmallMotion)
{
    const G::LocalTRS before = {
        { 2.f, -0.f, 0.f},
        { .2f, -.3f, .4f},
        {-1.f, -0.f, 0.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {}, .001f, &target));
    for (double angle : {0.0, -0.0, 2.0 * Pi, -4.0 * Pi, std::nextafter(2.0 * Pi, 0.0)})
    {
        G::RotationDelta delta = {};
        ASSERT_TRUE(G::MakeRotationDelta({0.f, 0.f, 1.f}, angle, &delta));
        EXPECT_TRUE(delta.Identity);
        G::LocalTRS result = {};
        ASSERT_TRUE(G::RotateLocal(target, delta, &result));
        Exact(result, before);
    }
    G::RotationDelta delta = {};
    ASSERT_TRUE(G::MakeRotationDelta({0.f, 0.f, 1.f}, 1e-8, &delta));
    EXPECT_FALSE(delta.Identity);
    EXPECT_GT(delta.Coefficients[3], 0.0);
}

TEST(RotationMathTest, SharedRotationRejectsLostSmallOrbitDisplacementAtLargeWorldOrigins)
{
    const G::LocalTRS before = {
        {1e12f, 1e12f, 0.f},
        {},
        {1.f, 1.f, 1.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(Identity<Mat4f>(), before, {1e12f, 1e12f - 65536.f, 0.f}, .001f, &target));
    G::LocalTRS result = {
        .Position = {77.f, -0.f, 3.f}
    };
    const auto       previous = result;
    G::RotationDelta delta    = {};
    ASSERT_TRUE(G::MakeRotationDelta({0.f, 0.f, 1.f}, 1e-5, &delta));
    EXPECT_FALSE(G::RotateLocal(target, delta, &result));
    Exact(result, previous);
    ASSERT_TRUE(G::MakeRotationDelta({0.f, 0.f, 1.f}, Pi / 2.0, &delta));
    ASSERT_TRUE(G::RotateLocal(target, delta, &result));
    EXPECT_EQ(result.Position.x, before.Position.x - 65536.f);
    EXPECT_EQ(result.Position.y, before.Position.y - 65536.f);
}

TEST(RotationMathTest, SharedRotationRejectsUnderflowRatherThanCollapsingAnAuthoredScale)
{
    const G::LocalTRS before = {
        {},
        {},
        {1.f, std::numeric_limits<float>::denorm_min(), 1.f}
    };
    G::RotationTarget target = {};
    ASSERT_TRUE(G::CaptureRotationTarget(ComposeTransformMatrix({}, {}, {2.f, 1.f, 1.f}), before, {}, .001f, &target));
    G::RotationDelta delta = {};
    ASSERT_TRUE(G::MakeRotationDelta({0.f, 0.f, 1.f}, Pi / 2.0, &delta));
    G::LocalTRS result = {
        .Position = {77.f, -0.f, 3.f}
    };
    const auto previous = result;
    EXPECT_FALSE(G::RotateLocal(target, delta, &result));
    Exact(result, previous);
}
