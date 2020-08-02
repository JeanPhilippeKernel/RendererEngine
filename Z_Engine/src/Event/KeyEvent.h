#pragma once

#include "CoreEvent.h"
#include "../Inputs/KeyCode.h"



namespace Z_Engine::Event {
	
	class KeyEvent : public CoreEvent {
	public:
		 KeyEvent(Inputs::KeyCode key) : m_keycode(key)	{}
		
		 Inputs::KeyCode GetKeyCode() const {  return m_keycode; }

		 EVENT_CATEGORY(Keyboard | EventCategory::Input)
	
	protected:

		Inputs::KeyCode m_keycode;
	};
}
