#include <Editor.h>
#include <Layers/ExampleLayer.h>
#include <Layers/UserInterfaceLayer.h>
#include <Components/AboutUIComponent.h>
#include <Components/DemoUIComponent.h>


namespace Tetragrama {
	
	Editor::Editor()
		: m_engine(new ZEngine::Engine{})
	{
		ZEngine::Ref<ZEngine::Layers::Layer> example_layer(new Layers::ExampleLayer{});
		m_engine->GetWindow()->PushLayer(example_layer);

		ZEngine::Ref<ZEngine::Layers::ImguiLayer> gui_layer(new Layers::UserInterfaceLayer{});
		std::vector<ZEngine::Ref<ZEngine::Components::UI::UIComponent>> ui_components{
			ZEngine::Ref<ZEngine::Components::UI::UIComponent>(new Components::AboutUIComponent()),
			ZEngine::Ref<ZEngine::Components::UI::UIComponent>(new Components::DemoUIComponent())
		};

		gui_layer->AddUIComponent(std::move(ui_components));
		m_engine->GetWindow()->PushOverlayLayer(gui_layer);
	}

	Editor::~Editor() 
	{
	}


	void Editor::Run() {
		m_engine->Initialize();
		m_engine->Run();
	}
}