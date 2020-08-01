#pragma once
#include <string>
#include "../Z_EngineDef.h"
#include "EventType.h"
#include "EventCategory.h"
																					 
namespace Z_Engine::Event {

#define EVENT_TYPE(x) static EventType GetStaticType() { return EventType::x; }
#define EVENT_CATEGORY(x) static int GetStaticCategory() { return EventCategory::x;  }
		
	class Z_ENGINE_API CoreEvent {
	public:
		CoreEvent() = default;
		virtual ~CoreEvent() = default;

		void SetHandled(bool value) { m_handled = value; }
		bool IsHandled() const { return m_handled; }

		const std::string& GetName() const { return m_name; }
		void SetName(const char* value) { m_name = std::string(value); }

		virtual EventType GetType()			const = 0;
		virtual int GetCategory() const = 0;
		virtual std::string ToString()		const = 0;

	protected:
		bool m_handled{ false };
		std::string m_name{};
	};
}
