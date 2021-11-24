#pragma once

#include <Event/CoreEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Event {
#ifdef ZENGINE_WINDOW_SDL
    class MouseEvent : public CoreEvent {
    public:
        MouseEvent() = default;
        explicit MouseEvent(unsigned char button) : m_button(button) {}

        unsigned char GetButton() const {
            return m_button;
        }

        EVENT_CATEGORY(Mouse | EventCategory::Input)

    protected:
        unsigned char m_button{0};
    };
#else
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
#endif
} // namespace ZEngine::Event
