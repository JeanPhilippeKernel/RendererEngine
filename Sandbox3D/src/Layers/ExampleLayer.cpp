#include "ExampleLayer.h"


using namespace Z_Engine;

using namespace Z_Engine::Rendering::Materials;
using namespace Z_Engine::Rendering::Scenes;
using namespace Z_Engine::Rendering::Renderers;
using namespace Z_Engine::Window;
using namespace Z_Engine::Core;
using namespace Z_Engine::Inputs;
using namespace Z_Engine::Event;

using namespace Z_Engine::Managers;
using namespace Z_Engine::Rendering::Textures;
using namespace Z_Engine::Controllers;

using namespace Z_Engine::Rendering::Meshes;

namespace Sandbox3D::Layers {
	
	void ExampleLayer::Initialize() {

		m_texture_manager.reset(new Z_Engine::Managers::TextureManager());
		m_texture_manager->Load("Assets/Images/free_image.png");
		m_texture_manager->Load("Assets/Images/Crate.png");
		m_texture_manager->Load("Assets/Images/Checkerboard_2.png");


		m_scene.reset(new GraphicScene3D(new OrbitCameraController(GetAttachedWindow(), glm::vec3(0.0f, 20.0f, 50.f), 10.0f, -20.0f)));
		m_scene->Initialize();
		
		quad_mesh_ptr.reset(MeshBuilder::CreateCube({ 0.f, -0.5f, 0.0f }, { 100.f, .0f, 100.f }, 0.0f,  glm::vec3(1.f, 0.0f, 0.0f), m_texture_manager->Obtains("Checkerboard_2")));
		
		quad_mesh_ptr_3.reset(MeshBuilder::CreateCube({ -20.f, 10.f, 0.0f }, { 5.f, 5.0f, 5.f }, {45.0f, 120.0f, 30.0f}, 0.0f, glm::vec3(0.f, 1.0f, 0.0f)));
		
		Ref<MixedTextureMaterial> material(new MixedTextureMaterial{});
		material->SetInterpolateFactor(0.5f);
		material->SetTexture(m_texture_manager->Obtains("free_image"));
		material->SetSecondTexture(m_texture_manager->Obtains("Crate"));
		
		quad_mesh_ptr_2.reset(MeshBuilder::CreateCube({ 0.f, 10.f, 0.0f }, { 10.f, 10.0f, 10.f }, 0.0f, glm::vec3(0.f, 1.0f, 0.0f)));
		quad_mesh_ptr_2->SetMaterial(material);

	}

	void ExampleLayer::Update(TimeStep dt) {
		m_scene->GetCameraController()->Update(dt);

		quad_mesh_ptr_2
			->GetGeometry()
			->ApplyTransform(
				glm::rotate(glm::mat4(1.0f), glm::sin((float)dt) * 0.005f, glm::vec3(0.f, 1.0f, 0.0f)) 
			);

		if(IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_J)) {
		
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_F)) {
			
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_B)) {
			
		}

		if (IDevice::As<Z_Engine::Inputs::Keyboard>()->IsKeyPressed(Z_ENGINE_KEY_Y)) {
			
		}
	}

	bool ExampleLayer::OnEvent(CoreEvent& e) {
		m_scene->GetCameraController()->OnEvent(e);
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
		m_scene->Add(quad_mesh_ptr);
		m_scene->Add(quad_mesh_ptr_2);
		m_scene->Add(quad_mesh_ptr_3);

		m_scene->Render();
	}

}