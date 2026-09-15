#include <ZEngine/Rendering/Renderers/Graphics/ToneMappingPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    void ToneMappingPass::SetUseCompositedSceneColor(bool enabled)
    {
        m_use_composited_scene_color = enabled;
    }

    bool ToneMappingPass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        if (!device || !res_builder)
            return false;

        const uint32_t width  = frame_context.RenderWidth != 0 ? frame_context.RenderWidth : device->SwapchainPtr->SwapchainImageWidth;
        const uint32_t height = frame_context.RenderHeight != 0 ? frame_context.RenderHeight : device->SwapchainPtr->SwapchainImageHeight;
        if (width == 0 || height == 0)
            return false;

        res_builder->ReadTexture(m_use_composited_scene_color ? RendererResourceName::FrameHdrCompositedRenderTargetName : RendererResourceName::FrameHdrColorRenderTargetName, "SceneColor");
        res_builder->WriteColorAttachment(
            RendererResourceName::FrameColorRenderTargetName,
            {
            .IsUsageSampled      = true,
            .IsRenderTargetSized = true,
            .Width               = width,
            .Height              = height,
            .Format              = ImageFormat::R8G8B8A8_UNORM,
            .LoadOp              = LoadOperation::CLEAR,
            .ClearColor          = {0.0f, 0.0f, 0.0f, 1.0f},
        });
        return true;
    }

    GraphicsPipelineDesc ToneMappingPass::BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* /*arena*/) const
    {
        GraphicsPipelineDesc description          = {};
        description.DebugName                     = "Tone-Mapping-Pipeline";
        description.EnableDepthTest               = false;
        description.EnableBlending                = false;
        description.ShaderSpecificationValue.Name = "tone_mapping";
        return description;
    }

    void ToneMappingPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        if (!device || !res_inspector || !pass)
            return;

        const Textures::TextureHandle scene_color = res_inspector->GetRenderTarget(m_use_composited_scene_color ? RendererResourceName::FrameHdrCompositedRenderTargetName : RendererResourceName::FrameHdrColorRenderTargetName);
        if (!scene_color.Valid())
            return;

        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        graphic_pass->SetTexture("SceneColor", scene_color);
        graphic_pass->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    void ToneMappingPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!framebuffer || framebuffer->Handle == VK_NULL_HANDLE)
            return;

        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        if (!command_buffer->BeginRenderPass(graphic_pass, framebuffer->Handle, false))
            return;
        RecordDraw(device, res_inspector, scene, pass, framebuffer, command_buffer);
        command_buffer->EndRenderPass();
    }

    bool ToneMappingPass::RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* const graphic_pass = static_cast<RenderPasses::GraphicPass*>(pass);
        command_buffer->SetViewport(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->SetScissor(graphic_pass->GetRenderAreaWidth(), graphic_pass->GetRenderAreaHeight());
        command_buffer->BindPipeline(graphic_pass->Pipeline);
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->Draw(3, 1, 0, 0);
        return true;
    }
} // namespace ZEngine::Rendering::Renderers
