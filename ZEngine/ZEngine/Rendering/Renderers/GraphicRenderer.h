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
        // Rebinds VertexSB/IndexSB to the RMM-owned device-local buffers when handles become valid.
        void                    UpdateRMMBindings(Scenes::SceneDataPtr scene);
        void                    ApplySkyConfig(const Scenes::SkyConfig& sky);
        void                    ApplyGridConfig(const Scenes::GridConfig& cfg);
        Textures::TextureHandle GetFrameOutput();

    private:
        bool m_static_buffers_bound = false;
        bool m_global_buffers_bound = false;
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
