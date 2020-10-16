#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>


namespace Z_Engine::Event {
	class WindowClosedEvent : public CoreEvent {
	public:
		WindowClosedEvent() { m_name = "WindowClosed"; }
		~WindowClosedEvent() = default;


		EventType GetType() const override {
			return GetStaticType();
		}

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const {
			return fmt::format("WindowClosedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowClosed)

	};
}
