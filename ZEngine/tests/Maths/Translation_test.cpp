#include <ZEngine/Core/Maths/Geometry.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <bit>
#include <cmath>
#include <limits>
#include <random>

using namespace ZEngine::Core::Maths;
namespace G = ZEngine::Core::Maths::Geometry;

namespace
{
    constexpr float   Epsilon = 2e-4f;
    const G::Viewport Bounds  = {
        { 37.f,  61.f},
        {800.f, 600.f}
    };

    G::ProjectionFrame Projection(bool orthographic = false, bool reverse = false, bool oblique = true)
    {
        using namespace ZEngine::Rendering::Cameras;
        CameraSetting settings = {};
        settings.FOV           = 60.f;
        settings.FarPlane      = 1000.f;
        FlyCamera camera(4.f / 3.f, settings);
        camera.SetPosition(oblique ? Vec3f(7.f, 5.f, 10.f) : Vec3f(0.f, 0.f, 10.f));
        camera.SetOrientation(oblique ? 25.f : 0.f, oblique ? -35.f : 0.f);
        if (orthographic)
            camera.SetProjectionType(CameraType::ORTHOGRAPHIC);
        auto captured = camera.CaptureFrameData();
        if (reverse)
            for (uint32_t col = 0; col < 4; ++col)
                captured.Projection(2, col) = captured.Projection(3, col) - captured.Projection(2, col);
        G::ProjectionFrame result = {};
        EXPECT_TRUE(G::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, orthographic ? G::ProjectionKind::Orthographic : G::ProjectionKind::Perspective, reverse, &result));
        return result;
    }

    Vec2f Screen(const G::ProjectionFrame& projection, const Vec3f& point)
    {
        G::ProjectedPoint result = {};
        EXPECT_TRUE(G::Project(point, projection, Bounds, &result));
        return result.Screen;
    }

    void Near(const Vec3f& actual, const Vec3f& expected, float tolerance = Epsilon)
    {
        for (uint32_t axis = 0; axis < 3; ++axis)
            EXPECT_NEAR(actual[axis], expected[axis], tolerance) << axis;
    }

    G::TranslationCapture Capture(const G::ProjectionFrame& projection, uint8_t axes, const Vec3f& anchor = {}, const Vec3f& grab = {})
    {
        return {.Projection = projection, .Bounds = Bounds, .Pointer = Screen(projection, anchor + grab), .Anchor = anchor, .AxisMinDenominator = 1e-4f, .PlaneMinCosine = 1e-3f, .MinimumBasisVolume = 1e-6f, .Axes = axes};
    }

    Vec3f Direction(uint32_t axis)
    {
        Vec3f direction = {};
        direction[axis] = 1.f;
        return direction;
    }
} // namespace

TEST(TranslationMathTest, RecordsDefaultToNoLiveReferenceOrTarget)
{
    G::TranslationCapture   capture   = {};
    G::TranslationReference reference = {};
    G::TranslationTarget    target    = {};
    G::TranslationSample    sample    = {};
    EXPECT_EQ(capture.Axes, 0u);
    EXPECT_FALSE(reference.Valid);
    EXPECT_FALSE(target.Valid);
    Near(sample.Delta, {});
    Near(sample.Anchor, {});
    EXPECT_FALSE(G::CaptureTranslation(capture, &reference));
    EXPECT_FALSE(G::SampleTranslation(reference, {}, 1.f, false, 0.f, &sample));
    G::LocalTRS local = {};
    EXPECT_FALSE(G::TranslateLocal(target, {}, &local));
    static_assert(sizeof(G::TranslationReference) <= 512);
    static_assert(sizeof(G::TranslationTarget) <= 256);
}

TEST(TranslationMathTest, AllAxesUseTheirCapturedRayParameterAcrossProjectionAndDepthModes)
{
    for (bool orthographic : {false, true})
        for (bool reverse : {false, true})
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const auto              projection = Projection(orthographic, reverse);
                const auto              direction  = Direction(axis);
                const Vec3f             anchor(0.13f, -0.27f, 0.11f);
                const auto              capture   = Capture(projection, uint8_t(1u << axis), anchor, direction * .7f);
                G::TranslationReference reference = {};
                ASSERT_TRUE(G::CaptureTranslation(capture, &reference));
                const float start_parameter = reference.StartAxisParameter;
                for (float delta : {-2.f, -.2f, .4f, 1.2f})
                {
                    G::TranslationSample sample = {};
                    ASSERT_TRUE(G::SampleTranslation(reference, Screen(projection, anchor + direction * (.7f + delta)), 1.f, false, 0.f, &sample));
                    Near(sample.Delta, direction * delta);
                    Near(sample.Anchor, anchor + direction * delta);
                }
                EXPECT_FLOAT_EQ(reference.StartAxisParameter, start_parameter);
                Near(reference.Anchor, anchor, 0.f);
            }
}

TEST(TranslationMathTest, EveryPlaneRetainsTheGrabOffsetAndOnlyMovesInItsCapturedSpan)
{
    for (bool orthographic : {false, true})
        for (bool reverse : {false, true})
            for (uint32_t normal = 0; normal < 3; ++normal)
            {
                const auto              projection = Projection(orthographic, reverse);
                const auto              a = Direction((normal + 1) % 3), b = Direction((normal + 2) % 3);
                const Vec3f             anchor(.13f, -.27f, .11f);
                const auto              grab      = a * .4f + b * .7f;
                const auto              delta     = a * -1.25f + b * .35f;
                G::TranslationReference reference = {};
                ASSERT_TRUE(G::CaptureTranslation(Capture(projection, uint8_t(7u ^ (1u << normal)), anchor, grab), &reference));
                G::TranslationSample sample = {};
                ASSERT_TRUE(G::SampleTranslation(reference, Screen(projection, anchor + grab + delta), 1.f, false, 0.f, &sample));
                Near(sample.Delta, delta);
                EXPECT_FLOAT_EQ(sample.Delta[normal], 0.f);
            }
}

TEST(TranslationMathTest, FreeMotionUsesTheCapturedCameraFacingPlaneAndAllowsOffViewportPointers)
{
    for (bool orthographic : {false, true})
    {
        const auto              projection = Projection(orthographic);
        const auto              a          = cross3d(projection.Forward, {0.f, 1.f, 0.f}).normalize();
        const auto              b          = cross3d(a, projection.Forward);
        const auto              grab       = a * .3f;
        const auto              delta      = a * 30.f + b * -4.f;
        G::TranslationReference reference  = {};
        ASSERT_TRUE(G::CaptureTranslation(Capture(projection, 7, {}, grab), &reference));
        const auto pointer = Screen(projection, grab + delta);
        EXPECT_TRUE(pointer.x < Bounds.Origin.x || pointer.x > Bounds.Origin.x + Bounds.Size.x);
        G::TranslationSample sample = {};
        ASSERT_TRUE(G::SampleTranslation(reference, pointer, 1.f, false, 0.f, &sample));
        Near(sample.Delta, delta, .002f);
        EXPECT_NEAR(dot(sample.Delta, projection.Forward), 0.f, .002f);
    }
}

TEST(TranslationMathTest, AbsoluteSnappingChangesOnlyConstrainedCoordinatesAndNeverSnapsAClick)
{
    const auto  projection = Projection(false, false, false);
    const Vec3f anchor(.13f, .27f, .11f), grab(.7f, .3f, 0.f), delta(.71f, -.43f, 0.f);
    for (uint8_t mask : {uint8_t(1), uint8_t(3), uint8_t(7)})
    {
        const auto              offset    = mask == 1 ? Vec3f(grab.x, 0.f, 0.f) : grab;
        G::TranslationReference reference = {};
        ASSERT_TRUE(G::CaptureTranslation(Capture(projection, mask, anchor, offset), &reference));
        G::TranslationSample sample = {};
        ASSERT_TRUE(G::SampleTranslation(reference, reference.Pointer, 1.f, true, .25f, &sample));
        Near(sample.Delta, {}, 0.f);
        Near(sample.Anchor, anchor, 0.f);
        ASSERT_TRUE(G::SampleTranslation(reference, Screen(projection, anchor + offset + delta), 1.f, true, .25f, &sample));
        EXPECT_NEAR(sample.Anchor.x, .75f, Epsilon);
        EXPECT_NEAR(sample.Anchor.y, mask == 1 ? .27f : -.25f, Epsilon);
        EXPECT_NEAR(sample.Anchor.z, mask == 7 ? 0.f : .11f, Epsilon);
        ASSERT_TRUE(G::SampleTranslation(reference, reference.Pointer, 1.f, true, .25f, &sample));
        Near(sample.Delta, {}, 0.f);
        Near(sample.Anchor, anchor, 0.f);
    }
}

TEST(TranslationMathTest, SkewedScaledLocalAxesUseReciprocalCoordinatesForSnapping)
{
    const auto  projection = Projection();
    const Vec3f a(1.f, 0.f, 0.f), b(.6f, .8f, 0.f), c(0.f, 0.f, 1.f);
    const auto  anchor  = a * .13f + b * .27f + c * .11f;
    const auto  grab    = a * .7f + b * .3f;
    auto        capture = Capture(projection, 3, anchor, grab);
    for (uint32_t row = 0; row < 3; ++row)
    {
        capture.Basis(row, 0) = a[row] * 2.f;
        capture.Basis(row, 1) = b[row] * 3.f;
        capture.Basis(row, 2) = c[row] * -4.f;
    }
    G::TranslationReference reference = {};
    ASSERT_TRUE(G::CaptureTranslation(capture, &reference));
    G::TranslationSample sample = {};
    ASSERT_TRUE(G::SampleTranslation(reference, Screen(projection, anchor + grab + a * .71f - b * .43f), 1.f, true, .25f, &sample));
    Near(sample.Delta, a * .62f - b * .52f);
    EXPECT_FLOAT_EQ(sample.Delta.z, 0.f);
}

TEST(TranslationMathTest, FineMotionScalesScreenDisplacementBeforePerspectiveUnprojection)
{
    const auto              projection = Projection();
    G::TranslationReference reference  = {};
    ASSERT_TRUE(G::CaptureTranslation(Capture(projection, 4, {}, {0.f, 0.f, .7f}), &reference));
    const auto           end  = Screen(projection, {0.f, 0.f, 4.f});
    G::TranslationSample fine = {}, raw = {}, expected = {};
    ASSERT_TRUE(G::SampleTranslation(reference, end, .1f, false, 0.f, &fine));
    ASSERT_TRUE(G::SampleTranslation(reference, end, 1.f, false, 0.f, &raw));
    ASSERT_TRUE(G::SampleTranslation(reference, reference.Pointer + (end - reference.Pointer) * .1f, 1.f, false, 0.f, &expected));
    Near(fine.Delta, expected.Delta);
    EXPECT_GT(std::abs(fine.Delta.z - raw.Delta.z * .1f), .01f);
}

TEST(TranslationMathTest, ParallelSamplesPreserveTheLastValidPreviewAndCanRecover)
{
    const auto projection = Projection(false, false, false);
    for (uint8_t mask : {uint8_t(4), uint8_t(6)})
    {
        const Vec3f             anchor(1.f, 0.f, 0.f), grab(0.f, 0.f, .7f);
        G::TranslationReference reference = {};
        ASSERT_TRUE(G::CaptureTranslation(Capture(projection, mask, anchor, grab), &reference));
        G::TranslationSample sample = {};
        ASSERT_TRUE(G::SampleTranslation(reference, Screen(projection, anchor + grab + Vec3f(0.f, 0.f, .4f)), 1.f, false, 0.f, &sample));
        const auto last = sample;
        EXPECT_FALSE(G::SampleTranslation(reference, Screen(projection, {}), 1.f, false, 0.f, &sample));
        Near(sample.Delta, last.Delta, 0.f);
        Near(sample.Anchor, last.Anchor, 0.f);
        ASSERT_TRUE(G::SampleTranslation(reference, reference.Pointer, 1.f, false, 0.f, &sample));
        Near(sample.Delta, {}, 0.f);
    }
}

TEST(TranslationMathTest, InvalidCaptureNeverReplacesAnExistingReference)
{
    const auto              projection = Projection();
    const auto              valid      = Capture(projection, 1);
    G::TranslationReference output     = {
        .Anchor = {42.f, 0.f, 0.f}
    };
    const auto reject = [&](const G::TranslationCapture& capture) {
        EXPECT_FALSE(G::CaptureTranslation(capture, &output));
        EXPECT_FALSE(output.Valid);
        EXPECT_FLOAT_EQ(output.Anchor.x, 42.f);
    };
    reject({});
    auto bad = valid;
    bad.Axes = 0;
    reject(bad);
    bad      = valid;
    bad.Axes = 8;
    reject(bad);
    bad                    = valid;
    bad.AxisMinDenominator = 1.f;
    reject(bad);
    bad                = valid;
    bad.PlaneMinCosine = -1.f;
    reject(bad);
    bad                    = valid;
    bad.MinimumBasisVolume = std::numeric_limits<float>::quiet_NaN();
    reject(bad);
    bad               = valid;
    bad.Bounds.Size.y = 0.f;
    reject(bad);
    bad           = valid;
    bad.Pointer.x = std::numeric_limits<float>::infinity();
    reject(bad);
    bad          = valid;
    bad.Anchor.x = std::numeric_limits<float>::quiet_NaN();
    reject(bad);
    bad             = valid;
    bad.Basis(0, 3) = 1.f;
    reject(bad);
    bad             = valid;
    bad.Basis(3, 0) = 1.f;
    reject(bad);
    bad = valid;
    for (uint32_t row = 0; row < 3; ++row)
        bad.Basis(row, 1) = bad.Basis(row, 0);
    reject(bad);
    bad = Capture(Projection(false, false, false), 4);
    reject(bad);
    EXPECT_FALSE(G::CaptureTranslation(valid, nullptr));
}

TEST(TranslationMathTest, InvalidSamplesLeaveBothOutputVectorsUntouched)
{
    G::TranslationReference reference = {};
    ASSERT_TRUE(G::CaptureTranslation(Capture(Projection(), 1), &reference));
    G::TranslationSample sample = {
        {11.f, 12.f, 13.f},
        {21.f, 22.f, 23.f}
    };
    const auto pointer = reference.Pointer + Vec2f(20.f, 0.f);
    EXPECT_FALSE(G::SampleTranslation(reference, {std::numeric_limits<float>::quiet_NaN(), 0.f}, 1.f, false, 0.f, &sample));
    EXPECT_FALSE(G::SampleTranslation(reference, pointer, 0.f, false, 0.f, &sample));
    EXPECT_FALSE(G::SampleTranslation(reference, pointer, 1.f, true, 0.f, &sample));
    EXPECT_FALSE(G::SampleTranslation(reference, pointer, 1.f, true, std::numeric_limits<float>::infinity(), &sample));
    EXPECT_FALSE(G::SampleTranslation(reference, {std::numeric_limits<float>::max(), 0.f}, std::numeric_limits<float>::max(), false, 0.f, &sample));
    Near(sample.Delta, {11.f, 12.f, 13.f}, 0.f);
    Near(sample.Anchor, {21.f, 22.f, 23.f}, 0.f);
    EXPECT_FALSE(G::SampleTranslation(reference, pointer, 1.f, false, 0.f, nullptr));
}

TEST(TranslationMathTest, SamplingIsIndependentOfSkippedIntermediatePointerUpdates)
{
    G::TranslationReference reference  = {};
    const auto              projection = Projection();
    ASSERT_TRUE(G::CaptureTranslation(Capture(projection, 3), &reference));
    const auto           end    = Screen(projection, {1.33f, -1.17f, 0.f});
    G::TranslationSample direct = {}, stepped = {};
    ASSERT_TRUE(G::SampleTranslation(reference, end, 1.f, true, .1f, &direct));
    for (uint32_t i = 0; i < 2000; ++i)
        ASSERT_TRUE(G::SampleTranslation(reference, reference.Pointer + (end - reference.Pointer) * (float(i) / 1999.f), 1.f, true, .1f, &stepped));
    Near(stepped.Delta, direct.Delta, 0.f);
    Near(stepped.Anchor, direct.Anchor, 0.f);
}

TEST(TranslationMathTest, ParentLocalConversionPreservesAuthoredRotationAndSignedScales)
{
    std::mt19937                          random(0x79d3u);
    std::uniform_real_distribution<float> values(-2.f, 2.f);
    for (uint32_t i = 0; i < 1000; ++i)
    {
        const G::LocalTRS before = {
            {values(random), values(random), values(random)},
            {values(random), values(random), values(random)},
            {          -2.f,            0.f,            3.f}
        };
        auto parent = ComposeTransformMatrix({values(random), values(random), values(random)}, {values(random), values(random), values(random)}, {-2.f, 3.f, .5f});
        if (i & 1u)
            parent(0, 1) += .3f; // translation is still representable under an affine shear
        G::TranslationTarget target = {};
        ASSERT_TRUE(G::CaptureTranslationTarget(parent, before, .001f, &target));
        const Vec3f delta(values(random), values(random), values(random));
        G::LocalTRS result = {};
        ASSERT_TRUE(G::TranslateLocal(target, delta, &result));
        const auto world_before = parent * ComposeTransformMatrix(before.Position, before.Rotation, before.Scale);
        const auto world_after  = parent * ComposeTransformMatrix(result.Position, result.Rotation, result.Scale);
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            EXPECT_NEAR(world_after(axis, 3) - world_before(axis, 3), delta[axis], Epsilon);
            EXPECT_EQ(std::bit_cast<uint32_t>(result.Rotation[axis]), std::bit_cast<uint32_t>(before.Rotation[axis]));
            EXPECT_EQ(std::bit_cast<uint32_t>(result.Scale[axis]), std::bit_cast<uint32_t>(before.Scale[axis]));
        }
    }
}

TEST(TranslationMathTest, ZeroDeltaPreservesEveryAuthoredFloatBitIncludingNegativeZero)
{
    G::LocalTRS before = {
        {-0.f,  2.f, -0.f},
        {-0.f,  7.f, -8.f},
        {-0.f, -2.f,  3.f}
    };
    G::TranslationTarget target = {};
    ASSERT_TRUE(G::CaptureTranslationTarget(Identity<Mat4f>(), before, .001f, &target));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::TranslateLocal(target, {}, &result));
    for (uint32_t axis = 0; axis < 3; ++axis)
    {
        EXPECT_EQ(std::bit_cast<uint32_t>(result.Position[axis]), std::bit_cast<uint32_t>(before.Position[axis]));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.Rotation[axis]), std::bit_cast<uint32_t>(before.Rotation[axis]));
        EXPECT_EQ(std::bit_cast<uint32_t>(result.Scale[axis]), std::bit_cast<uint32_t>(before.Scale[axis]));
    }
}

TEST(TranslationMathTest, NonzeroAxisMotionPreservesSignedZeroOnUnchangedLocalAxes)
{
    const G::LocalTRS before = {
        {0.f, -0.f, -0.f}
    };
    G::TranslationTarget target = {};
    ASSERT_TRUE(G::CaptureTranslationTarget(Identity<Mat4f>(), before, .001f, &target));
    G::LocalTRS result = {};
    ASSERT_TRUE(G::TranslateLocal(target, {1.f, 0.f, 0.f}, &result));
    EXPECT_FLOAT_EQ(result.Position.x, 1.f);
    EXPECT_EQ(std::bit_cast<uint32_t>(result.Position.y), std::bit_cast<uint32_t>(before.Position.y));
    EXPECT_EQ(std::bit_cast<uint32_t>(result.Position.z), std::bit_cast<uint32_t>(before.Position.z));
}

TEST(TranslationMathTest, SingularInvalidAndUnrepresentableParentResultsFailWithoutReplacingOutput)
{
    G::TranslationTarget target = {};
    EXPECT_FALSE(G::CaptureTranslationTarget({}, {}, .001f, &target));
    auto parent  = Identity<Mat4f>();
    parent(3, 0) = 1.f;
    EXPECT_FALSE(G::CaptureTranslationTarget(parent, {}, .001f, &target));
    EXPECT_FALSE(G::CaptureTranslationTarget(Identity<Mat4f>(), {}, -1.f, &target));
    EXPECT_FALSE(G::CaptureTranslationTarget(Identity<Mat4f>(), {}, .001f, nullptr));
    parent       = Identity<Mat4f>();
    parent(0, 0) = std::numeric_limits<float>::max();
    EXPECT_FALSE(
        G::CaptureTranslationTarget(
            parent,
            {
            {},
            {},
            {2.f, 1.f, 1.f}
    },
            .001f,
            &target));
    G::LocalTRS result = {
        {42.f, 43.f, 44.f}
    };
    ASSERT_TRUE(
        G::CaptureTranslationTarget(
            Identity<Mat4f>(),
            {
            {1e20f, 0.f, 0.f}
    },
            .001f,
            &target));
    EXPECT_FALSE(G::TranslateLocal(target, {1.f, 0.f, 0.f}, &result));
    Near(result.Position, {42.f, 43.f, 44.f}, 0.f);
    parent       = Identity<Mat4f>();
    parent(0, 0) = 1e-38f;
    ASSERT_TRUE(G::CaptureTranslationTarget(parent, {}, .001f, &target));
    EXPECT_FALSE(G::TranslateLocal(target, {4.f, 0.f, 0.f}, &result));
    EXPECT_FALSE(G::TranslateLocal(target, {std::numeric_limits<float>::infinity(), 0.f, 0.f}, &result));
    Near(result.Position, {42.f, 43.f, 44.f}, 0.f);
    EXPECT_FALSE(G::TranslateLocal(target, {}, nullptr));
}
