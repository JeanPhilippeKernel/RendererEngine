#include <GLFW/glfw3.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIWidgets.h>
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

TEST_F(FlyCameraControllerTest, ExplicitProjectionChoicePreservesPoseAndIsNotAToggle)
{
    const auto position = Camera.GetPosition();
    const auto forward  = Camera.GetForward();
    for (auto type : {CameraType::ORTHOGRAPHIC, CameraType::ORTHOGRAPHIC, CameraType::PERSPECTIVE})
    {
        ASSERT_TRUE(Controller.SetProjectionType(type));
        EXPECT_EQ(Camera.GetProjectionType(), type);
        const auto frame = Camera.CaptureFrameData();
        EXPECT_EQ(frame.ProjectionType, type);
        EXPECT_EQ(Camera.GetPosition(), position);
        EXPECT_EQ(Camera.GetForward(), forward);
    }
}

TEST_F(FlyCameraControllerTest, InvalidOrUnboundProjectionChoicePreservesTheCamera)
{
    for (auto type : {CameraType::UNDEFINED, static_cast<CameraType>(-1), static_cast<CameraType>(255)})
    {
        EXPECT_FALSE(Controller.SetProjectionType(type));
        EXPECT_EQ(Camera.GetProjectionType(), CameraType::PERSPECTIVE);
    }
    Controller.SetCamera(nullptr);
    EXPECT_FALSE(Controller.SetProjectionType(CameraType::ORTHOGRAPHIC));
    EXPECT_EQ(Camera.GetProjectionType(), CameraType::PERSPECTIVE);
}

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

TEST_F(FlyCameraControllerTest, InteractionCaptureStopsSmoothingFocusAndOrbitAtExactPoseWithoutResuming)
{
    for (uint32_t motion = 0; motion < 3; ++motion)
    {
        Camera.SetPosition({0.f, 5.f, 10.f});
        Camera.SetOrientation(0.f, 0.f);
        if (motion == 1)
            Camera.FocusOn(Core::Maths::Vec3f(20.f, 5.f, -10.f), 2.f);
        else
        {
            Camera.Input.ScrollDelta = 1.f;
            Camera.Input.AltDown = Camera.Input.LeftDown = motion == 2;
        }
        Camera.OnUpdate(1.f / 60.f);
        const auto before = Camera.CaptureFrameData();
        Controller.SetInputCapture(true, true);
        for (uint32_t i = 0; i < 60; ++i)
        {
            Controller.Update(Core::TimeStep(1.f / 60.f));
            const auto after = Camera.CaptureFrameData();
            for (uint32_t row = 0; row < 4; ++row)
                for (uint32_t column = 0; column < 4; ++column)
                {
                    EXPECT_EQ(after.View(row, column), before.View(row, column));
                    EXPECT_EQ(after.Projection(row, column), before.Projection(row, column));
                }
        }
        EXPECT_EQ(Camera.State, FlyCameraState::Free);
        Controller.SetInputCapture(false, false);
        for (uint32_t i = 0; i < 60; ++i)
            Controller.Update(Core::TimeStep(1.f / 60.f));
        const auto after_release = Camera.CaptureFrameData();
        EXPECT_EQ(after_release.Position.x, before.Position.x);
        EXPECT_EQ(after_release.Position.y, before.Position.y);
        EXPECT_EQ(after_release.Position.z, before.Position.z);
    }
}

TEST_F(FlyCameraControllerTest, InteractionCaptureDiscardsCameraChordsWheelAndButtonsWithoutCallingHooks)
{
    uint32_t calls                  = 0;
    Camera.Hooks.Context            = &calls;
    Camera.Hooks.GetSelectionBounds = [](void* context, Core::Maths::Vec3f& center, float& radius) {
        ++*static_cast<uint32_t*>(context);
        center = {20.f, 5.f, -10.f};
        radius = 2.f;
        return true;
    };
    Controller.SetInputCapture(true, true);
    const auto before = Camera.CaptureFrameData();
    for (uint32_t i = 0; i < 60; ++i)
    {
        Camera.Input.RightDown = Camera.Input.MiddleDown = Camera.Input.LeftDown = Camera.Input.AltDown = true;
        Camera.Input.Keys[GLFW_KEY_W]                                                                   = true;
        Camera.Input.ScrollDelta                                                                        = 5.f;
        Camera.Input.MouseDeltaX = Camera.Input.MouseDeltaY = 10.f;
        Camera.Input.FrameSelectionRequested = Camera.Input.ToggleProjectionRequested = true;
        Camera.Input.AxisViewRequested                                                = FlyCameraAxisView::Top;
        Camera.Input.BookmarkSlotRequested                                            = 0;
        Controller.Update(Core::TimeStep(1.f / 60.f));
    }
    const auto after = Camera.CaptureFrameData();
    EXPECT_EQ(calls, 0u);
    EXPECT_EQ(after.ProjectionType, before.ProjectionType);
    EXPECT_EQ(after.Position.x, before.Position.x);
    EXPECT_EQ(after.Position.y, before.Position.y);
    EXPECT_EQ(after.Position.z, before.Position.z);
    EXPECT_FALSE(Camera.Input.FrameSelectionRequested);
    EXPECT_FALSE(Camera.Input.Keys[GLFW_KEY_W]);
    EXPECT_FALSE(Camera.Input.MiddleDown);
}

TEST_F(FlyCameraControllerTest, PointerOnlyCaptureDoesNotCancelAnExistingFocusAnimation)
{
    Camera.FocusOn(Core::Maths::Vec3f(20.f, 5.f, -10.f), 2.f);
    Camera.OnUpdate(1.f / 60.f);
    const auto before = Camera.GetPosition();
    Controller.SetInputCapture(true, false);
    Controller.Update(Core::TimeStep(1.f / 60.f));
    EXPECT_EQ(Camera.State, FlyCameraState::Animating);
    EXPECT_NE(Camera.GetPosition().x, before.x);
}

TEST_F(FlyCameraControllerTest, PendingPopupStopsCameraMotionWithoutAnOverlayAndDoesNotReplayItAfterClose)
{
    UI::ZUIContext ctx   = {};
    ctx.ViewportInputKey = ctx.HotKey = 42;
    Camera.SetPosition({0.f, 5.f, 10.f});
    Camera.SetOrientation(0.f, 0.f);
    Camera.FocusOn(Core::Maths::Vec3f(20.f, 5.f, -10.f), 2.f);
    Camera.OnUpdate(1.f / 60.f);
    ASSERT_EQ(Camera.State, FlyCameraState::Animating);
    const auto before = Camera.CaptureFrameData();
    const auto offset = Memory.MainArena.m_current_offset;
    UI::ZUIOpenPopup(&ctx, "PendingViewportPopup", 30.f, 40.f);
    for (uint32_t update = 0; update < 60; ++update)
    {
        const auto capture = UI::ZUIGetInputCapture(&ctx);
        Controller.SetInputCapture(capture.Pointer, capture.Keyboard);
        Controller.Update(Core::TimeStep(1.f / 60.f));
        const auto after = Camera.CaptureFrameData();
        for (uint32_t row = 0; row < 4; ++row)
            for (uint32_t column = 0; column < 4; ++column)
            {
                EXPECT_EQ(after.View(row, column), before.View(row, column));
                EXPECT_EQ(after.Projection(row, column), before.Projection(row, column));
            }
    }
    EXPECT_EQ(Camera.State, FlyCameraState::Free);
    EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
    UI::ZUIClosePopup(&ctx);
    const auto released = UI::ZUIGetInputCapture(&ctx);
    EXPECT_FALSE(released.Pointer);
    EXPECT_FALSE(released.Keyboard);
    Controller.SetInputCapture(released.Pointer, released.Keyboard);
    for (uint32_t update = 0; update < 60; ++update)
        Controller.Update(Core::TimeStep(1.f / 60.f));
    EXPECT_EQ(Camera.GetPosition().x, before.Position.x);
    EXPECT_EQ(Camera.GetPosition().y, before.Position.y);
    EXPECT_EQ(Camera.GetPosition().z, before.Position.z);
    Camera.FocusOn(Core::Maths::Vec3f(20.f, 5.f, -10.f), 2.f);
    Controller.Update(Core::TimeStep(1.f / 60.f));
    EXPECT_NE(Camera.GetPosition().x, before.Position.x);
}
