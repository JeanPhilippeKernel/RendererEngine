#pragma once
#include <ZEngineDef.h>

namespace ZEngine::Event {
	enum EventCategory {
		None		= 0,
		Engine		= BIT(0),
		Keyboard	= BIT(1),
		Mouse		= BIT(2),
		Input		= BIT(3),
		UserInterfaceComponent = BIT(4)
	};
}