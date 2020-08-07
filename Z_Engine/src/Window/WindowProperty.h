#pragma once
#include <functional>
#include <string>
#include "CoreWindow.h"
#include "../Event/CoreEvent.h"

namespace Z_Engine::Window {
	struct WindowProperty {
	public:
		unsigned int Width{ 0 }, Height{ 0 };
		std::string Title{};
		bool VSync{ true };

		std::function<void(Event::CoreEvent&)> CallbackFn;

	public:
		WindowProperty(
			unsigned int width = 1080,
			unsigned int height = 900,
			const char* title = "Engine Window"
		)
			: Width(width), Height(height), Title(title)
		{
		}

		~WindowProperty() = default;

	};
}
