#pragma once
#include <vector>
#include <ZEngine/Z_Engine.h>

namespace Sandbox3D::Layers {
	class ExampleLayer : public ZEngine::Layers::Layer {
	public:
		ExampleLayer(const char* name = "example layer")
			: Layer(name)
		{
		}


		virtual ~ExampleLayer() =  default;
		 
		virtual void Initialize()							override;
		virtual void Update(ZEngine::Core::TimeStep dt)	override;

		virtual void ImGuiRender()							override;
		virtual void Render()								override;
						   
		virtual bool OnEvent(ZEngine::Event::CoreEvent& e) override;


	private:
		ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>				m_scene;
		ZEngine::Ref<ZEngine::Managers::TextureManager>						m_texture_manager; 
		std::vector<ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>>			m_mesh_collection;
	};

} 