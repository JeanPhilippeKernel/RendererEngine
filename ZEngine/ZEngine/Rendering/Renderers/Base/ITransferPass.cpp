#include <ZEngine/Rendering/Renderers/Base/ITransferPass.h>

namespace ZEngine::Rendering::Renderers
{
    bool ITransferPass::Register(Hardwares::VulkanDevicePtr const device, cstring /*name*/, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        RegisterTransfer(device, frame_context, res_builder);
        return true;
    }

    void ITransferPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const /*pass*/, Buffers::FramebufferVNext* const /*framebuffer*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        RecordTransfer(device, res_inspector, scene, command_buffer);
    }
} // namespace ZEngine::Rendering::Renderers
