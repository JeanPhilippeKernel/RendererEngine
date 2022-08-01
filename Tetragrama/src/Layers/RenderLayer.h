#pragma once
#include <vector>
#include <ZEngine/ZEngine.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Messengers/Message.h>

namespace Tetragrama::Layers {
    class RenderLayer : public ZEngine::Layers::Layer {
    public:
        RenderLayer(std::string_view name = "Rendering layer");

        virtual ~RenderLayer() = default;

        virtual void Initialize() override;
        virtual void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render() override;

        virtual bool OnEvent(ZEngine::Event::CoreEvent& e) override;

    public:
        void SceneRequestResizeMessageHandler(Messengers::GenericMessage<std::pair<float, float>>&);
        void SceneRequestFocusMessageHandler(Messengers::GenericMessage<bool>&);
        void SceneRequestUnfocusMessageHandler(Messengers::GenericMessage<bool>&);

    protected:
        void OnSceneRenderCompletedCallback(uint32_t);

    private:
        ZEngine::Ref<ZEngine::Rendering::Scenes::GraphicScene> m_scene;
        ZEngine::Ref<ZEngine::Managers::TextureManager>        m_texture_manager;
    };

} // namespace Tetragrama::Layers
