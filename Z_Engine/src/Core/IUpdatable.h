#pragma once
#include "TimeStep.h"

namespace Z_Engine::Core {

	struct IUpdatable
	{
		IUpdatable()						= default;
		virtual ~IUpdatable()				= default;

		virtual void Update(TimeStep dt)	= 0;
	};
}