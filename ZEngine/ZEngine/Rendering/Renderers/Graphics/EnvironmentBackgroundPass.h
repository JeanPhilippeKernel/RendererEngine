#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Per-frame presentation controls for an environment-cubemap background.
    struct EnvironmentBackgroundPushConstants
    {
        float TintIntensity[4]      = {1.0f, 1.0f, 1.0f, 1.0f};
        float YawRadians            = 0.0f;
        float UseSolidColorFallback = 0.0f;
        /// @brief Far clip depth: 0 for reverse-Z and 1 otherwise.
        float FarDepth              = 1.0f;
        float Padding               = 0.0f;
    };
    static_assert(sizeof(EnvironmentBackgroundPushConstants) == 32, "Environment-background push constants must match GLSL");

    /// @brief Draws an HDRI cubemap or fixed loading fallback behind opaque geometry.
    struct EnvironmentBackgroundPass final : public IRenderGraphCallbackPass
    {
        /// @brief Selects the render-thread-published environment snapshot for this frame.
        void                                 SetEnvironment(Textures::TextureHandle environment_map, const Rendering::Scenes::SkyConfig& config);
        /// @brief Enables this fallback/HDRI background when no analytic background is active.
        void                                 SetActive(bool active);
        /// @brief Draws a fixed editor fallback instead of sampling the environment cubemap.
        void                                 SetUseSolidColorFallback(bool enabled);
        /// @brief Selects the far depth used by the current projection convention.
        void                                 SetCameraDepthConvention(bool uses_reverse_z);

        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        /// @brief Builds the static PSO recipe for the full-screen sky draw.
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        void                                 Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        [[nodiscard]] bool           IsActive() const;

        Textures::TextureHandle      m_environment_map          = {};
        Rendering::Scenes::SkyConfig m_presentation_config      = {};
        bool                         m_active                   = true;
        bool                         m_use_solid_color_fallback = false;
        bool                         m_uses_reverse_z           = false;
    };
} // namespace ZEngine::Rendering::Renderers
