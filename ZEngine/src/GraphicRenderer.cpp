#include <pch.h>
#include <Rendering/Renderers/GraphicRenderer.h>
#include <Rendering/Specifications/FrameBufferSpecification.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    uint32_t                                                        GraphicRenderer::s_viewport_width           = 1;
    uint32_t                                                        GraphicRenderer::s_viewport_height          = 1;
    Ref<Buffers::IndirectBuffer>                                    GraphicRenderer::s_indirect_buffer          = CreateRef<Buffers::IndirectBuffer>();
    std::array<Ref<Buffers::FramebufferVNext>, RenderTarget::Count> GraphicRenderer::s_render_target_collection = {};

    const Ref<GraphicRendererInformation>& GraphicRenderer::GetRendererInformation() const
    {
        return m_renderer_information;
    }

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

    void GraphicRenderer::Initialize()
    {
        s_render_target_collection[RenderTarget::FRAME_OUTPUT] = CreateRef<Buffers::FramebufferVNext>(FrameBufferSpecificationVNext{
            .Width = 1000, .Height = 1000, .AttachmentSpecifications = {Specifications::ImageFormat::R8G8B8A8_UNORM, Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE}});
    }

    void GraphicRenderer::Deinitialize()
    {
        s_render_target_collection.fill(nullptr);
        s_indirect_buffer->Dispose();
    }

    void GraphicRenderer::BeginRenderPass(Buffers::CommandBuffer* const command_buffer, const Ref<RenderPasses::RenderPass>& render_pass)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer != nullptr, "Command buffer can't be null")

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

        vkCmdBeginRenderPass(command_buffer->GetHandle(), &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

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

        vkCmdSetViewport(command_buffer->GetHandle(), 0, 1, &viewport);
        vkCmdSetScissor(command_buffer->GetHandle(), 0, 1, &scissor);
        vkCmdBindPipeline(command_buffer->GetHandle(), VK_PIPELINE_BIND_POINT_GRAPHICS, render_pass_pipeline->GetHandle());

        render_pass_pipeline->UpdateDescriptorSets();

        std::vector<VkDescriptorSet> descriptorset_collection = {};
        auto                         descriptor_set           = render_pass_pipeline->GetDescriptorSet();
        if (descriptor_set)
        {
            descriptorset_collection.push_back(descriptor_set);
        }

        if (!descriptorset_collection.empty())
        {
            vkCmdBindDescriptorSets(
                command_buffer->GetHandle(),
                VK_PIPELINE_BIND_POINT_GRAPHICS,
                render_pass_pipeline->GetPipelineLayout(),
                0,
                descriptorset_collection.size(),
                descriptorset_collection.data(),
                0,
                nullptr);
        }
    }

    void GraphicRenderer::EndRenderPass(Buffers::CommandBuffer* const command_buffer)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer != nullptr, "Command buffer can't be null")

        vkCmdEndRenderPass(command_buffer->GetHandle());
    }

    void GraphicRenderer::RenderGeometry(Buffers::CommandBuffer* const command_buffer, const std::vector<VkDrawIndirectCommand>& command_collection)
    {
        ZENGINE_VALIDATE_ASSERT(command_buffer != nullptr, "Command buffer can't be null")

        if (!command_collection.empty())
        {
            s_indirect_buffer->SetData(command_collection);
            vkCmdDrawIndirect(
                command_buffer->GetHandle(), reinterpret_cast<VkBuffer>(s_indirect_buffer->GetNativeBufferHandle()), 0, command_collection.size(), sizeof(VkDrawIndirectCommand));
        }
    }
} // namespace ZEngine::Rendering::Renderers
