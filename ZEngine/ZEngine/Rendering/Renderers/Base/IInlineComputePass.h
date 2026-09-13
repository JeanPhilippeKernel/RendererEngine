#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Base class for compute work recorded by the render graph.
    /// @details Compute passes do not allocate graphics attachments or framebuffers.
    struct IInlineComputePass : public IRenderGraphCallbackPass
    {
        /// @brief Registers the pass and forwards resource declarations to RegisterCompute().
        bool                           Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) final;
        /// @brief Dispatches the pass through ExecuteCompute().
        void                           Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) final;

        /// @brief Identifies this callback as a compute-pipeline pass.
        Specifications::RenderPassType GetPipelineType() const final
        {
            return Specifications::RenderPassType::COMPUTE;
        }
        /// @brief Forwards this pass's compute shader recipe to the graph.
        cstring GetComputeShaderName() const final
        {
            return GetShaderName();
        }
        /// @brief Forwards this pass's compute push-constant size to the graph.
        uint32_t GetComputePushConstantSize() const final
        {
            return GetPushConstantSize();
        }

        /// @brief Declares this frame's graph resources.
        virtual void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder)                                                                                       = 0;
        /// @brief Records this pass's compute dispatch commands.
        virtual void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) = 0;
        /// @brief Returns the compute shader asset name.
        virtual cstring  GetShaderName() const                                                                                                                                                                                                                         = 0;

        /// @brief Returns the byte size of the pass push-constant block.
        virtual uint32_t GetPushConstantSize() const
        {
            return 0;
        }

        /// @brief Requests the device's compute queue or its graphics-queue fallback.
        Rendering::QueueType GetRequestedQueue() const override
        {
            return Rendering::QueueType::COMPUTE_QUEUE;
        }
    };

} // namespace ZEngine::Rendering::Renderers
