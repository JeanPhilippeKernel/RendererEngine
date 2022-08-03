#include <pch.h>
#include <RenderLayer.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

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
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Geometries;

namespace Tetragrama::Layers {

    RenderLayer::RenderLayer(std::string_view name) : m_texture_manager(new ZEngine::Managers::TextureManager()), Layer(name.data()) {}

    void RenderLayer::Initialize() {

        m_texture_manager->Load("Assets/Images/free_image.png");
        m_texture_manager->Load("Assets/Images/Crate.png");
        m_texture_manager->Load("Assets/Images/Checkerboard_2.png");
        m_texture_manager->Load("Assets/Images/container2_specular.png");
        m_texture_manager->Load("Assets/Images/container2.png");

        m_scene = CreateRef<GraphicScene3D>();
        m_scene->Initialize();
        m_scene->OnSceneRenderCompleted = std::bind(&RenderLayer::OnSceneRenderCompletedCallback, this, std::placeholders::_1);

        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<Ref<GraphicScene>>>(
            EDITOR_RENDER_LAYER_SCENE_AVAILABLE, Messengers::GenericMessage<Ref<GraphicScene>>{m_scene});

        auto camera_entity = m_scene->CreateEntity("Main camera");
        camera_entity.AddComponent<CameraComponent>(CreateRef<OrbitCameraController>(GetAttachedWindow(), Vector3(0.0f, 20.0f, 120.f), 10.0f, -20.0f));

        auto light_entity = m_scene->CreateEntity("light");
        light_entity.AddComponent<GeometryComponent>(CreateRef<CubeGeometry>());
        auto& light_transform = light_entity.GetComponent<TransformComponent>();
        light_transform.SetPosition({-30.0f, 10.f, 0.0f});
        light_transform.SetScaleSize({1.f, 1.0f, 1.f});
        light_transform.SetRotationAngle(0.0f);
        light_transform.SetRotationAxis({0.0f, 1.0f, 0.0f});
        Ref<BasicMaterial> light_material = CreateRef<BasicMaterial>();
        light_material->SetTexture(CreateTexture(1, 1));
        light_entity.AddComponent<LightComponent>(Maths::Vector3{0.2f, 0.2f, 0.2f}, Maths::Vector3{0.5f, 0.5f, 0.5f}, Maths::Vector3{1.0f, 1.0f, 1.0f});
        light_entity.AddComponent<MaterialComponent>(std::move(light_material));

        auto  checkboard_entity    = m_scene->CreateEntity("Checkboard");
        auto& checkboard_transform = checkboard_entity.GetComponent<TransformComponent>();
        checkboard_transform.SetPosition({0.f, -0.5f, 0.0f});
        checkboard_transform.SetScaleSize({1000.f, .0f, 1000.f});
        checkboard_transform.SetRotationAngle(0.0f);
        checkboard_transform.SetRotationAxis({1.f, 0.0f, 0.0f});
        checkboard_entity.AddComponent<GeometryComponent>(CreateRef<CubeGeometry>());
        Ref<StandardMaterial> material = CreateRef<StandardMaterial>();
        material->SetDiffuseMap(m_texture_manager->Obtains("Checkerboard_2"));
        material->SetTileFactor(20.f);
        Ref<Rendering::Textures::Texture> specular_map_2(Rendering::Textures::CreateTexture(1, 1));
        specular_map_2->SetData(60, 60, 60, 100);
        material->SetSpecularMap(specular_map_2);
        material->SetShininess(10.0f);
        material->SetLight(light_entity.GetComponent<LightComponent>().GetLight());
        checkboard_entity.AddComponent<MaterialComponent>(std::move(material));

        auto  cube_box_entity    = m_scene->CreateEntity("box");
        auto& cube_box_transform = cube_box_entity.GetComponent<TransformComponent>();
        cube_box_transform.SetPosition({0.f, 10.f, 0.0f});
        cube_box_transform.SetScaleSize({10.f, 10.0f, 10.f});
        cube_box_transform.SetRotationAngle(0.0f);
        cube_box_transform.SetRotationAxis({0.f, 1.0f, 0.0f});
        cube_box_entity.AddComponent<GeometryComponent>(CreateRef<CubeGeometry>());
        Ref<StandardMaterial> cube_box_material = CreateRef<StandardMaterial>();
        cube_box_material->SetDiffuseMap(m_texture_manager->Obtains("container2"));
        cube_box_material->SetSpecularMap(m_texture_manager->Obtains("container2_specular"));
        cube_box_material->SetShininess(100.0f);
        cube_box_material->SetLight(light_entity.GetComponent<LightComponent>().GetLight());
        cube_box_entity.AddComponent<MaterialComponent>(std::move(cube_box_material));

        auto  cube_box_2_entity    = m_scene->CreateEntity();
        auto& cube_box_2_transform = cube_box_2_entity.GetComponent<TransformComponent>();
        cube_box_2_transform.SetPosition({19.f, 9.f, -80.0f});
        cube_box_2_transform.SetScaleSize({10.f, 10.0f, 10.f});
        cube_box_2_entity.AddComponent<GeometryComponent>(CreateRef<CubeGeometry>());
        Ref<StandardMaterial> cube_box_2_material = CreateRef<StandardMaterial>();
        cube_box_2_material->SetTexture(CreateTexture(1, 1));
        auto& cube_box_2_material_texture = cube_box_2_material->GetTexture();
        cube_box_2_material_texture->SetData(255.0f, 127.5f, 79.05f, 255.f);
        cube_box_2_material->SetShininess(32.0f);
        cube_box_2_material->SetLight(light_entity.GetComponent<LightComponent>().GetLight());
        cube_box_2_entity.AddComponent<MaterialComponent>(std::move(cube_box_2_material));
    }

    void RenderLayer::Update(TimeStep dt) {
        m_scene->Update(dt);

        auto box_entity   = m_scene->GetEntity("box");
        auto light_entity = m_scene->GetEntity("light");

        auto& box_component = box_entity.GetComponent<TransformComponent>();
        auto  angle         = box_component.GetRotationAngle();
        box_component.SetRotationAngle(angle + std::sin(dt * 0.05f));

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_UP, GetAttachedWindow())) {
            auto& component = light_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.y += .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_DOWN, GetAttachedWindow())) {
            auto& component = light_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.y -= .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_LEFT, GetAttachedWindow())) {
            auto& component = light_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.x -= .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_RIGHT, GetAttachedWindow())) {
            auto& component = light_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.x += .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_W, GetAttachedWindow())) {
            auto& component = box_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.y += .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_S, GetAttachedWindow())) {
            auto& component = box_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.y -= .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_A, GetAttachedWindow())) {
            auto& component = box_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.x -= .5f;
            component.SetPosition(position);
        }

        if (IDevice::As<Inputs::Keyboard>()->IsKeyPressed(ZENGINE_KEY_D, GetAttachedWindow())) {
            auto& component = box_entity.GetComponent<TransformComponent>();
            auto  position  = component.GetPosition();
            position.x += .5f;
            component.SetPosition(position);
        }
    }

    bool RenderLayer::OnEvent(CoreEvent& e) {
        if (m_scene->ShouldReactToEvent()) {
            m_scene->OnEvent(e);
        }
        return false;
    }

    void RenderLayer::Render() {
        m_scene->Render();
    }

    void RenderLayer::SceneRequestResizeMessageHandler(Messengers::GenericMessage<std::pair<float, float>>& message) {
        const auto& value = message.GetValue();
        m_scene->RequestNewSize(value.first, value.second);
    }

    void RenderLayer::SceneRequestFocusMessageHandler(Messengers::GenericMessage<bool>& message) {
        m_scene->SetShouldReactToEvent(message.GetValue());
    }

    void RenderLayer::SceneRequestUnfocusMessageHandler(Messengers::GenericMessage<bool>& message) {
        m_scene->SetShouldReactToEvent(message.GetValue());
    }

    void RenderLayer::OnSceneRenderCompletedCallback(uint32_t scene_texture_id) {
        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<uint32_t>>(
            EDITOR_COMPONENT_SCENEVIEWPORT_TEXTURE_AVAILABLE, Messengers::GenericMessage<uint32_t>{scene_texture_id});
    }
} // namespace Tetragrama::Layers
