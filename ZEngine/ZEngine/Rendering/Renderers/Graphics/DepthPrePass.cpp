#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/DepthPrePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    bool DepthPrePass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        const uint32_t w = frame_context.RenderWidth != 0 ? frame_context.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t h = frame_context.RenderHeight != 0 ? frame_context.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadBuffer(RendererBufferName::GlobalVertex, "VertexSB");
        res_builder->ReadBuffer(RendererBufferName::GlobalIndex, "IndexSB");
        res_builder->ReadBuffer(RendererBufferName::Transform, "TransformSB");
        res_builder->ReadBuffer(RendererBufferName::RenderData, "DrawDataSB");
        res_builder->ReadIndirectBuffer(RendererBufferName::CulledIndirect);
        res_builder->WriteDepthAttachment(RendererResourceName::FrameDepthRenderTargetName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::DEPTH_STENCIL_FROM_DEVICE});
        return true;
    }

    Specifications::GraphicsPipelineDesc DepthPrePass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Depth-Prepass-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.ShaderSpecificationValue.Name        = "depth_prepass_scene";
        return desc;
    }

    void DepthPrePass::Prepare(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void DepthPrePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false);
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool DepthPrePass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0 || !scene->RMMVertexHandle.IsValid())
            return false;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
        command_buffer->DrawIndirect(scene->CulledIndirectBuffers[device->SwapchainPtr->CurrentFrame->Index].Handle, 0, scene->IndirectCommandCount);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
