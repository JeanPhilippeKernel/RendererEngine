#include "ExampleLayer.h"
#include "dependencies/glm/gtc/type_ptr.hpp"	

using namespace Z_Engine::Rendering::Materials;

using namespace Z_Engine;
using namespace Z_Engine::Rendering::Geometries;

using namespace Z_Engine::Rendering::Renderers;
using namespace Z_Engine::Rendering::Cameras;
using namespace Z_Engine::Rendering::Shaders;
using namespace Z_Engine::Rendering::Buffers;
using namespace Z_Engine::Window;
using namespace Z_Engine::Core;
using namespace Z_Engine::Inputs;
using namespace Z_Engine::Event;

using namespace Z_Engine::Managers;
using namespace Z_Engine::Rendering::Textures;
using namespace Z_Engine::Controllers;

using namespace Z_Engine::Rendering::Meshes;

namespace Sandbox::Layers {
	
	void ExampleLayer::Initialize() {

		m_texture_manager.reset(new Z_Engine::Managers::TextureManager());
		
													   
		m_texture_manager->Load("src/Assets/Images/free_image.png");
		//m_texture_manager->Load("src/Assets/Images/Checkerboard_2.png");
		//m_texture_manager->Load("src/Assets/Images/Crate.png");
		m_texture_manager->Load("src/Assets/Images/Flying_Mario.png");
		m_texture_manager->Load("src/Assets/Images/mario_and_sonic.png");


		m_camera_controller.reset(new OrthographicCameraController(GetAttachedWindow(), true));
		m_renderer.reset(new GraphicRenderer2D());
		
		m_camera_controller->Initialize();
		m_renderer->Initialize();


		quad_mesh_ptr_3.reset(MeshBuilder::CreateQuad({-0.8f, -0.8f}, {0.5f, 0.5f},  glm::radians(30.0f), m_texture_manager->Obtains("free_image")));
		quad_mesh_ptr_2.reset(MeshBuilder::CreateQuad({0.0f, 0.0f}, {0.5f, 0.5f},  glm::radians(60.0f), m_texture_manager->Obtains("mario_and_sonic")));

		
		auto material  = new StandardMaterial();
		material->SetTexture(m_texture_manager->Obtains("Flying_Mario"));
		material->SetTileFactor(5.f);
		material->SetTintColor({0.5f, 1.0f, 0.0f, 1.0f});

		quad_mesh_ptr_1.reset(MeshBuilder::CreateQuad({1.0f, 1.0f}, {0.5f, 0.5f},  glm::radians(45.0f), material));
		
	}

	void ExampleLayer::Update(TimeStep dt) {
		m_camera_controller->Update(dt);

		//if(IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_J)) {
		//	//m_rect_1_pos.y -= 0.1f * dt;
		//	m_rect_1_pos.x -= 0.1f * dt;
		//}

		//if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_F)) {
		//	//m_rect_1_pos.y += 0.1f * dt;
		//	m_rect_1_pos.x += 0.1f * dt;
		//}

		//if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_B)) {
		//	m_rect_1_pos.y += 0.1f * dt;
		//	//m_rect_1_pos.x += 0.1f * dt;
		//}
		//if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_Y)) {
		//	m_rect_1_pos.y -= 0.1f * dt;
		//	//m_rect_1_pos.x += 0.1f * dt;
		//}
	}

	bool ExampleLayer::OnEvent(CoreEvent& e) {
		m_camera_controller->OnEvent(e);
		return false;
	}

	void ExampleLayer::ImGuiRender()
	{
		/*ImGui::Begin("Editor");
		ImGui::DragFloat2("Rectangle_one", glm::value_ptr(m_rect_1_pos), .5f);
		ImGui::DragFloat2("Rectangle_two", glm::value_ptr(m_rect_2_pos), .05f);
		ImGui::DragFloat2("Rectangle_three", glm::value_ptr(m_rect_3_pos), .5f);
		ImGui::End();*/
	}

	void ExampleLayer::Render() {

		RendererCommand::SetClearColor({ 0.2f, 0.2f, 0.2f, 1.0f });
		RendererCommand::Clear();


		m_renderer->StartScene(m_camera_controller->GetCamera());
		m_renderer->Draw(quad_mesh_ptr_1);
		m_renderer->Draw(quad_mesh_ptr_2);
		m_renderer->Draw(quad_mesh_ptr_3);

		m_renderer->EndScene();
		

		
	}

}