#pragma once
#include <ZEngine/Rendering/Renderers/Pipelines/RendererPipeline.h>
#include <ZEngine/Rendering/Renderers/RenderGraph.h>
#include <ZEngine/Rendering/Sky/SkyConfig.h>
#include <ZEngine/Rendering/Textures/Texture.h>

namespace ZEngine::Rendering::Renderers
{
    struct SkyAtmosphereUBO
    {
        Core::Maths::Vec4f RayleighScattering;
        Core::Maths::Vec4f MieScattering;
        Core::Maths::Vec4f MieAbsorption;
        Core::Maths::Vec4f OzoneAbsorption;
        float              PlanetRadius;
        float              AtmosphereRadius;
        float              RayleighScaleHeight;
        float              MieScaleHeight;
        float              MieAnisotropy;
        float              OzoneLayerCentre;
        float              OzoneLayerWidth;
        float              SunIlluminanceScale;
        Core::Maths::Vec4f SunDirection;
        float              SunAngularRadius;
        float              _pad[3];
        Core::Maths::Vec4f CameraPositionKm;
        uint32_t           TransmittanceLUTIndex;
        uint32_t           MultiscatterLUTIndex;
        uint32_t           SkyviewLUTIndex;
        float              _pad2;
    };
    static_assert(sizeof(SkyAtmosphereUBO) == 160);

    struct SkyAtmospherePass : public IRenderGraphCallbackPass
    {
        Sky::SkyConfig Config    = {};
        bool           LUTsDirty = true;

        void           Setup(Hardwares::VulkanDevicePtr const device, cstring name, RenderGraphResourceBuilderPtr const res_builder, RenderGraphResourceInspectorPtr res_inspector) override;
        void           Compile(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPassBuilder* pass_builder, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass** const output_pass) override;
        void           Execute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr res_inspector, Rendering::Scenes::SceneDataPtr const scene, RenderPasses::RenderPass* const pass, Buffers::FramebufferVNext* const framebuffer, Hardwares::CommandBufferPtr const command_buffer) override;
        void           Deinitialize(Hardwares::VulkanDevicePtr const device) override;

    private:
        void                       InitLUTs(Hardwares::VulkanDevice* device);
        void                       InitComputePipelines(Hardwares::VulkanDevice* device);
        void                       InitLUTDescriptors(Hardwares::VulkanDevice* device);
        void                       DispatchLUTs(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* cmd);

        // Persistent LUT textures (DeviceTexture domain, never freed while pass is alive)
        Textures::TextureHandle    m_transmittance_lut;
        Textures::TextureHandle    m_multiscatter_lut;
        Textures::TextureHandle    m_skyview_lut;

        // Descriptor sets for compute pipelines (one per LUT generation step)
        VkDescriptorSet            m_transmittance_ds   = VK_NULL_HANDLE;
        VkDescriptorSet            m_multiscatter_ds    = VK_NULL_HANDLE;
        VkDescriptorSet            m_skyview_ds         = VK_NULL_HANDLE;
        VkDescriptorSetLayout      m_lut_ds_layout      = VK_NULL_HANDLE;
        VkDescriptorPool           m_ds_pool            = VK_NULL_HANDLE;

        // Atmosphere UBO heap offset (pushed per-frame)
        uint32_t                   m_camera_heap_offset = 0;
        uint32_t                   m_atmo_heap_offset   = 0;

        Pipelines::ComputePipeline m_transmittance_pipeline;
        Pipelines::ComputePipeline m_multiscatter_pipeline;
        Pipelines::ComputePipeline m_skyview_pipeline;

        bool                       m_luts_initialized = false;
        bool                       m_combine_ready    = false;
    };
} // namespace ZEngine::Rendering::Renderers
