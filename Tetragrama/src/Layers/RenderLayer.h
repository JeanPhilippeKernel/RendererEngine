#pragma once
#include <vector>
#include <queue>
#include <mutex>
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Messengers/Message.h>
#include <EditorCameraController.h>

namespace Tetragrama::Layers
{
    class RenderLayer : public ZEngine::Layers::Layer
    {
    public:
        RenderLayer(std::string_view name = "Rendering layer");

        virtual ~RenderLayer();

        virtual void Initialize() override;
        virtual void Deinitialize() override;
        virtual void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

        virtual bool OnEvent(ZEngine::Event::CoreEvent& e) override;

    public:
        std::future<void> SceneRequestResizeMessageHandler(Messengers::GenericMessage<std::pair<float, float>>&);
        std::future<void> SceneRequestFocusMessageHandler(Messengers::GenericMessage<bool>&);
        std::future<void> SceneRequestUnfocusMessageHandler(Messengers::GenericMessage<bool>&);
        std::future<void> SceneRequestSerializationMessageHandler(Messengers::GenericMessage<std::string>&);
        std::future<void> SceneRequestDeserializationMessageHandler(Messengers::GenericMessage<std::string>&);

        std::future<void> SceneRequestNewSceneMessageHandler(Messengers::EmptyMessage&);
        std::future<void> SceneRequestOpenSceneMessageHandler(Messengers::GenericMessage<std::string>&);
        std::future<void> SceneRequestImportAssetModelAsync(Messengers::GenericMessage<std::string>&);

        std::future<void> SceneRequestSelectEntityFromPixelMessageHandler(Messengers::GenericMessage<std::pair<int, int>>&);

    private:
        ZEngine::Ref<ZEngine::Rendering::Renderers::SceneRenderer> m_scene_renderer;
        ZEngine::Ref<ZEngine::Serializers::GraphicSceneSerializer> m_scene_serializer;
        ZEngine::Ref<EditorCameraController>                       m_editor_camera_controller;
        std::queue<std::function<void(void)>>                      m_deferral_operation;
        std::mutex                                                 m_message_handler_mutex;
        std::mutex                                                 m_mutex;
        std::queue<std::pair<float, float>>                        m_viewport_requested_size_collection;

    private:
        void HandleNewSceneMessage(const Messengers::EmptyMessage&);
        void HandleOpenSceneMessage(const Messengers::GenericMessage<std::string>&);
    };

} // namespace Tetragrama::Layers
