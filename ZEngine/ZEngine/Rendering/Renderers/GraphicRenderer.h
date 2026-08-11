#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        cstring                 VertexBufferName        = "VertexStorageBuffer";
        cstring                 IndexBufferName         = "IndexStorageBuffer";
        cstring                 TransformBufferName     = "TransformStorageBuffer";
        cstring                 RenderDataBufferName    = "RenderDataStorageBuffer";
        cstring                 MaterialBufferName      = "MaterialStorageBuffer";

        Textures::TextureHandle FrameSharedRenderTarget = {};
        Textures::TextureHandle FrameColorRenderTarget  = {};
        Textures::TextureHandle FrameDepthRenderTarget  = {};

        void                    Initialize(Hardwares::VulkanDevicePtr device) override;
        void                    Deinitialize() override;
        void                    DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, Cameras::CameraPtr const camera);
        void                    ApplySkyConfig(const Scenes::SkyConfig& sky);
        Textures::TextureHandle GetFrameOutput();
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
