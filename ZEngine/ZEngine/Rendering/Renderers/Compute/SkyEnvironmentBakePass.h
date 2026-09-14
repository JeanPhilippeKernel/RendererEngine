#pragma once
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Builds the mip chain required for filtered HDRI cubemap sampling.
    struct SkyEnvironmentMipGenerationPass final : public IInlineComputePass
    {
        explicit SkyEnvironmentMipGenerationPass(Scenes::SkyEnvironment* environment) : m_environment(environment) {}

        cstring              GetShaderName() const override;
        uint32_t             GetPushConstantSize() const override;
        bool                 ShouldRegisterCompute() const override;
        RGPassFlags          GetPassFlags() const override;
        Rendering::QueueType GetRequestedQueue() const override;
        void                 RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                 ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Scenes::SkyEnvironment*    m_environment  = nullptr;
        RenderPasses::ComputePass* m_compute_pass = nullptr;
    };

    /// @brief Convolves source radiance into a low-frequency diffuse irradiance cubemap.
    struct SkyEnvironmentDiffuseIrradiancePass final : public IInlineComputePass
    {
        explicit SkyEnvironmentDiffuseIrradiancePass(Scenes::SkyEnvironment* environment) : m_environment(environment) {}

        cstring              GetShaderName() const override;
        uint32_t             GetPushConstantSize() const override;
        bool                 ShouldRegisterCompute() const override;
        RGPassFlags          GetPassFlags() const override;
        Rendering::QueueType GetRequestedQueue() const override;
        void                 RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                 ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Scenes::SkyEnvironment*    m_environment  = nullptr;
        RenderPasses::ComputePass* m_compute_pass = nullptr;
    };

    /// @brief Prefilters source radiance per roughness mip for split-sum specular IBL.
    struct SkyEnvironmentSpecularPrefilterPass final : public IInlineComputePass
    {
        explicit SkyEnvironmentSpecularPrefilterPass(Scenes::SkyEnvironment* environment) : m_environment(environment) {}

        cstring              GetShaderName() const override;
        uint32_t             GetPushConstantSize() const override;
        bool                 ShouldRegisterCompute() const override;
        RGPassFlags          GetPassFlags() const override;
        Rendering::QueueType GetRequestedQueue() const override;
        void                 RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                 ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;

    private:
        Scenes::SkyEnvironment*    m_environment  = nullptr;
        RenderPasses::ComputePass* m_compute_pass = nullptr;
    };
} // namespace ZEngine::Rendering::Renderers
