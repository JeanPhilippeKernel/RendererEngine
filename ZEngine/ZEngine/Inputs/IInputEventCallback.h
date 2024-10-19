#pragma once
#include <Event/KeyEvent.h>
#include <Event/MouseEvent.h>
#include <Event/TextInputEvent.h>

namespace ZEngine::Inputs
{
    struct IMouseEventCallback
    {
        virtual bool OnMouseButtonPressed(Event::MouseButtonPressedEvent&)   = 0;
        virtual bool OnMouseButtonReleased(Event::MouseButtonReleasedEvent&) = 0;
        virtual bool OnMouseButtonMoved(Event::MouseButtonMovedEvent&)       = 0;
        virtual bool OnMouseButtonWheelMoved(Event::MouseButtonWheelEvent&)  = 0;
    };

    struct IKeyboardEventCallback
    {
        virtual bool OnKeyPressed(Event::KeyPressedEvent&)   = 0;
        virtual bool OnKeyReleased(Event::KeyReleasedEvent&) = 0;
    };

    struct ITextInputEventCallback
    {
        virtual bool OnTextInputRaised(Event::TextInputEvent&) = 0;
    };
} // namespace ZEngine::Inputs
