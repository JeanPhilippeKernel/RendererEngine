#include <pch.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Editor.h>
#include <Layers/RenderLayer.h>
#include <Layers/UILayer.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

using namespace std::chrono_literals;
using namespace ZEngine::Components::UI::Event;
using namespace Tetragrama::Messengers;

namespace Tetragrama
{

    Editor::Editor() {}

    Editor::~Editor() {}

    void Editor::Initialize()
    {
        m_engine_configuration                                      = std::make_shared<ZEngine::EngineConfiguration>();
        m_engine_configuration->LoggerConfiguration                 = {.PeriodicInvokeCallbackInterval = 50ms};
        m_engine_configuration->LoggerConfiguration.MessageCallback = std::bind(&Editor::LoggerMessageCallback, this, std::placeholders::_1);

        m_engine_configuration->WindowConfiguration             = {};
        m_engine_configuration->WindowConfiguration.EnableVsync = true;
        m_engine_configuration->WindowConfiguration.Title       = "Tetragramma editor";
        m_engine_configuration->WindowConfiguration.Width       = 1500;
        m_engine_configuration->WindowConfiguration.Height      = 800;

        m_ui_layer     = ZEngine::CreateRef<Layers::UILayer>();
        m_render_layer = ZEngine::CreateRef<Layers::RenderLayer>();

        m_engine = ZEngine::CreateScope<ZEngine::Engine>(*m_engine_configuration);
        m_engine->GetWindow()->PushLayer(m_render_layer);
        m_engine->GetWindow()->PushOverlayLayer(m_ui_layer);

        // Register components
        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::pair<float, float>>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE,
            std::bind(&Layers::RenderLayer::SceneRequestResizeMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS,
            std::bind(&Layers::RenderLayer::SceneRequestFocusMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS,
            std::bind(&Layers::RenderLayer::SceneRequestUnfocusMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION,
            std::bind(&Layers::RenderLayer::SceneRequestSerializationMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION,
            std::bind(&Layers::RenderLayer::SceneRequestDeserializationMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::EmptyMessage>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE,
            std::bind(&Layers::RenderLayer::SceneRequestNewSceneMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE,
            std::bind(&Layers::RenderLayer::SceneRequestOpenSceneMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<std::pair<int, int>>>(m_render_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL,
            std::bind(&Layers::RenderLayer::SceneRequestSelectEntityFromPixelMessageHandler, reinterpret_cast<Layers::RenderLayer*>(m_render_layer.get()), std::placeholders::_1));
    }

    void Editor::Run()
    {
        m_engine->Initialize();
        m_engine->Start();
    }

    ZEngine::Ref<ZEngine::EngineConfiguration> Editor::GetCurrentEngineConfiguration() const
    {
        return m_engine_configuration;
    }

    void Editor::LoggerMessageCallback(std::vector<std::string> message) const
    {
        IMessenger::SendAsync<ZEngine::Components::UI::UIComponent, GenericMessage<std::vector<std::string>>>(
            EDITOR_COMPONENT_LOG_RECEIVE_LOG_MESSAGE, GenericMessage<std::vector<std::string>>{std::move(message)});
    }
} // namespace Tetragrama
