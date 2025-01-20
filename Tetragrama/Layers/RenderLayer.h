#pragma once
#include <Components/Events/SceneTextureAvailableEvent.h>
#include <Components/Events/SceneViewportFocusedEvent.h>
#include <Components/Events/SceneViewportResizedEvent.h>
#include <Components/Events/SceneViewportUnfocusedEvent.h>
#include <EditorCameraController.h>
#include <Messengers/Message.h>
#include <ZEngine/Serializers/GraphicSceneSerializer.h>
#include <mutex>
#include <queue>
#include <vector>

namespace Tetragrama::Layers
{
    class RenderLayer : public ZEngine::Windows::Layers::Layer
    {
    public:
        RenderLayer(std::string_view name = "Rendering layer");

        virtual ~RenderLayer();

        virtual void Initialize() override;
        virtual void Deinitialize() override;
        virtual void Update(ZEngine::Core::TimeStep dt) override;

        virtual void Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;

        virtual bool OnEvent(ZEngine::Core::CoreEvent& e) override;

    private:
        ZEngine::Helpers::Ref<ZEngine::Serializers::GraphicSceneSerializer> m_scene_serializer;
    };

} // namespace Tetragrama::Layers
