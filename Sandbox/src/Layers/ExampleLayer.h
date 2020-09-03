#pragma once
#include <Z_Engine.h>

namespace Sandbox::Layers {
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
		Z_Engine::Ref<Z_Engine::Rendering::Renderer::GraphicRenderer2D>		m_renderer;
		Z_Engine::Ref<Z_Engine::Controllers::OrthographicCameraController>	m_camera_controller;

		/*glm::mat4 m_transformation_one;
		glm::mat4 m_transformation_two;
		
		glm::vec3 m_position_one;
		glm::vec3 m_position_two;
		glm::vec3 m_scale;
		glm::vec3 m_color;*/
	};

}