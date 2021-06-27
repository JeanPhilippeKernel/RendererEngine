#pragma once
#include <ZEngine/Z_Engine.h>

namespace Sandbox::Layers {
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
		ZEngine::Ref<ZEngine::Managers::TextureManager>						m_texture_manager; 
		ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene>				m_scene;

		ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>						quad_mesh_ptr;
		ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>						quad_mesh_ptr_1;
		ZEngine::Ref<ZEngine::Rendering::Meshes::Mesh>						quad_mesh_ptr_2;
	};

} 