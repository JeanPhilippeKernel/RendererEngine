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

namespace Tetragrama::Layers
{

    RenderLayer::RenderLayer(std::string_view name) : Layer(name.data()) {}

    void RenderLayer::Initialize()
    {

        m_editor_camera_controller = CreateRef<EditorCameraController>(GetAttachedWindow(), 300.0f, 0.f, 30.f);

        /*m_scene = CreateRef<GraphicScene3D>();
        m_scene->Initialize();
        m_scene->SetCameraController(m_editor_camera_controller);
        m_scene->SetWindowParent(GetAttachedWindow());
        m_scene->OnSceneRenderCompleted = std::bind(&RenderLayer::OnSceneRenderCompletedCallback, this, std::placeholders::_1);
        m_scene_serializer              = CreateRef<Serializers::GraphicScene3DSerializer>(m_scene);

        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<Ref<GraphicScene>>>(
            EDITOR_RENDER_LAYER_SCENE_AVAILABLE, Messengers::GenericMessage<Ref<GraphicScene>>{m_scene});

        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<Ref<EditorCameraController>>>(
            EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE, Messengers::GenericMessage<Ref<EditorCameraController>>{m_editor_camera_controller});*/
    }

    void RenderLayer::Update(TimeStep dt)
    {
       /* m_editor_camera_controller->Update(dt);
        m_scene->Update(dt);

        if (!m_deferral_operation.empty())
        {
            auto& operation = m_deferral_operation.front();
            operation();
            m_deferral_operation.pop();
        }*/
    }

    bool RenderLayer::OnEvent(CoreEvent& e)
    {
        /*m_editor_camera_controller->OnEvent(e);
        if (m_scene->ShouldReactToEvent())
        {
            m_scene->OnEvent(e);
        }*/
        return false;
    }

    void RenderLayer::Render()
    {
        //m_scene->Render();
    }

    void RenderLayer::SceneRequestResizeMessageHandler(Messengers::GenericMessage<std::pair<float, float>>& message)
    {
        /*const auto& value = message.GetValue();
        m_editor_camera_controller->SetViewportSize(value.first, value.second);
        m_scene->RequestNewSize(value.first, value.second);*/
    }

    void RenderLayer::SceneRequestFocusMessageHandler(Messengers::GenericMessage<bool>& message)
    {
        //m_scene->SetShouldReactToEvent(message.GetValue());
    }

    void RenderLayer::SceneRequestUnfocusMessageHandler(Messengers::GenericMessage<bool>& message)
    {
        //m_scene->SetShouldReactToEvent(message.GetValue());
    }

    void RenderLayer::SceneRequestSerializationMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        // Todo: We need to replace this whole part by using system FileDialog API
        if (!m_scene->HasEntities())
        {
            ZENGINE_EDITOR_WARN("There are no entities in the current scene to serialize")
            return;
        }

        auto scene_filename = message.GetValue();
        if (scene_filename.empty())
        {
            scene_filename = "SampleScene.zengine";
        }

        auto process_info = m_scene_serializer->Serialize(scene_filename);
        if (!process_info.IsSuccess)
        {
            ZENGINE_EDITOR_ERROR("Scene Serialization process failed with following errors : \n {0}", process_info.ErrorMessage)
            return;
        }

        ZENGINE_EDITOR_INFO("Scene Serialization succeeded")
    }

    void RenderLayer::SceneRequestDeserializationMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        {
            std::unique_lock lock(m_mutex);
            // Todo: We need to replace this whole part by using system FileDialog API
            auto scene_filename = message.GetValue();
            if (scene_filename.empty())
            {
                scene_filename = "SampleScene.zengine";
            }

            m_deferral_operation.push([this, scene_filename] {
                auto process_info = m_scene_serializer->Deserialize(scene_filename);
                if (!process_info.IsSuccess)
                {
                    ZENGINE_EDITOR_ERROR("Scene Deserialization process failed with following errors : \n {0}", process_info.ErrorMessage)
                    return;
                }

                ZENGINE_EDITOR_INFO("Scene Deserialization succeeded")

                Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                    EDITOR_COMPONENT_SCENEVIEWPORT_REQUEST_RECOMPUTATION, Messengers::EmptyMessage{});
            });
        }
    }

    void RenderLayer::SceneRequestNewSceneMessageHandler(Messengers::EmptyMessage& message)
    {
        {
            std::unique_lock lock(m_mutex);

            if (m_scene->HasEntities())
            {
                Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                    EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});
            }

            HandleNewSceneMessage(message);
        }
    }

    void RenderLayer::SceneRequestOpenSceneMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        {
            std::unique_lock lock(m_mutex);

            if (m_scene->HasEntities())
            {
                Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
                    EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});
            }

            HandleOpenSceneMessage(message);
        }
    }

    void RenderLayer::SceneRequestSelectEntityFromPixelMessageHandler(Messengers::GenericMessage<std::pair<int, int>>& mouse_position)
    {
        const auto& value  = mouse_position.GetValue();
        auto        entity = m_scene->GetEntity(value.first, value.second);
        ZENGINE_EDITOR_INFO("Mouse Pos: X={} -- Y={}", value.first, value.second)
    }

    void RenderLayer::OnSceneRenderCompletedCallback(uint32_t scene_texture_id)
    {
       /* Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<uint32_t>>(
            EDITOR_COMPONENT_SCENEVIEWPORT_TEXTURE_AVAILABLE, Messengers::GenericMessage<uint32_t>{scene_texture_id});*/
    }

    void RenderLayer::HandleNewSceneMessage(const Messengers::EmptyMessage&)
    {
        /*m_deferral_operation.push([this]() { m_scene->InvalidateAllEntities(); });*/
    }

    void RenderLayer::HandleOpenSceneMessage(const Messengers::GenericMessage<std::string>& message)
    {
       /* m_deferral_operation.push([this, message]() {
            m_scene->InvalidateAllEntities();

            Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
                EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION, Messengers::GenericMessage<std::string>{message});
        });*/
    }
} // namespace Tetragrama::Layers
