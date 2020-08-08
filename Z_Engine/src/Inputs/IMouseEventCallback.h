#pragma once
#include "../Event/MouseButtonPressedEvent.h"
#include "../Event/MouseButtonReleasedEvent.h"
#include "../Event/MouseButtonMovedEvent.h"
#include "../Event/MouseButtonWheelEvent.h"

namespace Z_Engine::Inputs {

	struct IMouseEventCallback
	{
		IMouseEventCallback() = default;
		~IMouseEventCallback() = default;

		virtual bool OnMouseButtonPressed(Z_Engine::Event::MouseButtonPressedEvent&) = 0;
		virtual bool OnMouseButtonReleased(Z_Engine::Event::MouseButtonReleasedEvent&) = 0;
		virtual bool OnMouseButtonMoved(Z_Engine::Event::MouseButtonMovedEvent&) = 0;
		virtual bool OnMouseButtonWheelMoved(Z_Engine::Event::MouseButtonWheelEvent&) = 0;

	};
}