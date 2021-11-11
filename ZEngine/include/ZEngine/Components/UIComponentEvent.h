#pragma once
#include <Event/CoreEvent.h>

namespace ZEngine::Components::UI::Event {

	class UIComponentEvent : public ZEngine::Event::CoreEvent {
	public:
		UIComponentEvent() = default;
        virtual ~UIComponentEvent() = default;
		
		EVENT_TYPE(UserInterfaceComponent)

		EVENT_CATEGORY(UserInterfaceComponent)

		virtual ZEngine::Event::EventType GetType() const override  {
			return GetStaticType();
		}

		virtual int GetCategory() const override {
			return GetStaticCategory();
		}

		virtual std::string ToString() const override {
            return "UIComponentEvent";
		}
	};
}