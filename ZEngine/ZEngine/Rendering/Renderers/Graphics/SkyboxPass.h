#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Per-frame presentation controls for the skybox draw.
    struct SkyboxPushConstants
    {
        float TintIntensity[4]      = {1.0f, 1.0f, 1.0f, 1.0f};
        float YawRadians            = 0.0f;
        float UseSolidColorFallback = 0.0f;
        float Padding[2]            = {};
    };
    static_assert(sizeof(SkyboxPushConstants) == 32, "Skybox push constants must match GLSL");

    struct SkyboxPass : public IRenderGraphCallbackPass
    {
        /// @brief Selects the render-thread-published environment snapshot for this frame.
        void                                 SetEnvironment(Textures::TextureHandle environment_map, const Rendering::Scenes::SkyConfig& config);
        /// @brief Disables cubemap background drawing while analytic sky composition is active.
        void                                 SetEnabled(bool enabled);
        /// @brief Draws a fixed editor fallback instead of sampling the environment cubemap.
        void                                 SetUseSolidColorFallback(bool enabled);

        bool                                 Register(Hardwares::VulkanDevicePtr const device, cstring name, const RenderGraphFrameContext& frame_context, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        /// @brief Builds the static PSO recipe for the full-screen sky draw.
        Specifications::GraphicsPipelineDesc BuildGraphicsPipelineDescription(Core::Memory::ArenaAllocator* arena) const override;
        void                                 Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass) override;
        virtual void                         Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 RecordDraw(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        bool                                 SupportsSecondaryRecording() const override
        {
            return true;
        }

    private:
        Textures::TextureHandle      m_env_map                  = {};
        Rendering::Scenes::SkyConfig m_sky_config               = {};
        bool                         m_enabled                  = true;
        bool                         m_use_solid_color_fallback = false;
    };
} // namespace ZEngine::Rendering::Renderers
