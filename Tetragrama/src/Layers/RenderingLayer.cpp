#include <pch.h>
#include <RenderingLayer.h>

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

namespace Tetragrama::Layers {

    RenderingLayer::RenderingLayer(ZEngine::Ref<Editor>&& editor, const char* name)
        : m_editor(std::move(editor)), m_texture_manager(new ZEngine::Managers::TextureManager()), Layer(name) {}

    void RenderingLayer::Initialize() {

        m_texture_manager->Load("Assets/Images/free_image.png");
        m_texture_manager->Load("Assets/Images/Crate.png");
        m_texture_manager->Load("Assets/Images/Checkerboard_2.png");
        m_texture_manager->Load("Assets/Images/container2_specular.png");
        m_texture_manager->Load("Assets/Images/container2.png");

        m_scene.reset(new GraphicScene3D(new OrbitCameraController(GetAttachedWindow(), Vector3(0.0f, 20.0f, 50.f), 10.0f, -20.0f)));
        m_scene->Initialize();

        Ref<ZEngine::Rendering::Meshes::Mesh> checkboard_mesh;
        Ref<ZEngine::Rendering::Meshes::Mesh> multi_textured_cube_mesh;
        Ref<ZEngine::Rendering::Meshes::Mesh> mesh_three;

        Ref<ZEngine::Rendering::Lights::Light> light_source_mesh;
        light_source_mesh.reset(MeshBuilder::CreateLight({-30.0f, 10.f, 0.0f}, {1.f, 1.0f, 1.f}, 0.0f, {0.0f, 1.0f, 0.0f}));
        light_source_mesh->SetAmbientColor({0.2f, 0.2f, 0.2f});
        light_source_mesh->SetDiffuseColor({0.5f, 0.5f, 0.5f});
        light_source_mesh->SetSpecularColor({1.0f, 1.0f, 1.0f});

        checkboard_mesh.reset(MeshBuilder::CreateCube({0.f, -0.5f, 0.0f}, {1000.f, .0f, 1000.f}, 0.0f, Vector3(1.f, 0.0f, 0.0f), m_texture_manager->Obtains("Checkerboard_2")));
        StandardMaterial* checkboard_material = reinterpret_cast<StandardMaterial*>(checkboard_mesh->GetMaterial().get());
        checkboard_material->SetTileFactor(20.f);
        Ref<Rendering::Textures::Texture> specular_map(Rendering::Textures::CreateTexture(1, 1));
        specular_map->SetData(60, 60, 60, 100);
        checkboard_material->SetSpecularMap(specular_map);
        checkboard_material->SetShininess(10.0f);
        checkboard_material->SetLight(light_source_mesh);

        multi_textured_cube_mesh.reset(MeshBuilder::CreateCube({0.f, 10.f, 0.0f}, {10.f, 10.0f, 10.f}, 0.0f, Vector3(0.f, 1.0f, 0.0f), m_texture_manager->Obtains("container2")));
        StandardMaterial* multi_textured_cube_material = reinterpret_cast<StandardMaterial*>(multi_textured_cube_mesh->GetMaterial().get());
        multi_textured_cube_material->SetSpecularMap(m_texture_manager->Obtains("container2_specular"));
        multi_textured_cube_material->SetShininess(100.0f);
        multi_textured_cube_material->SetLight(light_source_mesh);

        mesh_three.reset(MeshBuilder::CreateCube({-20.f, 10.f, 0.0f}, {5.f, 5.0f, 5.f}, {255.0f, 127.5f, 30.0f}, 0.0f, Vector3(0.f, 1.0f, 0.0f)));
        StandardMaterial* mesh_three_material         = reinterpret_cast<StandardMaterial*>(mesh_three->GetMaterial().get());
        auto&             mesh_three_material_texture = mesh_three_material->GetTexture();
        mesh_three_material_texture->SetData(255.0f, 127.5f, 79.05f, 255.f);
        mesh_three_material->SetShininess(32.0f);

        mesh_three_material->SetLight(light_source_mesh);

        m_mesh_collection.push_back(std::move(checkboard_mesh));
        m_mesh_collection.push_back(std::move(multi_textured_cube_mesh));
        // m_mesh_collection.push_back(std::move(mesh_three));
        m_mesh_collection.push_back(std::move(light_source_mesh));
    }

    void RenderingLayer::Update(TimeStep dt) {
        m_scene->GetCameraController()->Update(dt);

        // quad_mesh_ptr_2
        /* auto rotation_angle = m_mesh_collection[0]->GetGeometry()->GetRotationAngle() + (dt * 0.05f);
         m_mesh_collection[0]->GetGeometry()->SetRotationAngle(rotation_angle);*/

        StandardMaterial* multi_textured_cube_material = reinterpret_cast<StandardMaterial*>(m_mesh_collection[1]->GetMaterial().get());
        multi_textured_cube_material->SetViewPosition(m_scene->GetCameraController()->GetPosition());

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_UP, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.y += .5f;
            geometry->SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_DOWN, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.y -= .5f;
            geometry->SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.x -= .5f;
            geometry->SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_RIGHT, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.x += .5f;
            geometry->SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_B, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.z -= .5f;
            geometry->SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_V, GetAttachedWindow())) {
            auto& geometry = m_mesh_collection[2]->GetGeometry();
            auto  position = geometry->GetPosition();
            position.z += .5f;
            geometry->SetPosition(position);
        }

        // quad_mesh_ptr_1
        // auto& mat     = m_mesh_collection[0]->GetMaterial();
        // auto& texture = mat->GetTexture();
        // texture->SetData(150, 50, 25, 255.f);
    }

    bool RenderingLayer::OnEvent(CoreEvent& e) {
        if (m_scene->ShouldReactToEvent()) {
            m_scene->GetCameraController()->OnEvent(e);
        }
        return false;
    }

    void RenderingLayer::OnUIComponentRaised(ZEngine::Components::UI::Event::UIComponentEvent& e) {
        ZEngine::Event::EventDispatcher event_dispatcher(e);
        event_dispatcher.Dispatch<Components::Event::SceneViewportResizedEvent>(std::bind(&RenderingLayer::OnSceneViewportResized, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Components::Event::SceneTextureAvailableEvent>(std::bind(&RenderingLayer::OnSceneTextureAvailable, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Components::Event::SceneViewportFocusedEvent>(std::bind(&RenderingLayer::OnSceneViewportFocused, this, std::placeholders::_1));
        event_dispatcher.Dispatch<Components::Event::SceneViewportUnfocusedEvent>(std::bind(&RenderingLayer::OnSceneViewportUnfocused, this, std::placeholders::_1));
    }

    void RenderingLayer::Render() {
        m_scene->Add(m_mesh_collection);
        m_scene->Render();
    }

    bool RenderingLayer::OnSceneViewportResized(Components::Event::SceneViewportResizedEvent& e) {
        m_scene->UpdateSize(e.GetWidth(), e.GetHeight());

        Components::Event::SceneTextureAvailableEvent scene_texture_event{m_scene->ToTextureRepresentation()};
        OnSceneTextureAvailable(scene_texture_event);
        return true;
    }

    bool RenderingLayer::OnSceneViewportFocused(Components::Event::SceneViewportFocusedEvent& e) {
        m_scene->SetShouldReactToEvent(true);
        return true;
    }

    bool RenderingLayer::OnSceneViewportUnfocused(Components::Event::SceneViewportUnfocusedEvent& e) {
        m_scene->SetShouldReactToEvent(false);
        return true;
    }

    bool RenderingLayer::OnSceneTextureAvailable(Components::Event::SceneTextureAvailableEvent& e) {
        ZEngine::Event::EventDispatcher event_dispatcher(e);
        if (!m_editor.expired()) {
            const auto editor_ptr = m_editor.lock();
            event_dispatcher.ForwardTo<Components::Event::SceneTextureAvailableEvent>(std::bind(&Editor::OnUIComponentRaised, editor_ptr.get(), std::placeholders::_1));
        }
        return false;
    }
} // namespace Tetragrama::Layers
