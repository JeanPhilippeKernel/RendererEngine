#pragma once
#include <ZEngine/Core/Maths/Vec.h>
#include <stdint.h>

namespace ZEngine::Input
{
    enum class InputActionType : uint8_t
    {
        Button,
        Axis1D,
        Axis2D,
    };

    struct InputBinding
    {
        enum class Device : uint8_t
        {
            Keyboard,
            MouseButton,
            MouseScroll,
            Gamepad,
        };

        Device   Source     = Device::Keyboard;
        uint32_t Code       = 0;
        float    Scale      = 1.0f;
        float    Deadzone   = 0.1f;
        int      GamepadIdx = 0;
    };

    struct InputAction
    {
        uint32_t        NameHash     = 0;
        InputActionType Type         = InputActionType::Button;
        InputBinding    Bindings[4]  = {};
        uint8_t         BindingCount = 0;
    };

    struct InputButtonState
    {
        bool Held     = false;
        bool JustDown = false;
        bool JustUp   = false;
    };

    struct InputAxisState
    {
        float              Value   = 0.0f;
        Core::Maths::Vec2f Value2D = {};
    };

    static constexpr uint32_t kMaxActions = 64;

    struct InputFrame
    {
        uint32_t         FrameNumber               = 0;
        InputButtonState ButtonStates[kMaxActions] = {};
        InputAxisState   AxisStates[kMaxActions]   = {};
        uint32_t         ActionCount               = 0;
    };

} // namespace ZEngine::Input
