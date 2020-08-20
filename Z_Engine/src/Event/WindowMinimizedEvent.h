#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>

namespace Z_Engine::Event {
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

		std::string ToString() const {
			return fmt::format("WindowMinimizedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowMinimized)
	};
}
