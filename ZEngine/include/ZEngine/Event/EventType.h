#pragma once

namespace ZEngine::Event
{
    enum class EventType
    {
        None = 0,

        WindowShown,
        WindowHidden,
        WindowMoved,
        WindowResized,
        WindowClosed,
        WindowSizeChanged,
        WindowMinimized,
        WindowMaximized,
        WindowRestored,
        WindowFocusLost,
        WindowFocusGained,

        KeyPressed,
        KeyReleased,
        MouseButtonPressed,
        MouseButtonReleased,
        MouseMoved,
        MouseWheel,

        EngineClosed,

        TextInput,

        UserInterfaceComponent,
        SceneViewportResized,
        SceneViewportFocused,
        SceneViewportUnfocused,
        SceneTextureAvailable
    };
} // namespace ZEngine::Event
