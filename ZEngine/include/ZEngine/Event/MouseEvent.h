#pragma once

#include <Event/CoreEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Event {

    class MouseEvent : public CoreEvent {
    public:
        MouseEvent() = default;
        explicit MouseEvent(ZENGINE_KEYCODE button) : m_button(button) {}

        ZENGINE_KEYCODE GetButton() const {
            return m_button;
        }

        EVENT_CATEGORY(Mouse | EventCategory::Input)

    protected:
        ZENGINE_KEYCODE m_button{0};
    };
} // namespace ZEngine::Event
