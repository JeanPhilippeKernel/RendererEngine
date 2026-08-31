#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    // Vertex layout shared by ZUIRenderer and zui_draw.vert / zui_draw.frag.
    struct UIDrawVert
    {
        typedef struct _vec2
        {
            float x, y;
        } vec2;
        vec2         pos;
        vec2         uv;
        unsigned int col;
    };

    struct RendererResourceName
    {
        inline static cstring FrameDepthRenderTargetName  = "g_frame_depth_render_target";
        inline static cstring FrameSharedRenderTargetName = "g_frame_shared_render_target";
        inline static cstring FrameColorRenderTargetName  = "g_frame_color_render_target";

        inline static cstring GBufferAlbedoAOName         = "g_gbuffer_albedo_ao";
        inline static cstring GBufferNormalRoughnessName  = "g_gbuffer_normal_roughness";
        inline static cstring GBufferMetallicEmissiveName = "g_gbuffer_metallic_emissive";

        inline static cstring SceneCameraBufferName       = "SceneCamera";
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
