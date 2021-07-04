#include "ExampleLayer.h"


using namespace ZEngine;

using namespace ZEngine::Rendering::Materials;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Window;
using namespace ZEngine::Core;
using namespace ZEngine::Inputs;
using namespace ZEngine::Event;

using namespace ZEngine::Managers;
using namespace ZEngine::Rendering::Textures;
using namespace ZEngine::Controllers;

using namespace ZEngine::Rendering::Meshes;
using namespace ZEngine::Maths;

namespace Sandbox3D::Layers {
	
	void ExampleLayer::Initialize() {

		m_texture_manager.reset(new ZEngine::Managers::TextureManager());
		m_texture_manager->Load("Assets/Images/free_image.png");
		m_texture_manager->Load("Assets/Images/Crate.png");
		m_texture_manager->Load("Assets/Images/Checkerboard_2.png");
		m_texture_manager->Load("Assets/Images/zota.jpg");


		m_scene.reset(new GraphicScene3D(new OrbitCameraController(GetAttachedWindow(), Vector3(0.0f, 20.0f, 50.f), 10.0f, -20.0f)));
		m_scene->Initialize();
		

		Ref<ZEngine::Rendering::Meshes::Mesh> mesh_one;
		Ref<ZEngine::Rendering::Meshes::Mesh> mesh_two;
		Ref<ZEngine::Rendering::Meshes::Mesh> mesh_three;

		mesh_one.reset(MeshBuilder::CreateCube({ 0.f, -0.5f, 0.0f }, { 100.f, .0f, 100.f }, 0.0f, Vector3(1.f, 0.0f, 0.0f), m_texture_manager->Obtains("Checkerboard_2")));
		mesh_three.reset(MeshBuilder::CreateCube({ -20.f, 10.f, 0.0f }, { 5.f, 5.0f, 5.f }, {45.0f, 120.0f, 30.0f}, 0.0f, Vector3(0.f, 1.0f, 0.0f)));
		
		Ref<MixedTextureMaterial> material(new MixedTextureMaterial{});
		material->SetInterpolateFactor(.5f);
		material->SetTexture(m_texture_manager->Obtains("zota"));
		material->SetSecondTexture(m_texture_manager->Obtains("Crate"));
		
		mesh_two.reset(MeshBuilder::CreateCube({ 0.f, 10.f, 0.0f }, { 10.f, 10.0f, 10.f }, 0.0f, Vector3(0.f, 1.0f, 0.0f)));
		mesh_two->SetMaterial(material);


		m_mesh_collection.emplace_back(mesh_one);
		m_mesh_collection.emplace_back(mesh_two);
		m_mesh_collection.emplace_back(mesh_three);
	}

	void ExampleLayer::Update(TimeStep dt) {
		m_scene->GetCameraController()->Update(dt);

		//quad_mesh_ptr_2
		m_mesh_collection[1]
			->GetGeometry()
			->ApplyTransform(
				glm::rotate(Matrix4(1.0f), glm::sin((float)dt) * 0.005f, Vector3(0.f, 1.0f, 0.0f))
			);
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
		/*m_scene->Add(quad_mesh_ptr);
		m_scene->Add(quad_mesh_ptr_2);
		m_scene->Add(quad_mesh_ptr_3);*/

		m_scene->Add(m_mesh_collection);
		m_scene->Render();
	}

}