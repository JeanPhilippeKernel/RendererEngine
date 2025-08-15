#include <pch.h>
#include <AppRenderPipeline.h>

namespace ZEngine::Applications
{
    void AppRenderPipeline::Initialize(Hardwares::VulkanDevicePtr device)
    {
        Device        = device;
        SceneRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::GraphicRenderer);
        ImguiRenderer = ZPushStructCtor(Device->Arena, Rendering::Renderers::ImGUIRenderer);

        SceneRenderer->Initialize(Device);
        ImguiRenderer->Initialize(Device);
    }

    void AppRenderPipeline::Shutdown()
    {
        SceneRenderer->Deinitialize();
        ImguiRenderer->Deinitialize();
    }

    void AppRenderPipeline::BeginFrame()
    {
        // if (g_renderer->EnqueuedResizeRequests.Size())
        //{
        //     Rendering::Renderers::ResizeRequest req;
        //     if (g_renderer->EnqueuedResizeRequests.Pop(req))
        //     {
        //         g_renderer->RenderGraph->Resize(req.Width, req.Height);
        //         continue;
        //     }
        // }

        Device->NewFrame();
        CurrentCmdBuf = Device->GetCommandBuffer();
    }

    void AppRenderPipeline::EndFrame()
    {
        Device->EnqueueCommandBuffer(CurrentCmdBuf);
        Device->Present();
    }

    void AppRenderPipeline::BeginOverlayFrame()
    {
        ImguiRenderer->NewFrame();
    }

    void AppRenderPipeline::EndOverlayFrame()
    {
        ImguiRenderer->DrawFrame(CurrentCmdBuf);
    }
} // namespace ZEngine::Applications