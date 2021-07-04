#pragma once

#include <fmt/format.h>
#include <Event/CoreEvent.h>

namespace ZEngine::Event {

	class TextInputEvent : public CoreEvent {
	public:
		TextInputEvent(const char * content)
			:m_text(content)
		{
		}							  

		EVENT_TYPE(TextInput)
		EVENT_CATEGORY(Keyboard | EventCategory::Input)

		virtual EventType GetType() const override {
			return GetStaticType();
		}

		virtual int GetCategory() const override {
			return GetStaticCategory();
		}

		virtual std::string ToString() const override {
			return fmt::format("TextInputEvent : {0}", this->m_text);
		}

		const std::string& GetText() const  { return m_text; }

	protected:
		std::string m_text;
	};
}