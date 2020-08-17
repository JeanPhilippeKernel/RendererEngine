#pragma once

namespace Z_Engine::Core {

	struct IInitializable
	{
		IInitializable()			= default;
		virtual ~IInitializable()	= default;

		virtual void Initialize()	=  0;
	};
}