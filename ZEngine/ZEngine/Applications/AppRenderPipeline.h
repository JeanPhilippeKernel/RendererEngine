#pragma once
#include <Hardwares/VulkanDevice.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/ImGUIRenderer.h>

namespace ZEngine::Applications
{
    struct AppRenderPipeline
    {
        Hardwares::VulkanDevicePtr               Device        = nullptr;
        Rendering::Renderers::GraphicRendererPtr SceneRenderer = nullptr;
        Rendering::Renderers::ImGUIRendererPtr   ImguiRenderer = nullptr;
        Hardwares::CommandBufferPtr              CurrentCmdBuf = nullptr;

        ZEngine::Core::Memory::ArenaAllocator    LocalArena    = {};

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     ResizeRenderTarget(uint32_t w, uint32_t h);

        void                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(Rendering::Cameras::CameraPtr camera, Rendering::Scenes::RenderScenePtr scene);

        void                                     BeginOverlayFrame();
        void                                     EndOverlayFrame();
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
