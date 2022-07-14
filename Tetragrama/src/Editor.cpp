#include <pch.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Editor.h>
#include <Layers/RenderingLayer.h>
#include <Layers/UserInterfaceLayer.h>

using namespace ZEngine::Components::UI::Event;

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

        m_engine = std::make_unique<ZEngine::Engine>(engine_configuration);
        m_ui_layer.reset(new Layers::UserInterfaceLayer(shared_from_this()));
        m_rendering_layer.reset(new Layers::RenderingLayer(shared_from_this()));

        m_engine->GetWindow()->PushLayer(m_rendering_layer);
        m_engine->GetWindow()->PushOverlayLayer(m_ui_layer);
    }

    void Editor::Run() {
        m_engine->Initialize();
        m_engine->Start();
    }

    void Editor::OnUIComponentRaised(UIComponentEvent& e) {
        const auto rendering_layer_ptr      = reinterpret_cast<Layers::RenderingLayer*>(m_rendering_layer.get());
        const auto user_interface_layer_ptr = reinterpret_cast<Layers::UserInterfaceLayer*>(m_ui_layer.get());

        ZEngine::Event::EventDispatcher event_dispatcher(e);

        event_dispatcher.ForwardTo<Components::Event::SceneViewportResizedEvent>(
            std::bind(&Layers::RenderingLayer::OnUIComponentRaised, rendering_layer_ptr, std::placeholders::_1));
        event_dispatcher.ForwardTo<Components::Event::SceneViewportFocusedEvent>(
            std::bind(&Layers::RenderingLayer::OnUIComponentRaised, rendering_layer_ptr, std::placeholders::_1));
        event_dispatcher.ForwardTo<Components::Event::SceneViewportUnfocusedEvent>(
            std::bind(&Layers::RenderingLayer::OnUIComponentRaised, rendering_layer_ptr, std::placeholders::_1));

        event_dispatcher.ForwardTo<Components::Event::SceneTextureAvailableEvent>(
            std::bind(&Layers::UserInterfaceLayer::OnUIComponentRaised, user_interface_layer_ptr, std::placeholders::_1));
    }
} // namespace Tetragrama
