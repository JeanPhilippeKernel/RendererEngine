#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Renderers/Contracts/RendererDataContract.h>

using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Rendering::Renderers::Contracts;

namespace ZEngine::Rendering::Renderers
{
    RendererInformation            GraphicRenderer::s_renderer_information      = {};
    WeakRef<Rendering::Swapchain>  GraphicRenderer::s_main_window_swapchain     = {};
    Ref<Buffers::UniformBufferSet> GraphicRenderer::s_UBCamera                  = {};
    Ref<Textures::TextureArray>    GraphicRenderer::GlobalTextures              = {};
    Pools::CommandPool*            GraphicRenderer::s_command_pool              = nullptr;
    Buffers::CommandBuffer*        GraphicRenderer::s_current_command_buffer    = nullptr;
    Buffers::CommandBuffer*        GraphicRenderer::s_current_command_buffer_ui = nullptr;
    Ref<SceneRenderer>             GraphicRenderer::s_scene_renderer            = CreateRef<SceneRenderer>();
    Ref<ImGUIRenderer>             GraphicRenderer::s_imgui_renderer            = CreateRef<ImGUIRenderer>();
    Scope<RenderGraph>             GraphicRenderer::s_render_graph              = CreateScope<RenderGraph>();

    void GraphicRenderer::Initialize()
    {
        s_command_pool = Hardwares::VulkanDevice::GetCommandPool(QueueType::GRAPHIC_QUEUE);
        /*
         * Shared Buffers
         */
        const auto& device_properties = Hardwares::VulkanDevice::GetPhysicalDeviceProperties();
        s_UBCamera                    = CreateRef<Buffers::UniformBufferSet>(s_renderer_information.FrameCount);
        GlobalTextures                = CreateRef<Textures::TextureArray>(device_properties.limits.maxDescriptorSetSampledImages - 20);

        auto builder = s_render_graph->GetBuilder();
        builder->AttachBuffer("scene_camera", s_UBCamera);
        /*
         * Sub Renderer Initialization
         */
        s_scene_renderer->Initialize(s_render_graph.get());
        s_imgui_renderer->Initialize(s_render_graph.get());

        s_render_graph->Setup();
        s_render_graph->Compile();
    }

    void GraphicRenderer::Deinitialize()
    {
        s_render_graph->Dispose();

        s_scene_renderer->Deinitialize();
        s_imgui_renderer->Deinitialize();

        s_UBCamera->Dispose();
        GlobalTextures->Dispose();

        s_main_window_swapchain.reset();
    }

    void GraphicRenderer::SetViewportSize(uint32_t width, uint32_t height)
    {
        s_render_graph->Resize(width, height);
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
        GetRendererInformation();
    }

    void GraphicRenderer::DrawScene(const Ref<Rendering::Cameras::Camera>& camera, const Ref<Rendering::Scenes::SceneRawData>& scene_data)
    {
        uint32_t frame_index = s_renderer_information.CurrentFrameIndex;

        auto& scene_camera    = *s_UBCamera;
        auto  ubo_camera_data = UBOCameraLayout{
             .View         = camera->GetViewMatrix(),
             .RotScaleView = glm::mat4(glm::mat3(camera->GetViewMatrix())),
             .Projection   = camera->GetPerspectiveMatrix(),
             .Position     = glm::vec4(camera->GetPosition(), 1.0f)};

        scene_camera[frame_index].SetData(&ubo_camera_data, sizeof(UBOCameraLayout));

        s_current_command_buffer = s_command_pool->GetCommmandBuffer();

        s_render_graph->Execute(frame_index, s_current_command_buffer, scene_data.get());
        s_current_command_buffer->Submit();
    }

    void GraphicRenderer::BeginImguiFrame()
    {
        s_current_command_buffer_ui = s_command_pool->GetCommmandBuffer();
        s_imgui_renderer->BeginFrame(s_current_command_buffer_ui);
    }

    void GraphicRenderer::DrawUIFrame()
    {
        s_imgui_renderer->Draw(s_current_command_buffer_ui, s_renderer_information.CurrentFrameIndex);
    }

    void GraphicRenderer::EndImguiFrame()
    {
        s_imgui_renderer->EndFrame(s_current_command_buffer_ui, s_renderer_information.CurrentFrameIndex);
        s_current_command_buffer_ui->Submit();
    }

    VkDescriptorSet GraphicRenderer::GetImguiFrameOutput()
    {
        auto& frame_color_res = s_render_graph->GetResource("lighting_render_target");
        return s_imgui_renderer->UpdateFrameOutput(frame_color_res.ResourceInfo.TextureHandle->GetBuffer());
    }

    void GraphicRenderer::BindGlobalTextures(RenderPasses::RenderPass* pass)
    {
        static int cached_used_slot_count = -1;

        if (cached_used_slot_count != GlobalTextures->GetUsedSlotCount())
        {
            cached_used_slot_count = GlobalTextures->GetUsedSlotCount();
            pass->SetInput("TextureArray", GlobalTextures);
            pass->MarkDirty();
        }
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
