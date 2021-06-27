#pragma once
#include <functional>
#include <string>
#include <Window/CoreWindow.h>
#include <Event/CoreEvent.h>

namespace ZEngine::Window {
	struct WindowProperty {
	public:
		unsigned int Width{ 0 }, Height{ 0 };
		std::string Title{};
		bool VSync{ true };
		float AspectRatio{ 0.0f };
		bool IsMinimized{false};

		std::function<void(Event::CoreEvent&)> CallbackFn;

	public:
		WindowProperty(
			unsigned int width	= 1080,
			unsigned int height = 800,
			const char* title	= "Engine Window"
		)
			: Width(width), Height(height), Title(title)
		{
			UpdateAspectRatio();
		}

		~WindowProperty() = default;

		void SetWidth(unsigned int w) { 
			Width = w; 
			UpdateAspectRatio();
		}

		void SetHeight(unsigned int h) { 
			Height = h; 
			UpdateAspectRatio();
		}

	private:
		void UpdateAspectRatio() {
			AspectRatio = (float)Width / (float)Height;
		}

	};
}
