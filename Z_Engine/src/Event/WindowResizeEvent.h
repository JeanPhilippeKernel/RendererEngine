#pragma once
#include "CoreEvent.h"
#include <fmt/format.h>

namespace Z_Engine::Event {
	class WindowResizeEvent : public CoreEvent {
	public:
		WindowResizeEvent(unsigned int width, unsigned int height)
			:m_width(width), m_height(height)
		{
			m_name = "WindowResized";
		}

		~WindowResizeEvent() = default;


		unsigned int GetWidth() const { return m_width; }
		unsigned int GetHeight() const { return m_height; }

		void SetWidth(unsigned int value) {
			m_width = value;
		}
		
		void SetHeight(unsigned int value) {
			m_height = value;
		}

		EventType GetType() const override {
			return GetStaticType();
		}

		int GetCategory() const override {
			return GetStaticCategory();
		}

		std::string ToString() const {
			return fmt::format("WindowResizeEvent X: {0}, Y: {1}", m_width, m_height);
		}

		EVENT_CATEGORY(Engine)
		EVENT_TYPE(WindowResized)

	private:
		unsigned int m_width{ 0 };
		unsigned int m_height{ 0 };
	};
}
