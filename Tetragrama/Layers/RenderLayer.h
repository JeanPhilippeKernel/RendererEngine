#pragma once
#include <EditorCameraController.h>

namespace Tetragrama::Layers
{
    struct RenderLayer : public ZEngine::Windows::Layers::Layer
    {
        RenderLayer(const char* name = "Rendering layer");

        virtual ~RenderLayer();

        ZEngine::Core::Containers::HashMap<uuids::uuid, ZEngine::Core::Containers::Array<EditorSceneNodeHierarchy>> RenderableNodes = {};

        virtual void                                                                                                Initialize(ZEngine::Core::Memory::ArenaAllocator* arena) override;
        virtual void                                                                                                Deinitialize() override;
        virtual void                                                                                                Update(ZEngine::Core::TimeStep dt) override;
        virtual void                                                                                                Render(ZEngine::Rendering::Renderers::GraphicRenderer* const renderer, ZEngine::Hardwares::CommandBuffer* const command_buffer) override;
        virtual bool                                                                                                OnEvent(ZEngine::Core::CoreEvent& e) override;
    };

} // namespace Tetragrama::Layers
