#include <pch.h>
#include <RenderLayer.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

using namespace ZEngine;
using namespace ZEngine::Rendering::Scenes;
using namespace ZEngine::Rendering::Renderers;
using namespace ZEngine::Window;
using namespace ZEngine::Core;
using namespace ZEngine::Inputs;
using namespace ZEngine::Event;

namespace Tetragrama::Layers
{

    RenderLayer::RenderLayer(std::string_view name) : Layer(name) {}

    RenderLayer::~RenderLayer() {}

    void RenderLayer::Initialize()
    {
        auto current_window        = GetAttachedWindow();
        m_editor_camera_controller = CreateRef<EditorCameraController>(current_window, 300.0f, 0.f, 30.f);
        m_scene_renderer           = CreateRef<SceneRenderer>(current_window->GetSwapchain());

        m_scene_renderer->Initialize();
        GraphicScene::Initialize();

        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<Ref<EditorCameraController>>>(
            EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE, Messengers::GenericMessage<Ref<EditorCameraController>>{m_editor_camera_controller});
    }

    void RenderLayer::Deinitialize()
    {
        GraphicScene::Deinitialize();
        m_scene_renderer->Deinitialize();
    }

    void RenderLayer::Update(TimeStep dt)
    {
        m_editor_camera_controller->Update(dt);
        GraphicScene::ComputeAllTransforms();
        m_scene_renderer->Tick();
    }

    bool RenderLayer::OnEvent(CoreEvent& e)
    {
        m_editor_camera_controller->OnEvent(e);
        return false;
    }

    void RenderLayer::Render()
    {
        auto camera = m_editor_camera_controller->GetCamera();

        m_scene_renderer->StartScene(camera->GetPosition(), camera->GetViewMatrix(), camera->GetProjectionMatrix());
        m_scene_renderer->RenderScene(GraphicScene::GetRawData());
        m_scene_renderer->EndScene();
    }

    std::future<void> RenderLayer::SceneRequestResizeMessageHandler(Messengers::GenericMessage<std::pair<float, float>>& message)
    {
        std::unique_lock lock(m_message_handler_mutex);

        const auto& value = message.GetValue();
        m_editor_camera_controller->SetViewportSize(value.first, value.second);
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestFocusMessageHandler(Messengers::GenericMessage<bool>& message)
    {
        // std::unique_lock lock(m_message_handler_mutex);
        // GraphicScene::SetShouldReactToEvent(message.GetValue());
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestUnfocusMessageHandler(Messengers::GenericMessage<bool>& message)
    {
        // std::unique_lock lock(m_message_handler_mutex);
        // GraphicScene::SetShouldReactToEvent(message.GetValue());
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestSerializationMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        // std::unique_lock lock(m_message_handler_mutex);
        //// Todo: We need to replace this whole part by using system FileDialog API
        // if (!m_scene->HasEntities())
        //{
        //     ZENGINE_EDITOR_WARN("There are no entities in the current scene to serialize")
        //     return;
        // }

        // auto scene_filename = message.GetValue();
        // if (scene_filename.empty())
        //{
        //     scene_filename = "SampleScene.zengine";
        // }

        // auto process_info = m_scene_serializer->Serialize(scene_filename);
        // if (!process_info.IsSuccess)
        //{
        //     ZENGINE_EDITOR_ERROR("Scene Serialization process failed with following errors : \n {0}", process_info.ErrorMessage)
        //     return;
        // }

        // ZENGINE_EDITOR_INFO("Scene Serialization succeeded")
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestDeserializationMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        //{
        //    std::unique_lock lock(m_message_handler_mutex);
        //    // Todo: We need to replace this whole part by using system FileDialog API
        //    auto scene_filename = message.GetValue();
        //    if (scene_filename.empty())
        //    {
        //        scene_filename = "SampleScene.zengine";
        //    }
        //    auto process_info = m_scene_serializer->Deserialize(scene_filename);
        //    if (!process_info.IsSuccess)
        //    {
        //        ZENGINE_EDITOR_ERROR("Scene Deserialization process failed with following errors : \n {0}", process_info.ErrorMessage)
        //        return;
        //    }

        //    ZENGINE_EDITOR_INFO("Scene Deserialization succeeded")
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestNewSceneMessageHandler(Messengers::EmptyMessage& message)
    {
        //{
        //    std::unique_lock lock(m_message_handler_mutex);

        //    if (m_scene->HasEntities())
        //    {
        //        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
        //            EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});
        //    }

        //    HandleNewSceneMessage(message);
        //}
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestOpenSceneMessageHandler(Messengers::GenericMessage<std::string>& message)
    {
        //{
        //    std::unique_lock lock(m_message_handler_mutex);

        //    if (m_scene->HasEntities())
        //    {
        //        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::EmptyMessage>(
        //            EDITOR_COMPONENT_HIERARCHYVIEW_NODE_DELETED, Messengers::EmptyMessage{});
        //    }

        //    HandleOpenSceneMessage(message);
        //}
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestImportAssetModelAsync(Messengers::GenericMessage<std::string>& message)
    {
        const auto& value = message.GetValue();
        co_return co_await GraphicScene::ImportAssetAsync(value);
    }

    std::future<void> RenderLayer::SceneRequestSelectEntityFromPixelMessageHandler(Messengers::GenericMessage<std::pair<int, int>>& mouse_position)
    {
        // const auto& value  = mouse_position.GetValue();
        // auto        entity = m_scene->GetEntity(value.first, value.second);
        // ZENGINE_EDITOR_INFO("Mouse Pos: X={} -- Y={}", value.first, value.second)
        co_return;
    }

    void RenderLayer::HandleNewSceneMessage(const Messengers::EmptyMessage&)
    {
        //{
        //    std::unique_lock lock(m_message_handler_mutex);
        //    m_scene->InvalidateAllEntities();
        //}
    }

    void RenderLayer::HandleOpenSceneMessage(const Messengers::GenericMessage<std::string>& message)
    {
        //{
        //    std::unique_lock lock(m_message_handler_mutex);
        //    m_scene->InvalidateAllEntities();
        //    Messengers::IMessenger::SendAsync<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
        //        EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION, Messengers::GenericMessage<std::string>{message});
        //}
    }
} // namespace Tetragrama::Layers
