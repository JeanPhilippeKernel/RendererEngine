#pragma once

#include <fmt/include/fmt/format.h>
#include <Event/KeyEvent.h>
#include <Inputs/KeyCode.h>

namespace ZEngine::Event {

	class KeyPressedEvent : public KeyEvent {
	public:
		KeyPressedEvent(Inputs::KeyCode key, int repeat_count)
			:KeyEvent(key), m_repeat_count(repeat_count)
		{
		}

		EVENT_TYPE(KeyPressed)

		virtual EventType GetType() const override  {
			return GetStaticType();
		}
		virtual int GetCategory() const override {
			return GetStaticCategory();
		}
		virtual std::string ToString() const override {
			return fmt::format("KeyPressedEvent : {0}, repeated count : {1}", this->m_keycode, m_repeat_count);
		}

	protected:
		int m_repeat_count {0};
	};
}