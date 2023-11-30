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
    Ref<SceneRenderer>                                              GraphicRenderer::s_scene_renderer           = CreateRef<SceneRenderer>();

    void GraphicRenderer::Initialize()
    {
        FrameBufferSpecificationVNext frame_ouput_spec         = {};
        frame_ouput_spec.ClearColor                            = true;
        frame_ouput_spec.ClearDepth                            = true;
        frame_ouput_spec.AttachmentSpecifications              = {ImageFormat::R8G8B8A8_UNORM, ImageFormat::DEPTH_STENCIL_FROM_DEVICE};
        s_render_target_collection[RenderTarget::FRAME_OUTPUT] = Buffers::FramebufferVNext::Create(frame_ouput_spec);

        s_scene_renderer->Initialize();
    }

    void GraphicRenderer::Deinitialize()
    {
        s_scene_renderer->Deinitialize();
        s_render_target_collection.fill(nullptr);

        s_main_window_swapchain.reset();
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
            s_scene_renderer->SetViewportSize(s_viewport_width, s_viewport_height);
        }
    }

    void GraphicRenderer::SetMainSwapchain(const Ref<Rendering::Swapchain>& swapchain)
    {
        s_main_window_swapchain                    = swapchain;
        s_renderer_information.FrameCount          = swapchain->GetImageCount();
        s_renderer_information.CurrentFrameIndex   = swapchain->GetCurrentFrameIndex();
        s_renderer_information.SwapchainIdentifier = swapchain->GetIdentifier();
    }

    void GraphicRenderer::Update()
    {
        s_scene_renderer->Tick();
    }

    void GraphicRenderer::DrawScene(const Ref<Rendering::Cameras::Camera>& camera, const Ref<Rendering::Scenes::SceneRawData>& data)
    {
        s_scene_renderer->StartScene(camera->GetPosition(), camera->GetViewMatrix(), camera->GetPerspectiveMatrix());
        s_scene_renderer->RenderScene(data);
        s_scene_renderer->EndScene();
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
} // namespace ZEngine::Rendering::Renderers
