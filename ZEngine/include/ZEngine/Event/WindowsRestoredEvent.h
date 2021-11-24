#pragma once
#include <Event/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event {
    class WindowRestoredEvent : public CoreEvent {
    public:
        WindowRestoredEvent() {
            m_name = "WindowRestored";
        }
        ~WindowRestoredEvent() = default;

        EventType GetType() const override {
            return GetStaticType();
        }

        int GetCategory() const override {
            return GetStaticCategory();
        }

        std::string ToString() const override {
            return fmt::format("WindowRestoredEvent");
        }

        EVENT_CATEGORY(Engine)
        EVENT_TYPE(WindowRestored)
    };
} // namespace ZEngine::Event
