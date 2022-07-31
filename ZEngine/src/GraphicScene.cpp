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

using namespace ZEngine::Controllers;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace ZEngine::Rendering::Scenes {

    void GraphicScene::Initialize() {
        m_renderer->Initialize();
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
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

    bool GraphicScene::OnEvent(Event::CoreEvent& e) {
        auto views = m_entity_registry->view<CameraComponent>();
        for (auto entity : views) {
            auto& component = views.get<CameraComponent>(entity);
            if (component.IsPrimaryCamera) {
                component.GetCameraController()->OnEvent(e);
            }
        }
        return false;
    }

    void GraphicScene::RequestNewSize(uint32_t width, uint32_t height) {
        if ((width > 0) && (height > 0)) {
            m_pending_operation.emplace([w = width, h = height, this]() {
                auto views = m_entity_registry->view<CameraComponent>();
                for (auto entity : views) {
                    auto& component  = views.get<CameraComponent>(entity);
                    auto  controller = component.GetCameraController();
                    controller->SetAspectRatio(((float) w) / ((float) h));
                    controller->UpdateProjectionMatrix();

                    if (component.IsPrimaryCamera) {
                        m_renderer->GetFrameBuffer()->Resize(w, h);
                    }
                }
            });
        }
    }

    void GraphicScene::SetShouldReactToEvent(bool value) {
        m_should_react_to_event = value;
    }

    bool GraphicScene::ShouldReactToEvent() const {
        return m_should_react_to_event;
    }

    void GraphicScene::Update(Core::TimeStep dt) {
        {
            auto views = m_entity_registry->view<CameraComponent>();
            for (auto entity : views) {
                auto& component = views.get<CameraComponent>(entity);
                if (component.IsPrimaryCamera) {
                    m_scene_camera = component.GetCamera();
                    component.GetCameraController()->Update(dt);
                }
            }
        }

        while (!m_pending_operation.empty()) {
            auto& callback_func = m_pending_operation.front();
            callback_func();
            m_pending_operation.pop();
        }

        // Todo : Should be refactored
        auto view_light_component = m_entity_registry->view<LightComponent, TransformComponent, GeometryComponent, MaterialComponent>();
        for (auto entity : view_light_component) {
            auto [light_component, transform_component, geometry_component, material_component] =
                view_light_component.get<LightComponent, TransformComponent, GeometryComponent, MaterialComponent>(entity);

            auto geometry = geometry_component.GetGeometry();
            geometry->SetPosition(transform_component.GetPosition());
            geometry->SetRotationAxis(transform_component.GetRotationAxis());
            geometry->SetRotationAngle(transform_component.GetRotationAngle());
            geometry->SetScaleSize(transform_component.GetScaleSize());

            // We update light position with is geometry position, so we have the correct light's source position
            light_component.GetLight()->SetPosition(geometry->GetPosition());
            auto mesh = CreateRef<Meshes::Mesh>(std::move(geometry), material_component.GetMaterial());
            m_mesh_list.push_back(std::move(mesh));
        }

        auto views = m_entity_registry->view<TransformComponent, GeometryComponent, MaterialComponent>();
        for (auto entity : views) {
            auto [transform_component, geometry_component, material_component] = views.get<TransformComponent, GeometryComponent, MaterialComponent>(entity);

            auto geometry = geometry_component.GetGeometry();
            geometry->SetPosition(transform_component.GetPosition());
            geometry->SetRotationAxis(transform_component.GetRotationAxis());
            geometry->SetRotationAngle(transform_component.GetRotationAngle());
            geometry->SetScaleSize(transform_component.GetScaleSize());

            auto mesh = CreateRef<Meshes::Mesh>(std::move(geometry), material_component.GetMaterial());
            m_mesh_list.push_back(std::move(mesh));
        }
    }

    void GraphicScene::Render() {

        if (m_scene_camera) {
            m_renderer->StartScene(m_scene_camera);
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
