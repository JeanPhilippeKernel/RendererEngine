#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>

namespace ZEngine::Applications
{
    struct AppRenderPipeline
    {
        const uint8_t                                        RenderMainThreadIndex    = 0;
        uint8_t                                              RenderWorkerThreadCount  = 0;
        uint8_t                                              UICommandBufferIndex     = 0xff;
        Hardwares::VulkanDevicePtr                           Device                   = nullptr;
        Rendering::Renderers::GraphicRendererPtr             SceneRenderer            = nullptr;
        Rendering::Renderers::ImGUIRendererPtr               ImguiRenderer            = nullptr;
        Hardwares::CommandBufferPtr                          CurrentCmdBuf            = nullptr;
        Core::Containers::Array<Hardwares::CommandBufferPtr> ExecutableCommandBuffers = {};

        ZEngine::Core::Memory::ArenaAllocator                LocalArena               = {};

        void                                                 Initialize(Hardwares::VulkanDevicePtr device);
        void                                                 Shutdown();

        void                                                 ResizeRenderTarget(uint32_t w, uint32_t h);

        void                                                 BeginFrame();
        void                                                 EndFrame();

        void                                                 RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene);

        void                                                 BeginOverlayFrame();
        void                                                 EndOverlayFrame();
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
