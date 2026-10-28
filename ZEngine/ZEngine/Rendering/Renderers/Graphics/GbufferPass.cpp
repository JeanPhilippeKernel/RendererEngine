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
        res_builder->WriteColorAttachment(RendererResourceName::GBufferAlbedoAOName, {.IsRenderTargetSized = true, .Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferNormalRoughnessName, {.IsRenderTargetSized = true, .Width = w, .Height = h, .Format = Specifications::ImageFormat::R16G16B16A16_SFLOAT});
        res_builder->WriteColorAttachment(RendererResourceName::GBufferMetallicEmissiveName, {.IsRenderTargetSized = true, .Width = w, .Height = h, .Format = Specifications::ImageFormat::R8G8B8A8_UNORM});
        // A discrete attachment keeps selection tokens exact and reuses the same
        // GPU-driven indirect geometry records as the material G-buffer.
        const RGResourceHandle object_id = res_builder->WriteColorAttachment(RendererResourceName::GBufferObjectIdName, {.IsRenderTargetSized = true, .Width = w, .Height = h, .Format = Specifications::ImageFormat::R32_UINT});
        for (uint32_t request_index = 0; request_index < m_object_id_pick_count; ++request_index)
        {
            const ObjectIdPickRequest& request = m_object_id_picks[request_index];
            if (!request.Valid() || request.X >= w || request.Y >= h)
            {
                if (request.Cancelled)
                {
                    request.Cancelled(request.Context);
                }
                continue;
            }
            const RGImageReadbackRegion region = {
                .Subresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                .Offset      = {static_cast<int32_t>(request.X), static_cast<int32_t>(request.Y), 0},
                .Extent      = {1, 1, 1},
            };
            const cstring          readback_name = request_index == 0 ? "ObjectIdPickReadback" : "ObjectIdHoverReadback";
            const RGReadbackHandle handle        = res_builder->DeclareImageReadback(readback_name, object_id, region, request.Callback, request.Context, sizeof(uint32_t), request.Cancelled);
            if (!handle.Valid() && request.Cancelled)
            {
                request.Cancelled(request.Context);
            }
        }
        // Requests are one-shot. A failed graph compilation cancels the ring lease;
        // callers can issue another request instead of replaying a stale click.
        m_object_id_picks[0]   = {};
        m_object_id_picks[1]   = {};
        m_object_id_pick_count = 0;
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
        if (!framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(gp, framebuffer->Handle, false))
            return;
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
