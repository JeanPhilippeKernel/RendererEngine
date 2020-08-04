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

		virtual void Initialize() override;

		virtual void Update(Z_Engine::Core::TimeStep dt) override;

		virtual void Render() override;
						   
		virtual bool OnEvent(Z_Engine::Event::CoreEvent& e) override {

			return true;
		}


	private:
		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexBuffer<float>> m_vertex_buffer;
		std::shared_ptr<Z_Engine::Rendering::Buffer::IndexBuffer<unsigned int>> m_index_buffer;
		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexArray<float, unsigned int>> m_vertex_array;

		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexBuffer<float>> m_vertex_buffer_2;
		std::shared_ptr<Z_Engine::Rendering::Buffer::IndexBuffer<unsigned int>> m_index_buffer_2;
		std::shared_ptr<Z_Engine::Rendering::Buffer::VertexArray<float, unsigned int>> m_vertex_array_2;

		std::shared_ptr<Z_Engine::Rendering::Shaders::Shader> m_shader;
		std::shared_ptr<Z_Engine::Rendering::Shaders::Shader> m_shader_2;

		std::shared_ptr<Z_Engine::Rendering::Renderer::GraphicRenderer> m_renderer;
		std::shared_ptr<Z_Engine::Rendering::Cameras::OrthographicCamera> m_camera;


		glm::mat4 m_transformation_one;
		glm::mat4 m_transformation_two;
		
		glm::vec3 m_position_one;
		glm::vec3 m_position_two;
		glm::vec3 m_scale;
	};

}