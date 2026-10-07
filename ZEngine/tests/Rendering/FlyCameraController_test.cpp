#include <GLFW/glfw3.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>
#include <limits>

using namespace ZEngine;
using namespace ZEngine::Controllers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Input;
using namespace ZEngine::Rendering::Cameras;

namespace
{
    struct TestFlyCameraController final : FlyCameraController
    {
        using FlyCameraController::EnterFly;

        void SetCamera(FlyCamera* camera)
        {
            m_camera = camera;
        }

        bool IsFlying() const
        {
            return m_state == CamState::Fly;
        }

        bool IsInputEnabled() const
        {
            return m_input_enabled;
        }

        bool IsHovered() const
        {
            return m_state == CamState::Hover;
        }

        float ViewportBound(uint32_t index) const
        {
            return m_vp[index];
        }
    };

    struct FlyCameraControllerTest : ::testing::Test
    {
        MemoryManager           Memory{};
        InputManager            Input{};
        FlyCamera               Camera{16.0f / 9.0f, {}};
        TestFlyCameraController Controller{};

        void                    SetUp() override
        {
            Memory.Initialize(ZMega(4ULL), {});
            Input.Initialize(&Memory.MainArena);
            Controller.Initialize(&Input, &Memory.MainArena);
            Controller.SetCamera(&Camera);
        }

        void TearDown() override
        {
            Input.Dispose();
            Memory.Shutdown();
        }
    };
} // namespace

TEST_F(FlyCameraControllerTest, PointerCaptureExitsFlyAndClearsDirectCameraInput)
{
    Controller.EnterFly();
    Camera.Input.RightDown        = true;
    Camera.Input.Keys[GLFW_KEY_W] = true;

    Controller.SetInputCapture(true, false);

    EXPECT_FALSE(Controller.IsFlying());
    EXPECT_FALSE(Camera.Input.RightDown);
    EXPECT_FALSE(Camera.Input.Keys[GLFW_KEY_W]);
}

TEST_F(FlyCameraControllerTest, KeyboardCaptureExitsFlyAndReleaseAllowsASecondFlySession)
{
    Controller.EnterFly();
    Controller.SetInputCapture(false, true);
    EXPECT_FALSE(Controller.IsFlying());

    Controller.SetInputCapture(false, false);
    Controller.EnterFly();
    EXPECT_TRUE(Controller.IsFlying());
}

TEST_F(FlyCameraControllerTest, PauseAndResumeControlCameraInputLifecycle)
{
    Controller.EnterFly();
    Controller.PauseEventProcessing();

    EXPECT_FALSE(Controller.IsFlying());
    EXPECT_FALSE(Controller.IsInputEnabled());

    Controller.ResumeEventProcessing();
    EXPECT_TRUE(Controller.IsInputEnabled());
}

TEST_F(FlyCameraControllerTest, ScaledUiViewportUsesWindowCoordinatesForHoverAndRays)
{
    const float rect[4]        = {100.f, 150.f, 900.f, 750.f};
    const float input_scale[2] = {0.8f, 0.75f};
    Controller.SetViewportFromUI(rect, input_scale);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(0), 125.f);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(1), 200.f);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(2), 1125.f);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(3), 1000.f);
    EXPECT_FLOAT_EQ(Camera.AspectRatio, 1000.f / 800.f);

    // This raw cursor lies inside the displayed viewport, but outside the old
    // unconverted UI bounds. It must remain available to camera navigation.
    Input.AccumulateCursorPosition(1000, 600);
    Controller.Update(Core::TimeStep(0.f));
    EXPECT_TRUE(Controller.IsHovered());
    EXPECT_FLOAT_EQ(Camera.Input.MouseViewportX, 875.f);
    EXPECT_FLOAT_EQ(Camera.Input.MouseViewportY, 400.f);

    const auto ray     = Camera.GetRayFromViewport(500.f, 400.f);
    const auto forward = Camera.GetForward();
    EXPECT_NEAR(ray.Direction.x, forward.x, 0.0001f);
    EXPECT_NEAR(ray.Direction.y, forward.y, 0.0001f);
    EXPECT_NEAR(ray.Direction.z, forward.z, 0.0001f);

    Input.AccumulateCursorPosition(1200, 600);
    Controller.Update(Core::TimeStep(0.f));
    EXPECT_FALSE(Controller.IsHovered());
}

TEST_F(FlyCameraControllerTest, NativeWaylandAndRetinaInputNeedsNoExtraDpiScaling)
{
    const float rect[4]        = {100.f, 150.f, 900.f, 750.f};
    const float input_scale[2] = {1.f, 1.f};
    Controller.SetViewportFromUI(rect, input_scale);
    for (uint32_t index = 0; index < 4; ++index)
        EXPECT_FLOAT_EQ(Controller.ViewportBound(index), rect[index]);
    EXPECT_FLOAT_EQ(Camera.AspectRatio, 800.f / 600.f);

    Input.AccumulateCursorPosition(500, 450);
    Controller.Update(Core::TimeStep(0.f));
    EXPECT_TRUE(Controller.IsHovered());
    EXPECT_FLOAT_EQ(Camera.Input.MouseViewportX, 400.f);
    EXPECT_FLOAT_EQ(Camera.Input.MouseViewportY, 300.f);
}

TEST_F(FlyCameraControllerTest, DpiTransitionUpdatesBoundsAndExtentWithoutStaleScale)
{
    const float rect[4]   = {100.f, 150.f, 900.f, 750.f};
    const float scaled[2] = {0.8f, 0.75f};
    const float native[2] = {1.f, 1.f};
    Controller.SetViewportFromUI(rect, scaled);
    Controller.SetViewportFromUI(rect, native);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(0), 100.f);
    EXPECT_FLOAT_EQ(Controller.ViewportBound(2), 900.f);
    EXPECT_FLOAT_EQ(Camera.AspectRatio, 800.f / 600.f);
}

TEST_F(FlyCameraControllerTest, InvalidInputScalesDoNotDivideByZeroOrProduceNan)
{
    const float rect[4] = {100.f, 150.f, 900.f, 750.f};
    for (float invalid_scale : {0.f, -1.f, std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()})
    {
        const float scales[2] = {invalid_scale, invalid_scale};
        Controller.SetViewportFromUI(rect, scales);
        EXPECT_FLOAT_EQ(Controller.ViewportBound(0), 100.f);
        EXPECT_FLOAT_EQ(Controller.ViewportBound(3), 750.f);
        EXPECT_FLOAT_EQ(Camera.AspectRatio, 800.f / 600.f);
    }
}
