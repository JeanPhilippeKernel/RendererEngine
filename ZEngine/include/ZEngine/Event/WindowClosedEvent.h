#pragma once
#include <Event/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event {
    class WindowClosedEvent : public CoreEvent {
    public:
        WindowClosedEvent() {
            m_name = "WindowClosed";
        }
        ~WindowClosedEvent() = default;

        EventType GetType() const override {
            return GetStaticType();
        }

        int GetCategory() const override {
            return GetStaticCategory();
        }

        std::string ToString() const override {
            return fmt::format("WindowClosedEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowClosed)
    };
} // namespace ZEngine::Event
