#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <vulkan/vulkan.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Base class for compute dispatch passes in the render graph.
    ///
    /// @details Handles the boilerplate shared by every compute pass:
    ///  - Setup()   delegates to SetupCompute() so subclasses only see the resource builder.
    ///  - Compile() builds a COMPUTE RenderPass automatically from GetShaderName() and
    ///              GetPushConstantSize() — subclasses never touch RenderPassBuilder.
    ///  - Execute() binds the compute pipeline then delegates to ExecuteCompute().
    ///
    /// Concrete compute passes only implement SetupCompute(), ExecuteCompute(), and
    /// GetShaderName(). GetPushConstantSize() is optional (defaults to 0).
    ///
    /// The render graph interacts with compute passes through the same IRenderGraphCallbackPass
    /// interface as graphics passes. The COMPUTE type in the RenderPassSpecification produced by
    /// Compile() tells the graph to skip VkRenderPass / framebuffer creation for this pass.
    struct IComputeCallbackPass : public IRenderGraphCallbackPass
    {
        /// @brief Delegates to SetupCompute() — subclasses declare resource reads/writes there.
        void                Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) final;

        /// @brief Builds a COMPUTE RenderPass from GetShaderName() and GetPushConstantSize().
        ///        Subclasses do not override this.
        ///        NOTE: Full implementation requires ComputePassBuilder and ComputePipeline
        ///        (see compute-pipeline.md §2 and §5). Stubbed until those land.
        void                Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) final;

        /// @brief Binds the compute pipeline then calls ExecuteCompute().
        ///        NOTE: Pipeline bind requires ComputePipeline on RenderPass (compute-pipeline.md §4).
        ///        Stubbed until that lands.
        void                Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) final;

        /// @brief Declare resource reads and writes for this pass.
        ///        Called from Setup(); receives only the resource builder.
        virtual void        SetupCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceBuilderPtr const res_builder)                                                                                                                                        = 0;

        /// @brief Record the compute dispatch commands for this pass.
        ///        Called from Execute() after the compute pipeline is bound.
        ///        pipeline / layout are extracted from pass->ComputePipeline (see compute-pipeline.md §4).
        virtual void        ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) = 0;

        /// @brief Name of the compute shader (e.g. "ssao_compute", "bloom_threshold_compute").
        ///        Used by Compile() to look up the shader in the device cache.
        virtual const char* GetShaderName() const                                                                                                                                                                                                                         = 0;

        /// @brief Push constant size in bytes. Override to non-zero if the shader uses push constants.
        virtual uint32_t    GetPushConstantSize() const
        {
            return 0;
        }
    };

} // namespace ZEngine::Rendering::Renderers
