#pragma once
#include <Event/CoreEvent.h>
#include <fmt/include/fmt/format.h>

namespace ZEngine::Event {
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

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const override {
			return fmt::format("EngineClosedEvent");
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(EngineClosed)

	private:
		std::string m_reason{};
	};
}
