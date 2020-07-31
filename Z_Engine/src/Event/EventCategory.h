#pragma once
#include "../Z_EngineDef.h"


namespace Z_Engine::Event {
	enum  EventCategory {
		None = 0,
		Engine = BIT(0),
		Keyboard = BIT(1),
		Mouse = BIT(2),
		Input = BIT(3),
	};
}