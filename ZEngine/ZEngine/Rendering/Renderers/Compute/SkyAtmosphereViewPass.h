#pragma once
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Camera-relative physical inputs shared by the view-local atmosphere kernels.
    /// @details Every field is a vec4 so its C++ layout exactly matches the common
    /// GLSL push-constant prefix. PlanetCenterRelativeAndMaxDistance is in
    /// kilometres relative to the current camera; no world-origin-sized value
    /// reaches a shader.
    struct AtmosphereViewPushConstants
    {
        float PlanetCenterRelativeAndMaxDistance[4] = {};
        float RadiiAndScaleHeights[4]               = {};
        float RayleighScattering[4]                 = {};
        float MieAndOzone[4]                        = {};
        float OzoneAbsorption[4]                    = {};
        float SunDirectionAndRadius[4]              = {};
        float SunRadianceAndAvailability[4]         = {};
        float PresentationTintAndIntensity[4]       = {};
    };
    static_assert(sizeof(AtmosphereViewPushConstants) == 128, "Atmosphere view push constants must match the common GLSL prefix");

    /// @brief Additional inputs required only by the sky-view ground closure.
    struct SkyViewPushConstants
    {
        AtmosphereViewPushConstants Atmosphere                = {};
        /// @brief RGB=implicit planet albedo, w=scene-linear diffuse fill irradiance.
        float                       GroundAlbedoAndAmbient[4] = {};
    };
    static_assert(sizeof(SkyViewPushConstants) == 144, "Sky-view push constants must match GLSL");

    /// @brief Common immutable-frame state for camera-local atmosphere compute passes.
    struct SkyAtmosphereViewPass : public IInlineComputePass
    {
        /// @brief Copies the pinned environment selected at frame start.
        void SetEnvironment(const Scenes::SkyEnvironmentSnapshot* snapshot, const Scenes::SkyConfig& presentation);
        /// @brief Copies the frame camera position before graph registration.
        void SetCameraPosition(const Core::Maths::Vec3f& position);
        void Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;

    protected:
        [[nodiscard]] bool                        IsViewActive() const;
        [[nodiscard]] bool                        IsRenderableView(const RenderGraphFrameContext& frame_context) const;
        [[nodiscard]] AtmosphereViewPushConstants MakePushConstants() const;

        Scenes::SkyConfig                         m_config       = {};
        Scenes::SkyConfig                         m_presentation = {};
        Scenes::SkyCelestialLight                 m_celestial    = {};
        Scenes::AtmosphereStaticResources         m_atmosphere   = {};
        Core::Maths::Vec3f                        m_camera_pos   = {};
        RenderPasses::ComputePass*                m_compute_pass = nullptr;
        bool                                      m_active       = false;
    };

    /// @brief Generates a fixed-resolution sky lookup for the active render view.
    struct SkyViewLutPass final : public SkyAtmosphereViewPass
    {
        cstring  GetShaderName() const override;
        uint32_t GetPushConstantSize() const override;
        bool     ShouldRegisterCompute(const RenderGraphFrameContext& frame_context) const override;
        void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };

    /// @brief Generates per-pixel distance slices for in-scattering and transmittance.
    struct AerialPerspectivePass final : public SkyAtmosphereViewPass
    {
        cstring  GetShaderName() const override;
        uint32_t GetPushConstantSize() const override;
        bool     ShouldRegisterCompute(const RenderGraphFrameContext& frame_context) const override;
        void     RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder) override;
        void     ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr scene, VkPipeline pipeline, VkPipelineLayout layout, Hardwares::CommandBufferPtr const command_buffer) override;
    };
} // namespace ZEngine::Rendering::Renderers
