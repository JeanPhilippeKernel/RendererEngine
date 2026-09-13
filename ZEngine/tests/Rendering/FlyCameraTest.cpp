#include <ZEngine/Core/Maths/MathUtils.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <cmath>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::Rendering::Cameras;

TEST(FlyCameraTest, ViewportExtentUpdatesProjectionAndCursorRay)
{
    CameraSetting settings = {};
    FlyCamera     camera(16.0f / 9.0f, settings);
    camera.SetOrientation(0.0f, 0.0f);
    camera.SetViewportSize(1600.0f, 800.0f);

    const CameraFrameData frame        = camera.CaptureFrameData();
    const float           tan_half_fov = std::tan(radians(settings.FOV) * 0.5f);
    EXPECT_NEAR(camera.AspectRatio, 2.0f, 1e-6f);
    EXPECT_NEAR(frame.Projection(0, 0), 1.0f / (2.0f * tan_half_fov), 1e-6f);

    const FlyCamera::Ray right_ray = camera.GetRayFromViewport(1600.0f, 400.0f);
    EXPECT_NEAR(dot(right_ray.Direction, camera.GetRight()) / dot(right_ray.Direction, camera.GetForward()), 2.0f * tan_half_fov, 1e-5f);
}

TEST(FlyCameraTest, EmptyViewportRetainsLastValidProjection)
{
    FlyCamera camera(16.0f / 9.0f, {});
    camera.SetViewportSize(1200.0f, 600.0f);
    const CameraFrameData before = camera.CaptureFrameData();

    camera.SetViewportSize(0.0f, 600.0f);
    const CameraFrameData after = camera.CaptureFrameData();

    EXPECT_NEAR(camera.AspectRatio, 2.0f, 1e-6f);
    EXPECT_NEAR(after.Projection(0, 0), before.Projection(0, 0), 1e-6f);
    EXPECT_NEAR(after.Projection(1, 1), before.Projection(1, 1), 1e-6f);
}

TEST(FlyCameraTest, FrameSnapshotContainsTheLatestCameraState)
{
    FlyCamera camera(16.0f / 9.0f, {});
    camera.SetPosition({3.0f, 4.0f, 5.0f});

    const CameraFrameData frame = camera.CaptureFrameData();
    EXPECT_NEAR(frame.Position.x, 3.0f, 1e-6f);
    EXPECT_NEAR(frame.Position.y, 4.0f, 1e-6f);
    EXPECT_NEAR(frame.Position.z, 5.0f, 1e-6f);
}

TEST(FlyCameraTest, MouseLookAppliesPointerDelta)
{
    CameraSetting settings = {};
    FlyCamera     camera(16.0f / 9.0f, settings);
    camera.SetOrientation(0.0f, 0.0f);
    camera.SetViewportSize(1000.0f, 1000.0f);

    camera.Input.RightDown   = true;
    camera.Input.MouseDeltaX = 100.0f;
    camera.Input.MouseDeltaY = -40.0f;
    camera.OnUpdate(1.0f / 120.0f);

    const float expected_yaw   = -0.1f * PI<float> * settings.RotationSpeed;
    const float expected_pitch = 0.04f * PI<float> * settings.RotationSpeed;
    EXPECT_NEAR(camera.Yaw, expected_yaw, 1e-6f);
    EXPECT_NEAR(camera.Pitch, expected_pitch, 1e-6f);

    camera.Input.MouseDeltaX = 0.0f;
    camera.Input.MouseDeltaY = 0.0f;
    camera.OnUpdate(1.0f / 120.0f);
    EXPECT_NEAR(camera.Yaw, expected_yaw, 1e-4f);
    EXPECT_NEAR(camera.Pitch, expected_pitch, 1e-4f);
}

TEST(FlyCameraTest, YawIsContinuousAcrossZero)
{
    FlyCamera camera(16.0f / 9.0f, {});
    camera.SetOrientation(0.0f, -0.1f);
    const Vec3f left_of_zero = camera.GetForward();
    camera.SetOrientation(0.0f, 0.1f);
    const Vec3f right_of_zero = camera.GetForward();

    EXPECT_LT(left_of_zero.x, 0.0f);
    EXPECT_GT(right_of_zero.x, 0.0f);
    EXPECT_NEAR(left_of_zero.x, -right_of_zero.x, 1e-6f);
    EXPECT_NEAR(left_of_zero.z, right_of_zero.z, 1e-6f);
}

TEST(FlyCameraTest, OrbitExitsWhenAltIsReleasedWhileLeftMouseIsHeld)
{
    FlyCamera camera(16.0f / 9.0f, {});
    camera.Input.AltDown  = true;
    camera.Input.LeftDown = true;
    camera.OnUpdate(1.0f / 60.0f);
    ASSERT_EQ(camera.State, FlyCameraState::Orbit);

    camera.Input.AltDown  = false;
    camera.Input.LeftDown = true;
    camera.OnUpdate(1.0f / 60.0f);
    EXPECT_EQ(camera.State, FlyCameraState::Free);
}
