#pragma once

#include <Event/CoreEvent.h>
#include <Inputs/KeyCode.h>



namespace ZEngine::Event {

	class MouseEvent : public CoreEvent {
	public:
		MouseEvent() = default;
		explicit MouseEvent(unsigned char button) : m_button(button) {}

		unsigned char GetButton() const { return m_button; }

		EVENT_CATEGORY(Mouse | EventCategory::Input)

	protected:
		unsigned char m_button {0};
	};
}
