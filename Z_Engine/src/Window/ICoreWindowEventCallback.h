#pragma once
#include "../Event/WindowClosedEvent.h"
#include "../Event/WindowResizedEvent.h"
#include "../Event/WindowMaximizedEvent.h"
#include "../Event/WindowMinimizedEvent.h"
#include "../Event/WindowsRestoredEvent.h"

namespace Z_Engine::Window {
	struct ICoreWindowEventCallback {
		ICoreWindowEventCallback()	= default;
		~ICoreWindowEventCallback() = default;

		virtual bool OnWindowClosed(Event::WindowClosedEvent&)			= 0;
		virtual bool OnWindowResized(Event::WindowResizedEvent&)		= 0;
		virtual bool OnWindowMinimized(Event::WindowMinimizedEvent&)	= 0;
		virtual bool OnWindowMaximized(Event::WindowMaximizedEvent&)	= 0;
		virtual bool OnWindowRestored(Event::WindowRestoredEvent&)		= 0;

	};
}