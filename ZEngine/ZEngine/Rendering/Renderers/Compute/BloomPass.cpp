#include <ZEngine/Rendering/Renderers/Compute/BloomPass.h>

namespace ZEngine::Rendering::Renderers
{
    void BloomPass::SetupCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceBuilderPtr const /*res_builder*/) {}

    void BloomPass::ExecuteCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const /*command_buffer*/) {}
} // namespace ZEngine::Rendering::Renderers
