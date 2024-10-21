#pragma once
#include <Core/CoreEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Windows::Events
{

    class KeyEvent : public Core::CoreEvent
    {
    public:
        KeyEvent(ZENGINE_KEYCODE key) : m_keycode(key) {}

        ZENGINE_KEYCODE GetKeyCode() const
        {
            return m_keycode;
        }

        EVENT_CATEGORY(Keyboard | Core::EventCategory::Input)

    protected:
        ZENGINE_KEYCODE m_keycode;
    };

    class KeyPressedEvent : public KeyEvent
    {
    public:
        KeyPressedEvent(ZENGINE_KEYCODE key, int repeat_count) : KeyEvent(key), m_repeat_count(repeat_count) {}

        EVENT_TYPE(KeyPressed)

        virtual Core::EventType GetType() const override
        {
            return GetStaticType();
        }

        virtual int GetCategory() const override
        {
            return GetStaticCategory();
        }

        virtual std::string ToString() const override
        {
            return fmt::format("KeyPressedEvent : {0}, repeated count : {1}", m_keycode, m_repeat_count);
        }

    protected:
        int m_repeat_count{0};
    };

    class KeyReleasedEvent : public KeyEvent
    {
    public:
        KeyReleasedEvent(ZENGINE_KEYCODE key) : KeyEvent(key) {}

        EVENT_TYPE(KeyReleased)

        virtual Core::EventType GetType() const override
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
} // namespace ZEngine::Windows::Events
