#pragma once

#include <Event/CoreEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Event
{

    class MouseEvent : public CoreEvent
    {
    public:
        MouseEvent() = default;
        explicit MouseEvent(ZENGINE_KEYCODE button) : m_button(button) {}

        ZENGINE_KEYCODE GetButton() const
        {
            return m_button;
        }

        EVENT_CATEGORY(Mouse | EventCategory::Input)

    protected:
        ZENGINE_KEYCODE m_button{0};
    };

    class MouseButtonPressedEvent : public MouseEvent
    {
    public:
        MouseButtonPressedEvent(ZENGINE_KEYCODE button) : MouseEvent(button) {}

        EVENT_TYPE(MouseButtonPressed)

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
            return fmt::format("MouseButtonPressedEvent : {0}", m_button);
        }
    };

    class MouseButtonReleasedEvent : public MouseEvent
    {
    public:
        explicit MouseButtonReleasedEvent(ZENGINE_KEYCODE button) : MouseEvent(button) {}

        EVENT_TYPE(MouseButtonReleased)

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
            return fmt::format("MouseButtonReleasedEvent : {0}", m_button);
        }
    };

    class MouseButtonMovedEvent : public MouseEvent
    {
    public:
        MouseButtonMovedEvent(double xpos, double ypos) : m_xpos(xpos), m_ypos(ypos) {}

        double GetPosX() const
        {
            return m_xpos;
        }

        double GetPosY() const
        {
            return m_ypos;
        }

        EVENT_TYPE(MouseMoved)

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
            return fmt::format("MouseButtonMovedEvent");
        }

    private:
        double m_xpos{0};
        double m_ypos{0};
    };

    class MouseButtonWheelEvent : public MouseEvent
    {
    public:
        MouseButtonWheelEvent(double offset_x, double offset_y) : m_offset_x(offset_x), m_offset_y(offset_y) {}

        double GetOffetX() const
        {
            return m_offset_x;
        }

        double GetOffetY() const
        {
            return m_offset_y;
        }

        EVENT_TYPE(MouseWheel)

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
            return fmt::format("MouseButtonWheelEvent : {0}", m_button);
        }

    private:
        double m_offset_x{0};
        double m_offset_y{0};
    };
} // namespace ZEngine::Event
