#pragma once
#include <Event/CoreEvent.h>
#include <fmt/format.h>

namespace ZEngine::Event {
	class WindowMinimizedEvent : public CoreEvent {
	public:
		WindowMinimizedEvent() { m_name = "WindowMinimized"; }
		~WindowMinimizedEvent() = default;


		EventType GetType() const override {
			return GetStaticType();
		}

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const override {
			return fmt::format("WindowMinimizedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowMinimized)
	};
}
