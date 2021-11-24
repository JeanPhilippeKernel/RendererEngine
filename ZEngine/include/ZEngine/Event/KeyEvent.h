#pragma once
#include <ZEngineDef.h>
#include <Event/CoreEvent.h>
#include <Inputs/KeyCode.h>

namespace ZEngine::Event {

    class KeyEvent : public CoreEvent {
    public:
        KeyEvent(ZENGINE_KEYCODE key) : m_keycode(key) {}

        ZENGINE_KEYCODE GetKeyCode() const {
            return m_keycode;
        }

        EVENT_CATEGORY(Keyboard | EventCategory::Input)

    protected:
        ZENGINE_KEYCODE m_keycode;
    };
} // namespace ZEngine::Event
