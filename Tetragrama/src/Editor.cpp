#include <pch.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Editor.h>
#include <Layers/UILayer.h>
#include <Messengers/Messenger.h>
#include <MessageToken.h>

using namespace std::chrono_literals;
using namespace ZEngine::Components::UI::Event;
using namespace Tetragrama::Messengers;

namespace Tetragrama
{

    Editor::Editor()
    {
        m_ui_layer     = ZEngine::CreateRef<Layers::UILayer>();
        m_render_layer = ZEngine::CreateRef<Layers::RenderLayer>();

        m_engine_configuration                     = {};
        m_engine_configuration.LoggerConfiguration = {.PeriodicInvokeCallbackInterval = 50ms};
        m_engine_configuration.WindowConfiguration = {
            .Width = 1500, .Height = 800, .EnableVsync = true, .Title = "Tetragramma editor", .RenderingLayerCollection = {m_render_layer}, .OverlayLayerCollection = {m_ui_layer}};
    }

    Editor::~Editor()
    {
        m_ui_layer.reset();
        m_render_layer.reset();
        ZEngine::Engine::Dispose();
    }

    void Editor::Initialize()
    {
        ZEngine::Engine::Initialize(m_engine_configuration);

        // Register components
        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::pair<float, float>>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<std::pair<float, float>>*>(message);
                return m_render_layer->SceneRequestResizeMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<bool>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<bool>*>(message);
                return m_render_layer->SceneRequestFocusMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<bool>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<bool>*>(message);
                return m_render_layer->SceneRequestUnfocusMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::string>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<std::string>*>(message);
                return m_render_layer->SceneRequestSerializationMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, Messengers::GenericMessage<std::string>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<std::string>*>(message);
                return m_render_layer->SceneRequestDeserializationMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, EmptyMessage>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<EmptyMessage*>(message);
                return m_render_layer->SceneRequestNewSceneMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::string>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<std::string>*>(message);
                return m_render_layer->SceneRequestOpenSceneMessageHandler(*message_ptr);
            });

        IMessenger::Register<ZEngine::Layers::Layer, GenericMessage<std::pair<int, int>>>(
            m_render_layer.get(), EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL, [this](void* const message) -> std::future<void> {
                auto message_ptr = reinterpret_cast<GenericMessage<std::pair<int, int>>*>(message);
                return m_render_layer->SceneRequestSelectEntityFromPixelMessageHandler(*message_ptr);
            });

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<std::string>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_IMPORTASSETMODEL,
            m_render_layer.get(),
            return m_render_layer->SceneRequestImportAssetModelAsync(*message_ptr))
    }

    void Editor::Run()
    {
        ZEngine::Engine::Start();
    }

    const ZEngine::EngineConfiguration& Editor::GetCurrentEngineConfiguration() const
    {
        return m_engine_configuration;
    }
} // namespace Tetragrama
