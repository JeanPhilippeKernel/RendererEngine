#pragma once

namespace Z_Engine::Core {

	struct IRenderable
	{
		IRenderable()			= default;
		virtual ~IRenderable()	= default;

		virtual void Render()	= 0;
	};
}