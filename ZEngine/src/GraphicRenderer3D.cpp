#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>
#include <Window/GlfwWindow/VulkanWindow.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>
#include <Helpers/RendererHelper.h>
#include <Helpers/MeshHelper.h>


using namespace ZEngine::Rendering::Buffers::FrameBuffers;

namespace ZEngine::Rendering::Renderers
{

    GraphicRenderer3D::GraphicRenderer3D() : GraphicRenderer()
    {
        m_renderer_information->GraphicStorageType = Storages::GraphicRendererStorageType::GRAPHIC_3D_STORAGE_TYPE;
    }

    GraphicRenderer3D::~GraphicRenderer3D() {}

    void GraphicRenderer3D::Initialize()
    {
        RequestOutputImageSize();

        //// enable management of transparent background image (RGBA-8)
        // glEnable(GL_BLEND);
        // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        //// enable Z-depth and stencil testing
        // glEnable(GL_DEPTH_TEST);
        // glEnable(GL_STENCIL_TEST);
    }

    void GraphicRenderer3D::Deinitialize()
    {
        m_vertex_buffer.Dispose();
        m_index_buffer.Dispose();
        for (auto& framebuffer : m_framebuffer_collection)
        {
            framebuffer.Dispose();
        }
        m_standard_pipeline.Dispose();
    }

    void GraphicRenderer3D::StartScene(const Contracts::GraphicSceneLayout& scene_data)
    {
        GraphicRenderer::StartScene(scene_data);

        auto ubo_camera_data = Contracts::UBOCameraLayout{
            .position   = Maths::Vector4(m_scene_data.SceneCamera->GetPosition(), 1.f),
            .View       = m_scene_data.SceneCamera->GetViewMatrix(),
            .Projection = m_scene_data.SceneCamera->GetProjectionMatrix()};
        ubo_camera_data.Projection[1][1] *= -1;
        m_standard_pipeline.UBOCameraPropertiesCollection[m_scene_data.FrameIndex].SetData(ubo_camera_data);

        if (!m_scene_data.MeshCollection.empty())
        {
            auto ubo_model_data = Contracts::UBOModelLayout{.Model = m_scene_data.MeshCollection[0].LocalTransform}; // ToDo : Should be revisted
            m_standard_pipeline.UBOModelPropertiesCollection[m_scene_data.FrameIndex].SetData(ubo_model_data);
        }
    }

    void GraphicRenderer3D::EndScene()
    {
        /* this is an hack for now*/
        if (m_scene_data.MeshCollection.empty())
        {
            return;
        }
        m_vertex_buffer.SetData(m_scene_data.MeshCollection[0].Vertices);
        m_index_buffer.SetData(m_scene_data.MeshCollection[0].Indices);

        /*Todo: all of this should be rethinking */
        auto& device = Engine::GetVulkanInstance()->GetHighPerformantDevice();

        auto  current_window    = Engine::GetWindow();
        auto  vulkan_window_ptr = reinterpret_cast<Window::GLFWWindow::VulkanWindow*>(current_window.get());
        auto& window_frame      = vulkan_window_ptr->GetWindowFrame(m_scene_data.FrameIndex);

        VkCommandBuffer             command_buffer                     = VK_NULL_HANDLE;
        VkCommandBufferAllocateInfo graphic_command_buffer_create_info = {};
        graphic_command_buffer_create_info.sType                       = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        graphic_command_buffer_create_info.commandPool                 = window_frame.GraphicCommandPool;
        graphic_command_buffer_create_info.level                       = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        graphic_command_buffer_create_info.commandBufferCount          = 1;
        ZENGINE_VALIDATE_ASSERT(
            vkAllocateCommandBuffers(device.GetNativeDeviceHandle(), &graphic_command_buffer_create_info, &command_buffer) == VK_SUCCESS, "Failed to allocate Command Buffer")

        VkCommandBufferBeginInfo command_buffer_begin_info = {};
        command_buffer_begin_info.sType                    = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

        ZENGINE_VALIDATE_ASSERT(vkBeginCommandBuffer(command_buffer, &command_buffer_begin_info) == VK_SUCCESS, "Failed to begin the Command Buffer")

        {
            VkRenderPassBeginInfo render_pass_begin_info = {};
            render_pass_begin_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            render_pass_begin_info.renderPass            = m_framebuffer_collection[m_scene_data.FrameIndex].RenderPass;
            render_pass_begin_info.framebuffer           = m_framebuffer_collection[m_scene_data.FrameIndex].Framebuffer;
            render_pass_begin_info.renderArea.extent     = {m_viewport_width, m_viewport_height};

            std::array<VkClearValue, 2> clear_values = {};
            clear_values[0].color                    = {{0.0f, 0.0f, 0.0f, 1.0f}};
            clear_values[1].depthStencil             = {1.0f, 0};
            render_pass_begin_info.clearValueCount   = clear_values.size();
            render_pass_begin_info.pClearValues      = clear_values.data();

            vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
            {
                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_standard_pipeline.Pipeline);

                VkViewport viewport{};
                viewport.x        = 0.0f;
                viewport.y        = 0.0f;
                viewport.width    = m_viewport_width;
                viewport.height   = m_viewport_height;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(command_buffer, 0, 1, &viewport);

                VkRect2D scissor{};
                scissor.offset = {0, 0};
                scissor.extent = {m_viewport_width, m_viewport_height};
                vkCmdSetScissor(command_buffer, 0, 1, &scissor);

                VkDeviceSize offsets[]        = {0};
                VkBuffer     vertex_buffers[] = {m_vertex_buffer.GetNativeBufferHandle()};
                vkCmdBindVertexBuffers(command_buffer, 0, 1, vertex_buffers, offsets);

                vkCmdBindIndexBuffer(command_buffer, m_index_buffer.GetNativeBufferHandle(), 0, VK_INDEX_TYPE_UINT32);
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_standard_pipeline.PipelineLayout,
                    0,
                    1,
                    &(m_standard_pipeline.DescriptorSetCollection[m_scene_data.FrameIndex]),
                    0,
                    nullptr);

                /*ToDo : Mesh Indice seems incorrect, need to be fix -- we use vkCmdDraw for now*/
                vkCmdDraw(command_buffer, m_scene_data.MeshCollection[0].VertexCount, 1, 0, 0);
                // vkCmdDrawIndexed(command_buffer, (uint32_t) m_scene_data.MeshCollection[0].Indices.size(), 1, 0, 0, 0);
            }
            vkCmdEndRenderPass(command_buffer);
        }

        ZENGINE_VALIDATE_ASSERT(vkEndCommandBuffer(command_buffer) == VK_SUCCESS, "Failed to end the Command Buffer")

        window_frame.GraphicCommandBuffers.push_back(std::move(command_buffer));
    }

    void GraphicRenderer3D::RequestOutputImageSize(uint32_t width, uint32_t height)
    {
        m_viewport_width  = width;
        m_viewport_height = height;

        /*Invalidate and recreate internal state*/
        auto& device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        MarkVulkanInternalObjectDirty(device);
    }

    void GraphicRenderer3D::MarkVulkanInternalObjectDirty(Hardwares::VulkanDevice& device)
    {
        vkQueueWaitIdle(device.GetCurrentGraphicQueue(true));
        vkQueueWaitIdle(device.GetCurrentTransferQueue());

        for (uint32_t i = 0; i < m_framebuffer_collection.size(); ++i)
        {
            m_framebuffer_collection[i].Dispose();
        }
        m_framebuffer_collection.clear();
        m_framebuffer_collection.shrink_to_fit();
        m_standard_pipeline.Dispose();

        auto     window_ptr                = ZEngine::Engine::GetWindow();
        auto     current_vulkan_window_ptr = reinterpret_cast<ZEngine::Window::GLFWWindow::VulkanWindow*>(window_ptr.get());
        uint32_t frame_count               = current_vulkan_window_ptr->GetWindowFrameCollection().size();

        m_framebuffer_collection.resize(frame_count);

        Buffers::FrameBufferSpecificationVNext framebuffer_specification = {};
        framebuffer_specification.Width                                  = m_viewport_width;
        framebuffer_specification.Height                                 = m_viewport_height;
        framebuffer_specification.AttachmentSpecifications               = {
            Buffers::FrameBuffers::FrameBufferAttachmentSpecificationVNext{
                              {.ImageType     = VK_IMAGE_TYPE_2D,
                               .Format        = VK_FORMAT_R8G8B8A8_UNORM,
                               .Tiling        = VK_IMAGE_TILING_OPTIMAL,
                               .InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                               .SampleCount   = VK_SAMPLE_COUNT_1_BIT}},
            Buffers::FrameBuffers::FrameBufferAttachmentSpecificationVNext{
                              {.ImageType     = VK_IMAGE_TYPE_2D,
                               .Format        = (uint32_t) Helpers::FindDepthFormat(device),
                               .Tiling        = VK_IMAGE_TILING_OPTIMAL,
                               .InitialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                               .SampleCount   = VK_SAMPLE_COUNT_1_BIT}}};

        /*RenderPass definition*/
        RenderPasses::RenderPassSpecification render_pass_specification = {};
        render_pass_specification                                       = {
                                                  .ColorAttachements = {
                VkAttachmentDescription{
                                                          .format         = VK_FORMAT_R8G8B8A8_UNORM,
                                                          .samples        = VK_SAMPLE_COUNT_1_BIT,
                                                          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                                          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                                          .finalLayout    = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},
                VkAttachmentDescription{
                                                          .format         = Helpers::FindDepthFormat(device),
                                                          .samples        = VK_SAMPLE_COUNT_1_BIT,
                                                          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                          .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                                          .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}}};

        RenderPasses::SubPassSpecification sub_pass_specification = {};
        sub_pass_specification.ColorAttachementReferences         = {VkAttachmentReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        sub_pass_specification.DepthStencilAttachementReference   = VkAttachmentReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        sub_pass_specification.SubpassDescription                 = VkSubpassDescription{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
        render_pass_specification.SubpassSpecifications           = {sub_pass_specification};
        render_pass_specification.SubpassDependencies             = {VkSubpassDependency{
                        .srcSubpass    = VK_SUBPASS_EXTERNAL,
                        .dstSubpass    = 0,
                        .srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                        .srcAccessMask = 0,
                        .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT}};

        framebuffer_specification.RenderPass = Helpers::CreateRenderPass(device, render_pass_specification);

        for (uint32_t i = 0; i < m_framebuffer_collection.size(); ++i)
        {
            m_framebuffer_collection[i] = Buffers::FramebufferVNext{framebuffer_specification};
        }

        ZENGINE_VALIDATE_ASSERT(!m_framebuffer_collection.empty(), "Framebuffer collection can't be empty")

        {
            std::vector<VkImageView> image_view_handle_collection = {};
            std::vector<VkSampler>   sampler_handle_collection    = {};
            // for (const auto& framebuffer : m_framebuffer_collection)
            //{
            //     image_view_handle_collection.push_back(framebuffer.ColorAttachmentCollection[0]);
            //     sampler_handle_collection.push_back(framebuffer.Sampler);
            // }
            m_standard_pipeline = Helpers::CreateStandardGraphicPipeline(
                device,
                VkExtent2D{m_viewport_width, m_viewport_height},
                framebuffer_specification.RenderPass,
                image_view_handle_collection,
                sampler_handle_collection,
                frame_count);
        }
    }
} // namespace ZEngine::Rendering::Renderers
