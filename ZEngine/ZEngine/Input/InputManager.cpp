#include <GLFW/glfw3.h>
#include <ZEngine/Input/InputManager.h>
#include <ZEngine/ZEngineDef.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace ZEngine::Input
{
    uint32_t InputManager::FNV32(const char* str)
    {
        uint32_t h = 2166136261u;
        while (*str)
            h = (h ^ (uint8_t) *str++) * 16777619u;
        return h;
    }

    void InputManager::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t max_actions)
    {
        ZENGINE_VALIDATE_ASSERT(arena, "InputManager::Initialize: arena must not be null")
        ZENGINE_VALIDATE_ASSERT(max_actions <= kMaxActions, "InputManager::Initialize: max_actions exceeds kMaxActions")

        m_arena        = arena;
        m_max_actions  = max_actions;
        m_action_count = 0;
        m_first_poll   = true;

        m_actions      = ZPushArray(arena, InputAction, max_actions);
        memset(m_actions, 0, sizeof(InputAction) * max_actions);
        memset(m_scroll_scale, 0, sizeof(m_scroll_scale));
        m_current = {};
        m_prev    = {};
    }

    void InputManager::Dispose()
    {
        m_actions      = nullptr;
        m_action_count = 0;
    }

    uint32_t InputManager::RegisterAction(const char* name, InputActionType type)
    {
        ZENGINE_VALIDATE_ASSERT(m_action_count < m_max_actions, "InputManager::RegisterAction: action capacity exceeded")

        uint32_t slot                = m_action_count++;
        m_actions[slot].NameHash     = FNV32(name);
        m_actions[slot].Type         = type;
        m_actions[slot].BindingCount = 0;
        return slot;
    }

    void InputManager::BindKey(uint32_t slot, int glfw_key, float scale)
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::BindKey: invalid slot")
        auto& action = m_actions[slot];
        ZENGINE_VALIDATE_ASSERT(action.BindingCount < 4, "InputManager::BindKey: binding limit reached")

        auto& b  = action.Bindings[action.BindingCount++];
        b.Source = InputBinding::Device::Keyboard;
        b.Code   = (uint32_t) glfw_key;
        b.Scale  = scale;
    }

    void InputManager::BindMouseButton(uint32_t slot, int glfw_mouse_button, float scale)
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::BindMouseButton: invalid slot")
        auto& action = m_actions[slot];
        ZENGINE_VALIDATE_ASSERT(action.BindingCount < 4, "InputManager::BindMouseButton: binding limit reached")

        auto& b  = action.Bindings[action.BindingCount++];
        b.Source = InputBinding::Device::MouseButton;
        b.Code   = (uint32_t) glfw_mouse_button;
        b.Scale  = scale;
    }

    void InputManager::BindScrollAxis(uint32_t slot, float scale)
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::BindScrollAxis: invalid slot")
        ZENGINE_VALIDATE_ASSERT(m_actions[slot].Type == InputActionType::Axis1D, "InputManager::BindScrollAxis: slot must be Axis1D type")

        auto& action = m_actions[slot];
        ZENGINE_VALIDATE_ASSERT(action.BindingCount < 4, "InputManager::BindScrollAxis: binding limit reached")

        auto& b              = action.Bindings[action.BindingCount++];
        b.Source             = InputBinding::Device::MouseScroll;
        b.Scale              = scale;

        m_scroll_scale[slot] = scale;
    }

    void InputManager::AccumulateScroll(double yoffset)
    {
        m_scroll_accum += yoffset;
    }

    void InputManager::Poll(GLFWwindow* window)
    {
        ZENGINE_VALIDATE_ASSERT(window, "InputManager::Poll: window must not be null")

        // Carry current → prev for JustDown/JustUp derivation.
        m_prev                = m_current;
        m_current             = {};
        m_current.FrameNumber = m_prev.FrameNumber + 1;
        m_current.ActionCount = m_action_count;

        // Mouse position and delta.
        double mx = 0.0, my = 0.0;
        glfwGetCursorPos(window, &mx, &my);
        Core::Maths::Vec2f new_pos = {(float) mx, (float) my};

        if (m_first_poll)
        {
            m_last_mouse_pos = new_pos;
            m_first_poll     = false;
        }

        m_mouse_delta    = {new_pos.x - m_last_mouse_pos.x, new_pos.y - m_last_mouse_pos.y};
        m_last_mouse_pos = new_pos;
        m_mouse_pos      = new_pos;

        // Drain scroll accumulator, clamped to ±1 to prevent trackpad momentum spikes.
        m_scroll_delta   = (float) std::clamp(m_scroll_accum, -1.0, 1.0);
        m_scroll_accum   = 0.0;

        // Evaluate each action.
        for (uint32_t i = 0; i < m_action_count; ++i)
        {
            const auto& action = m_actions[i];

            if (action.Type == InputActionType::Button)
            {
                bool held = false;
                for (uint8_t b = 0; b < action.BindingCount; ++b)
                {
                    const auto& binding = action.Bindings[b];
                    if (binding.Source == InputBinding::Device::Keyboard)
                        held |= glfwGetKey(window, (int) binding.Code) == GLFW_PRESS;
                    else if (binding.Source == InputBinding::Device::MouseButton)
                        held |= glfwGetMouseButton(window, (int) binding.Code) == GLFW_PRESS;
                }

                m_current.ButtonStates[i].Held     = held;
                m_current.ButtonStates[i].JustDown = held && !m_prev.ButtonStates[i].Held;
                m_current.ButtonStates[i].JustUp   = !held && m_prev.ButtonStates[i].Held;
            }
            else if (action.Type == InputActionType::Axis1D)
            {
                float value = 0.0f;
                for (uint8_t b = 0; b < action.BindingCount; ++b)
                {
                    const auto& binding = action.Bindings[b];
                    float       v       = 0.0f;
                    if (binding.Source == InputBinding::Device::Keyboard)
                        v = (glfwGetKey(window, (int) binding.Code) == GLFW_PRESS) ? 1.0f : 0.0f;
                    else if (binding.Source == InputBinding::Device::MouseButton)
                        v = (glfwGetMouseButton(window, (int) binding.Code) == GLFW_PRESS) ? 1.0f : 0.0f;
                    else if (binding.Source == InputBinding::Device::MouseScroll)
                        v = m_scroll_delta;

                    v *= binding.Scale;

                    // For axis: largest absolute value wins.
                    if (std::abs(v) > std::abs(value))
                        value = v;
                }
                m_current.AxisStates[i].Value = value;
            }
            else if (action.Type == InputActionType::Axis2D)
            {
                // Axis2D from mouse delta — no binding needed, populated directly.
                m_current.AxisStates[i].Value2D = m_mouse_delta;
            }
        }
    }

    const InputButtonState& InputManager::GetButton(uint32_t slot) const
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::GetButton: invalid slot")
        return m_current.ButtonStates[slot];
    }

    float InputManager::GetAxis(uint32_t slot) const
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::GetAxis: invalid slot")
        return m_current.AxisStates[slot].Value;
    }

    Core::Maths::Vec2f InputManager::GetAxis2D(uint32_t slot) const
    {
        ZENGINE_VALIDATE_ASSERT(slot < m_action_count, "InputManager::GetAxis2D: invalid slot")
        return m_current.AxisStates[slot].Value2D;
    }

    Core::Maths::Vec2f InputManager::GetMouseDelta() const
    {
        return m_mouse_delta;
    }

    Core::Maths::Vec2f InputManager::GetMousePosition() const
    {
        return m_mouse_pos;
    }

    float InputManager::GetScrollDelta() const
    {
        return m_scroll_delta;
    }

    const InputFrame& InputManager::GetCurrentFrame() const
    {
        return m_current;
    }

} // namespace ZEngine::Input
