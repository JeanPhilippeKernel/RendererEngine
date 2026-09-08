#pragma once
// Dual-Kawase bloom threshold pass (Sprint 8)
#include <ZEngine/Rendering/Renderers/Base/IComputeCallbackPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct BloomPass final : public IComputeCallbackPass
    {
        const char* GetShaderName() const override
        {
            return "bloom_threshold_compute";
        }

        void SetupCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceBuilderPtr const res_builder) override;

        void ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
