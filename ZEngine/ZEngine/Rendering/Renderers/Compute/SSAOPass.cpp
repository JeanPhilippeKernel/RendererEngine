#include <ZEngine/Rendering/Renderers/Compute/SSAOPass.h>

namespace ZEngine::Rendering::Renderers
{
    void SSAOPass::SetupCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceBuilderPtr const /*res_builder*/) {}

    void SSAOPass::ExecuteCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const /*command_buffer*/) {}
} // namespace ZEngine::Rendering::Renderers
