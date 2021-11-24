#pragma once

#include <fmt/format.h>
#include <Event/MouseEvent.h>
#include <Inputs/KeyCode.h>
#include <ZEngineDef.h>

namespace ZEngine::Event {

#ifdef ZENGINE_WINDOW_SDL
    class MouseButtonMovedEvent : public MouseEvent {
    public:
        MouseButtonMovedEvent(int xpos, int ypos) : m_xpos(xpos), m_ypos(ypos) {}

        int GetPosX() const {
            return m_xpos;
        }
        int GetPosY() const {
            return m_ypos;
        }

        EVENT_TYPE(MouseMoved)

        virtual EventType GetType() const override {
            return GetStaticType();
        }

        virtual int GetCategory() const override {
            return GetStaticCategory();
        }

        virtual std::string ToString() const override {
            return fmt::format("MouseButtonMovedEvent");
        }

    private:
        int m_xpos{0};
        int m_ypos{0};
    };

#else
    class MouseButtonMovedEvent : public MouseEvent {
    public:
        MouseButtonMovedEvent(double xpos, double ypos) : m_xpos(xpos), m_ypos(ypos) {}

        double GetPosX() const {
            return m_xpos;
        }
        double GetPosY() const {
            return m_ypos;
        }

        EVENT_TYPE(MouseMoved)

        virtual EventType GetType() const override {
            return GetStaticType();
        }

        virtual int GetCategory() const override {
            return GetStaticCategory();
        }

        virtual std::string ToString() const override {
            return fmt::format("MouseButtonMovedEvent");
        }

    private:
        double m_xpos{0};
        double m_ypos{0};
    };
#endif

} // namespace ZEngine::Event
