#include <ZEngine/Engine.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Helpers;
using namespace ZEngine::Rendering::Specifications;
using namespace ZEngine::Core::Containers;

namespace ZEngine::Rendering::Renderers
{
    Specifications::GraphicsPipelineDesc GridPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const
    {
        Specifications::GraphicsPipelineDesc desc = {};
        desc.DebugName                            = "Infinite-Grid-Pipeline";
        desc.EnableDepthTest                      = true;
        desc.EnableDepthWrite                     = false;
        desc.DepthCompareOp                       = 3;
        desc.EnableBlending                       = true;
        desc.CullMode                             = 0;
        desc.ShaderSpecificationValue.Name        = "infinite_grid";

        desc.VertexInputBindingSpecifications.init(arena, 1, 1);
        desc.VertexInputBindingSpecifications[0] = {.Stride = sizeof(float) * 8, .Rate = VK_VERTEX_INPUT_RATE_VERTEX, .Binding = 0};
        desc.VertexInputAttributeSpecifications.init(arena, 1, 1);
        desc.VertexInputAttributeSpecifications[0] = {.Location = 0, .Binding = 0, .Offset = 0, .Format = Specifications::ImageFormat::R32G32B32_SFLOAT};
        return desc;
    }

    bool GridPass::Register(Hardwares::VulkanDevicePtr const /*device*/, cstring /*name*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!Enabled)
            return false;

        // Quad vertex layout: position, upward normal, and XZ UVs.
        static constexpr float verts[] = {
            -1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1000.f, 0.f, -1000.f, 0.f, 1.f, 0.f, 1.f, 0.f, 1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 1.f, 1.f, -1000.f, 0.f, 1000.f, 0.f, 1.f, 0.f, 0.f, 1.f,
        };
        static constexpr uint32_t idxs[] = {0, 1, 2, 2, 3, 0};

        if (!m_geometry_registered)
        {
            auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
            ZENGINE_VALIDATE_ASSERT(rrm, "GridPass::Register: RenderResourceManager not available")
            rrm->RegisterBuiltinGeometry(verts, sizeof(verts), idxs, 6, m_vtx_offset, m_idx_offset);
            m_geometry_registered = true;
        }

        res_builder->ReadDepth(RendererResourceName::FrameDepthRenderTargetName);
        res_builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    void GridPass::Prepare(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        if (!scene || !pass)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        gp->SetDynamicUniform("UBCamera", sizeof(UBOCameraLayout));
    }

    void GridPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!Enabled)
            return;

        auto* gp = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->BeginRenderPass(gp, framebuffer ? framebuffer->Handle : VK_NULL_HANDLE, false);
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool GridPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!Enabled)
            return false;

        auto* gp  = static_cast<RenderPasses::GraphicPass*>(pass);
        auto* rrm = ZEngine::Engine::GetContext()->RenderResourceManager;
        if (!rrm)
            return false;
        command_buffer->SetViewport(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->SetScissor(gp->GetRenderAreaWidth(), gp->GetRenderAreaHeight());
        command_buffer->BindPipeline(gp->Pipeline);
        command_buffer->BindVertexBuffer(*rrm->GetBuiltinVertexBuffer());
        command_buffer->BindIndexBuffer(*rrm->GetBuiltinIndexBuffer(), VK_INDEX_TYPE_UINT32);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index, scene ? &scene->CameraHeapOffset : nullptr, scene ? 1u : 0u);
        command_buffer->PushConstants(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(GridPushConstantData), &PushData);
        command_buffer->DrawIndexed(6, 1, m_idx_offset, static_cast<int32_t>(m_vtx_offset), 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
