#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/CompositePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void CompositePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        res_builder->ReadTexture("gbuffer_albedo_render_target", "sharedRTAsTex");
        res_builder->WriteColorAttachment(RendererResourceName::FrameColorRenderTargetName, {});
    }

    void CompositePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Composite-Pipeline").SetInputBindingCount(0).EnablePipelineDepthTest(false).UseShader("composite").Detach();
            *output_pass   = device->CreateRenderPass(std::move(pass_spec));
            (*output_pass)->Bake();
        }
        auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
        gp->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
        gp->Verify();
    }

    void CompositePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, nullptr, 0u);
        command_buffer->Draw(3, 1, 0, 0);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
