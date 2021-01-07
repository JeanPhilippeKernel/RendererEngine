#pragma once
#include <EntryPoint.h>
#include "Layers/ExampleLayer.h"

using namespace Sandbox3D::Layers;

namespace Sandbox3D {

	class Sandbox : public Z_Engine::Engine {
	public:
		Sandbox() {
			PushLayer(new ExampleLayer());
		}

		~Sandbox() = default;
	};

}

namespace Z_Engine {
	Engine* CreateEngine() { return new Sandbox3D::Sandbox(); }
}
