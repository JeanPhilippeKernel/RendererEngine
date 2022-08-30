#include <pch.h>
#include <Rendering/Scenes/GraphicScene.h>
#include <Rendering/Renderers/RenderCommand.h>
#include <Rendering/Components/TransformComponent.h>
#include <Rendering/Components/NameComponent.h>
#include <Rendering/Components/MaterialComponent.h>
#include <Rendering/Components/GeometryComponent.h>
#include <Rendering/Components/LightComponent.h>
#include <Rendering/Components/CameraComponent.h>
#include <Rendering/Entities/GraphicSceneEntity.h>
#include <Rendering/Materials/StandardMaterial.h>
#include <Rendering/Components/UUIComponent.h>
#include <Rendering/Components/ValidComponent.h>
#include <Rendering/Lights/DirectionalLight.h>

using namespace ZEngine::Controllers;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace ZEngine::Rendering::Scenes {

    void GraphicScene::Initialize() {
        m_renderer->Initialize();
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>();
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(uuids::uuid uuid, std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view uuid_string, std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid_string);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::GetEntity(std::string_view entity_name) {
        entt::entity entity_handle{entt::null};
        auto         views = m_entity_registry->view<NameComponent>();
        for (auto entity : views) {
            auto name = views.get<NameComponent>(entity).Name;
            if (name == entity_name) {
                entity_handle = entity;
                break;
            }
        }

        if (entity_handle == entt::null) {
            ZENGINE_CORE_ERROR("An entity with name {0} deosn't exist", entity_name)
        }

        return GraphicSceneEntity(entity_handle, m_entity_registry);
    }

    void GraphicScene::RemoveEntity(const Entities::GraphicSceneEntity& entity) {
        if (!m_entity_registry->valid(entity)) {
            ZENGINE_CORE_ERROR("This entity is no longer valid")
            return;
        }
        m_entity_registry->destroy(entity);
    }

    void GraphicScene::RemoveAllEntities() {
        m_entity_registry->clear();
    }

    void GraphicScene::RemoveInvalidEntities() {
        m_entity_registry->view<ValidComponent>().each([&](entt::entity handle, ValidComponent& component) {
            if (!component.IsValid) {
                m_entity_registry->destroy(handle);
            }
        });
    }

    void GraphicScene::InvalidateAllEntities() {
        m_entity_registry->view<ValidComponent>().each([&](ValidComponent& component) { component.IsValid = false; });
    }

    Ref<entt::registry> GraphicScene::GetRegistry() const {
        return m_entity_registry;
    }

    bool GraphicScene::HasEntities() const {
        return !m_entity_registry->empty();
    }

    bool GraphicScene::HasInvalidEntities() const {
        bool found = false;
        auto view  = m_entity_registry->view<ValidComponent>();
        for (auto entity : view) {
            auto& component = view.get<ValidComponent>(entity);
            if (!component.IsValid) {
                found = true;
                break;
            }
        }
        return found;
    }

    bool GraphicScene::IsValidEntity(const Entities::GraphicSceneEntity& entity) const {
        return m_entity_registry->valid(entity);
    }

    bool GraphicScene::OnEvent(Event::CoreEvent& e) {
        m_entity_registry->view<CameraComponent>().each([&](CameraComponent& component) {
            if (component.IsPrimaryCamera) {
                component.GetCameraController()->OnEvent(e);
            }
        });
        return false;
    }

    void GraphicScene::RequestNewSize(float width, float height) {
        if ((width > 0.0f) && (height > 0.0f)) {
            m_scene_requested_size = {width, height};
        }
    }

    void GraphicScene::SetShouldReactToEvent(bool value) {
        m_should_react_to_event = value;
    }

    bool GraphicScene::ShouldReactToEvent() const {
        return m_should_react_to_event;
    }

    void GraphicScene::SetCameraController(const Ref<Controllers::ICameraController>& camera_controller) {
        m_camera_controller = camera_controller;
    }

    void GraphicScene::SetWindowParent(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window) {
        m_parent_window = window;
    }

    Ref<ZEngine::Window::CoreWindow> GraphicScene::GetWindowParent() const {
        return m_parent_window.lock();
    }

    Entities::GraphicSceneEntity GraphicScene::GetPrimariyCameraEntity() const {
        Entities::GraphicSceneEntity camera_entity;

        auto view_cameras = m_entity_registry->view<CameraComponent>();
        for (auto entity : view_cameras) {
            auto& component = view_cameras.get<CameraComponent>(entity);
            if (component.IsPrimaryCamera) {
                camera_entity = {entity, m_entity_registry};
                break;
            }
        }
        return camera_entity;
    }

    void GraphicScene::Update(Core::TimeStep dt) {
        if ((m_scene_requested_size.first > 0.0f) && (m_scene_requested_size.second > 0.0f)) {
            if (auto controller = m_camera_controller.lock()) {
                controller->SetAspectRatio(m_scene_requested_size.first / m_scene_requested_size.second);
            }
            m_renderer->GetFrameBuffer()->Resize(m_scene_requested_size.first, m_scene_requested_size.second);
            m_last_scene_requested_size = m_scene_requested_size;
            m_scene_requested_size      = {0.0f, 0.0f};
        }

        // Todo : Should be refactored
        //
        m_entity_registry->view<TransformComponent, GeometryComponent, MaterialComponent>().each(
            [this](entt::entity handle, TransformComponent& transform_component, GeometryComponent& geometry_component, MaterialComponent& material_component) {
                auto geometry = geometry_component.GetGeometry();
                geometry->SetTransform(transform_component.GetTransform());

                auto mesh = CreateRef<Meshes::Mesh>(std::move(geometry), material_component.GetMaterials());
                m_mesh_list.push_back(std::move(mesh));
            });
    }

    void GraphicScene::Render() {
        if (auto controller = m_camera_controller.lock()) {
            m_scene_camera = controller->GetCamera();
        }

        if (!m_scene_camera.expired()) {
            m_renderer->StartScene(m_scene_camera.lock());
            m_renderer->AddMesh(m_mesh_list);
            m_renderer->EndScene();

            m_mesh_list.clear();
            m_mesh_list.shrink_to_fit();
        }

        if (OnSceneRenderCompleted) {
            OnSceneRenderCompleted(ToTextureRepresentation());
        }
    }

    unsigned int GraphicScene::ToTextureRepresentation() const {
        return m_renderer->GetFrameBuffer()->GetTexture();
    }

} // namespace ZEngine::Rendering::Scenes
