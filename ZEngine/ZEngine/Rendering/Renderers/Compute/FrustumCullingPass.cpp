#include <ZEngine/Rendering/Renderers/Compute/FrustumCullingPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

namespace ZEngine::Rendering::Renderers
{
    void FrustumCullingPass::SetupCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        res_builder->ReadBuffer(RendererBufferName::CullingInput, "CullingInputSB");
        res_builder->ReadWriteBuffer(RendererBufferName::CulledIndirect, "CulledIndirectSB");
    }

    void FrustumCullingPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr scene, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!scene || scene->IndirectCommandCount == 0)
            return;

        ZENGINE_VALIDATE_ASSERT(scene->IndirectCommandCount <= Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS, "Frustum culling command count exceeds its fixed GPU buffer capacity")
        auto push      = scene->CullingPushConstants;
        push.DrawCount = scene->IndirectCommandCount;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((push.DrawCount + 63u) / 64u, 1, 1);
    }
} // namespace ZEngine::Rendering::Renderers
