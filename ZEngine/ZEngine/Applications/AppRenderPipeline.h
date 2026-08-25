#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/ImGUIRenderer.h>
#include <ZEngine/Rendering/Renderers/ZUIRenderer.h>

namespace ZEngine::UI { struct ZUIContext; }

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
        Rendering::Renderers::ZUIRenderPayload     ZUIOverlay         = {};
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
        // ZUIPayloadArenas and ZUICtx are sub-arenas of LocalArena; declared after it
        // so C++ destroys them before LocalArena (reverse order)
        ZEngine::Core::Memory::ArenaAllocator    ZUIPayloadArenas[3]      = {};
        ZEngine::UI::ZUIContext*                 ZUICtx                   = nullptr;
        Hardwares::VulkanDevicePtr               Device                   = nullptr;
        Rendering::Renderers::GraphicRendererPtr SceneRenderer            = nullptr;
        Rendering::Renderers::ImGUIRendererPtr   ImguiRenderer            = nullptr;
        Rendering::Renderers::ZUIRendererPtr     ZUIRenderer              = nullptr;
        Hardwares::CommandBufferPtr              CurrentCmdBuf            = nullptr;

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     ResizeRenderTarget(uint32_t w, uint32_t h);

        // Returns false when the frame was aborted (OUT_OF_DATE at acquire or
        // zero-size surface). The caller must skip all rendering work for that frame.
        bool                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene);

        void                                     BeginOverlayFrame(float dt = 0.f);
        void                                     EndOverlayFrame();
        void                                     FillOverlayPayload(RenderPayload& payload);
        void                                     RenderOverlay(const RenderPayload& payload);
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
