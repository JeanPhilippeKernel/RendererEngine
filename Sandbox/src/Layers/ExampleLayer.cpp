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
using namespace Z_Engine::Controllers;

namespace Sandbox::Layers {
	
	void ExampleLayer::Initialize() {

		TextureManager::Load("src/Assets/Images/free_image.png");
		TextureManager::Load("src/Assets/Images/Crate.png");

		TextureManager::Add("custom", 1, 1);

		m_camera_controller.reset(new OrthographicCameraController(GetAttachedWindow()));
		m_renderer.reset(new GraphicRenderer2D());
		
		m_camera_controller->Initialize();
		m_renderer->Initialize();

		m_position_one = glm::vec3(0.1f, 0.1f, 0.0f);
		m_position_two = glm::vec3(0.5f, 0.5f, 0.0f);
		
		m_scale = glm::vec3(1.5f, 1.5f, 0.0f);
		m_color = glm::vec3(1, 1, 1);

		m_transformation_one = glm::translate(glm::mat4(1.0f), m_position_one) * glm::scale(glm::mat4(1.0f), m_scale);
		m_transformation_two = glm::translate(glm::mat4(1.0f), m_position_two) * glm::scale(glm::mat4(1.0f), m_scale);
		
	}

	void ExampleLayer::Update(Z_Engine::Core::TimeStep dt) {
		m_camera_controller->Update(dt);

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

	bool ExampleLayer::OnEvent(Z_Engine::Event::CoreEvent& e) {
		m_camera_controller->OnEvent(e);
		return false;
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


		m_renderer->BeginScene(m_camera_controller->GetCamera());
		m_renderer->DrawRect({ -0.6f, 0.6f }, { 1.0f, 1.0f }, TextureManager::Get("free_image"));
		m_renderer->DrawRect({ -0.6f, -0.75f }, { 1.0f, 1.0f }, TextureManager::Get("Crate"));
		m_renderer->DrawRect({ -0.9f, -0.4f }, { 1.0f, 1.0f }, m_color);
		
		m_renderer->DrawTriangle({ -0.5f, 0.6f }, {.5f, .5f }, m_color);
		m_renderer->DrawTriangle({ 0.0f, 0.0f }, { 1.5f, 1.0f}, TextureManager::Get("Crate"));
		
		m_renderer->EndScene();
	}

}