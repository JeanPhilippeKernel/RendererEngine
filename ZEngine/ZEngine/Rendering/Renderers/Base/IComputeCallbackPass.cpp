#include <ZEngine/Rendering/Renderers/Base/IComputeCallbackPass.h>

namespace ZEngine::Rendering::Renderers
{
    void IComputeCallbackPass::Setup(Hardwares::VulkanDevicePtr const device, cstring /*name*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        SetupCompute(device, res_builder);
    }

    void IComputeCallbackPass::Compile(Hardwares::VulkanDevicePtr const /*device*/, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPassBuilder* /*pass_builder*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass** const /*output_pass*/)
    {
        // TODO(compute-pipeline.md §5): construct ComputePassBuilder, call UseShader(GetShaderName()),
        // optionally SetPushConstantRange(GetPushConstantSize()), then create and Bake() a
        // COMPUTE RenderPass and write it to *output_pass.
    }

    void IComputeCallbackPass::Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const /*pass*/, Buffers::FramebufferVNext* const /*framebuffer — always null for compute passes*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        // TODO(compute-pipeline.md §4): bind compute pipeline via
        //   vkCmdBindPipeline(command_buffer->GetHandle(), VK_PIPELINE_BIND_POINT_COMPUTE,
        //                      pass->ComputePipeline->Handle);
        // then delegate:
        //   ExecuteCompute(device, res_inspector, scene,
        //                  pass->ComputePipeline->Handle,
        //                  pass->ComputePipeline->Layout, command_buffer);
        ExecuteCompute(device, res_inspector, scene, VK_NULL_HANDLE, VK_NULL_HANDLE, command_buffer);
    }

} // namespace ZEngine::Rendering::Renderers
