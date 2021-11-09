#pragma once
#include <ZEngine/EntryPoint.h>
#include <ExampleLayer.h>
#include <GUILayer.h>
#include <AboutUIComponent.h>
#include <DemoUIComponent.h>

using namespace Sandbox3D::Layers;
using namespace Sandbox3D::Components;

namespace Sandbox3D {

	class Sandbox : public ZEngine::Engine {
	public:
		Sandbox() {
			auto window = this->GetWindow();
			
			ZEngine::Ref<ZEngine::Layers::Layer> example_layer(new ExampleLayer{});
			window->PushLayer(example_layer);
			
			ZEngine::Ref<ZEngine::Layers::ImguiLayer> gui_layer(new GUILayer{});
			std::vector<ZEngine::Ref<ZEngine::Components::UI::UIComponent>> ui_components {
				ZEngine::Ref<ZEngine::Components::UI::UIComponent> (new AboutUIComponent()),
				ZEngine::Ref<ZEngine::Components::UI::UIComponent> (new DemoUIComponent())
			};
			gui_layer->AddUIComponent(std::move(ui_components));
			window->PushOverlayLayer(gui_layer);			
		}		

		~Sandbox() = default;
	};

}

namespace ZEngine {
	Engine* CreateEngine() { return new Sandbox3D::Sandbox(); }
}
