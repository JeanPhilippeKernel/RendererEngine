#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void LightingPass::SetEnvironmentLighting(const EnvironmentLightingResources& lighting, const Rendering::Scenes::SkyConfig& config)
    {
        m_environment_lighting = lighting;
        m_sky_config           = config;
    }

    bool LightingPass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!m_environment_lighting.Valid())
            return false;

        const uint32_t w = frame_context.RenderWidth != 0 ? frame_context.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t h = frame_context.RenderHeight != 0 ? frame_context.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadBuffer(RendererBufferName::Light, "LightSB");
        res_builder->ReadTexture(RendererResourceName::GBufferAlbedoAOName, "GBufferAlbedoAO");
        res_builder->ReadTexture(RendererResourceName::GBufferNormalRoughnessName, "GBufferNormalRoughness");
        res_builder->ReadTexture(RendererResourceName::GBufferMetallicEmissiveName, "GBufferMetallicEmissive");
        res_builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "GBufferDepth");
        res_builder->ReadTexture(res_builder->ImportTexture("DiffuseIrradiance", m_environment_lighting.DiffuseIrradiance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->ReadTexture(res_builder->ImportTexture("SpecularEnvironment", m_environment_lighting.SpecularEnvironment, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->ReadTexture(res_builder->ImportTexture("BrdfIntegrationLut", m_environment_lighting.BrdfIntegrationLut, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL));
        res_builder->WriteColorAttachment(
            RendererResourceName::FrameHdrColorRenderTargetName,
            {
            .IsUsageSampled = true, .IsRenderTargetSized = true, .Width = w, .Height = h, .BytePerPixel = sizeof(uint16_t) * 4, .Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT, .LoadOp = LoadOperation::CLEAR, .ClearColor = {0.11f, 0.11f, 0.11f, 1.0f}
        });

        return true;
    }

    Specifications::GraphicsPipelineDesc LightingPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Deferred-Lighting-Pipeline";
        desc.EnableDepthTest                      = false;
        desc.ShaderSpecificationValue.Name        = "deferred_lighting";
        return desc;
    }

    void LightingPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        if (!pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));

        auto albedo_ao_handle     = res_inspector->GetRenderTarget(RendererResourceName::GBufferAlbedoAOName);
        auto normal_rough_handle  = res_inspector->GetRenderTarget(RendererResourceName::GBufferNormalRoughnessName);
        auto metallic_emit_handle = res_inspector->GetRenderTarget(RendererResourceName::GBufferMetallicEmissiveName);
        auto depth_handle         = res_inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName);

        if (albedo_ao_handle.Valid())
            gp->SetTexture("GBufferAlbedoAO", albedo_ao_handle);
        if (normal_rough_handle.Valid())
            gp->SetTexture("GBufferNormalRoughness", normal_rough_handle);
        if (metallic_emit_handle.Valid())
            gp->SetTexture("GBufferMetallicEmissive", metallic_emit_handle);
        if (depth_handle.Valid())
            gp->SetTexture("GBufferDepth", depth_handle);

        gp->SetTexture("DiffuseIrradiance", m_environment_lighting.DiffuseIrradiance);
        gp->SetTexture("SpecularEnvironment", m_environment_lighting.SpecularEnvironment);
        gp->SetTexture("BrdfIntegrationLut", m_environment_lighting.BrdfIntegrationLut);

        gp->SetSampler("GBufferSampler", device->GlobalLinearWrapSamplerImageInfo);
        gp->SetSampler("EnvironmentSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool LightingPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        EnvironmentLightingPushConstants environment = {};
        for (uint32_t index = 0; index < 3; ++index)
            environment.TintIntensity[index] = m_sky_config.EnvironmentTint[index] * m_sky_config.EnvironmentIntensity;
        environment.YawRadians     = m_sky_config.EnvironmentYawRadians;
        environment.SpecularMaxLod = m_environment_lighting.SpecularMipCount > 0 ? static_cast<float>(m_environment_lighting.SpecularMipCount - 1) : 0.0f;
        command_buffer->PushConstants(VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(environment), &environment);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
