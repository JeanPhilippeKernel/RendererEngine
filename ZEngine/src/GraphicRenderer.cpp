#include <pch.h>
#include <Rendering/Buffers/FrameBuffers/FrameBufferSpecification.h>
#include <Rendering/Renderers/Pipelines/GraphicRendererPipelineSpecification.h>
#include <Rendering/Scenes/GraphicScene.h>

using namespace ZEngine::Rendering::Buffers::FrameBuffers;

namespace ZEngine::Rendering::Renderers
{
    Pools::CommandPool*                                                                         GraphicRenderer::m_command_pool = nullptr;
    Ref<Buffers::UniformBuffer>                                                                 GraphicRenderer::m_UBOCamera = CreateRef<Buffers::UniformBuffer>();
    Ref<RenderPasses::RenderPass>                                                               GraphicRenderer::m_final_color_output_pass     = {};
    Contracts::GraphicSceneLayout                                                               GraphicRenderer::m_scene_information           = {};

    const Ref<GraphicRendererInformation>& GraphicRenderer::GetRendererInformation() const
    {
        return m_renderer_information;
    }

    void GraphicRenderer::Initialize()
    {
        m_command_pool = Hardwares::VulkanDevice::CreateCommandPool(QueueType::GRAPHIC_QUEUE, true);

        {
            Buffers::FrameBufferSpecificationVNext framebuffer_specification = {};
            framebuffer_specification.Width                                  = 1;
            framebuffer_specification.Height                                 = 1;
            framebuffer_specification.AttachmentSpecifications               = {ImageFormat::R8G8B8A8_UNORM, ImageFormat::DEPTH_STENCIL};

            Pipelines::GraphicRendererPipelineSpecification pipeline_spec = {};
            pipeline_spec.DebugName                                       = "Standard-Pipeline";
            pipeline_spec.TargetFrameBufferSpecification                  = framebuffer_specification;
            pipeline_spec.VertexShaderFilename                            = "Resources/Windows/Shaders/standard_shader_light_vertex.spv";
            pipeline_spec.FragmentShaderFilename                          = "Resources/Windows/Shaders/standard_shader_light_fragment.spv";
            pipeline_spec.LayoutBindingCollection                         = {
                VkDescriptorSetLayoutBinding{
                                            .binding            = 0,
                                            .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                            .descriptorCount    = 1,
                                            .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                                            .pImmutableSamplers = nullptr},
                VkDescriptorSetLayoutBinding{
                                            .binding            = 1,
                                            .descriptorType     = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                            .descriptorCount    = 1,
                                            .stageFlags         = VK_SHADER_STAGE_VERTEX_BIT,
                                            .pImmutableSamplers = nullptr}};


            RenderPasses::RenderPassSpecification color_pass = {};
            color_pass.DebugName                             = "Final-Color-Attachment";
            color_pass.Pipeline                              = Pipelines::GraphicPipeline::Create(pipeline_spec);
            m_final_color_output_pass                        = RenderPasses::RenderPass::Create(color_pass);

            // m_final_color_output_pass->SetInput("scene_camera", m_UBOCamera);

            m_final_color_output_pass->Bake();
        }
    }

    void GraphicRenderer::Deinitialize()
    {
        m_final_color_output_pass.reset();
        m_UBOCamera.reset();
        Hardwares::VulkanDevice::DisposeCommandPool(m_command_pool);
    }

    void GraphicRenderer::BeginRenderPass(const Buffers::CommandBuffer& command_buffer, const Ref<RenderPasses::RenderPass>& render_pass)
    {
        auto                  render_pass_pipeline   = render_pass->GetPipeline();
        auto                  framebuffer            = render_pass_pipeline->GetTargetFrameBuffer();
        VkRenderPassBeginInfo render_pass_begin_info = {};
        render_pass_begin_info.sType                 = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        render_pass_begin_info.renderPass            = render_pass_pipeline->GetRenderPassHandle();
        render_pass_begin_info.framebuffer           = framebuffer->GetHandle();
        render_pass_begin_info.renderArea.extent     = VkExtent2D{framebuffer->GetWidth(), framebuffer->GetHeight()};

        // ToDo : should be move to FrambufferSpecification
        std::array<VkClearValue, 2> clear_values = {};
        clear_values[0].color                    = {{0.1f, 0.1f, 0.1f, 1.0f}};
        clear_values[1].depthStencil             = {1.0f, 0};
        render_pass_begin_info.clearValueCount   = clear_values.size();
        render_pass_begin_info.pClearValues      = clear_values.data();

        vkCmdBeginRenderPass(command_buffer.GetHandle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport = {};
        viewport.x          = 0.0f;
        viewport.y          = 0.0f;
        viewport.width      = framebuffer->GetWidth();
        viewport.height     = framebuffer->GetHeight();
        viewport.minDepth   = 0.0f;
        viewport.maxDepth   = 1.0f;

        /*Scissor definition*/
        VkRect2D scissor = {};
        scissor.offset   = {0, 0};
        scissor.extent   = {framebuffer->GetWidth(), framebuffer->GetHeight()};

        vkCmdSetViewport(command_buffer.GetHandle(), 0, 1, &viewport);
        vkCmdSetScissor(command_buffer.GetHandle(), 0, 1, &scissor);

        vkCmdBindPipeline(command_buffer.GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, render_pass_pipeline->GetHandle());

        vkCmdBindDescriptorSets(
            command_buffer.GetHandle(),
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            render_pass_pipeline->GetPipelineLayout(),
            0,
            render_pass_pipeline->GetDescriptorSetCollectionCount(),
            render_pass_pipeline->GetDescriptorSetCollectionData(),
            0,
            nullptr);
    }

    void GraphicRenderer::EndRenderPass(const Buffers::CommandBuffer& command_buffer)
    {
        vkCmdEndRenderPass(command_buffer.GetHandle());
    }

    void GraphicRenderer::StartScene(Ref<Cameras::Camera> scene_camera)
    {
        auto ubo_camera_data = Contracts::UBOCameraLayout{
            .position = Maths::Vector4(scene_camera->GetPosition(), 1.f), .View = scene_camera->GetViewMatrix(), .Projection = scene_camera->GetProjectionMatrix()};

        m_UBOCamera->SetData(&ubo_camera_data, sizeof(Contracts::UBOCameraLayout));
    }

    void GraphicRenderer::Submit(uint32_t mesh_idx)
    {
        if (!m_scene_information.GraphicScenePtr)
        {
            return;
        }
        auto  scene_ptr = reinterpret_cast<Rendering::Scenes::GraphicScene*>(m_scene_information.GraphicScenePtr);
        auto& mesh      = scene_ptr->GetMesh(mesh_idx);

        auto pass_pipeline = m_final_color_output_pass->GetPipeline();
        pass_pipeline->SetUniformBuffer(m_UBOCamera, 0, 0);
        pass_pipeline->SetUniformBuffer(mesh.GetUniformBuffer(), 1, 0);

        auto command_buffer = m_command_pool->GetCurrentCommmandBuffer();
        command_buffer->Begin();
        {

            BeginRenderPass(*command_buffer, m_final_color_output_pass);
            {
                mesh.UpdateUniformBuffers(); // ToDo : Should be revisted
                mesh.Draw(command_buffer->GetHandle());
            }
            EndRenderPass(*command_buffer);
        }
        command_buffer->End();
        command_buffer->Submit();
    }

    void GraphicRenderer::EndScene()
    {
    }

    Contracts::FramebufferViewLayout GraphicRenderer::GetOutputImage(uint32_t frame_index)
    {
        // if (m_standard_pipeline.Pipeline)
        //{
        //     return Contracts::FramebufferViewLayout{.Handle = m_standard_pipeline.FragmentDescriptorSetCollection};
        // }
        return {};
    }
} // namespace ZEngine::Rendering::Renderers
