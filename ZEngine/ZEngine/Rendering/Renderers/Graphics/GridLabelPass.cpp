#include <ZEngine/Rendering/Renderers/Graphics/GridLabelPass.h>

namespace ZEngine::Rendering::Renderers
{
    void GridLabelPass::SetFrame(const GridLabelFrameData& frame)
    {
        m_frame = frame.Count && ValidGridLabelFrame(frame) ? frame : GridLabelFrameData{};
        m_atlas = {};
        m_depth = {};
    }

    Specifications::GraphicsPipelineDesc GridLabelPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator*) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Grid-Labels-Pipeline";
        desc.EnableDepthTest                      = false;
        desc.EnableDepthWrite                     = false;
        desc.EnableBlending                       = true;
        desc.CullMode                             = VK_CULL_MODE_NONE;
        desc.ShaderSpecificationValue.Name        = "grid_labels";
        return desc;
    }

    bool GridLabelPass::Register(Hardwares::VulkanDevicePtr device, cstring, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        m_atlas = {};
        m_depth = {};
        if (!m_frame.Count || !builder || !context.RenderWidth || !context.RenderHeight || (device && !device->GlobalTextures.Access(m_frame.Atlas)))
            return false;
        const auto atlas = builder->ImportTexture(GridLabelAtlasName, m_frame.Atlas, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        m_atlas          = builder->ReadTexture(atlas, "FontAtlas");
        m_depth          = builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "SceneDepth");
        if (!m_atlas.Valid() || !m_depth.Valid())
            return false;
        builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = Specifications::LoadOperation::LOAD});
        return true;
    }

    void GridLabelPass::Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr, RenderGraphResourceInspectorPtr inspector, RenderPasses::RenderPass* pass)
    {
        if (!m_frame.Count || !device || !inspector || !pass || !m_atlas.Valid() || !m_depth.Valid())
            return;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetTexture("FontAtlas", inspector->GetTextureHandle(m_atlas));
        gp->SetTexture("SceneDepth", inspector->GetTextureHandle(m_depth));
        gp->SetSampler("AtlasSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void GridLabelPass::Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_frame.Count || !device || !pass || !command_buffer || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
        RecordDraw(device, inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GridLabelPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_frame.Count || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !pass || !command_buffer || !inspector || !inspector->GetTextureHandle(m_atlas).Valid() || !inspector->GetTextureHandle(m_depth).Valid())
            return false;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!gp->GetRenderAreaWidth() || !gp->GetRenderAreaHeight())
            return false;
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, nullptr, 0);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(m_frame.Push), &m_frame.Push);
        command_buffer->Draw(6, m_frame.Count, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
