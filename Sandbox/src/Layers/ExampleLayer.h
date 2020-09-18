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
		Z_Engine::Ref<Z_Engine::Managers::TextureManager> m_texture_manager;

		Z_Engine::Ref<Z_Engine::Rendering::Renderers::GraphicRenderer2D>		m_renderer;
		Z_Engine::Ref<Z_Engine::Controllers::OrthographicCameraController>	m_camera_controller;

		glm::vec2 m_rect_1_pos;
		glm::vec2 m_rect_2_pos;
		glm::vec2 m_rect_3_pos;
	};

}