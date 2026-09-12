#pragma once
#include <ZEngine/Rendering/Renderers/RenderGraph.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Base class for cullable transfer work recorded by the render graph.
    /// @details Transfer passes declare source and destination resources in RegisterTransfer().
    /// RecordTransfer() receives no RenderPass or pipeline; export an output or declare a
    /// readback sink for externally observable work.
    struct ITransferPass : public IRenderGraphCallbackPass
    {
        /// @brief Registers the pass and forwards resource declarations to RegisterTransfer().
        bool                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) final;
        /// @brief Records transfer commands through RecordTransfer().
        void                 Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) final;

        /// @brief Requests the device's transfer queue or its graphics-queue fallback.
        Rendering::QueueType GetRequestedQueue() const final
        {
            return Rendering::QueueType::TRANSFER_QUEUE;
        }
        /// @brief Indicates that transfer passes do not require a RenderPass object.
        bool RequiresRenderPass() const final
        {
            return false;
        }

        /// @brief Declares the transfer pass's resource reads and writes for this frame.
        virtual void RegisterTransfer(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder)                                        = 0;
        /// @brief Records the transfer commands after graph barriers have been emitted.
        virtual void RecordTransfer(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, Hardwares::CommandBufferPtr const command_buffer) = 0;
    };
} // namespace ZEngine::Rendering::Renderers
