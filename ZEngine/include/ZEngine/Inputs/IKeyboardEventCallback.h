#pragma once
#include <Event/KeyPressedEvent.h>
#include <Event/KeyReleasedEvent.h>

namespace ZEngine::Inputs {

    struct IKeyboardEventCallback {
        IKeyboardEventCallback()  = default;
        ~IKeyboardEventCallback() = default;

        virtual bool OnKeyPressed(ZEngine::Event::KeyPressedEvent&)   = 0;
        virtual bool OnKeyReleased(ZEngine::Event::KeyReleasedEvent&) = 0;
    };
} // namespace ZEngine::Inputs
