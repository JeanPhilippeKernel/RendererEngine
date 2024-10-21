#pragma once
#include <Events/KeyEvent.h>
#include <Events/MouseEvent.h>
#include <Events/TextInputEvent.h>
#include <Events/WindowEvent.h>

namespace ZEngine::Windows::Inputs
{
    struct IMouseEventCallback
    {
        virtual bool OnMouseButtonPressed(Events::MouseButtonPressedEvent&)   = 0;
        virtual bool OnMouseButtonReleased(Events::MouseButtonReleasedEvent&) = 0;
        virtual bool OnMouseButtonMoved(Events::MouseButtonMovedEvent&)       = 0;
        virtual bool OnMouseButtonWheelMoved(Events::MouseButtonWheelEvent&)  = 0;
    };

    struct IKeyboardEventCallback
    {
        virtual bool OnKeyPressed(Events::KeyPressedEvent&)   = 0;
        virtual bool OnKeyReleased(Events::KeyReleasedEvent&) = 0;
    };

    struct ITextInputEventCallback
    {
        virtual bool OnTextInputRaised(Events::TextInputEvent&) = 0;
    };

    struct IWindowEventCallback
    {
        virtual bool OnWindowClosed(Events::WindowClosedEvent&)       = 0;
        virtual bool OnWindowResized(Events::WindowResizedEvent&)     = 0;
        virtual bool OnWindowMinimized(Events::WindowMinimizedEvent&) = 0;
        virtual bool OnWindowMaximized(Events::WindowMaximizedEvent&) = 0;
        virtual bool OnWindowRestored(Events::WindowRestoredEvent&)   = 0;
    };
} // namespace ZEngine::Windows::Inputs
