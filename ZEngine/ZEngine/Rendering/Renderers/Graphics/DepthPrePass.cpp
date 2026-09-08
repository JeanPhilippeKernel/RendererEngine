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
            (*output_pass)->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
            // VertexSB/IndexSB/DrawDataSB/TransformSB bound by UpdateRMMBindings via BufferView*.
            (*output_pass)->Verify();
        }
    }

    void DepthPrePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
        {
            return;
        }

        command_buffer->BeginRenderPass(pass, framebuffer->Handle, false);
        {
            uint32_t w = pass->GetRenderAreaWidth();
            uint32_t h = pass->GetRenderAreaHeight();
            command_buffer->SetViewport(w, h);
            command_buffer->SetScissor(w, h);
        }
        command_buffer->BindPipeline(Specifications::PipelineBindPoint::GRAPHIC, pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->DrawIndirect(device->FrameHeaps[device->SwapchainPtr->CurrentFrame->Index].Handle, scene->IndirectHeapOffset, scene->IndirectCommandCount);
        command_buffer->EndRenderPass();
    }
} // namespace ZEngine::Rendering::Renderers
