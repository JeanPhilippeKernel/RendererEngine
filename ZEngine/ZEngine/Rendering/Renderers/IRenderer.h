#pragma once
#include <ZEngine/Core/Memory/GpuAllocator.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    struct ScissorCmd
    {
        uint32_t w = 0;
        uint32_t h = 0;
        int32_t  x = 0;
        int32_t  y = 0;
    };
    struct IndexedCmd
    {
        uint32_t IdxCount      = 0;
        uint32_t InstanceCount = 0;
        uint32_t FirstIndex    = 0;
        int32_t  VertexOffset  = 0;
        uint32_t FirstInstance = 0;
    };

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

    struct RenderOverlayPayload
    {
        bool                        IsIndexBufferUint16 = false;
        uint32_t                    VertexCount         = 0;
        uint32_t                    IndexCount          = 0;
        uint32_t                    DrawDataIndex       = 0;
        float                       Pc[4]               = {0.0f}; // {Scale}-{Translate}
        Core::Memory::BufferView    VBHandle            = {};
        Core::Memory::BufferView    IdxBHandle          = {};
        std::vector<uint32_t>       TextureIds          = {};
        std::vector<unsigned short> IndexData           = {};
        std::vector<UIDrawVert>     VertexData          = {};
        std::vector<ScissorCmd>     ScissorCmds         = {};
        std::vector<IndexedCmd>     IndexedCmds         = {};
    };

    struct RendererResourceName
    {
        inline static cstring FrameDepthRenderTargetName  = "g_frame_depth_render_target";
        inline static cstring FrameSharedRenderTargetName = "g_frame_shared_render_target";
        inline static cstring FrameColorRenderTargetName  = "g_frame_color_render_target";

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
