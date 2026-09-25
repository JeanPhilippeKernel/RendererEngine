#pragma once
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/Cameras/Camera.h>
#include <ZEngine/Rendering/Renderers/IRenderer.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>

namespace ZEngine::Rendering::Renderers
{
    struct LightingPass;
    struct GridPass;
    struct EnvironmentBackgroundPass;
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
    // Render-thread snapshot published for the editor. Its fields remain separate
    // because CPU arenas, VMA/driver accounting, environment policy, and graph
    // transients do not share an enforced capacity.
    struct RendererMemoryStatistics
    {
        Core::Memory::GpuMemoryStatistics Vma                          = {};
        uint64_t                          EnvironmentReservedBytes     = 0;
        uint64_t                          EnvironmentBudgetBytes       = 0;
        VkDeviceSize                      TransientVirtualImageBytes   = 0;
        VkDeviceSize                      TransientPhysicalImageBytes  = 0;
        VkDeviceSize                      TransientVirtualBufferBytes  = 0;
        VkDeviceSize                      TransientPhysicalBufferBytes = 0;
        uint32_t                          TransientImageBackingCount   = 0;
        uint32_t                          TransientBufferBackingCount  = 0;

        [[nodiscard]] VkDeviceSize        TransientVirtualBytes() const
        {
            return TransientVirtualImageBytes + TransientVirtualBufferBytes;
        }

        [[nodiscard]] VkDeviceSize TransientPhysicalBytes() const
        {
            return TransientPhysicalImageBytes + TransientPhysicalBufferBytes;
        }

        [[nodiscard]] VkDeviceSize TransientAliasingSavings() const
        {
            const VkDeviceSize virtual_bytes  = TransientVirtualBytes();
            const VkDeviceSize physical_bytes = TransientPhysicalBytes();
            return virtual_bytes > physical_bytes ? virtual_bytes - physical_bytes : 0;
        }
    };

    struct GraphicRenderer : public IRenderer
    {
        GraphicRenderer();
        ~GraphicRenderer();

        void                                   Initialize(Hardwares::VulkanDevicePtr device) override;
        void                                   Deinitialize() override;
        Hardwares::CommandBuffer*              DrawScene(uint8_t frame_index, uint8_t thread_index, Hardwares::CommandBufferPtr const cb, const Cameras::CameraFrameData& camera);
        /// @brief Accepts an immutable main-thread sky snapshot carried by the frame mailbox.
        void                                   ApplySkyConfig(const Scenes::SkyConfig& sky, const Scenes::SkyCelestialLight& celestial_light, uint64_t revision);
        /// @brief Pins the published environment and selects it for this immutable camera frame.
        void                                   BeginSkyFrame(const Cameras::CameraFrameData& camera);
        void                                   ApplyGridConfig(const Scenes::GridConfig& cfg);
        /// @brief Returns the stable frame-color texture used by UI viewport widgets.
        Textures::TextureHandle                GetFrameOutput();
        /// @brief Returns a coherent renderer-memory snapshot for a non-render-thread UI reader.
        [[nodiscard]] RendererMemoryStatistics GetMemoryStatistics() const;

    private:
        void                                            PublishFrameOutput(Textures::TextureHandle output);
        void                                            StartPendingSkyBake();
        void                                            PollSkyBake();
        void                                            CollectRetiredSkySnapshots();
        void                                            DiscardSkyTexture(Textures::TextureHandle texture);
        void                                            DiscardSkyResources(const Scenes::SkyEnvironmentResources& resources);
        void                                            PublishMemoryStatistics();
        /// @brief Verifies the shared RGBA16F cubemap contract used by IBL.
        [[nodiscard]] bool                              SupportsEnvironmentLightingResources(const EnvironmentLightingBakeSettings& bake_settings, cstring* out_reason = nullptr) const;
        /// @brief Verifies the additional LUT and source-radiance requirements of atmosphere baking.
        [[nodiscard]] bool                              SupportsAtmosphereBakeResources(const EnvironmentLightingBakeSettings& bake_settings, cstring* out_reason = nullptr) const;
        /// @brief Verifies the transient 2D/3D atmosphere-view resource contract.
        [[nodiscard]] bool                              SupportsAtmosphereViewResources(cstring* out_reason = nullptr) const;
        /// @brief Verifies that one cooked RGBA32F HDRI can be mip-generated on this device.
        [[nodiscard]] bool                              SupportsHDRISourceResources(uint32_t face_resolution, cstring* out_reason = nullptr) const;
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
        PaddedAtomic<uint64_t>                          m_frame_output_sequence                    = {};
        PaddedAtomic<uint64_t>                          m_frame_output_index                       = {.value = UINT64_MAX};
        PaddedAtomic<uint64_t>                          m_frame_output_generation                  = {};
        PaddedAtomic<uint64_t>                          m_memory_statistics_sequence               = {};
        PaddedAtomic<uint64_t>                          m_vma_allocation_bytes                     = {};
        PaddedAtomic<uint64_t>                          m_vma_block_bytes                          = {};
        PaddedAtomic<uint64_t>                          m_vma_heap_usage_bytes                     = {};
        PaddedAtomic<uint64_t>                          m_vma_heap_budget_bytes                    = {};
        PaddedAtomic<uint64_t>                          m_vma_heap_count                           = {};
        PaddedAtomic<uint64_t>                          m_vma_uses_driver_budget                   = {};
        PaddedAtomic<uint64_t>                          m_environment_reserved_bytes               = {};
        PaddedAtomic<uint64_t>                          m_environment_budget_bytes                 = {};
        PaddedAtomic<uint64_t>                          m_transient_virtual_image_bytes            = {};
        PaddedAtomic<uint64_t>                          m_transient_physical_image_bytes           = {};
        PaddedAtomic<uint64_t>                          m_transient_virtual_buffer_bytes           = {};
        PaddedAtomic<uint64_t>                          m_transient_physical_buffer_bytes          = {};
        PaddedAtomic<uint64_t>                          m_transient_image_backing_count            = {};
        PaddedAtomic<uint64_t>                          m_transient_buffer_backing_count           = {};
        Scenes::SkyEnvironment                          m_sky_environment                          = {};
        LightingPass*                                   m_lighting_pass                            = nullptr;
        GridPass*                                       m_grid_pass                                = nullptr;
        EnvironmentBackgroundPass*                      m_environment_background_pass              = nullptr;
        SkySpherePass*                                  m_sky_sphere_pass                          = nullptr;
        SkyViewLutPass*                                 m_sky_view_lut_pass                        = nullptr;
        AerialPerspectivePass*                          m_aerial_perspective_pass                  = nullptr;
        SkyCompositePass*                               m_sky_composite_pass                       = nullptr;
        ToneMappingPass*                                m_tone_mapping_pass                        = nullptr;
        SkyAtmosphereTransmittancePass*                 m_sky_atmosphere_transmittance_pass        = nullptr;
        SkyAtmosphereMultiscatteringPass*               m_sky_atmosphere_multiscattering_pass      = nullptr;
        SkyAtmosphereSourceRadiancePass*                m_sky_atmosphere_source_radiance_pass      = nullptr;
        SkyEnvironmentMipGenerationPass*                m_sky_hdri_mip_generation_pass             = nullptr;
        SkyEnvironmentMipGenerationPass*                m_sky_atmosphere_mip_generation_pass       = nullptr;
        SkyEnvironmentDiffuseIrradiancePass*            m_sky_diffuse_irradiance_pass              = nullptr;
        SkyEnvironmentSpecularPrefilterPass*            m_sky_specular_prefilter_pass              = nullptr;
        bool                                            m_environment_lighting_resources_supported = false;
        bool                                            m_atmosphere_bake_resources_supported      = false;
        bool                                            m_atmosphere_view_resources_supported      = false;
        cstring                                         m_environment_lighting_unavailable_reason  = "not evaluated";
        cstring                                         m_atmosphere_bake_unavailable_reason       = "not evaluated";
        cstring                                         m_atmosphere_view_unavailable_reason       = "not evaluated";
        Scenes::AtmosphereViewClass                     m_last_atmosphere_view_class               = Scenes::AtmosphereViewClass::Invalid;
    };
    ZDEFINE_PTR(GraphicRenderer);
} // namespace ZEngine::Rendering::Renderers
