#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer3D.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>
#include <Window/GlfwWindow/VulkanWindow.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>
#include <Helpers/RendererHelper.h>
#include <Helpers/MeshHelper.h>

#include <Rendering/Scenes/GraphicScene.h>


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
        /*Viewport definition*/
        m_viewport = {};
        m_viewport.x        = 0.0f;
        m_viewport.y        = 0.0f;
        m_viewport.width    = m_viewport_width;
        m_viewport.height   = m_viewport_height;
        m_viewport.minDepth = 0.0f;
        m_viewport.maxDepth = 1.0f;

        /*Scissor definition*/
        m_scissor        = {};
        m_scissor.offset = {0, 0};
        m_scissor.extent = {m_viewport_width, m_viewport_height};

        /*RenderPass definition*/
        auto&                                 performant_device        = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        RenderPasses::RenderPassSpecification renderpass_specification = {};
        renderpass_specification                                       = {
                                                  .ColorAttachements = {
                VkAttachmentDescription{
                                                          .format         = VK_FORMAT_R8G8B8A8_UNORM,
                                                          .samples        = VK_SAMPLE_COUNT_1_BIT,
                                                          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                          .storeOp        = VK_ATTACHMENT_STORE_OP_STORE,
                                                          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                                          .finalLayout    = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                VkAttachmentDescription{
                                                          .format         = Helpers::FindDepthFormat(performant_device),
                                                          .samples        = VK_SAMPLE_COUNT_1_BIT,
                                                          .loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR,
                                                          .storeOp        = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                                                          .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                                                          .initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED,
                                                          .finalLayout    = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL}}};

        RenderPasses::SubPassSpecification subpass_specification = {};
        subpass_specification.ColorAttachementReferences         = {VkAttachmentReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
        subpass_specification.DepthStencilAttachementReference   = VkAttachmentReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        subpass_specification.SubpassDescription                 = VkSubpassDescription{.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS};
        renderpass_specification.SubpassSpecifications           = {subpass_specification};
        renderpass_specification.SubpassDependencies             = {
            VkSubpassDependency{
                            .srcSubpass      = VK_SUBPASS_EXTERNAL,
                            .dstSubpass      = 0,
                            .srcStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                            .dstStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                            .srcAccessMask   = VK_ACCESS_SHADER_READ_BIT,
                            .dstAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT},
            VkSubpassDependency{
                            .srcSubpass      = 0,
                            .dstSubpass      = VK_SUBPASS_EXTERNAL,
                            .srcStageMask    = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                            .dstStageMask    = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                            .srcAccessMask   = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                            .dstAccessMask   = VK_ACCESS_SHADER_READ_BIT,
                            .dependencyFlags = VK_DEPENDENCY_BY_REGION_BIT}};

        m_renderpass = Helpers::CreateRenderPass(performant_device, renderpass_specification);

        RequestOutputImageSize();
    }

    void GraphicRenderer3D::Deinitialize()
    {
        m_standard_pipeline.Dispose();
        if (m_renderpass)
        {
            auto& device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
            vkDestroyRenderPass(device.GetNativeDeviceHandle(), m_renderpass, nullptr);
            m_renderpass = VK_NULL_HANDLE;
        }
    }

    void GraphicRenderer3D::StartScene(const Contracts::GraphicSceneLayout& scene_data)
    {
        GraphicRenderer::StartScene(scene_data);
        auto ubo_camera_data = Contracts::UBOCameraLayout{
            .position   = Maths::Vector4(m_scene_data.SceneCamera->GetPosition(), 1.f),
            .View       = m_scene_data.SceneCamera->GetViewMatrix(),
            .Projection = m_scene_data.SceneCamera->GetProjectionMatrix()};
        m_standard_pipeline.UBOCameraPropertiesCollection[m_scene_data.FrameIndex].SetData(ubo_camera_data);
    }

    void GraphicRenderer3D::EndScene()
    {
        /* this is an hack for now*/
        if (m_scene_data.MeshCollectionIdentifiers.empty())
        {
            return;
        }

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
            render_pass_begin_info.renderPass            = m_standard_pipeline.FramebufferCollection[m_scene_data.FrameIndex].RenderPass;
            render_pass_begin_info.framebuffer           = m_standard_pipeline.FramebufferCollection[m_scene_data.FrameIndex].Framebuffer;
            render_pass_begin_info.renderArea.extent     = {m_viewport_width, m_viewport_height};

            std::array<VkClearValue, 2> clear_values = {};
            clear_values[0].color                    = {{0.1f, 0.1f, 0.1f, 1.0f}};
            clear_values[1].depthStencil             = {1.0f, 0};
            render_pass_begin_info.clearValueCount   = clear_values.size();
            render_pass_begin_info.pClearValues      = clear_values.data();

            vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
            {
                m_viewport.width  = m_viewport_width;
                m_viewport.height = m_viewport_height;
                vkCmdSetViewport(command_buffer, 0, 1, &m_viewport);

                m_scissor.extent = {m_viewport_width, m_viewport_height};
                vkCmdSetScissor(command_buffer, 0, 1, &m_scissor);

                vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_standard_pipeline.Pipeline);

                auto descriptor_set_collection =
                    std::array{m_standard_pipeline.DescriptorSetCollection[m_scene_data.FrameIndex], m_standard_pipeline.FragmentDescriptorSetCollection[m_scene_data.FrameIndex]};
                vkCmdBindDescriptorSets(
                    command_buffer,
                    VK_PIPELINE_BIND_POINT_GRAPHICS,
                    m_standard_pipeline.PipelineLayout,
                    0,
                    descriptor_set_collection.size(),
                    descriptor_set_collection.data(),
                    0,
                    nullptr);

                if (m_scene_data.GraphicScenePtr)
                {
                    for (uint32_t i = 0; i < m_scene_data.MeshCollectionIdentifiers.size(); ++i)
                    {
                        auto& mesh           = reinterpret_cast<Rendering::Scenes::GraphicScene*>(m_scene_data.GraphicScenePtr)->GetMesh(i);
                        auto  ubo_model_data = Contracts::UBOModelLayout{.Model = mesh.LocalTransform}; // ToDo : Should be revisted
                        m_standard_pipeline.UBOModelPropertiesCollection[m_scene_data.FrameIndex].SetData(ubo_model_data);
                        mesh.Draw(command_buffer);
                    }
                }
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

        MarkVulkanInternalObjectDirty();
    }

    Contracts::FramebufferViewLayout GraphicRenderer3D::GetOutputImage(uint32_t frame_index) const
    {
        if (m_standard_pipeline.Pipeline)
        {
            return Contracts::FramebufferViewLayout{
                .FrameId = frame_index,
                .Sampler = m_standard_pipeline.FramebufferCollection[frame_index].Sampler,
                .View    = m_standard_pipeline.FramebufferCollection[frame_index].AttachmentCollection[0],
                .Handle  = m_standard_pipeline.FragmentDescriptorSetCollection[frame_index]};
        }
        return {};
    }

    void GraphicRenderer3D::MarkVulkanInternalObjectDirty()
    {
        /*Invalidate and recreate internal state*/
        auto& performant_device = Engine::GetVulkanInstance()->GetHighPerformantDevice();
        vkQueueWaitIdle(performant_device.GetCurrentGraphicQueueWithPresentSupport().Queue);
        vkQueueWaitIdle(performant_device.GetCurrentTransferQueue().Queue);

        if (!m_standard_pipeline.Pipeline)
        {
            auto     window_ptr                = ZEngine::Engine::GetWindow();
            auto     current_vulkan_window_ptr = reinterpret_cast<ZEngine::Window::GLFWWindow::VulkanWindow*>(window_ptr.get());
            uint32_t frame_count               = current_vulkan_window_ptr->GetWindowFrameCollection().size();
            m_standard_pipeline = Helpers::CreateStandardGraphicPipeline(performant_device, VkExtent2D{m_viewport_width, m_viewport_height}, m_renderpass, frame_count);
        }
        m_standard_pipeline.CreateOrResizeFramebuffer(m_viewport_width, m_viewport_height);
        m_standard_pipeline.UpdateDescriptorSet();
    }
} // namespace ZEngine::Rendering::Renderers
