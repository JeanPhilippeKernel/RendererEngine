#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>

namespace ZEngine::Rendering::Renderers
{
    bool IInlineComputePass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        RegisterCompute(device, frame_context, res_builder);
        return true;
    }

    void IInlineComputePass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        auto* cp = static_cast<RenderPasses::ComputePass*>(pass);
        if (!cp || !cp->Pipeline || cp->Pipeline->Handle == VK_NULL_HANDLE)
            return;

        command_buffer->BindPipeline(cp->Pipeline);
        ExecuteCompute(device, res_inspector, scene, cp->Pipeline->Handle, cp->Pipeline->Layout, command_buffer);
    }

} // namespace ZEngine::Rendering::Renderers
