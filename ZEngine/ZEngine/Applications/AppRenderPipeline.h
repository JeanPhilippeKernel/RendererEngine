#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>

namespace ZEngine::Applications
{
    struct alignas(std::hardware_destructive_interference_size) PaddedAtomicInt
    {
        std::atomic_uint32_t value = 0;
    };

    struct RenderPayload
    {
        bool                                       RenderUIOverlay    = false;
        bool                                       ResizeRenderTarget = false;
        uint32_t                                   RenderTargetW      = 0;
        uint32_t                                   RenderTargetH      = 0;
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
        PaddedAtomicInt                          MailBoxBufferHead        = {};
        PaddedAtomicInt                          MailBoxBufferTail        = {};
        RenderPayload                            RenderPayloads[3]        = {};
        ZEngine::Core::Memory::ArenaAllocator    LocalArena               = {};
        Hardwares::VulkanDevicePtr               Device                   = nullptr;
        Rendering::Renderers::GraphicRendererPtr SceneRenderer            = nullptr;
        Rendering::Renderers::ImGUIRendererPtr   ImguiRenderer            = nullptr;
        Hardwares::CommandBufferPtr              CurrentCmdBuf            = nullptr;

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     ResizeRenderTarget(uint32_t w, uint32_t h);

        void                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene);

        void                                     BeginOverlayFrame();
        void                                     EndOverlayFrame();
        void                                     FillOverlayPayload(Rendering::Renderers::RenderOverlayPayload& payload);
        void                                     RenderOverlay(const Rendering::Renderers::RenderOverlayPayload& payload);
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
