#pragma once
#include <Event/MouseButtonPressedEvent.h>
#include <Event/MouseButtonReleasedEvent.h>
#include <Event/MouseButtonMovedEvent.h>
#include <Event/MouseButtonWheelEvent.h>

namespace ZEngine::Inputs {

    struct IMouseEventCallback {
        IMouseEventCallback()  = default;
        ~IMouseEventCallback() = default;

        virtual bool OnMouseButtonPressed(ZEngine::Event::MouseButtonPressedEvent&)   = 0;
        virtual bool OnMouseButtonReleased(ZEngine::Event::MouseButtonReleasedEvent&) = 0;
        virtual bool OnMouseButtonMoved(ZEngine::Event::MouseButtonMovedEvent&)       = 0;
        virtual bool OnMouseButtonWheelMoved(ZEngine::Event::MouseButtonWheelEvent&)  = 0;
    };
} // namespace ZEngine::Inputs
