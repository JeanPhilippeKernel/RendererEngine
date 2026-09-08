#pragma once
// Screen-space ambient occlusion (Sprint 8)
#include <ZEngine/Rendering/Renderers/Base/IComputeCallbackPass.h>

namespace ZEngine::Rendering::Renderers
{
    struct SSAOPass final : public IComputeCallbackPass
    {
        const char* GetShaderName() const override
        {
            return "ssao_compute";
        }

        void SetupCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceBuilderPtr const res_builder) override;

        void ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
