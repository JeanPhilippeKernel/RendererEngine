#include "ExampleLayer.h"
#include <glm/gtc/type_ptr.hpp>																																	 

using namespace Z_Engine::Rendering::Renderer;
using namespace Z_Engine::Rendering::Cameras;
using namespace Z_Engine::Rendering::Shaders;
using namespace Z_Engine::Rendering::Buffers;
using namespace Z_Engine::Window;
using namespace Z_Engine::Core;
using namespace Z_Engine::Inputs;

using namespace Z_Engine::Managers;
using namespace Z_Engine::Rendering::Textures;

namespace Sandbox::Layers {
	
	void ExampleLayer::Initialize() {
		m_camera.reset(new OrthographicCamera(-1.0f, 1.0f, -1.0f, 1.0f));
		m_renderer.reset(new GraphicRenderer());
		m_renderer->Initialize();
		

		ShaderManager::Load("src/Assets/Shaders/basic.glsl");
		ShaderManager::Load("src/Assets/Shaders/texture.glsl");
		
		TextureManager::Load("src/Assets/Images/free_image.png");
		TextureManager::Load("src/Assets/Images/ChernoLogo.png");

		m_position_one = glm::vec3(0.1f, 0.1f, 0.0f);
		m_position_two = glm::vec3(0.5f, 0.5f, 0.0f);
		
		m_scale =  glm::vec3(1.5f, 1.5f, 0.0f);
		m_color =  glm::vec3(0.6f, 0.0, 1.0f);

		m_transformation_one =  glm::translate(glm::mat4(1.0f), m_position_one) * glm::scale(glm::mat4(1.0f), m_scale);
		m_transformation_two = glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);


		std::vector<float> vertices {
			 0.5f, -0.5f, 1.0f,	1.0f, 0.5f, 0.3f, 1.0f,
			-0.5f, -0.5f, 1.0f,	0.5f, 0.1f, 0.6f, 1.0f,
			 0.0f,	0.5f, 1.0f,	0.1f, 0.3f, 0.2f, 1.0f
		};

		std::vector<unsigned int> indices{ 0, 1, 2 };

		Layout::BufferLayout<float> layout{ Layout::ElementLayout<float>{3, "position"}, Layout::ElementLayout<float>{4, "color"} };
		m_vertex_buffer.reset(new VertexBuffer(vertices, layout));

		m_vertex_array.reset(new VertexArray<float, unsigned int>());
		m_index_buffer.reset(new IndexBuffer(indices));

		m_vertex_array->SetIndexBuffer(m_index_buffer);
		m_vertex_array->AddVertexBuffer(m_vertex_buffer);


		// Drawing second mesh
		std::vector<float> vertices_2{
			-0.5f, -0.5f, 1.0f, 0.0f, 0.0f, 1.0f, 1.0f,	0.0f, 0.0f,
			 0.5f, -0.5f, 1.0f,	0.0f, 0.0f, 1.0f, 1.0f,	1.0f, 0.0f,
			 0.5f,	0.5f, 1.0f,	0.0f, 0.0f, 1.0f, 1.0f,	1.0f, 1.0f,
			-0.5f,	0.5f, 1.0f,	0.0f, 0.0f, 1.0f, 1.0f, 0.0f, 1.0f
		};

		std::vector<unsigned int> indices_2{
			0, 1, 2,
			2, 3, 0
		};

		Layout::BufferLayout<float> layout_2 {
			Layout::ElementLayout<float>{3, "position"}, 
			Layout::ElementLayout<float>{4, "color"}, 
			Layout::ElementLayout<float>{2, "texture"}
		};

		
		m_vertex_buffer_2.reset(new VertexBuffer(vertices_2, layout_2));

		m_vertex_array_2.reset(new VertexArray<float, unsigned int>());
		m_index_buffer_2.reset(new IndexBuffer(indices_2));

		m_vertex_array_2->SetIndexBuffer(m_index_buffer_2);
		m_vertex_array_2->AddVertexBuffer(m_vertex_buffer_2);
		
	}

	void ExampleLayer::Update(Z_Engine::Core::TimeStep dt) {
		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_LEFT)) {
			auto pos = m_camera->GetPosition();
			pos.x -= 0.1f * dt;
			m_camera->SetPosition(pos);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_RIGHT)) {
			auto pos = m_camera->GetPosition();
			pos.x += 0.1f * dt;
			m_camera->SetPosition(pos);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_UP)) {
			auto pos = m_camera->GetPosition();
			pos.y += 0.1f * dt;
			m_camera->SetPosition(pos);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_DOWN)) {
			auto pos = m_camera->GetPosition();
			pos.y -= 0.1f * dt;
			m_camera->SetPosition(pos);
		}


		if(IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_T)) {
			m_position_two.y -= 0.1f * dt;
			m_transformation_two = glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_B)) {
			m_position_two.y += 0.1f * dt;
			m_transformation_two = glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_F)) {
			m_position_two.x += 0.1f * dt;
			m_transformation_two = glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_G)) {
			m_position_two.x -= 0.1f * dt;
			m_transformation_two = 	glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);
		}

	}

	void ExampleLayer::ImGuiRender()
	{
		ImGui::Begin("Editor");
		ImGui::ColorEdit3("Color", glm::value_ptr(m_color));
		ImGui::End();
	}

	void ExampleLayer::Render() {
		RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		RendererCommand::Clear();

		m_renderer->BeginScene(m_camera);

		auto& texture			= TextureManager::Get("ChernoLogo");
		auto& texture_shader	= ShaderManager::Get("texture");
		texture->Bind();
		texture_shader->SetUniform("u_SamplerTex", 0);
		m_renderer->Submit(texture_shader, m_vertex_array_2, m_transformation_two);

		
		/* for (int y = 0; y < 20; ++y)
		 {
			for (int x = 0; x < 15; ++x)
			{
				const auto tranform = glm::translate(glm::mat4(1.0f), glm::vec3(0.11f * x, 0.11f * y, 0.0f)) *
					glm::scale(glm::mat4(1.0f), glm::vec3(0.1f, 0.1f, 0.0f));

				m_renderer->Submit(m_shader_2, m_vertex_array_2, tranform);

			}
		 }*/
		auto& basic_shader = ShaderManager::Get("basic");
		m_renderer->Submit(basic_shader, m_vertex_array, m_transformation_one);

		m_renderer->EndScene();
	}

}