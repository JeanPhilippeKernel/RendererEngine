#pragma once
#include <RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct RendererResourceName
    {
        inline static cstring FrameDepthRenderTargetName = "g_frame_depth_render_target";
        inline static cstring FrameColorRenderTargetName = "g_frame_color_render_target";

        inline static cstring SceneCameraBufferName      = "SceneCamera";
    };

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