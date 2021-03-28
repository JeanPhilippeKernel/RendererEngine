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
using namespace Z_Engine::Maths;

namespace Sandbox::Layers {
	
	void ExampleLayer::Initialize() {

		m_texture_manager.reset(new Z_Engine::Managers::TextureManager());
		
		m_texture_manager->Load("Assets/Images/free_image.png");
		m_texture_manager->Load("Assets/Images/Checkerboard_2.png");
		m_texture_manager->Load("Assets/Images/Crate.png");
		m_texture_manager->Load("Assets/Images/Flying_Mario.png");
		m_texture_manager->Load("Assets/Images/mario_and_sonic.png");

		m_scene.reset(new GraphicScene2D(new OrthographicCameraController(GetAttachedWindow(), true)));
		m_scene->Initialize();

		quad_mesh_ptr.reset(MeshBuilder::CreateQuad({-0.8f, -0.8f}, {0.5f, 0.5f}, radians(30.0f), m_texture_manager->Obtains("Flying_Mario")));
		quad_mesh_ptr_1.reset(MeshBuilder::CreateQuad({0.5f, 0.5f}, {0.5f, 0.5}, radians(0.0f), m_texture_manager->Obtains("mario_and_sonic")));
		
		Ref<MixedTextureMaterial> material(new MixedTextureMaterial{});
		material->SetInterpolateFactor(0.5f);
		material->SetTexture(m_texture_manager->Obtains("free_image"));
		material->SetSecondTexture(m_texture_manager->Obtains("Crate"));

		quad_mesh_ptr_2.reset(MeshBuilder::CreateQuad({0.0f, 0.0f}, {0.5f, 0.5}, 0.0f));
		quad_mesh_ptr_2->SetMaterial(material);
	}

	void ExampleLayer::Update(TimeStep dt) {
		m_scene->GetCameraController()->Update(dt);

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

		std::vector<Ref<Mesh>> meshes{ quad_mesh_ptr_2, quad_mesh_ptr_1, quad_mesh_ptr };
		m_scene->Add(meshes);
		m_scene->Render();
	}

}