#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/ImGUIRenderer.h>

namespace ZEngine::Applications
{
    struct RenderPayload
    {
        uint32_t                                   RenderTargetW      = 0;
        uint32_t                                   RenderTargetH      = 0;
        PaddedAtomic<bool>                         RenderUIOverlay    = {.value = false};
        PaddedAtomic<bool>                         ResizeRenderTarget = {.value = false};
        Rendering::Cameras::CameraPtr              Camera             = nullptr;
        Rendering::Scenes::RenderScenePtr          Scene              = nullptr;
        Rendering::Renderers::RenderOverlayPayload UIOverlay          = {};
    };

    struct AppRenderPipeline
    {
        const uint8_t                            MaxMailBoxBufferCount    = 3;
        const uint8_t                            RenderMainThreadIndex    = 0;
        uint8_t                                  RenderWorkerThreadCount  = 0;
        uint8_t                                  UICommandBufferIndex     = 0xff;
        uint32_t                                 CurrentMailBoxBufferHead = 0;
        PaddedAtomic<int>                        MailBoxBufferHead        = {.value = 0};
        PaddedAtomic<int>                        MailBoxBufferTail        = {.value = 0};
        RenderPayload                            RenderPayloads[3]        = {};
        ZEngine::Core::Memory::ArenaAllocator    LocalArena               = {};
        Hardwares::VulkanDevicePtr               Device                   = nullptr;
        Rendering::Renderers::GraphicRendererPtr SceneRenderer            = nullptr;
        Rendering::Renderers::ImGUIRendererPtr   ImguiRenderer            = nullptr;
        Hardwares::CommandBufferPtr              CurrentCmdBuf            = nullptr;

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     ResizeRenderTarget(uint32_t w, uint32_t h);

        // Returns false when the frame was aborted (OUT_OF_DATE at acquire or
        // zero-size surface). The caller must skip all rendering work for that frame.
        bool                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene);

        void                                     BeginOverlayFrame();
        void                                     EndOverlayFrame();
        void                                     FillOverlayPayload(Rendering::Renderers::RenderOverlayPayload& payload);
        void                                     RenderOverlay(const Rendering::Renderers::RenderOverlayPayload& payload);
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
