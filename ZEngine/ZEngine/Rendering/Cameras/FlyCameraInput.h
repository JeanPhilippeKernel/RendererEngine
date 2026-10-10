#pragma once
#include <cstdint>

namespace ZEngine::Rendering::Cameras
{
    /// @brief Discrete editor-view orientation requested by the input layer.
    enum class FlyCameraAxisView : uint8_t
    {
        None,
        Front,
        Back,
        Right,
        Left,
        Top,
        Bottom,
    };

    enum class FlyCameraState : uint8_t
    {
        Free,
        Orbit,
        Pan,
        Animating,
    };

    struct FlyCameraInput
    {
        bool              RightDown                 = false;
        bool              MiddleDown                = false;
        bool              LeftDown                  = false;
        bool              AltDown                   = false;
        bool              ShiftDown                 = false;
        /// @brief Command on macOS, Control on Windows and Linux.
        bool              PrimaryModifierDown       = false;
        bool              Keys[512]                 = {};
        float             MouseDeltaX               = 0.0f;
        float             MouseDeltaY               = 0.0f;
        float             ScrollDelta               = 0.0f;
        float             MouseViewportX            = 0.0f;
        float             MouseViewportY            = 0.0f;

        // Editor navigation commands are semantic rather than key-based so
        // controllers can map different profiles without leaking bindings into
        // the camera implementation.
        bool              FrameSelectionRequested   = false;
        bool              FrameAllRequested         = false;
        bool              ToggleProjectionRequested = false;
        FlyCameraAxisView AxisViewRequested         = FlyCameraAxisView::None;
        int8_t            BookmarkSlotRequested     = -1;
        bool              SaveBookmarkRequested     = false;

        void              FlushDeltas()
        {
            MouseDeltaX = 0.0f;
            MouseDeltaY = 0.0f;
            ScrollDelta = 0.0f;
        }

        void Reset()
        {
            *this = FlyCameraInput{};
        }

        void ClearCommands()
        {
            FrameSelectionRequested   = false;
            FrameAllRequested         = false;
            ToggleProjectionRequested = false;
            AxisViewRequested         = FlyCameraAxisView::None;
            BookmarkSlotRequested     = -1;
            SaveBookmarkRequested     = false;
        }
    };
} // namespace ZEngine::Rendering::Cameras
