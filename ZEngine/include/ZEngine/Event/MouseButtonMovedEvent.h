#pragma once

#include <Event/MouseEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>
#include <fmt/format.h>

namespace ZEngine::Event
{

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

} // namespace ZEngine::Event
