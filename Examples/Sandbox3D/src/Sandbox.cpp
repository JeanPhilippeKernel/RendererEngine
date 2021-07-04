#pragma once
#include <EntryPoint.h>
#include "Layers/ExampleLayer.h"

using namespace Sandbox3D::Layers;

namespace Sandbox3D {

	class Sandbox : public ZEngine::Engine {
	public:
		Sandbox() {
			PushLayer(new ExampleLayer());
		}

		~Sandbox() = default;
	};

}

namespace ZEngine {
	Engine* CreateEngine() { return new Sandbox3D::Sandbox(); }
}
