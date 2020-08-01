#pragma once

#include "CoreEvent.h"
#include "../Core/Input.h"


using namespace Z_Engine::Core::Input;

namespace Z_Engine::Event {
	
	class KeyEvent : public CoreEvent {
	public:

		KeyCode GetKeyCode() const {  return m_keycode; }

		EVENT_CATEGORY(Keyboard | EventCategory::Input)
	
	protected:
		KeyEvent(KeyCode key) : m_keycode(key)	{}

		KeyCode m_keycode;
	};
}
