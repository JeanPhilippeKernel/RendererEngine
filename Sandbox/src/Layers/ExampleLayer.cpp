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

		m_texture_manager.reset(new Z_Engine::Managers::TextureManager());
		

		m_texture_manager->Load("src/Assets/Images/free_image.png");
		m_texture_manager->Load("src/Assets/Images/Checkerboard_2.png");
		m_texture_manager->Load("src/Assets/Images/Crate.png");

		m_texture_manager->Add("custom", 1, 1);

		m_camera_controller.reset(new OrthographicCameraController(GetAttachedWindow(), true));
		m_renderer.reset(new GraphicRenderer2D());
		
		m_camera_controller->Initialize();
		m_renderer->Initialize();
		

		m_rect_1_pos = {-0.5f, 0.2f};
		m_rect_2_pos = {0.5f, 0.2f};
		m_rect_3_pos = {0.5f, -0.5f};
	}

	void ExampleLayer::Update(Z_Engine::Core::TimeStep dt) {
		m_camera_controller->Update(dt);

		/*if(IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_T)) {
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
		}*/

	}

	bool ExampleLayer::OnEvent(Z_Engine::Event::CoreEvent& e) {
		m_camera_controller->OnEvent(e);
		return false;
	}

	void ExampleLayer::ImGuiRender()
	{
		ImGui::Begin("Editor");
		ImGui::DragFloat2("Rectangle_one", glm::value_ptr(m_rect_1_pos), .001f);
		ImGui::DragFloat2("Rectangle_two", glm::value_ptr(m_rect_2_pos), .001f);
		ImGui::DragFloat2("Rectangle_three", glm::value_ptr(m_rect_3_pos), .001f);
		ImGui::End();
	}

	void ExampleLayer::Render() {
		RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		RendererCommand::Clear();


		m_renderer->BeginScene(m_camera_controller->GetCamera());
		m_renderer->DrawRect(m_rect_1_pos, { 1.0f, 1.0f }, 0.0f, m_texture_manager->Obtains("Crate"), {1, 255, 1, 255}, 2.0f);
		m_renderer->DrawRect(m_rect_2_pos, { 1.0f, 1.0f }, 0.0f, m_texture_manager->Obtains("Crate"));
		m_renderer->DrawRect(m_rect_3_pos, { 1.0f, 1.0f }, {12, 44, 45}, 0.0f);
		
		m_renderer->DrawTriangle({ 0.5f, -0.7f }, { 1.5f, 1.0f}, 0.0f, m_texture_manager->Obtains("Crate"), {25, 56, 89, 255}, 60);
		
		m_renderer->EndScene();
	}

}