#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct IRenderer
    {
        Hardwares::VulkanDevicePtr Device                                        = nullptr;
        Renderers::RenderGraphPtr  RenderGraph                                   = nullptr;
        Scenes::SceneDataPtr       RenderSceneData                               = nullptr;

        const size_t               DefaultBufferSize                             = ZMega(10);

        virtual void               Initialize(Hardwares::VulkanDevicePtr device) = 0;
        virtual void               Deinitialize()                                = 0;
    };
} // namespace ZEngine::Rendering::Renderers
