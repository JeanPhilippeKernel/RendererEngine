#include <ZEngine/Rendering/Renderers/Graphics/TransparentPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

namespace ZEngine::Rendering::Renderers
{
    bool TransparentPass::Register(Hardwares::VulkanDevicePtr, cstring, const RenderGraphFrameContext&, RenderGraphResourceBuilderPtr builder, RenderGraphResourceInspectorPtr)
    {
        if (!builder || !m_environment_lighting.Valid())
            return false;
        builder->ReadBuffer(RendererBufferName::GlobalVertex, "VertexSB");
        builder->ReadBuffer(RendererBufferName::GlobalIndex, "IndexSB");
        builder->ReadBuffer(RendererBufferName::Transform, "TransformSB");
        builder->ReadBuffer(RendererBufferName::RenderData, "DrawDataSB");
        builder->ReadBuffer(RendererBufferName::Material, "MatSB");
        builder->ReadBuffer(RendererBufferName::Light, "LightSB");
        builder->ReadIndirectBuffer(RendererBufferName::CulledIndirect);
        builder->ReadBindless();
        builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        builder->ReadTexture(builder->ImportTexture("DiffuseIrradiance", m_environment_lighting.DiffuseIrradiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        builder->ReadTexture(builder->ImportTexture("SpecularEnvironment", m_environment_lighting.SpecularEnvironment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        builder->ReadTexture(builder->ImportTexture("BrdfIntegrationLut", m_environment_lighting.BrdfIntegrationLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        builder->UpdateColorAttachment(m_use_composited ? RendererResourceName::FrameHdrCompositedRenderTargetName : RendererResourceName::FrameHdrColorRenderTargetName, {.LoadOp = Specifications::LoadOperation::LOAD});
        return true;
    }

    bool TransparentPass::TryBuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena, Specifications::GraphicsPipelineDesc* out) const
    {
        if (!out)
        {
            return false;
        }
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Forward-Transparent-Pipeline";
        desc.EnableBlending                       = true;
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = false;
        // Match the existing scene and preview geometry policy. Authored
        // double-sidedness is not represented by the current material schema.
        desc.CullMode                             = VK_CULL_MODE_NONE;
        desc.ShaderSpecificationValue.Name        = "forward_transparent";
        *out                                      = std::move(desc);
        return true;
    }

    void TransparentPass::Prepare(Hardwares::VulkanDevicePtr device, Scenes::SceneDataPtr, RenderGraphResourceInspectorPtr, RenderPasses::RenderPass* pass)
    {
        if (!device || !pass)
            return;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        gp->UseTextureArray("TextureArray");
        gp->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
        gp->SetTexture("DiffuseIrradiance", m_environment_lighting.DiffuseIrradiance);
        gp->SetTexture("SpecularEnvironment", m_environment_lighting.SpecularEnvironment);
        gp->SetTexture("BrdfIntegrationLut", m_environment_lighting.BrdfIntegrationLut);
        gp->SetSampler("EnvironmentSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    bool TransparentPass::RecordDraw(Hardwares::VulkanDevicePtr device, RenderGraphResourceInspectorPtr, Scenes::SceneDataPtr scene, RenderPasses::RenderPass* pass, Buffers::FramebufferVNext*, Hardwares::CommandBufferPtr commands)
    {
        if (!scene || !scene->TransparentIndirectCommandCount || !scene->RMMVertexHandle.IsValid())
            return true;
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        commands->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        commands->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        commands->BindPipeline(gp->Pipeline);
        commands->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        EnvironmentLightingPushConstants environment = {};
        for (uint32_t index = 0; index < 3; ++index)
            environment.TintIntensity[index] = m_sky_config.EnvironmentTint[index] * m_sky_config.EnvironmentIntensity;
        environment.YawRadians        = m_sky_config.EnvironmentYawRadians;
        environment.SpecularMaxLod    = m_environment_lighting.SpecularMipCount ? static_cast<float>(m_environment_lighting.SpecularMipCount - 1) : 0.f;
        environment.DiffuseIntensity  = m_sky_config.SkyDiffuseIntensity;
        environment.SpecularIntensity = m_sky_config.SkySpecularIntensity;
        commands->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(environment), &environment);
        commands->DrawIndirect(scene->GetCulledIndirectBuffer(device->SwapchainPtr->CurrentFrame->Index).Handle, scene->OpaqueIndirectCommandCount * sizeof(VkDrawIndirectCommand), scene->TransparentIndirectCommandCount);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
