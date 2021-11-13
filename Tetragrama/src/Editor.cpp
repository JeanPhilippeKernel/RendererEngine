#include <Editor.h>
#include <Layers/ExampleLayer.h>
#include <Layers/UserInterfaceLayer.h>
#include <Components/DockspaceUIComponent.h>
#include <Components/AboutUIComponent.h>
#include <Components/DemoUIComponent.h>


namespace Tetragrama {
	
	Editor::Editor()
		: 
		m_engine(new ZEngine::Engine{}), 
		m_ui_layer(new Layers::UserInterfaceLayer{})
	{		
	}

	Editor::~Editor() 
	{
	}

	void Editor::Initialize() {
		// ZEngine::Ref<Layers::ExampleLayer> example(new Layers::ExampleLayer{});
		// m_engine->GetWindow()->PushLayer(example);

		ZEngine::Ref<ZEngine::Components::UI::UIComponent> dockspace_component(new Components::DockspaceUIComponent{});
		ZEngine::Ref<ZEngine::Components::UI::UIComponent> about_component(new Components::AboutUIComponent{});
		ZEngine::Ref<ZEngine::Components::UI::UIComponent> demo_component(new Components::DemoUIComponent{});

		m_ui_layer->AddUIComponent(std::move(about_component));
		m_ui_layer->AddUIComponent(std::move(demo_component));
		m_ui_layer->AddUIComponent(std::move(dockspace_component));
		m_engine->GetWindow()->PushOverlayLayer(m_ui_layer);
	}

	void Editor::Run() {
		m_engine->Initialize();
		m_engine->Run();
	}
}