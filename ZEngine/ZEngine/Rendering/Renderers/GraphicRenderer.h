#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    struct LightingPass;
    struct GridPass;
    struct SkyboxPass;
    struct SkySpherePass;
    struct SkyViewLutPass;
    struct AerialPerspectivePass;
    struct SkyCompositePass;
    struct ToneMappingPass;
    struct SkyAtmosphereTransmittancePass;
    struct SkyAtmosphereMultiscatteringPass;
    struct SkyAtmosphereSourceRadiancePass;
    struct SkyEnvironmentMipGenerationPass;
    struct SkyEnvironmentDiffuseIrradiancePass;
    struct SkyEnvironmentSpecularPrefilterPass;
} // namespace ZEngine::Rendering::Renderers

namespace ZEngine::Rendering::Renderers
{
    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        void                      Initialize(Hardwares::VulkanDevicePtr device) override;
        void                      Deinitialize() override;
        Hardwares::CommandBuffer* DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, const Cameras::CameraFrameData& camera);
        /// @brief Accepts an immutable main-thread sky snapshot carried by the frame mailbox.
        void                      ApplySkyConfig(const Scenes::SkyConfig& sky, const Scenes::SkyCelestialLight& celestial_light, uint64_t revision);
        /// @brief Pins the published environment and selects it for this immutable camera frame.
        void                      BeginSkyFrame(const Cameras::CameraFrameData& camera);
        void                      ApplyGridConfig(const Scenes::GridConfig& cfg);
        /// @brief Returns the stable frame-color texture used by UI viewport widgets.
        Textures::TextureHandle   GetFrameOutput();

    private:
        void                                            PublishFrameOutput(Textures::TextureHandle output);
        void                                            StartPendingSkyBake();
        void                                            PollSkyBake();
        void                                            CollectRetiredSkySnapshots();
        void                                            DiscardSkyTexture(Textures::TextureHandle texture);
        void                                            DiscardSkyResources(const Scenes::SkyEnvironmentResources& resources);
        [[nodiscard]] bool                              SupportsAtmosphereBakeResources(const EnvironmentLightingBakeSettings& bake_settings) const;
        [[nodiscard]] bool                              SupportsAtmosphereViewResources() const;
        [[nodiscard]] Scenes::AtmosphereStaticResources CreateAtmosphereStaticResources();
        [[nodiscard]] Textures::TextureHandle           CreateAtmosphereSourceRadiance(const EnvironmentLightingBakeSettings& bake_settings);
        EnvironmentLightingResources                    CreateSkyLightingResources(const EnvironmentLightingBakeSettings& bake_settings);
        static void                                     OnSkyFrameSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);
        static void                                     OnSkyFrameCancelled(void* context);
        static void                                     OnSkyBakeStageSubmitted(void* context, Rendering::Primitives::Semaphore* timeline, uint64_t timeline_value);

        // The main thread reads this handle to build the next UI payload while
        // the render thread publishes it after compiling the current graph.
        // Sequence-guard the two 64-bit handle fields so readers never observe
        // a mixed index/generation pair.
        PaddedAtomic<uint64_t>                          m_frame_output_sequence               = {};
        PaddedAtomic<uint64_t>                          m_frame_output_index                  = {.value = UINT64_MAX};
        PaddedAtomic<uint64_t>                          m_frame_output_generation             = {};
        Scenes::SkyEnvironment                          m_sky_environment                     = {};
        LightingPass*                                   m_lighting_pass                       = nullptr;
        GridPass*                                       m_grid_pass                           = nullptr;
        SkyboxPass*                                     m_skybox_pass                         = nullptr;
        SkySpherePass*                                  m_sky_sphere_pass                     = nullptr;
        SkyViewLutPass*                                 m_sky_view_lut_pass                   = nullptr;
        AerialPerspectivePass*                          m_aerial_perspective_pass             = nullptr;
        SkyCompositePass*                               m_sky_composite_pass                  = nullptr;
        ToneMappingPass*                                m_tone_mapping_pass                   = nullptr;
        SkyAtmosphereTransmittancePass*                 m_sky_atmosphere_transmittance_pass   = nullptr;
        SkyAtmosphereMultiscatteringPass*               m_sky_atmosphere_multiscattering_pass = nullptr;
        SkyAtmosphereSourceRadiancePass*                m_sky_atmosphere_source_radiance_pass = nullptr;
        SkyEnvironmentMipGenerationPass*                m_sky_hdri_mip_generation_pass        = nullptr;
        SkyEnvironmentMipGenerationPass*                m_sky_atmosphere_mip_generation_pass  = nullptr;
        SkyEnvironmentDiffuseIrradiancePass*            m_sky_diffuse_irradiance_pass         = nullptr;
        SkyEnvironmentSpecularPrefilterPass*            m_sky_specular_prefilter_pass         = nullptr;
        bool                                            m_atmosphere_view_resources_supported = false;
        Scenes::AtmosphereViewClass                     m_last_atmosphere_view_class          = Scenes::AtmosphereViewClass::Invalid;
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
