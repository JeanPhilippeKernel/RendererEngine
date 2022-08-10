﻿#include <pch.h>
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
#include <ZEngine/Rendering/Components/UUIComponent.h>

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
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(uuids::uuid uuid, std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view uuid_string, std::string_view entity_name) {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid_string);
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
        m_entity_registry->destroy(entity);
    }

    Ref<entt::registry> GraphicScene::GetRegistry() const {
        return m_entity_registry;
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

    void GraphicScene::SetWindowParent(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window) {
        m_parent_window = window;
    }

    void GraphicScene::Update(Core::TimeStep dt) {
        m_entity_registry->view<CameraComponent>().each([&dt, this](CameraComponent& component) {
            if (component.IsPrimaryCamera) {
                m_scene_camera = component.GetCamera();
                component.GetCameraController()->Update(dt);
            }
        });

        if ((m_scene_requested_size.first > 0.0f) && (m_scene_requested_size.second > 0.0f)) {
            m_entity_registry->view<CameraComponent>().each([&, this](CameraComponent& component) {
                if (component.IsPrimaryCamera) {
                    auto controller = component.GetCameraController();
                    controller->SetAspectRatio(m_scene_requested_size.first / m_scene_requested_size.second);
                    m_renderer->GetFrameBuffer()->Resize(m_scene_requested_size.first, m_scene_requested_size.second);
                }
            });
            m_last_scene_requested_size = m_scene_requested_size;
            m_scene_requested_size      = {0.0f, 0.0f};
        }

        // Todo : Should be refactored
        //
        // Update Light property for material that don't have it
        auto light_entity_ptr = m_entity_registry->view<LightComponent>().front();
        auto view_material    = m_entity_registry->view<MaterialComponent>();
        for (auto entity : view_material) {
            auto& material_component = view_material.get<MaterialComponent>(entity);
            auto  material           = material_component.GetMaterial();

            if (light_entity_ptr != entt::null) {
                auto light = m_entity_registry->get<LightComponent>(light_entity_ptr).GetLight();
                if (material->GetShaderBuiltInType() == Rendering::Shaders::ShaderBuiltInType::STANDARD) {
                    auto material_ptr = reinterpret_cast<Rendering::Materials::StandardMaterial*>(material.get());
                    if (!material_ptr->HasLight()) {
                        material_ptr->SetLight(light);
                    }
                }
            }
        }

        m_entity_registry->view<LightComponent, TransformComponent>().each([](entt::entity handle, LightComponent& light_component, TransformComponent& transform_component) {
            // We update light position with is geometry position, so we have the correct light's source position
            light_component.GetLight()->SetPosition(transform_component.GetPosition());
        });

        m_entity_registry->view<TransformComponent, GeometryComponent, MaterialComponent>().each(
            [this](entt::entity handle, TransformComponent& transform_component, GeometryComponent& geometry_component, MaterialComponent& material_component) {
                auto geometry = geometry_component.GetGeometry();
                geometry->SetPosition(transform_component.GetPosition());
                geometry->SetRotationAxis(transform_component.GetRotationAxis());
                geometry->SetRotationAngle(transform_component.GetRotationAngle());
                geometry->SetScaleSize(transform_component.GetScaleSize());

                auto mesh = CreateRef<Meshes::Mesh>(std::move(geometry), material_component.GetMaterial());
                m_mesh_list.push_back(std::move(mesh));
            });
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
