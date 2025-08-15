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

        void                                     Initialize(Hardwares::VulkanDevicePtr device);
        void                                     Shutdown();

        void                                     BeginFrame();
        void                                     EndFrame();

        void                                     RenderScene(Rendering::Cameras::CameraPtr camera, Scenes::RenderScenePtr scene);

        void                                     BeginOverlayFrame();
        void                                     EndOverlayFrame();
    };
    ZDEFINE_PTR(AppRenderPipeline);

} // namespace ZEngine::Applications
