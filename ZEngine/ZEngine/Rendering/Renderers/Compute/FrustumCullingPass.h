#pragma once
// GPU frustum culling via compute + vkDrawIndirectCountKHR (#663)
#include <ZEngine/Rendering/Renderers/Base/IComputeCallbackPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct FrustumCullingPass final : public IComputeCallbackPass
    {
        cstring GetShaderName() const override
        {
            return "frustum_cull_compute";
        }

        void SetupCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceBuilderPtr const res_builder) override;

        void ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
