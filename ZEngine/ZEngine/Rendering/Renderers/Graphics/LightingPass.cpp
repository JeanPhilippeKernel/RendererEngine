#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/Contracts/RendererDataContract.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/LightingPass.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void LightingPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadTexture(RendererResourceName::GBufferAlbedoAOName, "GBufferAlbedoAO");
        res_builder->ReadTexture(RendererResourceName::GBufferNormalRoughnessName, "GBufferNormalRoughness");
        res_builder->ReadTexture(RendererResourceName::GBufferMetallicEmissiveName, "GBufferMetallicEmissive");
        res_builder->ReadTexture(RendererResourceName::FrameDepthRenderTargetName, "GBufferDepth");
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM, .LoadOp = LoadOperation::LOAD});
    }

    void LightingPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Deferred-Lighting-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(false).UseShader("deferred_lighting").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        (*output_pass)->SetDynamicUniform("UBCamera", sizeof(Contracts::UBOCameraLayout));

        auto albedo_ao_handle     = res_inspector->GetRenderTarget(RendererResourceName::GBufferAlbedoAOName);
        auto normal_rough_handle  = res_inspector->GetRenderTarget(RendererResourceName::GBufferNormalRoughnessName);
        auto metallic_emit_handle = res_inspector->GetRenderTarget(RendererResourceName::GBufferMetallicEmissiveName);
        auto depth_handle         = res_inspector->GetRenderTarget(RendererResourceName::FrameDepthRenderTargetName);

        if (albedo_ao_handle.Valid())
            (*output_pass)->SetTexture("GBufferAlbedoAO", albedo_ao_handle);
        if (normal_rough_handle.Valid())
            (*output_pass)->SetTexture("GBufferNormalRoughness", normal_rough_handle);
        if (metallic_emit_handle.Valid())
            (*output_pass)->SetTexture("GBufferMetallicEmissive", metallic_emit_handle);
        if (depth_handle.Valid())
            (*output_pass)->SetTexture("GBufferDepth", depth_handle);

        if (scene && scene->LightBuffer.Handle)
            (*output_pass)->SetStorageBuffer("LightSB", &scene->LightBuffer);

        (*output_pass)->SetSampler("GBufferSampler", device->GlobalLinearWrapSamplerImageInfo);
        (*output_pass)->Verify();
    }

    void LightingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
