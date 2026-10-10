#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/Compute/SkyEnvironmentBakePass.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr const char* kSourceRadianceName              = "SkyBakeSourceRadiance";
        constexpr const char* kDiffuseIrradianceName           = "SkyBakeDiffuseIrradiance";
        constexpr const char* kSpecularEnvironmentName         = "SkyBakeSpecularEnvironment";
        constexpr const char* kAtmosphereTransmittanceName     = "SkyAtmosphereTransmittance";
        constexpr const char* kAtmosphereMultiscatteringName   = "SkyAtmosphereMultiscattering";
        constexpr uint32_t    kLocalSize                       = 8;
        constexpr uint32_t    kMaxSkyMipCount                  = 16;
        constexpr float       kSourceCaptureAltitudeKilometers = 0.0f;

        struct MipGenerationPushConstants
        {
            uint32_t SourceMip             = 0;
            uint32_t DestinationMip        = 0;
            uint32_t DestinationResolution = 1;
            uint32_t Padding               = 0;
        };

        struct DiffuseIrradiancePushConstants
        {
            uint32_t Resolution  = 0;
            uint32_t SampleCount = 0;
            uint32_t Padding[2]  = {};
        };

        struct SpecularPrefilterPushConstants
        {
            uint32_t Resolution     = 0;
            uint32_t MipLevel       = 0;
            uint32_t SampleCount    = 0;
            uint32_t SourceMipCount = 1;
            float    Roughness      = 0.0f;
            float    Padding[6]     = {};
        };

        struct AtmosphereStaticPushConstants
        {
            float RadiiAndScaleHeights[4] = {};
            float RayleighScattering[4]   = {};
            float MieAndOzone[4]          = {};
            float OzoneAbsorption[4]      = {};
        };

        struct AtmosphereSourceRadiancePushConstants
        {
            AtmosphereStaticPushConstants StaticAtmosphere                             = {};
            float                         SunDirectionAndRadius[4]                     = {};
            float                         SunRadianceAvailabilityAndCaptureAltitude[4] = {};
            float                         GroundAlbedoAndAmbient[4]                    = {};
        };

        static_assert(sizeof(MipGenerationPushConstants) == 16, "Sky mip push constants must match GLSL");
        static_assert(sizeof(DiffuseIrradiancePushConstants) == 16, "Diffuse irradiance push constants must match GLSL");
        static_assert(sizeof(SpecularPrefilterPushConstants) == 44, "Specular prefilter push constants must cover the GLSL layout");
        static_assert(sizeof(AtmosphereStaticPushConstants) == 64, "Atmosphere static push constants must match GLSL");
        static_assert(sizeof(AtmosphereSourceRadiancePushConstants) == 112, "Atmosphere source push constants must match GLSL");

        AtmosphereStaticPushConstants MakeAtmosphereStaticPushConstants(const Scenes::AtmosphereSettings& atmosphere)
        {
            AtmosphereStaticPushConstants push = {};
            push.RadiiAndScaleHeights[0]       = atmosphere.PlanetRadiusKilometers;
            push.RadiiAndScaleHeights[1]       = atmosphere.AtmosphereRadiusKilometers;
            push.RadiiAndScaleHeights[2]       = atmosphere.RayleighScaleHeightKilometers;
            push.RadiiAndScaleHeights[3]       = atmosphere.MieScaleHeightKilometers;
            push.RayleighScattering[0]         = atmosphere.RayleighScatteringPerKilometer[0];
            push.RayleighScattering[1]         = atmosphere.RayleighScatteringPerKilometer[1];
            push.RayleighScattering[2]         = atmosphere.RayleighScatteringPerKilometer[2];
            push.MieAndOzone[0]                = atmosphere.MieScatteringPerKilometer;
            push.MieAndOzone[1]                = atmosphere.MieAbsorptionPerKilometer;
            push.MieAndOzone[2]                = atmosphere.OzoneCenterKilometers;
            push.MieAndOzone[3]                = atmosphere.OzoneThicknessKilometers;
            push.OzoneAbsorption[0]            = atmosphere.OzoneAbsorptionPerKilometer[0];
            push.OzoneAbsorption[1]            = atmosphere.OzoneAbsorptionPerKilometer[1];
            push.OzoneAbsorption[2]            = atmosphere.OzoneAbsorptionPerKilometer[2];
            push.OzoneAbsorption[3]            = atmosphere.MieAnisotropy;
            return push;
        }

        bool GetImage(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle, Hardwares::ImageBuffer** out_image)
        {
            if (out_image)
                *out_image = nullptr;
            if (!device || !texture_handle.Valid())
                return false;

            auto* const texture = device->GlobalTextures.Access(texture_handle);
            auto* const image   = texture ? device->ImageBufferManager.Access(texture->BufferHandle) : nullptr;
            if (!image || image->GetHandle() == VK_NULL_HANDLE)
                return false;
            if (out_image)
                *out_image = image;
            return true;
        }

        uint32_t GetMipCount(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            Hardwares::ImageBuffer* image = nullptr;
            return GetImage(device, texture_handle, &image) ? image->Specification.MipLevelCount : 0;
        }

        uint32_t GetWidth(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            auto* const texture = device && texture_handle.Valid() ? device->GlobalTextures.Access(texture_handle) : nullptr;
            return texture ? texture->Width : 0;
        }

        uint32_t GetHeight(Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            auto* const texture = device && texture_handle.Valid() ? device->GlobalTextures.Access(texture_handle) : nullptr;
            return texture ? texture->Height : 0;
        }

        VkImageSubresourceRange MakeColorRange(uint32_t mip_level, uint32_t mip_count, uint32_t layer_count = 6)
        {
            VkImageSubresourceRange range = {};
            range.aspectMask              = VK_IMAGE_ASPECT_COLOR_BIT;
            range.baseMipLevel            = mip_level;
            range.levelCount              = mip_count;
            range.baseArrayLayer          = 0;
            range.layerCount              = layer_count;
            return range;
        }

        bool BindMipDestinations(RenderPasses::ComputePass* pass, Hardwares::VulkanDevice* device, Textures::TextureHandle texture_handle)
        {
            const uint32_t mip_count = GetMipCount(device, texture_handle);
            if (!pass || mip_count == 0 || mip_count > kMaxSkyMipCount)
                return false;

            VkImageSubresourceRange ranges[kMaxSkyMipCount] = {};
            for (uint32_t index = 0; index < kMaxSkyMipCount; ++index)
                ranges[index] = MakeColorRange(index < mip_count ? index : mip_count - 1, 1);
            pass->SetStorageImageArray("Destinations", texture_handle, ranges, kMaxSkyMipCount);
            return true;
        }

        void MakeShaderReadable(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* command_buffer, Textures::TextureHandle texture_handle)
        {
            Hardwares::ImageBuffer* image = nullptr;
            if (!GetImage(device, texture_handle, &image))
                return;

            VkImageMemoryBarrier2 transition   = {};
            transition.sType                   = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            transition.srcStageMask            = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            transition.srcAccessMask           = VK_ACCESS_2_SHADER_WRITE_BIT;
            transition.dstStageMask            = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
            transition.dstAccessMask           = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            transition.oldLayout               = VK_IMAGE_LAYOUT_GENERAL;
            transition.newLayout               = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            transition.srcQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;
            transition.dstQueueFamilyIndex     = VK_QUEUE_FAMILY_IGNORED;
            transition.image                   = image->GetHandle();
            transition.subresourceRange        = MakeColorRange(0, image->Specification.MipLevelCount, image->Specification.LayerCount);

            VkDependencyInfo dependency        = {};
            dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers    = &transition;
            command_buffer->PipelineBarrier2(dependency);
            image->Layout = ImageLayout::SHADER_READ_ONLY_OPTIMAL;
        }

        void MakeMipVisible(Hardwares::VulkanDevice* device, Hardwares::CommandBuffer* command_buffer, Textures::TextureHandle texture_handle, uint32_t mip_level)
        {
            Hardwares::ImageBuffer* image = nullptr;
            if (!GetImage(device, texture_handle, &image))
                return;

            VkImageMemoryBarrier2 barrier      = {};
            barrier.sType                      = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            barrier.srcStageMask               = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.srcAccessMask              = VK_ACCESS_2_SHADER_WRITE_BIT;
            barrier.dstStageMask               = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
            barrier.dstAccessMask              = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            barrier.oldLayout                  = VK_IMAGE_LAYOUT_GENERAL;
            barrier.newLayout                  = VK_IMAGE_LAYOUT_GENERAL;
            barrier.srcQueueFamilyIndex        = VK_QUEUE_FAMILY_IGNORED;
            barrier.dstQueueFamilyIndex        = VK_QUEUE_FAMILY_IGNORED;
            barrier.image                      = image->GetHandle();
            barrier.subresourceRange           = MakeColorRange(mip_level, 1);

            VkDependencyInfo dependency        = {};
            dependency.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dependency.imageMemoryBarrierCount = 1;
            dependency.pImageMemoryBarriers    = &barrier;
            command_buffer->PipelineBarrier2(dependency);
        }

    } // namespace

    RGPassFlags SkyEnvironmentBakePass::GetPassFlags() const
    {
        return RGPassFlags::NeverCull;
    }

    Rendering::QueueType SkyEnvironmentBakePass::GetRequestedQueue() const
    {
        // Publication is synchronized through the render timeline. Keeping the
        // bake on this queue makes that ordering explicit on every topology.
        return Rendering::QueueType::GRAPHIC_QUEUE;
    }

    void SkyEnvironmentBakePass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        m_compute_pass = pass ? static_cast<RenderPasses::ComputePass*>(pass) : nullptr;
        if (m_compute_pass && device && UsesLinearClampSampler())
            m_compute_pass->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    bool SkyEnvironmentBakePass::IsActive(Scenes::SkyEnvironmentBakeStage stage) const
    {
        return m_environment && m_environment->CanRecordGpuBakeStage() && m_environment->GetActiveBakeStage() == stage && m_environment->GetActiveBakeSource().Valid();
    }

    cstring SkyAtmosphereTransmittancePass::GetShaderName() const
    {
        return "sky_atmosphere_transmittance";
    }

    uint32_t SkyAtmosphereTransmittancePass::GetPushConstantSize() const
    {
        return sizeof(AtmosphereStaticPushConstants);
    }

    bool SkyAtmosphereTransmittancePass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereTransmittance) && m_environment->GetActiveBakeAtmosphere().Transmittance.Valid();
    }

    void SkyAtmosphereTransmittancePass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        res_builder->ImportTexture(kAtmosphereTransmittanceName, m_environment->GetActiveBakeAtmosphere().Transmittance, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kAtmosphereTransmittanceName, "Destination");
    }

    void SkyAtmosphereTransmittancePass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereTransmittance) || !m_compute_pass)
            return;

        const Scenes::SkyEnvironmentBakeRequest* bake   = m_environment->GetActiveBake();
        const Textures::TextureHandle            output = m_environment->GetActiveBakeAtmosphere().Transmittance;
        const uint32_t                           width  = GetWidth(device, output);
        const uint32_t                           height = GetHeight(device, output);
        if (!bake || width == 0 || height == 0)
            return;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        const AtmosphereStaticPushConstants push = MakeAtmosphereStaticPushConstants(bake->Config.Atmosphere);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((width + kLocalSize - 1) / kLocalSize, (height + kLocalSize - 1) / kLocalSize, 1);
        MakeShaderReadable(device, command_buffer, output);
        m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::AtmosphereTransmittance);
    }

    cstring SkyAtmosphereMultiscatteringPass::GetShaderName() const
    {
        return "sky_atmosphere_multiscattering";
    }

    uint32_t SkyAtmosphereMultiscatteringPass::GetPushConstantSize() const
    {
        return sizeof(AtmosphereStaticPushConstants);
    }

    bool SkyAtmosphereMultiscatteringPass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereMultiscattering) && m_environment->GetActiveBakeAtmosphere().Valid();
    }

    void SkyAtmosphereMultiscatteringPass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Scenes::AtmosphereStaticResources& atmosphere = m_environment->GetActiveBakeAtmosphere();
        res_builder->ReadTexture(res_builder->ImportTexture(kAtmosphereTransmittanceName, atmosphere.Transmittance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Transmittance");
        res_builder->ImportTexture(kAtmosphereMultiscatteringName, atmosphere.Multiscattering, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kAtmosphereMultiscatteringName, "Destination");
    }

    void SkyAtmosphereMultiscatteringPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereMultiscattering) || !m_compute_pass)
            return;

        const Scenes::SkyEnvironmentBakeRequest* bake       = m_environment->GetActiveBake();
        const Textures::TextureHandle            output     = m_environment->GetActiveBakeAtmosphere().Multiscattering;
        const uint32_t                           resolution = GetWidth(device, output);
        if (!bake || resolution == 0)
            return;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        const AtmosphereStaticPushConstants push = MakeAtmosphereStaticPushConstants(bake->Config.Atmosphere);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 1);
        MakeShaderReadable(device, command_buffer, output);
        m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::AtmosphereMultiscattering);
    }

    cstring SkyAtmosphereSourceRadiancePass::GetShaderName() const
    {
        return "sky_atmosphere_source_radiance";
    }

    uint32_t SkyAtmosphereSourceRadiancePass::GetPushConstantSize() const
    {
        return sizeof(AtmosphereSourceRadiancePushConstants);
    }

    bool SkyAtmosphereSourceRadiancePass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereSourceRadiance) && m_environment->GetActiveBakeAtmosphere().Valid();
    }

    void SkyAtmosphereSourceRadiancePass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Scenes::AtmosphereStaticResources& atmosphere = m_environment->GetActiveBakeAtmosphere();
        res_builder->ReadTexture(res_builder->ImportTexture(kAtmosphereTransmittanceName, atmosphere.Transmittance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Transmittance");
        res_builder->ReadTexture(res_builder->ImportTexture(kAtmosphereMultiscatteringName, atmosphere.Multiscattering, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Multiscattering");
        res_builder->ImportTexture(kSourceRadianceName, m_environment->GetActiveBakeSource(), VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kSourceRadianceName, "Destination", {.LayerCount = 6});
    }

    void SkyAtmosphereSourceRadiancePass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::AtmosphereSourceRadiance) || !m_compute_pass)
            return;

        const Scenes::SkyEnvironmentBakeRequest* bake       = m_environment->GetActiveBake();
        const Textures::TextureHandle            output     = m_environment->GetActiveBakeSource();
        const uint32_t                           resolution = GetWidth(device, output);
        if (!bake || resolution == 0)
            return;

        AtmosphereSourceRadiancePushConstants push        = {};
        push.StaticAtmosphere                             = MakeAtmosphereStaticPushConstants(bake->Config.Atmosphere);
        push.SunDirectionAndRadius[0]                     = bake->CelestialLight.DirectionToLight[0];
        push.SunDirectionAndRadius[1]                     = bake->CelestialLight.DirectionToLight[1];
        push.SunDirectionAndRadius[2]                     = bake->CelestialLight.DirectionToLight[2];
        push.SunDirectionAndRadius[3]                     = bake->Config.Atmosphere.SunAngularRadiusRadians;
        push.SunRadianceAvailabilityAndCaptureAltitude[0] = Scenes::ConvertSunIlluminanceToSceneRadiance(bake->Config.Atmosphere.SunIlluminanceLux);
        push.SunRadianceAvailabilityAndCaptureAltitude[1] = bake->CelestialLight.IsAvailable ? 1.0f : 0.0f;
        push.SunRadianceAvailabilityAndCaptureAltitude[2] = kSourceCaptureAltitudeKilometers;
        push.GroundAlbedoAndAmbient[0]                    = bake->Config.Atmosphere.GroundAlbedo[0];
        push.GroundAlbedoAndAmbient[1]                    = bake->Config.Atmosphere.GroundAlbedo[1];
        push.GroundAlbedoAndAmbient[2]                    = bake->Config.Atmosphere.GroundAlbedo[2];
        push.GroundAlbedoAndAmbient[3]                    = bake->Config.Atmosphere.GroundAmbientIrradiance;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
        MakeShaderReadable(device, command_buffer, output);
        m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::AtmosphereSourceRadiance);
    }

    cstring SkyEnvironmentMipGenerationPass::GetShaderName() const
    {
        return m_shader_name;
    }

    uint32_t SkyEnvironmentMipGenerationPass::GetPushConstantSize() const
    {
        return sizeof(MipGenerationPushConstants);
    }

    bool SkyEnvironmentMipGenerationPass::ShouldRegisterCompute() const
    {
        const Scenes::SkyEnvironmentBakeRequest* const bake = m_environment ? m_environment->GetActiveBake() : nullptr;
        return bake && bake->Config.IsAtmosphere() == m_atmosphere_source && IsActive(Scenes::SkyEnvironmentBakeStage::SourceMipChain);
    }

    void SkyEnvironmentMipGenerationPass::RegisterCompute(Hardwares::VulkanDevicePtr const device, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        auto* const                   rrm    = m_environment && device && device->RRM ? static_cast<Rendering::RenderResourceManager*>(device->RRM) : nullptr;
        if (const auto* ticket = rrm ? rrm->FindStreamingUploadTicket(source) : nullptr)
            res_builder->ImportStreamingTexture(kSourceRadianceName, *ticket);
        else
            res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        res_builder->ReadWriteStorageImage(kSourceRadianceName, "Destinations");
    }

    void SkyEnvironmentMipGenerationPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        SkyEnvironmentBakePass::Prepare(device, scene, res_inspector, pass);
        if (!m_compute_pass || !m_environment)
            return;
        m_compute_pass->SetTexture("SourceRadiance", m_environment->GetActiveBakeSource(), VK_IMAGE_LAYOUT_GENERAL);
        BindMipDestinations(m_compute_pass, device, m_environment->GetActiveBakeSource());
    }

    void SkyEnvironmentMipGenerationPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::SourceMipChain) || !m_compute_pass)
            return;

        const Textures::TextureHandle source    = m_environment->GetActiveBakeSource();
        const uint32_t                mip_count = GetMipCount(device, source);
        if (mip_count == 0 || mip_count > kMaxSkyMipCount)
            return;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        for (uint32_t mip = 1; mip < mip_count; ++mip)
        {
            const uint32_t                   downsampled_resolution = GetWidth(device, source) >> mip;
            const uint32_t                   resolution             = downsampled_resolution > 1u ? downsampled_resolution : 1u;
            const MipGenerationPushConstants push                   = {.SourceMip = mip - 1, .DestinationMip = mip, .DestinationResolution = resolution};
            command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
            MakeMipVisible(device, command_buffer, source, mip);
        }
        MakeShaderReadable(device, command_buffer, source);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::SourceMipChain);
    }

    cstring SkyEnvironmentDiffuseIrradiancePass::GetShaderName() const
    {
        return "sky_environment_diffuse_irradiance";
    }

    uint32_t SkyEnvironmentDiffuseIrradiancePass::GetPushConstantSize() const
    {
        return sizeof(DiffuseIrradiancePushConstants);
    }

    bool SkyEnvironmentDiffuseIrradiancePass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance) && m_environment->GetActiveBakeLighting().DiffuseIrradiance.Valid();
    }

    void SkyEnvironmentDiffuseIrradiancePass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle output = m_environment->GetActiveBakeLighting().DiffuseIrradiance;
        res_builder->ReadTexture(res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "SourceRadiance");
        res_builder->ImportTexture(kDiffuseIrradianceName, output, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kDiffuseIrradianceName, "Destination");
    }

    void SkyEnvironmentDiffuseIrradiancePass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance) || !m_compute_pass)
            return;

        const EnvironmentLightingBakeSettings& bake_settings = m_environment->GetActiveBakeLighting().BakeSettings;
        const Textures::TextureHandle          output        = m_environment->GetActiveBakeLighting().DiffuseIrradiance;
        const uint32_t                         resolution    = GetWidth(device, output);
        if (resolution == 0)
            return;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        const DiffuseIrradiancePushConstants push = {.Resolution = resolution, .SampleCount = bake_settings.DiffuseSampleCount};
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
        MakeShaderReadable(device, command_buffer, output);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::DiffuseIrradiance);
    }

    cstring SkyEnvironmentSpecularPrefilterPass::GetShaderName() const
    {
        return "sky_environment_specular_prefilter";
    }

    uint32_t SkyEnvironmentSpecularPrefilterPass::GetPushConstantSize() const
    {
        return sizeof(SpecularPrefilterPushConstants);
    }

    bool SkyEnvironmentSpecularPrefilterPass::ShouldRegisterCompute() const
    {
        return IsActive(Scenes::SkyEnvironmentBakeStage::SpecularEnvironment) && m_environment->GetActiveBakeLighting().SpecularEnvironment.Valid();
    }

    void SkyEnvironmentSpecularPrefilterPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const scene, RenderGraphResourceInspectorPtr res_inspector, RenderPasses::RenderPass* const pass)
    {
        SkyEnvironmentBakePass::Prepare(device, scene, res_inspector, pass);
        if (m_compute_pass && m_environment)
            BindMipDestinations(m_compute_pass, device, m_environment->GetActiveBakeLighting().SpecularEnvironment);
    }

    void SkyEnvironmentSpecularPrefilterPass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const Textures::TextureHandle source = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle output = m_environment->GetActiveBakeLighting().SpecularEnvironment;
        res_builder->ReadTexture(res_builder->ImportTexture(kSourceRadianceName, source, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "SourceRadiance");
        res_builder->ImportTexture(kSpecularEnvironmentName, output, VK_IMAGE_LAYOUT_UNDEFINED);
        res_builder->ReadWriteStorageImage(kSpecularEnvironmentName, "Destinations");
    }

    void SkyEnvironmentSpecularPrefilterPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!IsActive(Scenes::SkyEnvironmentBakeStage::SpecularEnvironment) || !m_compute_pass)
            return;

        const EnvironmentLightingBakeSettings& bake_settings   = m_environment->GetActiveBakeLighting().BakeSettings;
        const Textures::TextureHandle          source          = m_environment->GetActiveBakeSource();
        const Textures::TextureHandle          output          = m_environment->GetActiveBakeLighting().SpecularEnvironment;
        const uint32_t                         mip_count       = GetMipCount(device, output);
        const uint32_t                         base_resolution = GetWidth(device, output);
        const uint32_t                         source_mips     = GetMipCount(device, source);
        if (mip_count == 0 || mip_count > kMaxSkyMipCount || base_resolution == 0 || source_mips == 0)
            return;

        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        for (uint32_t mip = 0; mip < mip_count; ++mip)
        {
            const uint32_t                       downsampled_resolution = base_resolution >> mip;
            const uint32_t                       resolution             = downsampled_resolution > 1u ? downsampled_resolution : 1u;
            const SpecularPrefilterPushConstants push                   = {
                .Resolution     = resolution,
                .MipLevel       = mip,
                .SampleCount    = bake_settings.SpecularSampleCount,
                .SourceMipCount = source_mips,
                .Roughness      = mip_count > 1 ? static_cast<float>(mip) / static_cast<float>(mip_count - 1) : 0.0f,
            };
            command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            command_buffer->Dispatch((resolution + kLocalSize - 1) / kLocalSize, (resolution + kLocalSize - 1) / kLocalSize, 6);
        }
        MakeShaderReadable(device, command_buffer, output);
        if (const auto* bake = m_environment->GetActiveBake())
            m_environment->NotifyGpuBakeStageRecorded(bake->Revision, Scenes::SkyEnvironmentBakeStage::SpecularEnvironment);
    }
} // namespace ZEngine::Rendering::Renderers
