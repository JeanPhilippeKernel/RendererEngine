#include <GLFW/glfw3.h>
#include <ZEngine/Controllers/FlyCameraController.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Input;

namespace ZEngine::Controllers
{
    void FlyCameraController::Initialize(InputManager* input_manager, Core::Memory::ArenaAllocator* /*arena*/)
    {
        m_input        = input_manager;

        m_slot_forward = m_input->RegisterAction("CameraForward", InputActionType::Axis1D);
        m_input->BindKey(m_slot_forward, GLFW_KEY_W, 1.0f);
        m_input->BindKey(m_slot_forward, GLFW_KEY_S, -1.0f);

        m_slot_right = m_input->RegisterAction("CameraRight", InputActionType::Axis1D);
        m_input->BindKey(m_slot_right, GLFW_KEY_D, 1.0f);
        m_input->BindKey(m_slot_right, GLFW_KEY_A, -1.0f);

        m_slot_up = m_input->RegisterAction("CameraUp", InputActionType::Axis1D);
        m_input->BindKey(m_slot_up, GLFW_KEY_E, 1.0f);
        m_input->BindKey(m_slot_up, GLFW_KEY_Q, -1.0f);

        m_slot_scroll = m_input->RegisterAction("CameraScroll", InputActionType::Axis1D);
        m_input->BindScrollAxis(m_slot_scroll, 1.0f);

        m_slot_rmb = m_input->RegisterAction("CameraRMB", InputActionType::Button);
        m_input->BindMouseButton(m_slot_rmb, GLFW_MOUSE_BUTTON_RIGHT);

        m_slot_mmb = m_input->RegisterAction("CameraMMB", InputActionType::Button);
        m_input->BindMouseButton(m_slot_mmb, GLFW_MOUSE_BUTTON_MIDDLE);

        m_slot_lmb = m_input->RegisterAction("CameraLMB", InputActionType::Button);
        m_input->BindMouseButton(m_slot_lmb, GLFW_MOUSE_BUTTON_LEFT);

        m_slot_alt = m_input->RegisterAction("CameraAlt", InputActionType::Button);
        m_input->BindKey(m_slot_alt, GLFW_KEY_LEFT_ALT);
        m_input->BindKey(m_slot_alt, GLFW_KEY_RIGHT_ALT);

        m_slot_shift = m_input->RegisterAction("CameraShift", InputActionType::Button);
        m_input->BindKey(m_slot_shift, GLFW_KEY_LEFT_SHIFT);
        m_input->BindKey(m_slot_shift, GLFW_KEY_RIGHT_SHIFT);

        m_slot_ctrl = m_input->RegisterAction("CameraCtrl", InputActionType::Button);
        m_input->BindKey(m_slot_ctrl, GLFW_KEY_LEFT_CONTROL);
        m_input->BindKey(m_slot_ctrl, GLFW_KEY_RIGHT_CONTROL);

        m_slot_focus = m_input->RegisterAction("CameraFocus", InputActionType::Button);
        m_input->BindKey(m_slot_focus, GLFW_KEY_F);

        for (int i = 0; i < 9; ++i)
        {
            char name[24];
            snprintf(name, sizeof(name), "CameraBookmark%d", i);
            m_slot_bookmark[i] = m_input->RegisterAction(name, InputActionType::Button);
            m_input->BindKey(m_slot_bookmark[i], GLFW_KEY_1 + i);
        }
    }

    void FlyCameraController::Update(Core::TimeStep dt)
    {
        if (m_active.value.load(std::memory_order_acquire))
        {
            auto& inp            = m_camera->Input;

            inp.Keys[GLFW_KEY_W] = m_input->GetAxis(m_slot_forward) > 0.5f;
            inp.Keys[GLFW_KEY_S] = m_input->GetAxis(m_slot_forward) < -0.5f;
            inp.Keys[GLFW_KEY_D] = m_input->GetAxis(m_slot_right) > 0.5f;
            inp.Keys[GLFW_KEY_A] = m_input->GetAxis(m_slot_right) < -0.5f;
            inp.Keys[GLFW_KEY_E] = m_input->GetAxis(m_slot_up) > 0.5f;
            inp.Keys[GLFW_KEY_Q] = m_input->GetAxis(m_slot_up) < -0.5f;

            inp.RightDown        = m_input->GetButton(m_slot_rmb).Held;
            inp.MiddleDown       = m_input->GetButton(m_slot_mmb).Held;
            inp.LeftDown         = m_input->GetButton(m_slot_lmb).Held;
            inp.AltDown          = m_input->GetButton(m_slot_alt).Held;
            inp.ShiftDown        = m_input->GetButton(m_slot_shift).Held;
            inp.CtrlDown         = m_input->GetButton(m_slot_ctrl).Held;

            inp.Keys[GLFW_KEY_F] = m_input->GetButton(m_slot_focus).JustUp;
            for (int i = 0; i < 9; ++i)
                inp.Keys[GLFW_KEY_1 + i] = m_input->GetButton(m_slot_bookmark[i]).JustUp;

            auto delta         = m_input->GetMouseDelta();
            inp.MouseDeltaX    = delta.x;
            inp.MouseDeltaY    = delta.y;
            inp.ScrollDelta    = m_input->GetAxis(m_slot_scroll);

            auto pos           = m_input->GetMousePosition();
            inp.MouseViewportX = pos.x - m_viewportOriginX;
            inp.MouseViewportY = pos.y - m_viewportOriginY;
        }

        m_camera->OnUpdate(dt.GetSecond());
    }

    bool FlyCameraController::OnEvent(Core::CoreEvent&)
    {
        return false;
    }

    Rendering::Cameras::CameraPtr FlyCameraController::GetCamera() const
    {
        return m_camera;
    }

    Core::Maths::Vec3f FlyCameraController::GetPosition() const
    {
        return m_camera->GetPosition();
    }

    void FlyCameraController::SetPosition(const Core::Maths::Vec3f& position)
    {
        m_camera->SetPosition(position);
    }

    void FlyCameraController::SetViewport(float logicalW, float logicalH)
    {
        m_camera->SetViewportSize(logicalW, logicalH);
    }

    void FlyCameraController::SetViewportOrigin(float x, float y)
    {
        m_viewportOriginX = x;
        m_viewportOriginY = y;
    }

    void FlyCameraController::ResumeEventProcessing()
    {
        m_active.value.store(true, std::memory_order_release);
    }

    void FlyCameraController::PauseEventProcessing()
    {
        m_active.value.store(false, std::memory_order_release);
        m_camera->Input.Reset();
    }

} // namespace ZEngine::Controllers
