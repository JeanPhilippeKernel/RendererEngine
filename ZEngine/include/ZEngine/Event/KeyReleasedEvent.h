#pragma once

#include <fmt/include/fmt/format.h>
#include <Event/KeyEvent.h>
#include <Inputs/KeyCode.h>


namespace ZEngine::Event {

	class KeyReleasedEvent : public KeyEvent {
	public:
		 KeyReleasedEvent(Inputs::KeyCode key)
			 : KeyEvent(key) 
		 {
		 }

		 EVENT_TYPE(KeyReleased)
		 
		virtual EventType GetType()	const override {
		  return GetStaticType();
		}
		
		virtual int GetCategory() const override {
			return GetStaticCategory();
		}
		virtual std::string ToString() const override {
			return fmt::format("KeyReleasedEvent : {0}", m_keycode);
		}

	protected:
	};
}