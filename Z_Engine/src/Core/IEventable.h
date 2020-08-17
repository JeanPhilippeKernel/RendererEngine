#pragma once

#include "../Event/CoreEvent.h"

namespace Z_Engine::Core {

	struct IEventable
	{
		IEventable()							= default;
		virtual ~IEventable()					= default;

		virtual bool OnEvent(Event::CoreEvent&) = 0;
	};
}