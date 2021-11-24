#pragma once
#include <Event/TextInputEvent.h>

namespace ZEngine::Inputs {

    struct ITextInputEventCallback {
        ITextInputEventCallback()  = default;
        ~ITextInputEventCallback() = default;

        virtual bool OnTextInputRaised(ZEngine::Event::TextInputEvent&) = 0;
    };
} // namespace ZEngine::Inputs
