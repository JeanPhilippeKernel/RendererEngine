#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/GbufferPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    bool GbufferPass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        const uint32_t w = frame_context.RenderWidth != 0 ? frame_context.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t h = frame_context.RenderHeight != 0 ? frame_context.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        res_builder->ReadBuffer(RendererBufferName::GlobalVertex, "VertexSB");
        res_builder->ReadBuffer(RendererBufferName::GlobalIndex, "IndexSB");
        res_builder->ReadBuffer(RendererBufferName::Transform, "TransformSB");
        res_builder->ReadBuffer(RendererBufferName::RenderData, "DrawDataSB");
        res_builder->ReadBuffer(RendererBufferName::Material, "MatSB");
        res_builder->ReadIndirectBuffer(RendererBufferName::CulledIndirect);
        res_builder->ReadBindless();
        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->WriteColorAttachment(RendererResourceName::GBufferAlbedoAOName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferNormalRoughnessName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferMetallicEmissiveName, {.Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        return true;
    }

    Specifications::GraphicsPipelineDesc GbufferPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "GBuffer-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = false;
        desc.ShaderSpecificationValue.Name        = "g_buffer";
        return desc;
    }

    void GbufferPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
        gp->UseTextureArray("TextureArray");
        gp->SetSampler("LinearWrapSampler", device->GlobalLinearWrapSamplerImageInfo);
    }

    void GbufferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        CHECK_AND_ESCAPE_NULL(scene)

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false);
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GbufferPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene)
            return false;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (scene->IndirectCommandCount > 0 && scene->RMMVertexHandle.IsValid())
        {
            command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
            command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
            command_buffer->BindPipeline(gp->Pipeline);
            command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, &scene->CameraHeapOffset, 1u);
            command_buffer->DrawIndirect(scene->CulledIndirectBuffers[device->SwapchainPtr->CurrentFrame->Index].Handle, 0, scene->IndirectCommandCount);
        }
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
