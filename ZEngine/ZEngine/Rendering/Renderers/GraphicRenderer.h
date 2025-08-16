#pragma once
#include <Camera.h>
#include <Hardwares/VulkanDevice.h>
#include <IRenderer.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        cstring                           FrameDepthRenderTargetName = "g_frame_depth_render_target";
        cstring                           FrameColorRenderTargetName = "g_frame_color_render_target";

        cstring                           SceneCameraBufferName      = "SceneCamera";
        cstring                           VertexBufferName           = "VertexStorageBuffer";
        cstring                           IndexBufferName            = "IndexStorageBuffer";
        cstring                           TransformBufferName        = "TransformStorageBuffer";
        cstring                           RenderDataBufferName       = "RenderDataStorageBuffer";
        cstring                           MaterialBufferName         = "MaterialStorageBuffer";

        const size_t                      DefaultBufferSize          = ZMega(10);

        Hardwares::UniformBufferSetHandle SceneCameraBufferHandle    = {};
        Textures::TextureHandle           FrameColorRenderTarget     = {};
        Textures::TextureHandle           FrameDepthRenderTarget     = {};

        void                              Initialize(Hardwares::VulkanDevicePtr device) override;
        void                              Deinitialize() override;
        void                              DrawScene(Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera);
        Textures::TextureHandle           GetFrameOutput();
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
