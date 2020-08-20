#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>

namespace Z_Engine::Event {
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

		std::string ToString() const {
			return fmt::format("WindowMaximizedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowMaximized)
	};
}
