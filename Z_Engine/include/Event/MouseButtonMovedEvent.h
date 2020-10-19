#pragma once

#include "../dependencies/fmt/include/fmt/format.h"
#include "MouseEvent.h"
#include "../Inputs/KeyCode.h"

namespace Z_Engine::Event {

	class MouseButtonMovedEvent : public MouseEvent {
	public:
		MouseButtonMovedEvent(int xpos, int ypos)
			:m_xpos(xpos), m_ypos(ypos)
		{
		}

		int GetPosX() const { return m_xpos; }
		int GetPosY() const { return m_ypos; }

		EVENT_TYPE(MouseMoved)

		virtual EventType GetType() const override {
			return GetStaticType();
		}

		virtual int GetCategory() const override {
			return GetStaticCategory();
		}

		virtual std::string ToString() const override {
			return fmt::format("MouseButtonMovedEvent");
		}

	private:
		int m_xpos {0};
		int m_ypos {0};
	};
}