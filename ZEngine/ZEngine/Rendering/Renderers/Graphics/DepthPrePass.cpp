#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    void DepthPrePass::Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector)
    {
        uint32_t w = device->SwapchainPtr->SwapchainImageWidth;
        uint32_t h = device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadBuffer(RendererBufferName::Transform, "TransformSB");
        res_builder->ReadBuffer(RendererBufferName::RenderData, "DrawDataSB");
        res_builder->ReadIndirectBuffer(RendererBufferName::CulledIndirect);
        res_builder->WriteDepthAttachment(RendererResourceName::FrameDepthRenderTargetName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
    }

    void DepthPrePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass)
    {
        CHECK_AND_ESCAPE_NULL(output_pass)

        if (output_pass && !(*output_pass))
        {
            auto pass_spec = pass_builder->SetPipelineName("Depth-Prepass-Pipeline")
                                 .EnablePipelineDepthTest(true)

                                 .UseShader("depth_prepass_scene")
                                 .Detach();
            // clang-format off
            *output_pass = device->CreateRenderPass(std::move(pass_spec));
            // clang-format on
            (*output_pass)->Bake();
        }

        if (scene)
        {
            auto* gp = static_cast<RenderPasses::GraphicPass*>(*output_pass);
            gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
            gp->Verify();
        }
    }

    void DepthPrePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer->Handle, false);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        command_buffer->DrawIndirect(scene->CulledIndirectBuffers[device->SwapchainPtr->CurrentFrame->Index].Handle, 0, scene->IndirectCommandCount);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
