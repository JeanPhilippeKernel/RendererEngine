#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void GbufferPass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::GBufferAlbedoAOName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferNormalRoughnessName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferMetallicEmissiveName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
    }

    void GbufferPass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("GBuffer-Pipeline").EnablePipelineDepthTest(true).UseShader("g_buffer").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }

        if (scene)
        {
            auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
            gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
            gp->UseTextureArray("TextureArray");
            gp->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
            gp->Verify();
        }
    }

    void GbufferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        CHECK_AND_ESCAPE_NULL(scene)

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        if (scene->IndirectCommandCount > 0 && scene->RMMVertexHandle.IsValid())
        {
            command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
            command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
            command_buffer->BindPipeline(gp->Pipeline);
            command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
            command_buffer->DrawIndirect(device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Handle, scene->IndirectHeapOffset, scene->IndirectCommandCount);
        }
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
