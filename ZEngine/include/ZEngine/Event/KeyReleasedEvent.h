#pragma once

#include <Event/KeyEvent.h>
#include <Inputs/KeyCode.h>
#include <fmt/format.h>

namespace ZEngine::Event
{

    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(ZENGINE_KEYCODE key) : KeyEvent(key) {}

        EVENT_TYPE(KeyReleased)

        virtual EventType GetType() const override
        {
            return GetStaticType();
        }

        virtual int GetCategory() const override
        {
            return GetStaticCategory();
        }
        virtual std::string ToString() const override
        {
            return fmt::format("KeyReleasedEvent : {0}", m_keycode);
        }
    };
} // namespace ZEngine::Event
