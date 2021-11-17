#pragma once
#include <vector>
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Editor.h>

namespace Tetragrama { class Editor; }

namespace Tetragrama::Layers {
	class ExampleLayer : public ZEngine::Layers::Layer {
	public:
		ExampleLayer(const char* name = "example layer")
			: Layer(name)
		{
		}

		ExampleLayer(ZEngine::Ref<Editor>&& editor, const char* name = "example layer")
			: m_editor(std::move(editor)), Layer(name)
		{
		}

		virtual ~ExampleLayer() =  default;
		 
		virtual void Initialize()							override;
		virtual void Update(ZEngine::Core::TimeStep dt)	override;

		virtual void Render()								override;
						   
		virtual bool OnEvent(ZEngine::Event::CoreEvent& e) override;

		virtual void OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent& e) {
			ZEngine::Event::EventDispatcher event_dispatcher(e);
			event_dispatcher.Dispatch<Components::Event::SceneViewportResizedEvent>(std::bind(&ExampleLayer::OnSceneViewportResized, this, std::placeholders::_1));
			event_dispatcher.Dispatch<Components::Event::SceneTextureAvailableEvent>(std::bind(&ExampleLayer::OnSceneTextureAvailable, this, std::placeholders::_1));
		}

	private:
		bool OnSceneViewportResized(Components::Event::SceneViewportResizedEvent& e);
		bool OnSceneTextureAvailable(Components::Event::SceneTextureAvailableEvent& e);
		
	private:
		ZEngine::WeakRef<Editor> m_editor;
		ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>				m_scene;
		ZEngine::Ref<ZEngine::Managers::TextureManager>						m_texture_manager; 
		std::vector<ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>>			m_mesh_collection;
	};

} 