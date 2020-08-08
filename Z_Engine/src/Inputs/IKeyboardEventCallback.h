#pragma once
#include "../Event/KeyPressedEvent.h"
#include "../Event/KeyReleasedEvent.h"


namespace Z_Engine::Inputs {

	struct IKeyboardEventCallback
	{
	   IKeyboardEventCallback() =  default;
	   ~IKeyboardEventCallback() =  default;

	   virtual bool OnKeyPressed(Z_Engine::Event::KeyPressedEvent&) = 0;
	   virtual bool OnKeyReleased(Z_Engine::Event::KeyReleasedEvent&) = 0;
	};
}