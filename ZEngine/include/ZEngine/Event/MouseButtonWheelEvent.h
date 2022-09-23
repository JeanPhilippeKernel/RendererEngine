#pragma once

#include <fmt/format.h>
#include <Event/MouseEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Event {


    class MouseButtonWheelEvent : public MouseEvent {
    public:
        MouseButtonWheelEvent(double offset_x, double offset_y) : m_offset_x(offset_x), m_offset_y(offset_y) {}

        double GetOffetX() const {
            return m_offset_x;
        }
        double GetOffetY() const {
            return m_offset_y;
        }

        EVENT_TYPE(MouseWheel)

        virtual EventType GetType() const override {
            return GetStaticType();
        }

        virtual int GetCategory() const override {
            return GetStaticCategory();
        }

        virtual std::string ToString() const override {
            return fmt::format("MouseButtonWheelEvent : {0}", m_button);
        }

    private:
        double m_offset_x{0};
        double m_offset_y{0};
    };

} // namespace ZEngine::Event
