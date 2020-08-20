#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>

namespace Z_Engine::Event {
	class WindowRestoredEvent : public CoreEvent {
	public:
		WindowRestoredEvent() { m_name = "WindowRestored"; }
		~WindowRestoredEvent() = default;


		EventType GetType() const override {
			return GetStaticType();
		}

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const {
			return fmt::format("WindowRestoredEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowRestored)
	};
}
