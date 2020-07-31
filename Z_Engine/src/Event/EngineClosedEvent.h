#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>

namespace Z_Engine::Event {
	class EngineClosedEvent : public CoreEvent {
	public:
		EngineClosedEvent(const char * r)
			:CoreEvent(), m_reason(r)
		{ 
			m_name = "EngineClosed"; 
		}
		~EngineClosedEvent() = default;


		void SetReason(const char* value) {
			m_reason = value;
		}


		EventType GetType() const override {
			return GetStaticType();
		}

		EventCategory GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const {
			return fmt::format("EngineClosedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(EngineClosed)

	private:
		std::string m_reason{};
	};
}
