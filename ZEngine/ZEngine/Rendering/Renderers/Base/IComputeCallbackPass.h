#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{
    struct IComputeCallbackPass : public IRenderGraphCallbackPass
    {
        void             Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) final;
        void             Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) final;
        void             Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) final;

        virtual void     SetupCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceBuilderPtr const res_builder)                                                                                                                                        = 0;
        virtual void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) = 0;
        virtual cstring  GetShaderName() const                                                                                                                                                                                                                         = 0;

        virtual uint32_t GetPushConstantSize() const
        {
            return 0;
        }
    };

} // namespace ZEngine::Rendering::Renderers
