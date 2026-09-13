#include <GLFW/glfw3.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <gtest/gtest.h>

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
