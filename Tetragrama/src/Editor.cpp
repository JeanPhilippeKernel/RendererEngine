#include <pch.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Editor.h>
#include <Layers/RenderingLayer.h>
#include <Layers/UserInterfaceLayer.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

using namespace ZEngine::Components::UI::Event;
using namespace Tetragrama::Messengers;

namespace Tetragrama {

    Editor::Editor() {}

    Editor::~Editor() {}

    void Editor::Initialize() {
        ZEngine::EngineConfiguration engine_configuration;
        engine_configuration.LoggerConfiguration = {};

        engine_configuration.WindowConfiguration             = {};
        engine_configuration.WindowConfiguration.EnableVsync = true;
        engine_configuration.WindowConfiguration.Title       = "Tetragramma editor";
        engine_configuration.WindowConfiguration.Width       = 1500;
        engine_configuration.WindowConfiguration.Height      = 800;

        m_engine          = std::make_unique<ZEngine::Engine>(engine_configuration);
        m_ui_layer        = std::make_shared<Layers::UserInterfaceLayer>();
        m_rendering_layer = std::make_shared<Layers::RenderingLayer>();

        m_engine->GetWindow()->PushLayer(m_rendering_layer);
        m_engine->GetWindow()->PushOverlayLayer(m_ui_layer);

        // Register components
        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::pair<float, float>>>(m_rendering_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE,
            std::bind(&Layers::RenderingLayer::SceneRequestResizeMessageHandler, reinterpret_cast<Layers::RenderingLayer*>(m_rendering_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(m_rendering_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS,
            std::bind(&Layers::RenderingLayer::SceneRequestFocusMessageHandler, reinterpret_cast<Layers::RenderingLayer*>(m_rendering_layer.get()), std::placeholders::_1));

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<bool>>(m_rendering_layer, EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS,
            std::bind(&Layers::RenderingLayer::SceneRequestUnfocusMessageHandler, reinterpret_cast<Layers::RenderingLayer*>(m_rendering_layer.get()), std::placeholders::_1));
    }

    void Editor::Run() {
        m_engine->Initialize();
        m_engine->Start();
    }
} // namespace Tetragrama
