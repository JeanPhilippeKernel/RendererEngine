#include <GLFW/glfw3.h>
#include <ZEngine/Controllers/FlyCameraController.h>

using namespace ZEngine::Rendering::Cameras;
using namespace ZEngine::Input;

namespace ZEngine::Controllers
{
    namespace
    {
        /// @brief Physical keys used by a platform's editor-navigation profile.
        struct NavigationShortcutProfile
        {
            int PrimaryModifierLeft  = GLFW_KEY_LEFT_CONTROL;
            int PrimaryModifierRight = GLFW_KEY_RIGHT_CONTROL;
        };

#if defined(__APPLE__)
        // Command is the primary shortcut modifier in macOS applications.
        constexpr NavigationShortcutProfile kNavigationShortcutProfile = {
            GLFW_KEY_LEFT_SUPER,
            GLFW_KEY_RIGHT_SUPER,
        };
#elif defined(_WIN32) || defined(__linux__)
        // Control is the primary shortcut modifier on Windows and Linux.
        constexpr NavigationShortcutProfile kNavigationShortcutProfile = {
            GLFW_KEY_LEFT_CONTROL,
            GLFW_KEY_RIGHT_CONTROL,
        };
#else
        constexpr NavigationShortcutProfile kNavigationShortcutProfile = {};
#endif
    } // namespace

    void FlyCameraController::Initialize(InputManager* input_manager, Core::Memory::ArenaAllocator* /*arena*/)
    {
        m_input             = input_manager;
        m_input_enabled     = true;
        m_pointer_captured  = false;
        m_keyboard_captured = false;

        m_slot_forward      = m_input->RegisterAction("CameraForward", InputActionType::Axis1D);
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
#if defined(__APPLE__) || defined(__linux__)
        // macOS: Option = GLFW_KEY_LEFT_ALT but Command (Super) is more natural for orbit.
        // Linux: some WMs (GNOME, KDE) intercept Alt+drag for window move — Super is a
        //        reliable fallback that is not grabbed by the compositor.
        m_input->BindKey(m_slot_alt, GLFW_KEY_LEFT_SUPER);
        m_input->BindKey(m_slot_alt, GLFW_KEY_RIGHT_SUPER);
#endif

        m_slot_shift = m_input->RegisterAction("CameraShift", InputActionType::Button);
        m_input->BindKey(m_slot_shift, GLFW_KEY_LEFT_SHIFT);
        m_input->BindKey(m_slot_shift, GLFW_KEY_RIGHT_SHIFT);

        m_slot_primary_modifier = m_input->RegisterAction("CameraPrimaryModifier", InputActionType::Button);
        m_input->BindKey(m_slot_primary_modifier, kNavigationShortcutProfile.PrimaryModifierLeft);
        m_input->BindKey(m_slot_primary_modifier, kNavigationShortcutProfile.PrimaryModifierRight);

#if defined(__APPLE__)
        // Control provides the top-row fallback on compact Apple keyboards;
        // Command remains reserved for bookmarks and inverse keypad views.
        m_slot_alternate_modifier = m_input->RegisterAction("CameraAlternateModifier", InputActionType::Button);
        m_input->BindKey(m_slot_alternate_modifier, GLFW_KEY_LEFT_CONTROL);
        m_input->BindKey(m_slot_alternate_modifier, GLFW_KEY_RIGHT_CONTROL);
#else
        // Shift is available on every Windows/Linux keyboard and leaves the
        // unmodified number row available for bookmark recall.
        m_slot_alternate_modifier = m_slot_shift;
#endif

        m_slot_focus = m_input->RegisterAction("CameraFocus", InputActionType::Button);
        m_input->BindKey(m_slot_focus, GLFW_KEY_F);

        m_slot_frame_all = m_input->RegisterAction("CameraFrameAll", InputActionType::Button);
        m_input->BindKey(m_slot_frame_all, GLFW_KEY_HOME);

        m_slot_projection = m_input->RegisterAction("CameraToggleProjection", InputActionType::Button);
        m_input->BindKey(m_slot_projection, GLFW_KEY_KP_5);
        m_slot_projection_top_row = m_input->RegisterAction("CameraToggleProjectionTopRow", InputActionType::Button);
        m_input->BindKey(m_slot_projection_top_row, GLFW_KEY_5);

        m_slot_axis_view[0] = m_input->RegisterAction("CameraViewFrontBack", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view[0], GLFW_KEY_KP_1);
        m_slot_axis_view_top_row[0] = m_input->RegisterAction("CameraViewFrontBackTopRow", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view_top_row[0], GLFW_KEY_1);
        m_slot_axis_view[1] = m_input->RegisterAction("CameraViewRightLeft", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view[1], GLFW_KEY_KP_3);
        m_slot_axis_view_top_row[1] = m_input->RegisterAction("CameraViewRightLeftTopRow", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view_top_row[1], GLFW_KEY_3);
        m_slot_axis_view[2] = m_input->RegisterAction("CameraViewTopBottom", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view[2], GLFW_KEY_KP_7);
        m_slot_axis_view_top_row[2] = m_input->RegisterAction("CameraViewTopBottomTopRow", InputActionType::Button);
        m_input->BindKey(m_slot_axis_view_top_row[2], GLFW_KEY_7);

        for (int i = 0; i < 9; ++i)
        {
            char name[24];
            snprintf(name, sizeof(name), "CameraBookmark%d", i);
            m_slot_bookmark[i] = m_input->RegisterAction(name, InputActionType::Button);
            m_input->BindKey(m_slot_bookmark[i], GLFW_KEY_1 + i);
        }
    }

    void FlyCameraController::EnterFly()
    {
        m_state = CamState::Fly;
        if (m_window)
        {
            auto* glfw = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
            glfwSetInputMode(glfw, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(glfw, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        }
        if (m_input)
            m_input->ResetMouseDelta();
    }

    void FlyCameraController::ExitFly()
    {
        m_state = CamState::Hover;
        if (m_window)
        {
            auto* glfw = static_cast<GLFWwindow*>(m_window->GetNativeWindow());
            if (glfwRawMouseMotionSupported())
                glfwSetInputMode(glfw, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
            glfwSetInputMode(glfw, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        if (m_input)
            m_input->ResetMouseDelta();
    }

    void FlyCameraController::ClearKeyboardInput()
    {
        if (!m_camera)
            return;

        auto& input               = m_camera->Input;
        input.RightDown           = false;
        input.ShiftDown           = false;
        input.PrimaryModifierDown = false;
        input.Keys[GLFW_KEY_W]    = false;
        input.Keys[GLFW_KEY_S]    = false;
        input.Keys[GLFW_KEY_D]    = false;
        input.Keys[GLFW_KEY_A]    = false;
        input.Keys[GLFW_KEY_E]    = false;
        input.Keys[GLFW_KEY_Q]    = false;
    }

    void FlyCameraController::FeedNavigationCommands(bool enabled)
    {
        auto& input = m_camera->Input;
        input.ClearCommands();
        if (!enabled)
            return;

        const bool primary_modifier_down   = m_input->GetButton(m_slot_primary_modifier).Held;
        const bool alternate_modifier_down = m_input->GetButton(m_slot_alternate_modifier).Held;
        const bool shift_down              = m_input->GetButton(m_slot_shift).Held;
        const bool focus_key_pressed       = m_input->GetButton(m_slot_focus).JustDown;

#if defined(__APPLE__)
        // macOS: F frames selection; Command+Shift+F frames all. The latter
        // avoids relying on Home, which compact Apple keyboards often omit.
        const bool frame_all_chord = focus_key_pressed && primary_modifier_down && shift_down && !alternate_modifier_down;
#else
        // Windows/Linux: Shift+F is the compact-keyboard alternative to Home.
        const bool frame_all_chord = focus_key_pressed && shift_down && !primary_modifier_down;
#endif
        input.FrameSelectionRequested   = focus_key_pressed && !primary_modifier_down && !alternate_modifier_down && !shift_down;
        input.FrameAllRequested         = m_input->GetButton(m_slot_frame_all).JustDown || frame_all_chord;

        input.ToggleProjectionRequested = m_input->GetButton(m_slot_projection).JustDown;
#if defined(__APPLE__)
        // Control+5 is the projection fallback; Command+5 remains bookmark 5.
        input.ToggleProjectionRequested |= m_input->GetButton(m_slot_projection_top_row).JustDown && alternate_modifier_down && !primary_modifier_down;
#else
        // Shift+5 is available on compact Windows/Linux keyboards.
        input.ToggleProjectionRequested |= m_input->GetButton(m_slot_projection_top_row).JustDown && shift_down && !primary_modifier_down;
#endif

        if (m_input->GetButton(m_slot_axis_view[0]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Back : FlyCameraAxisView::Front;
        else if (m_input->GetButton(m_slot_axis_view[1]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Left : FlyCameraAxisView::Right;
        else if (m_input->GetButton(m_slot_axis_view[2]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Bottom : FlyCameraAxisView::Top;
#if defined(__APPLE__)
        else if (alternate_modifier_down && !primary_modifier_down && m_input->GetButton(m_slot_axis_view_top_row[0]).JustDown)
            input.AxisViewRequested = shift_down ? FlyCameraAxisView::Back : FlyCameraAxisView::Front;
        else if (alternate_modifier_down && !primary_modifier_down && m_input->GetButton(m_slot_axis_view_top_row[1]).JustDown)
            input.AxisViewRequested = shift_down ? FlyCameraAxisView::Left : FlyCameraAxisView::Right;
        else if (alternate_modifier_down && !primary_modifier_down && m_input->GetButton(m_slot_axis_view_top_row[2]).JustDown)
            input.AxisViewRequested = shift_down ? FlyCameraAxisView::Bottom : FlyCameraAxisView::Top;
#else
        else if (shift_down && m_input->GetButton(m_slot_axis_view_top_row[0]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Back : FlyCameraAxisView::Front;
        else if (shift_down && m_input->GetButton(m_slot_axis_view_top_row[1]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Left : FlyCameraAxisView::Right;
        else if (shift_down && m_input->GetButton(m_slot_axis_view_top_row[2]).JustDown)
            input.AxisViewRequested = primary_modifier_down ? FlyCameraAxisView::Bottom : FlyCameraAxisView::Top;
#endif

        for (int i = 0; i < 9; ++i)
        {
            if (!m_input->GetButton(m_slot_bookmark[i]).JustDown)
                continue;

            // The top-row view/projection fallbacks take precedence over
            // bookmark numbers. Numpad commands never share a binding.
            if (alternate_modifier_down)
                break;

            input.BookmarkSlotRequested = static_cast<int8_t>(i);
            input.SaveBookmarkRequested = primary_modifier_down;
            break;
        }
    }

    void FlyCameraController::Update(Core::TimeStep dt)
    {
        if (!m_input || !m_camera)
            return;

        // Navigation commands are editor commands, not fly-only controls. They
        // remain available while the hierarchy owns the pointer, but never
        // while a UI text field, popup, or modal owns keyboard input.
        const bool navigation_enabled = m_input_enabled && m_input->IsWindowFocused() && !m_keyboard_captured;

        if (!m_input_enabled || !m_input->IsWindowFocused() || m_pointer_captured)
        {
            if (m_state == CamState::Fly)
                ExitFly();
            m_state = CamState::Idle;
            m_camera->Input.Reset();
            FeedNavigationCommands(navigation_enabled);
            // Discard all movement collected while another UI element owns the pointer.
            m_input->ResetMouseDelta();
            m_camera->OnUpdate(dt.GetSecond());
            return;
        }

        // HOT PATH — runs every frame, no heap allocation allowed.
        // Compute hover from raw cursor position vs stored viewport rect.
        // This bypasses the ZUI hit-test chain entirely — no ViewportHovered dependency.
        auto pos     = m_input->GetMousePosition();
        bool hovered = pos.x >= m_vp[0] && pos.x <= m_vp[2] && pos.y >= m_vp[1] && pos.y <= m_vp[3];
        bool rmb     = m_input->GetButton(m_slot_rmb).Held;

        // State transitions
        switch (m_state)
        {
            case CamState::Idle:
                if (hovered)
                    m_state = CamState::Hover;
                break;
            case CamState::Hover:
                if (!hovered)
                    m_state = CamState::Idle;
                else if (rmb && !m_keyboard_captured)
                    EnterFly();
                break;
            case CamState::Fly:
                if (!rmb || m_keyboard_captured)
                    ExitFly();
                break;
        }

        if (m_state == CamState::Idle)
        {
            m_camera->Input.Reset();
        }
        else
        {
            auto&      inp          = m_camera->Input;

            // Scroll, pan, orbit — always active when hovered or flying
            const auto delta        = m_input->GetMouseDelta();
            inp.MouseDeltaX         = delta.x;
            inp.MouseDeltaY         = delta.y;
            inp.ScrollDelta         = m_input->GetAxis(m_slot_scroll);
            inp.MiddleDown          = m_input->GetButton(m_slot_mmb).Held;
            inp.AltDown             = m_input->GetButton(m_slot_alt).Held;
            inp.LeftDown            = m_input->GetButton(m_slot_lmb).Held;
            inp.ShiftDown           = m_input->GetButton(m_slot_shift).Held;
            inp.PrimaryModifierDown = m_input->GetButton(m_slot_primary_modifier).Held;
            inp.MouseViewportX      = pos.x - m_vp[0];
            inp.MouseViewportY      = pos.y - m_vp[1];

            // Fly-only: WASD, mouselook (RightDown tells FlyCamera to activate them)
            if (m_state == CamState::Fly)
            {
                inp.RightDown        = true;
                inp.Keys[GLFW_KEY_W] = m_input->GetAxis(m_slot_forward) > 0.5f;
                inp.Keys[GLFW_KEY_S] = m_input->GetAxis(m_slot_forward) < -0.5f;
                inp.Keys[GLFW_KEY_D] = m_input->GetAxis(m_slot_right) > 0.5f;
                inp.Keys[GLFW_KEY_A] = m_input->GetAxis(m_slot_right) < -0.5f;
                inp.Keys[GLFW_KEY_E] = m_input->GetAxis(m_slot_up) > 0.5f;
                inp.Keys[GLFW_KEY_Q] = m_input->GetAxis(m_slot_up) < -0.5f;
            }
            else
            {
                ClearKeyboardInput();
            }
        }

        FeedNavigationCommands(navigation_enabled);
        m_camera->OnUpdate(dt.GetSecond());
    }

    void FlyCameraController::SetInputCapture(bool pointer_captured, bool keyboard_captured)
    {
        const bool capture_changed = m_pointer_captured != pointer_captured || m_keyboard_captured != keyboard_captured;
        m_pointer_captured         = pointer_captured;
        m_keyboard_captured        = keyboard_captured;

        if (!capture_changed || !m_camera || (!pointer_captured && !keyboard_captured))
            return;

        if (m_state == CamState::Fly)
            ExitFly();
        m_camera->Input.Reset();
        if (pointer_captured && m_input)
            m_input->ResetMouseDelta();
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
        // Origin is now derived from m_vp[0]/[1] set via SetViewportRect.
        // This stub satisfies the interface for callers that haven't migrated.
        m_vp[0] = x;
        m_vp[1] = y;
    }

    void FlyCameraController::SetViewportRect(float x0, float y0, float x1, float y1)
    {
        m_vp[0] = x0;
        m_vp[1] = y0;
        m_vp[2] = x1;
        m_vp[3] = y1;
    }

    void FlyCameraController::ResumeEventProcessing()
    {
        m_input_enabled = true;
        if (m_input)
            m_input->ResetMouseDelta();
    }

    void FlyCameraController::PauseEventProcessing()
    {
        m_input_enabled = false;
        if (m_state == CamState::Fly)
            ExitFly();
        m_state = CamState::Idle;
        if (m_camera)
            m_camera->Input.Reset();
    }

} // namespace ZEngine::Controllers
