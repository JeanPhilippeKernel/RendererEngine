#pragma once
#include <pch.h>
#include <ZEngine/ZEngineDef.h>
#include <ZEngine/Engine.h>

namespace Tetragrama {

	class Editor {
	public:
		Editor();
		~Editor();

        void Run();

	private:
		ZEngine::Scope<ZEngine::Engine> m_engine{ nullptr };
	};

}
