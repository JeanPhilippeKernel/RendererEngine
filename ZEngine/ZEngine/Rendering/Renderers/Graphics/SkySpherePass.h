#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief GPU parameters for the analytic, presentation-only sky background.
    struct SkySpherePushConstants
    {
        float HorizonColor[4]             = {};
        float ZenithColor[4]              = {};
        float GroundColor[4]              = {};
        /// @brief xyz is direction to the sun; w is the far clip depth.
        float SunDirection[4]             = {};
        float SunDiscAngularRadiusRadians = 0.0f;
        float SunDiscIntensity            = 0.0f;
        float HorizonSharpness            = 1.0f;
        float ShowSunDisc                 = 0.0f;
    };
    static_assert(sizeof(SkySpherePushConstants) == 80, "SkySphere push constants must match GLSL");
    static_assert(sizeof(SkySpherePushConstants) <= 128, "SkySphere must fit Vulkan's guaranteed push-constant budget");

    /// @brief Draws an analytic gradient behind opaque scene geometry.
    /// @details SkySphere is presentation-only and does not generate or own IBL resources.
    struct SkySpherePass final : public IRenderGraphCallbackPass
    {
        /// @brief Copies the scene presentation state and optional resolved sun for this frame.
        void                                 SetEnvironment(const Scenes::SkyConfig& config, const Scenes::SkyCelestialLight& celestial_light);
        /// @brief Selects the far depth used by the current projection convention.
        void                                 SetCameraDepthConvention(bool uses_reverse_z);

        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        [[nodiscard]] bool        IsViewActive() const;

        Scenes::SkyConfig         m_config         = {};
        Scenes::SkyCelestialLight m_celestial      = {};
        bool                      m_active         = false;
        bool                      m_uses_reverse_z = false;
    };
} // namespace ZEngine::Rendering::Renderers
