#pragma once
#include "../Event/TextInputEvent.h"

namespace Z_Engine::Inputs {

	struct ITextInputEventCallback
	{
		ITextInputEventCallback() = default;
		~ITextInputEventCallback() = default;

		virtual bool OnTextInputRaised(Z_Engine::Event::TextInputEvent&) = 0;

	};
}
