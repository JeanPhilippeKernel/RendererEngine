#pragma once
#include <RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct IRenderer
    {
        Hardwares::VulkanDevicePtr Device                                        = nullptr;
        Renderers::RenderGraphPtr  RenderGraph                                   = nullptr;
        Scenes::SceneDataPtr       RenderSceneData                               = nullptr;

        virtual void               Initialize(Hardwares::VulkanDevicePtr device) = 0;
        virtual void               Deinitialize()                                = 0;
    };
} // namespace ZEngine::Rendering::Renderers