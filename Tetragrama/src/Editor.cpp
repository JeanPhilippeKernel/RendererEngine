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

        using PairFloat = std::pair<float, float>;
        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<PairFloat>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_RESIZE,
            m_render_layer.get(),
            return m_render_layer->SceneRequestResizeMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<bool>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_FOCUS,
            m_render_layer.get(),
            return m_render_layer->SceneRequestFocusMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<bool>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_UNFOCUS,
            m_render_layer.get(),
            return m_render_layer->SceneRequestUnfocusMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<std::string>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_SERIALIZATION,
            m_render_layer.get(),
            return m_render_layer->SceneRequestSerializationMessageHandlerAsync(*message_ptr))
     
        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<std::string>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_DESERIALIZATION,
            m_render_layer.get(),
            return m_render_layer->SceneRequestDeserializationMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            EmptyMessage,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_NEWSCENE,
            m_render_layer.get(),
            return m_render_layer->SceneRequestNewSceneMessageHandlerAsync(*message_ptr))

        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<std::string>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_OPENSCENE,
            m_render_layer.get(),
            return m_render_layer->SceneRequestOpenSceneMessageHandlerAsync(*message_ptr))
        
        using PairInt = std::pair<int, int>;
        MESSENGER_REGISTER(
            ZEngine::Layers::Layer,
            GenericMessage<PairInt>,
            EDITOR_RENDER_LAYER_SCENE_REQUEST_SELECT_ENTITY_FROM_PIXEL,
            m_render_layer.get(),
            return m_render_layer->SceneRequestSelectEntityFromPixelMessageHandlerAsync(*message_ptr))
        
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
