#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering::Renderers::Contracts;

namespace ZEngine::Rendering::Renderers
{
    uint32_t                                                        GraphicRenderer::s_viewport_width           = 1;
    uint32_t                                                        GraphicRenderer::s_viewport_height          = 1;
    RendererInformation                                             GraphicRenderer::s_renderer_information     = {};
    WeakRef<Rendering::Swapchain>                                   GraphicRenderer::s_main_window_swapchain    = {};
    std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::COUNT> GraphicRenderer::s_render_target_collection = {};
    Ref<Buffers::UniformBufferSet>                                  GraphicRenderer::s_UBCamera                 = {};
    Pools::CommandPool*                                             GraphicRenderer::s_command_pool             = nullptr;
    Ref<SceneRenderer>                                              GraphicRenderer::s_scene_renderer           = CreateRef<SceneRenderer>();

    void GraphicRenderer::Initialize()
    {
        s_command_pool = Hardwares::VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, s_renderer_information.SwapchainIdentifier, true);

        FrameBufferSpecificationVNext render_target_ouput_spec       = {};
        render_target_ouput_spec.ClearColor                          = true;
        render_target_ouput_spec.ClearDepth                          = true;
        render_target_ouput_spec.AttachmentSpecifications            = {ImageFormat::R8G8B8A8_UNORM, ImageFormat::DEPTH_STENCIL_FROM_DEVICE};
        s_render_target_collection[RenderTarget::FRAME_OUTPUT]       = Buffers::FramebufferVNext::Create(render_target_ouput_spec);
        s_render_target_collection[RenderTarget::ENVIROMENT_CUBEMAP] = Buffers::FramebufferVNext::Create(render_target_ouput_spec);

        /*
        * Shared Uniform Buffers
        */
        s_UBCamera = CreateRef<Buffers::UniformBufferSet>(s_renderer_information.FrameCount);

        /*
         * Scene Renderer Initialization
         */
        s_scene_renderer->Initialize(s_UBCamera);
    }

    void GraphicRenderer::Deinitialize()
    {
        s_scene_renderer->Deinitialize();
        s_render_target_collection.fill(nullptr);

        s_UBCamera->Dispose();
        Hardwares::VulkanDevice::DisposeCommandPool(s_command_pool);

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
        s_command_pool->Tick();
    }

    void GraphicRenderer::DrawScene(const Ref<Rendering::Cameras::Camera>& camera, const Ref<Rendering::Scenes::SceneRawData>& data)
    {
        GetRendererInformation();

        auto& scene_camera    = *s_UBCamera;
        auto  ubo_camera_data = UBOCameraLayout{.View = camera->GetViewMatrix(), .Projection = camera->GetPerspectiveMatrix(), .Position = glm::vec4(camera->GetPosition(), 1.0f)};
        scene_camera[s_renderer_information.CurrentFrameIndex].SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        auto command_buffer = s_command_pool->GetCurrentCommmandBuffer();
        {
            s_scene_renderer->StartScene(command_buffer);
            s_scene_renderer->RenderScene(data, s_renderer_information.CurrentFrameIndex);
            s_scene_renderer->EndScene(command_buffer, s_renderer_information.CurrentFrameIndex);
        }
        command_buffer->Submit();
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
