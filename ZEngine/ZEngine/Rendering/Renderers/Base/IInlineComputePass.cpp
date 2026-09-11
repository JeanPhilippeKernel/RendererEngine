#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>

namespace ZEngine::Rendering::Renderers
{
    void IInlineComputePass::Setup(Hardwares::VulkanDevicePtr const device, cstring /*name*/, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr /*res_inspector*/)
    {
        SetupCompute(device, res_builder);
    }

    void IInlineComputePass::Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass** const output_pass)
    {
        if (!output_pass || *output_pass)
            return;

        auto spec    = pass_builder->UseComputeShader(GetShaderName(), GetPushConstantSize()).Detach();
        *output_pass = device->CreateRenderPass(std::move(spec));
        static_cast<RenderPasses::ComputePass*>(*output_pass)->Bake();
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
