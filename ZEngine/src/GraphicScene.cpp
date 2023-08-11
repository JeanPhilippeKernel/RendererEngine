#include <pch.h>
#include <Rendering/Scenes/GraphicScene.h>
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
#include <Core/Coroutine.h>

using namespace ZEngine::Controllers;
using namespace ZEngine::Rendering::Components;
using namespace ZEngine::Rendering::Entities;

namespace ZEngine::Rendering::Scenes
{
    GraphicScene::~GraphicScene()
    {
    }

    void GraphicScene::Initialize()
    {
        Renderers::GraphicRenderer::Initialize();
    }

    void GraphicScene::Deinitialize()
    {
        Renderers::GraphicRenderer::Deinitialize();
        for (auto& mesh : m_mesh_vnext_list)
        {
            mesh.Flush();
        }
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view entity_name)
    {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>();
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(uuids::uuid uuid, std::string_view entity_name)
    {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::CreateEntity(std::string_view uuid_string, std::string_view entity_name)
    {
        GraphicSceneEntity graphic_entity(m_entity_registry->create(), m_entity_registry);
        graphic_entity.AddComponent<UUIComponent>(uuid_string);
        graphic_entity.AddComponent<ValidComponent>(true);
        graphic_entity.AddComponent<NameComponent>(entity_name);
        graphic_entity.AddComponent<TransformComponent>();
        return graphic_entity;
    }

    GraphicSceneEntity GraphicScene::GetEntity(std::string_view entity_name)
    {
        entt::entity entity_handle{entt::null};
        auto         views = m_entity_registry->view<NameComponent>();
        for (auto entity : views)
        {
            auto name = views.get<NameComponent>(entity).Name;
            if (name == entity_name)
            {
                entity_handle = entity;
                break;
            }
        }

        if (entity_handle == entt::null)
        {
            ZENGINE_CORE_ERROR("An entity with name {0} deosn't exist", entity_name)
        }

        return GraphicSceneEntity(entity_handle, m_entity_registry);
    }

    Entities::GraphicSceneEntity GraphicScene::GetEntity(int mouse_pos_x, int mouse_pos_y)
    {
        //int entity_id = m_renderer->GetFrameBuffer()->ReadPixelAt(mouse_pos_x, mouse_pos_y, 1);
        //ZENGINE_CORE_WARN("Pixel ID: {}", entity_id)
        return Entities::GraphicSceneEntity();
    }

    void GraphicScene::RemoveEntity(const Entities::GraphicSceneEntity& entity)
    {
        if (!m_entity_registry->valid(entity))
        {
            ZENGINE_CORE_ERROR("This entity is no longer valid")
            return;
        }
        m_entity_registry->destroy(entity);
    }

    void GraphicScene::RemoveAllEntities()
    {
        m_entity_registry->clear();
    }

    void GraphicScene::RemoveInvalidEntities()
    {
        m_entity_registry->view<ValidComponent>().each(
            [&](entt::entity handle, ValidComponent& component)
            {
                if (!component.IsValid)
                {
                    m_entity_registry->destroy(handle);
                }
            });
    }

    void GraphicScene::InvalidateAllEntities()
    {
        m_entity_registry->view<ValidComponent>().each([&](ValidComponent& component) { component.IsValid = false; });
    }

    Ref<entt::registry> GraphicScene::GetRegistry() const
    {
        return m_entity_registry;
    }

    bool GraphicScene::HasEntities() const
    {
        return !m_entity_registry->empty();
    }

    bool GraphicScene::HasInvalidEntities() const
    {
        bool found = false;
        auto view  = m_entity_registry->view<ValidComponent>();
        for (auto entity : view)
        {
            auto& component = view.get<ValidComponent>(entity);
            if (!component.IsValid)
            {
                found = true;
                break;
            }
        }
        return found;
    }

    bool GraphicScene::IsValidEntity(const Entities::GraphicSceneEntity& entity) const
    {
        return m_entity_registry->valid(entity);
    }

    int32_t GraphicScene::AddMesh(Meshes::MeshVNext&& mesh)
    {
        int32_t index{-1};
        {
            std::unique_lock lock(this->m_mutex);
            m_mesh_vnext_list.push_back(std::move(mesh));

            index = (m_mesh_vnext_list.size() - 1);
        }
        return index;
    }

    const Meshes::MeshVNext& GraphicScene::GetMesh(int32_t mesh_id) const
    {
        std::unique_lock lock(this->m_mutex);

        ZENGINE_VALIDATE_ASSERT(mesh_id >= 0 && mesh_id < (m_mesh_vnext_list.size() - 1), "Mesh ID is invalid")
        return m_mesh_vnext_list.at(mesh_id);
    }

    Meshes::MeshVNext& GraphicScene::GetMesh(int32_t mesh_id)
    {
        std::unique_lock lock(this->m_mutex);

        ZENGINE_VALIDATE_ASSERT(mesh_id >= 0 && mesh_id < m_mesh_vnext_list.size(), "Mesh ID is invalid")
        return m_mesh_vnext_list[mesh_id];
    }

    bool GraphicScene::OnEvent(Event::CoreEvent& e)
    {
        m_entity_registry->view<CameraComponent>().each(
            [&](CameraComponent& component)
            {
                if (component.IsPrimaryCamera)
                {
                    component.GetCameraController()->OnEvent(e);
                }
            });
        return false;
    }

    std::future<void> GraphicScene::RequestNewSizeAsync(float width, float height)
    {
        std::unique_lock lock(m_mutex);

        if ((width > 0.0f) && (height > 0.0f))
        {
            m_scene_requested_size = {width, height};
        }
        co_return;
    }

    void GraphicScene::SetShouldReactToEvent(bool value)
    {
        m_should_react_to_event = value;
    }

    bool GraphicScene::ShouldReactToEvent() const
    {
        return m_should_react_to_event;
    }

    void GraphicScene::SetCameraController(const Ref<Controllers::ICameraController>& camera_controller)
    {
        m_camera_controller = camera_controller;
    }

    void GraphicScene::SetWindowParent(const ZEngine::Ref<ZEngine::Window::CoreWindow>& window)
    {
        m_parent_window = window;
    }

    Ref<ZEngine::Window::CoreWindow> GraphicScene::GetWindowParent() const
    {
        return m_parent_window.lock();
    }

    Entities::GraphicSceneEntity GraphicScene::GetPrimariyCameraEntity() const
    {
        Entities::GraphicSceneEntity camera_entity;

        auto view_cameras = m_entity_registry->view<CameraComponent>();
        for (auto entity : view_cameras)
        {
            auto& component = view_cameras.get<CameraComponent>(entity);
            if (component.IsPrimaryCamera)
            {
                camera_entity = {entity, m_entity_registry};
                break;
            }
        }
        return camera_entity;
    }

    void GraphicScene::Update(Core::TimeStep dt)
    {
        //  Todo : Should be refactored
        m_entity_registry->view<TransformComponent, MeshComponent>().each([this](entt::entity handle, TransformComponent& transform_component, MeshComponent& mesh_component) {
            auto& mesh          = m_mesh_vnext_list[mesh_component.GetMeshID()];
            mesh.LocalTransform = transform_component.GetTransform();
        });
    }

    void GraphicScene::Render()
    {
        {
            std::unique_lock lock(m_mutex);
            if (auto controller = m_camera_controller.lock())
            {
                {
                    if ((m_last_scene_requested_size.first != m_scene_requested_size.first) || (m_last_scene_requested_size.second != m_scene_requested_size.second))
                    {
                        controller->SetAspectRatio(m_scene_requested_size.first / m_scene_requested_size.second);
                        m_last_scene_requested_size = m_scene_requested_size;
                    }
                }

                m_scene_camera = controller->GetCamera();
                if (auto camera = m_scene_camera.lock())
                {
                    /*ToDo: you revisit this ID system*/
                    auto scene_information = Renderers::Contracts::GraphicSceneLayout{
                        .ViewportWidth             = (uint32_t)m_scene_requested_size.first,
                        .ViewportHeight            = (uint32_t)m_scene_requested_size.second,
                        .GraphicScenePtr           = this};

                    Renderers::GraphicRenderer::StartScene(camera);
                    for (uint32_t i = 0; i < m_mesh_vnext_list.size(); ++i)
                    {

                        Renderers::GraphicRenderer::Submit(i);
                    }
                    Renderers::GraphicRenderer::EndScene();
                    //m_renderer->EndScene();

                    if (OnSceneRenderCompleted)
                    {
                        // OnSceneRenderCompleted(m_renderer->GetOutputImage(0));
                    }
                }
            }
        }
    }

    void GraphicScene::UploadFrameInfo(uint32_t frame_index, VkQueue& present_queue)
    {

    }

    unsigned int GraphicScene::ToTextureRepresentation() const
    {
        return 0;
        //return m_renderer->GetFrameBuffer()->GetTexture();
    }

} // namespace ZEngine::Rendering::Scenes
