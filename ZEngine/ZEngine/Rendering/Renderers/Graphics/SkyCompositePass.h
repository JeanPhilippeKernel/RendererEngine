#pragma once
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    /// @brief Parameters for depth-aware HDR sky composition.
    struct SkyCompositePushConstants
    {
        /// @brief x=max aerial range in km, y=depth clear value, z=clear epsilon,
        /// w=scene world units per kilometre.
        float AerialMaxDistanceAndDepthClear[4] = {};
        /// @brief Planet centre relative to the camera, in kilometres.
        float PlanetCenterRelative[4]           = {};
        /// @brief x=planet radius, y=atmosphere radius, both in kilometres.
        float AtmosphereRadiiAndPadding[4]      = {};
        /// @brief Direction to the sun and angular radius in radians.
        float SunDirectionAndRadius[4]          = {};
        /// @brief RGB=scene-linear solar radiance after presentation controls, w=source available flag.
        float SunRadianceAndAvailability[4]     = {};
    };
    static_assert(sizeof(SkyCompositePushConstants) == 80, "Sky composite push constants must match GLSL");

    /// @brief Composites opaque HDR lighting with view-local sky and aerial LUTs.
    /// @details It writes a separate target, keeping attachment feedback loops
    /// out of the baseline Vulkan path. Forward transparent passes may consume
    /// that target later; the editor grid does so as an overlay today.
    struct SkyCompositePass final : public IRenderGraphCallbackPass
    {
        /// @brief Copies the pinned environment and its current presentation controls.
        void                                 SetEnvironment(const Scenes::SkyEnvironmentSnapshot* snapshot, const Scenes::SkyConfig& presentation);
        /// @brief Supplies depth convention for this immutable camera frame.
        void                                 SetCameraDepthConvention(bool uses_reverse_z);
        /// @brief Supplies the camera origin used to reconstruct stable LUT coordinates.
        void                                 SetCameraPosition(const Core::Maths::Vec3f& position);

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
        [[nodiscard]] bool                IsViewActive() const;

        Scenes::SkyConfig                 m_config         = {};
        Scenes::SkyConfig                 m_presentation   = {};
        Scenes::SkyCelestialLight         m_celestial      = {};
        Scenes::AtmosphereStaticResources m_atmosphere     = {};
        Core::Maths::Vec3f                m_camera_pos     = {};
        bool                              m_active         = false;
        bool                              m_uses_reverse_z = false;
    };
} // namespace ZEngine::Rendering::Renderers
