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

    void AppRenderPipeline::ResizeRenderTarget(uint32_t w, uint32_t h)
    {
        if (SceneRenderer && SceneRenderer->RenderGraph)
        {
            auto rendergraph = SceneRenderer->RenderGraph;
            rendergraph->Resize(w, h);
        }
    }

    void AppRenderPipeline::BeginFrame()
    {
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