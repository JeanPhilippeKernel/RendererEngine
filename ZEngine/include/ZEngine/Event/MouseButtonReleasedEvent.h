#pragma once

#include <fmt/include/fmt/format.h>
#include <Event/MouseEvent.h>
#include <Inputs/KeyCode.h>

namespace ZEngine::Event {

	class MouseButtonReleasedEvent : public MouseEvent {
	public:
		explicit MouseButtonReleasedEvent(unsigned char button)
			:MouseEvent(button)
		{
		}

		EVENT_TYPE(MouseButtonReleased)

			virtual EventType GetType() const override {
			return GetStaticType();
		}
		virtual int GetCategory() const override {
			return GetStaticCategory();
		}
		virtual std::string ToString() const override {
			return fmt::format("MouseButtonReleasedEvent : {0}", m_button);
		}

	};
}