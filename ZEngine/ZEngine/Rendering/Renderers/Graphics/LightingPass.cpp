#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    bool LightingPass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        const uint32_t w = frame_context.RenderWidth != 0 ? frame_context.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t h = frame_context.RenderHeight != 0 ? frame_context.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadBuffer(RendererBufferName::Light, "LightSB");
        res_builder->ReadTexture(RendererResourceName::GBufferAlbedoAOName, "GBufferAlbedoAO");
        res_builder->ReadTexture(RendererResourceName::GBufferNormalRoughnessName, "GBufferNormalRoughness");
        res_builder->ReadTexture(RendererResourceName::GBufferMetallicEmissiveName, "GBufferMetallicEmissive");
        res_builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "GBufferDepth");
        res_builder->WriteColorAttachment(
            RendererResourceName::FrameColorRenderTargetName,
            {
            // FrameColor is exposed through the editor viewport, so retain sampled
            // usage even in graph configurations that omit the ZUI consumer.
            .Width          = w,
            .Height         = h,
            .Format         = Specifications::ImageFormat::R8G8B8A8_UNORM,
            .IsUsageSampled = true,
            .LoadOp         = LoadOperation::CLEAR,
            .ClearColor     = {0.11f, 0.11f, 0.11f, 1.0f}
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

        gp->SetSampler("GBufferSampler", device->GlobalLinearWrapSamplerImageInfo);
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false);
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
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
