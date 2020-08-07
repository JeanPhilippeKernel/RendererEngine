#pragma once
#include <EntryPoint.h>
#include "Layers/ExampleLayer.h"
//#include "Layers/UILayer.h"

using namespace Sandbox::Layers;

namespace Sandbox {

	class Sandbox_One : public Z_Engine::Engine {
	public:																																			  
		Sandbox_One() {
			PushLayer(new ExampleLayer());
			//PushOverlayLayer(new UILayer());
		}
		
		~Sandbox_One() = default;
	};

}

namespace Z_Engine {
	Engine* CreateEngine() { return new Sandbox::Sandbox_One(); }
}
