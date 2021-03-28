#pragma once
#include <vector>
#include <Z_Engine.h>

namespace Sandbox3D::Layers {
	class ExampleLayer : public Z_Engine::Layers::Layer {
	public:
		ExampleLayer(const char* name = "example layer")
			: Layer(name)
		{
		}


		virtual ~ExampleLayer() =  default;
		 
		virtual void Initialize()							override;
		virtual void Update(Z_Engine::Core::TimeStep dt)	override;

		virtual void ImGuiRender()							override;
		virtual void Render()								override;
						   
		virtual bool OnEvent(Z_Engine::Event::CoreEvent& e) override;


	private:
		Z_Engine::Ref<Z_Engine::Rendering::Scenes::GraphicScene>				m_scene;
		Z_Engine::Ref<Z_Engine::Managers::TextureManager>						m_texture_manager; 
		std::vector<Z_Engine::Ref<Z_Engine::Rendering::Meshes::Mesh>>			m_mesh_collection;
	};

} 