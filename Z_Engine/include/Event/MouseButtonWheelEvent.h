#pragma once

#include "../dependencies/fmt/include/fmt/format.h"
#include "MouseEvent.h"
#include "../Inputs/KeyCode.h"

namespace Z_Engine::Event {

	class MouseButtonWheelEvent : public MouseEvent {
	public:
		MouseButtonWheelEvent(std::uint32_t direction, int offset_x, int offset_y)
			:m_offset_x(offset_x), m_offset_y(offset_y), m_direction(direction)
		{
		}

		int GetOffetX() const { return m_offset_x; }
		int GetOffetY() const { return m_offset_y; }


		EVENT_TYPE(MouseWheel)

		virtual EventType GetType() const override {
			return GetStaticType();
		}

		virtual int GetCategory() const override {
			return GetStaticCategory();
		}

		virtual std::string ToString() const override {
			return fmt::format("MouseButtonWheelEvent : {0}", m_button);
		}

	private:
		int m_offset_x{0};
		int m_offset_y{0};
		std::uint32_t m_direction{0};
	};
}