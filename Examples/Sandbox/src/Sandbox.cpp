#pragma once
#include <ZEngine/EntryPoint.h>
#include <ExampleLayer.h>
#include <GUILayer.h>
#include <AboutUIComponent.h>
#include <DemoUIComponent.h>

using namespace Sandbox::Layers;
using namespace Sandbox::Components;

namespace Sandbox {

	class Sandbox_One : public ZEngine::Engine {
	public:																																			  
		Sandbox_One() : ZEngine::Engine() {
			auto window = this->GetWindow();
			window->PushLayer(new ExampleLayer());
			

			ZEngine::Layers::ImguiLayer* gui_layer(new GUILayer{});
			std::vector<ZEngine::Ref<ZEngine::Components::UI::UIComponent>> ui_components {
				ZEngine::Ref<ZEngine::Components::UI::UIComponent> (new AboutUIComponent()),
				ZEngine::Ref<ZEngine::Components::UI::UIComponent> (new DemoUIComponent())
			};
			gui_layer->AddUIComponent(std::move(ui_components));
			window->PushOverlayLayer(gui_layer);			
		}
		~Sandbox_One() = default;
	};

}

namespace ZEngine {
	Engine* CreateEngine() { return new Sandbox::Sandbox_One(); }
}
