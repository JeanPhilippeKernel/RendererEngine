#pragma once

namespace ZEngine::Rendering::Cameras
{
    enum class FlyCameraState : uint8_t
    {
        Free,
        Orbit,
        Pan,
        Animating,
    };

    struct FlyCameraInput
    {
        bool  RightDown      = false;
        bool  MiddleDown     = false;
        bool  LeftDown       = false;
        bool  AltDown        = false;
        bool  ShiftDown      = false;
        bool  CtrlDown       = false;
        bool  Keys[512]      = {};
        float MouseDeltaX    = 0.0f;
        float MouseDeltaY    = 0.0f;
        float ScrollDelta    = 0.0f;
        float MouseViewportX = 0.0f;
        float MouseViewportY = 0.0f;

        void  FlushDeltas()
        {
            MouseDeltaX = 0.0f;
            MouseDeltaY = 0.0f;
            ScrollDelta = 0.0f;
        }

        void Reset()
        {
            *this = FlyCameraInput{};
        }
    };
} // namespace ZEngine::Rendering::Cameras
