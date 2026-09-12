#pragma once
// GPU frustum culling with a fixed-size indirect list. Culled commands carry
// instanceCount = 0, avoiding the non-portable draw-indirect-count extension.
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>

namespace ZEngine::Rendering::Renderers
{
    struct FrustumCullingPass final : public IInlineComputePass
    {
        cstring GetShaderName() const override
        {
            return "frustum_cull_compute";
        }

        uint32_t GetPushConstantSize() const override
        {
            return sizeof(Rendering::Scenes::FrustumCullingPushConstants);
        }

        void RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;

        void ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
