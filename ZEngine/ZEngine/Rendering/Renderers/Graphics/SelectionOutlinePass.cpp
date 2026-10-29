#include <ZEngine/Rendering/Renderers/Graphics/SelectionOutlinePass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    bool SelectionOutlinePass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!m_enabled || !device || !res_builder || frame_context.RenderWidth == 0 || frame_context.RenderHeight == 0)
        {
            return false;
        }
        m_object_id = res_builder->ReadTexture(RendererResourceName::GBufferObjectIdName, "ObjectId");
        res_builder->ReadBuffer(RendererBufferName::SelectionTokenState, "SelectionTokenStates");
        res_builder->UpdateColorAttachment(RendererResourceName::FrameColorRenderTargetName, {.LoadOp = LoadOperation::LOAD});
        return true;
    }

    GraphicsPipelineDesc SelectionOutlinePass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        GraphicsPipelineDesc description          = {};
        description.DebugName                     = "Selection-Outline-Pipeline";
        description.EnableDepthTest               = false;
        description.EnableDepthWrite              = false;
        description.EnableBlending                = true;
        description.CullMode                      = VK_CULL_MODE_NONE;
        description.ShaderSpecificationValue.Name = "selection_outline";
        return description;
    }

    void SelectionOutlinePass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        if (!m_enabled || !device || !pass)
        {
            return;
        }
        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        const auto  object_id    = res_inspector ? res_inspector->GetTextureHandle(m_object_id) : Textures::TextureHandle{};
        if (object_id.Valid())
        {
            graphic_pass->SetTexture("ObjectId", object_id);
        }
        graphic_pass->SetSampler("ObjectIdSampler", device->GlobalNearestClampToEdgeSamplerImageInfo);
    }

    void SelectionOutlinePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_enabled || !pass || !command_buffer || !framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
        {
            return;
        }
        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(graphic_pass, framebuffer->Handle, false))
        {
            return;
        }
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool SelectionOutlinePass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!m_enabled || !device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !pass || !command_buffer)
        {
            return false;
        }
        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (graphic_pass->GetRenderAreaWidth() == 0 || graphic_pass->GetRenderAreaHeight() == 0)
        {
            return false;
        }
        command_buffer->SetViewport(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->SetScissor(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->BindPipeline(graphic_pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
