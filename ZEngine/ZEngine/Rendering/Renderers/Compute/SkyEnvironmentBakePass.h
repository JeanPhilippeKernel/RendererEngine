#pragma once
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Common graph callback policy for one staged environment bake operation.
    struct SkyEnvironmentBakePass : public IInlineComputePass
    {
        explicit SkyEnvironmentBakePass(Scenes::SkyEnvironment* environment) : m_environment(environment) {}

        RGPassFlags          GetPassFlags() const final;
        Rendering::QueueType GetRequestedQueue() const final;
        void                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;

    protected:
        [[nodiscard]] bool         IsActive(Scenes::SkyEnvironmentBakeStage stage) const;

        Scenes::SkyEnvironment*    m_environment  = nullptr;
        RenderPasses::ComputePass* m_compute_pass = nullptr;
    };

    /// @brief Builds the mip chain required for filtered HDRI cubemap sampling.
    struct SkyEnvironmentMipGenerationPass final : public SkyEnvironmentBakePass
    {
        explicit SkyEnvironmentMipGenerationPass(Scenes::SkyEnvironment* environment) : SkyEnvironmentBakePass(environment) {}

        cstring  GetShaderName() const override;
        uint32_t GetPushConstantSize() const override;
        bool     ShouldRegisterCompute() const override;
        void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void     Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    /// @brief Convolves source radiance into a low-frequency diffuse irradiance cubemap.
    struct SkyEnvironmentDiffuseIrradiancePass final : public SkyEnvironmentBakePass
    {
        explicit SkyEnvironmentDiffuseIrradiancePass(Scenes::SkyEnvironment* environment) : SkyEnvironmentBakePass(environment) {}

        cstring  GetShaderName() const override;
        uint32_t GetPushConstantSize() const override;
        bool     ShouldRegisterCompute() const override;
        void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    /// @brief Prefilters source radiance per roughness mip for split-sum specular IBL.
    struct SkyEnvironmentSpecularPrefilterPass final : public SkyEnvironmentBakePass
    {
        explicit SkyEnvironmentSpecularPrefilterPass(Scenes::SkyEnvironment* environment) : SkyEnvironmentBakePass(environment) {}

        cstring  GetShaderName() const override;
        uint32_t GetPushConstantSize() const override;
        bool     ShouldRegisterCompute() const override;
        void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
