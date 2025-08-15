#pragma once
#include <RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct IRenderer
    {
        Hardwares::VulkanDevicePtr Device                                        = nullptr;
        Renderers::RenderGraph*    RenderGraph                                   = nullptr;

        virtual void               Initialize(Hardwares::VulkanDevicePtr device) = 0;
        virtual void               Deinitialize()                                = 0;
    };
} // namespace ZEngine::Rendering::Renderers