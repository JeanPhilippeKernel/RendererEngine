#pragma once
#include <Event/CoreEvent.h>
#include <fmt/include/fmt/format.h>

namespace ZEngine::Event {
	class WindowMaximizedEvent : public CoreEvent {
	public:
		WindowMaximizedEvent() { m_name = "WindowMaximized"; }
		~WindowMaximizedEvent() = default;


		EventType GetType() const override {
			return GetStaticType();
		}

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const override {
			return fmt::format("WindowMaximizedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowMaximized)
	};
}
