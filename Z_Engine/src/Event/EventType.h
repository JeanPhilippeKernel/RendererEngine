#pragma once

namespace Z_Engine::Event {
	enum class EventType {
		None = 0,

		WindowShown, WindowHidden, WindowMoved, WindowResized, WindowClosed,
		WindowSizeChanged, WindowMinized, WindowMaximized,
		WindowRestored, WindowFocusLost, WindowFocusGained,

		KeyPressed, KeyReleased,
		MouseButtonPressed, MouseButtonReleased, MouseMoved, MouseWheel,

		EngineClosed,

		TextInput
	};
}
