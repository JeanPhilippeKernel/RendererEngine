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
        GraphicScene::Initialize();

        m_editor_camera_controller = CreateRef<EditorCameraController>(50.0, 0.f, 45.f);
        Messengers::IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, Messengers::GenericMessage<Ref<EditorCameraController>>>(
            EDITOR_RENDER_LAYER_CAMERA_CONTROLLER_AVAILABLE, Messengers::GenericMessage<Ref<EditorCameraController>>{m_editor_camera_controller});
    }

    void RenderLayer::Deinitialize()
    {
        GraphicScene::Deinitialize();
    }

    void RenderLayer::Update(TimeStep dt)
    {
        m_editor_camera_controller->Update(dt);
        GraphicScene::ComputeAllTransforms();
    }

    bool RenderLayer::OnEvent(CoreEvent& e)
    {
        m_editor_camera_controller->OnEvent(e);
        return false;
    }

    void RenderLayer::Render()
    {
        auto camera = m_editor_camera_controller->GetCamera();
        GraphicRenderer::DrawScene(camera, GraphicScene::GetRawData());
    }

    std::future<void> RenderLayer::SceneRequestResizeMessageHandlerAsync(Messengers::GenericMessage<std::pair<float, float>>& message)
    {
        std::unique_lock lock(m_message_handler_mutex);

        const auto& value = message.GetValue();
        m_editor_camera_controller->SetViewport(value.first, value.second);
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestFocusMessageHandlerAsync(Messengers::GenericMessage<bool>& message)
    {
        m_editor_camera_controller->ResumeEventProcessing();
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestUnfocusMessageHandlerAsync(Messengers::GenericMessage<bool>& message)
    {
        m_editor_camera_controller->PauseEventProcessing();
        co_return;
    }

    std::future<void> RenderLayer::SceneRequestSelectEntityFromPixelMessageHandlerAsync(Messengers::GenericMessage<std::pair<int, int>>& mouse_position)
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
