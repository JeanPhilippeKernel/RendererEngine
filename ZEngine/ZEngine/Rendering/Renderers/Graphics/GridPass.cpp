#include <ZEngine/ECS/SceneGridSettings.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>
#include <cmath>

namespace ZEngine::Rendering::Renderers
{
    GridPass::GridPass()
    {
        SetConfig({});
    }

    bool GridPass::Pack(const Scenes::SceneGridRenderSnapshot& config, bool reverse_z, GridPushConstantData& output)
    {
        ECS::SceneGridSettings value = {};
        if (!Scenes::ReadSceneGridRenderSnapshot(config, value))
            return false;
        GridPushConstantData push = {};
        // Log-space positive values survive GPU denormal flushing and avoid
        // overflowing reciprocal spacing/LOD intermediates for valid extremes.
        push.OriginAndLogSpacing  = {value.Origin.x, value.Origin.y, value.Origin.z, float(std::log2(double(value.BaseSpacing)))};
        const auto& q             = value.Orientation;
        push.Orientation          = {q.x, q.y, q.z, q.w};
        push.Style                = {float(std::log2(double(value.FadeRadius))), value.FadeStrength, value.LineWidth, float(value.MaxLOD)};
        push.Controls             = {value.MajorLineEvery, reverse_z ? 1u : 0u, uint32_t(value.Labels), 0u};
        for (uint32_t i = 0; i < 4; ++i)
        {
            push.ColorThin[i]  = value.MinorColor[i];
            push.ColorThick[i] = value.MajorColor[i];
            push.ColorUAxis[i] = value.UColor[i];
            push.ColorVAxis[i] = value.VColor[i];
        }
        output = push;
        return true;
    }

    void GridPass::SetConfig(const Scenes::SceneGridRenderSnapshot& config)
    {
        m_enabled = config.Enabled && Pack(config, m_reverse_z, m_push);
    }

    void GridPass::SetCameraDepthConvention(bool reverse_z)
    {
        m_reverse_z        = reverse_z;
        m_push.Controls[1] = reverse_z ? 1u : 0u;
    }

    Specifications::GraphicsPipelineDesc GridPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator*) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Infinite-Grid-Pipeline";
        // Scene depth is sampled explicitly, so one immutable PSO works for
        // normal and reverse-Z. No depth attachment or depth write.
        desc.EnableDepthTest                      = false;
        desc.EnableDepthWrite                     = false;
        desc.EnableBlending                       = true;
        desc.CullMode                             = VK_CULL_MODE_NONE;
        desc.ShaderSpecificationValue.Name        = "infinite_grid";
        return desc;
    }

    bool GridPass::Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext& context, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        if (!m_enabled || !builder || !context.RenderWidth || !context.RenderHeight)
            return false;
        builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "SceneDepth");
        builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = Specifications::LoadOperation::LOAD});
        return true;
    }

    void GridPass::Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr scene, RenderGraphResourceInspectorPtr inspector, RenderPasses::RenderPass* pass)
    {
        if (!m_enabled || !device || !scene || !inspector || !pass)
            return;
        auto*      gp    = static_cast<RenderPasses::GraphicPass*>(pass);
        const auto depth = inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        if (depth.Valid())
            gp->SetTexture("SceneDepth", depth);
        // texelFetch does not filter: reuse this sampler, never allocate one.
        gp->SetSampler("DepthSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void GridPass::Execute(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext* framebuffer, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_enabled || !device || !scene || !pass || !command_buffer || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
        RecordDraw(device, inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GridPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr inspector, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr command_buffer)
    {
        if (!m_enabled || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !scene || !pass || !command_buffer || !inspector || !inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName).Valid())
            return false;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!gp->GetRenderAreaWidth() || !gp->GetRenderAreaHeight())
            return false;
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1);
        command_buffer->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(m_push), &m_push);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
