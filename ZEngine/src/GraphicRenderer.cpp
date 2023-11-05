#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    uint32_t                                                        GraphicRenderer::s_viewport_width           = 1;
    uint32_t                                                        GraphicRenderer::s_viewport_height          = 1;
    RendererInformation                                             GraphicRenderer::s_renderer_information     = {};
    WeakRef<Rendering::Swapchain>                                   GraphicRenderer::s_main_window_swapchain    = {};
    std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::COUNT> GraphicRenderer::s_render_target_collection = {};

    void GraphicRenderer::RebuildRenderTargets()
    {
        auto& render_target_frame_output       = s_render_target_collection[RenderTarget::FRAME_OUTPUT];
        auto& render_target_frame_output_spec  = render_target_frame_output->GetSpecification();
        render_target_frame_output_spec.Width  = s_viewport_width;
        render_target_frame_output_spec.Height = s_viewport_height;

        render_target_frame_output->Invalidate();
        render_target_frame_output->Create();
    }

    Ref<Buffers::FramebufferVNext> GraphicRenderer::GetRenderTarget(RenderTarget target)
    {
        return s_render_target_collection[target];
    }

    Ref<Buffers::FramebufferVNext> GraphicRenderer::GetFrameOutput()
    {
        return GetRenderTarget(RenderTarget::FRAME_OUTPUT);
    }

    void GraphicRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        if ((s_viewport_width != width) || (s_viewport_height != height))
        {
            s_viewport_width  = width;
            s_viewport_height = height;
            /*
             * Rebuild RenderTargets
             */
            RebuildRenderTargets();
        }
    }

    void GraphicRenderer::SetMainSwapchain(const Ref<Rendering::Swapchain>& swapchain)
    {
        s_main_window_swapchain                    = swapchain;
        s_renderer_information.FrameCount          = swapchain->GetImageCount();
        s_renderer_information.CurrentFrameIndex   = swapchain->GetCurrentFrameIndex();
        s_renderer_information.SwapchainIdentifier = swapchain->GetIdentifier();
    }

    const RendererInformation& GraphicRenderer::GetRendererInformation()
    {
        /*
         * Ensure Frame information are up to date
         */
        if (auto swapchain = s_main_window_swapchain.lock())
        {
            s_renderer_information.CurrentFrameIndex = swapchain->GetCurrentFrameIndex();
        }
        return s_renderer_information;
    }

    void GraphicRenderer::Initialize()
    {
        s_render_target_collection[RenderTarget::FRAME_OUTPUT] = CreateRef<Buffers::FramebufferVNext>(FrameBufferSpecificationVNext{
            .Width = 1000, .Height = 1000, .AttachmentSpecifications = {Specifications::ImageFormat::R8G8B8A8_UNORM, Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE}});
    }

    void GraphicRenderer::Deinitialize()
    {
        s_render_target_collection.fill(nullptr);
    }
} // namespace ZEngine::Rendering::Renderers
