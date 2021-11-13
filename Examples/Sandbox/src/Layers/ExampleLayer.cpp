#include <ExampleLayer.h>

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

namespace Sandbox::Layers {
	
	void ExampleLayer::Initialize() {

		m_texture_manager.reset(new ZEngine::Managers::TextureManager());
		
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

		if(IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_J, GetAttachedWindow())) {
		
		}

		if (IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_F, GetAttachedWindow())) {
			
		}

		if (IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_B, GetAttachedWindow())) {
			
		}

		if (IDevice::As<ZEngine::Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_Y, GetAttachedWindow())) {
			
		}
	}

	bool ExampleLayer::OnEvent(CoreEvent& e) {
		m_scene->GetCameraController()->OnEvent(e);
		return false;
	}

	void ExampleLayer::Render() {

		std::vector<Ref<Mesh>> meshes{ quad_mesh_ptr_2, quad_mesh_ptr_1, quad_mesh_ptr };
		m_scene->Add(meshes);
		m_scene->Render();
	}

}