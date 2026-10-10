#include <ZEngine/Rendering/Renderers/Compute/SkyAtmosphereViewPass.h>
#include <ZEngine/Rendering/Renderers/RendererContracts.h>

using namespace ZEngine::Rendering::Specifications;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        constexpr uint32_t kSkyViewWidth      = 192;
        constexpr uint32_t kSkyViewHeight     = 108;
        constexpr uint32_t kAerialResolution  = 32;
        constexpr uint32_t kLocalSize2D       = 8;
        constexpr uint32_t kLocalSizeAerialZ  = 4;
        constexpr float    kAerialMaxDistance = 96.0f;

        RGSubresourceRange MakeColorSubresourceRange()
        {
            RGSubresourceRange range = {};
            range.AspectMask         = VK_IMAGE_ASPECT_COLOR_BIT;
            range.BaseMipLevel       = 0;
            range.LevelCount         = 1;
            range.BaseArrayLayer     = 0;
            range.LayerCount         = 1;
            return range;
        }

        TextureSpecification MakeSkyViewSpecification()
        {
            TextureSpecification specification  = {};
            specification.IsUsageSampled        = true;
            specification.IsUsageStorage        = true;
            specification.IsUsageTransfert      = false;
            specification.IsUsageTransferSource = false;
            specification.Width                 = kSkyViewWidth;
            specification.Height                = kSkyViewHeight;
            specification.Depth                 = 1;
            specification.BytePerPixel          = sizeof(uint16_t) * 4;
            specification.Format                = ImageFormat::R16G16B16A16_SFLOAT;
            return specification;
        }

        TextureSpecification MakeAerialSpecification()
        {
            TextureSpecification specification  = {};
            specification.IsUsageSampled        = true;
            specification.IsUsageStorage        = true;
            specification.IsUsageTransfert      = false;
            specification.IsUsageTransferSource = false;
            specification.Is3D                  = true;
            specification.Width                 = kAerialResolution;
            specification.Height                = kAerialResolution;
            specification.Depth                 = kAerialResolution;
            specification.BytePerPixel          = sizeof(uint16_t) * 4;
            specification.Format                = ImageFormat::R16G16B16A16_SFLOAT;
            return specification;
        }
    } // namespace

    void SkyAtmosphereViewPass::SetEnvironment(const Scenes::SkyEnvironmentSnapshot* snapshot, const Scenes::SkyConfig& presentation)
    {
        m_active = snapshot && snapshot->Config.IsAtmosphere() && snapshot->Atmosphere.Valid() && snapshot->CelestialLight.IsAvailable && snapshot->CelestialLight.IsValid();
        if (!m_active)
        {
            m_config       = {};
            m_presentation = {};
            m_celestial    = {};
            m_atmosphere   = {};
            return;
        }

        m_config            = snapshot->Config;
        m_presentation      = presentation;
        m_celestial         = snapshot->CelestialLight;
        m_atmosphere        = snapshot->Atmosphere;

        // Scene placement is evaluated per view and does not affect the
        // planet-centred source capture. Ground values remain in the published
        // snapshot because they also contribute to source radiance and IBL.
        m_config.Atmosphere = Scenes::MakeAtmosphereViewSettings(snapshot->Config.Atmosphere, presentation.Atmosphere);
    }

    void SkyAtmosphereViewPass::SetCameraPosition(const Core::Maths::Vec3f& position)
    {
        m_camera_pos = position;
    }

    void SkyAtmosphereViewPass::Prepare(Hardwares::VulkanDevicePtr const device, Rendering::Scenes::SceneDataPtr const /*scene*/, RenderGraphResourceInspectorPtr /*res_inspector*/, RenderPasses::RenderPass* const pass)
    {
        m_compute_pass = pass ? static_cast<RenderPasses::ComputePass*>(pass) : nullptr;
        if (!m_compute_pass || !device || !IsViewActive())
            return;

        m_compute_pass->SetSampler("LinearClampToEdgeSampler", device->GlobalLinearClampToEdgeSamplerImageInfo);
    }

    bool SkyAtmosphereViewPass::IsViewActive() const
    {
        if (!m_active || !m_config.IsAtmosphere() || !m_atmosphere.Valid() || !m_celestial.IsAvailable || !m_celestial.IsValid())
            return false;

        const Scenes::AtmosphereViewClass view_class = Scenes::ClassifyAtmosphereView(m_config.Atmosphere, m_camera_pos);
        return view_class == Scenes::AtmosphereViewClass::InsideAtmosphere || view_class == Scenes::AtmosphereViewClass::OutsideAtmosphere;
    }

    bool SkyAtmosphereViewPass::IsRenderableView(const RenderGraphFrameContext& frame_context) const
    {
        return IsViewActive() && frame_context.RenderWidth != 0 && frame_context.RenderHeight != 0;
    }

    AtmosphereViewPushConstants SkyAtmosphereViewPass::MakePushConstants() const
    {
        const Scenes::AtmosphereSettings& atmosphere          = m_config.Atmosphere;
        const float                       units_per_kilometer = atmosphere.WorldUnitsPerMeter > 0.0f ? atmosphere.WorldUnitsPerMeter * 1000.0f : 1.0f;

        AtmosphereViewPushConstants       push                = {};
        push.PlanetCenterRelativeAndMaxDistance[0]            = (atmosphere.PlanetCenterWorld[0] - m_camera_pos.x) / units_per_kilometer;
        push.PlanetCenterRelativeAndMaxDistance[1]            = (atmosphere.PlanetCenterWorld[1] - m_camera_pos.y) / units_per_kilometer;
        push.PlanetCenterRelativeAndMaxDistance[2]            = (atmosphere.PlanetCenterWorld[2] - m_camera_pos.z) / units_per_kilometer;
        push.PlanetCenterRelativeAndMaxDistance[3]            = kAerialMaxDistance;
        push.RadiiAndScaleHeights[0]                          = atmosphere.PlanetRadiusKilometers;
        push.RadiiAndScaleHeights[1]                          = atmosphere.AtmosphereRadiusKilometers;
        push.RadiiAndScaleHeights[2]                          = atmosphere.RayleighScaleHeightKilometers;
        push.RadiiAndScaleHeights[3]                          = atmosphere.MieScaleHeightKilometers;
        push.RayleighScatteringAndGroundAlbedoR[0]            = atmosphere.RayleighScatteringPerKilometer[0];
        push.RayleighScatteringAndGroundAlbedoR[1]            = atmosphere.RayleighScatteringPerKilometer[1];
        push.RayleighScatteringAndGroundAlbedoR[2]            = atmosphere.RayleighScatteringPerKilometer[2];
        push.RayleighScatteringAndGroundAlbedoR[3]            = atmosphere.GroundAlbedo[0];
        push.MieAndOzone[0]                                   = atmosphere.MieScatteringPerKilometer;
        push.MieAndOzone[1]                                   = atmosphere.MieAbsorptionPerKilometer;
        push.MieAndOzone[2]                                   = atmosphere.OzoneCenterKilometers;
        push.MieAndOzone[3]                                   = atmosphere.OzoneThicknessKilometers;
        push.OzoneAbsorption[0]                               = atmosphere.OzoneAbsorptionPerKilometer[0];
        push.OzoneAbsorption[1]                               = atmosphere.OzoneAbsorptionPerKilometer[1];
        push.OzoneAbsorption[2]                               = atmosphere.OzoneAbsorptionPerKilometer[2];
        push.OzoneAbsorption[3]                               = atmosphere.MieAnisotropy;
        push.SunDirectionAndRadius[0]                         = m_celestial.DirectionToLight[0];
        push.SunDirectionAndRadius[1]                         = m_celestial.DirectionToLight[1];
        push.SunDirectionAndRadius[2]                         = m_celestial.DirectionToLight[2];
        push.SunDirectionAndRadius[3]                         = atmosphere.SunAngularRadiusRadians;
        push.SunRadianceAvailabilityAndGroundAlbedoGB[0]      = Scenes::ConvertSunIlluminanceToSceneRadiance(atmosphere.SunIlluminanceLux);
        push.SunRadianceAvailabilityAndGroundAlbedoGB[1]      = m_celestial.IsAvailable ? 1.0f : 0.0f;
        push.SunRadianceAvailabilityAndGroundAlbedoGB[2]      = atmosphere.GroundAlbedo[1];
        push.SunRadianceAvailabilityAndGroundAlbedoGB[3]      = atmosphere.GroundAlbedo[2];
        push.PresentationTintIntensityAndGroundAmbient[0]     = m_presentation.EnvironmentTint[0] * m_presentation.EnvironmentIntensity;
        push.PresentationTintIntensityAndGroundAmbient[1]     = m_presentation.EnvironmentTint[1] * m_presentation.EnvironmentIntensity;
        push.PresentationTintIntensityAndGroundAmbient[2]     = m_presentation.EnvironmentTint[2] * m_presentation.EnvironmentIntensity;
        push.PresentationTintIntensityAndGroundAmbient[3]     = atmosphere.GroundAmbientIrradiance;
        return push;
    }

    cstring SkyViewLutPass::GetShaderName() const
    {
        return "sky_atmosphere_sky_view";
    }

    uint32_t SkyViewLutPass::GetPushConstantSize() const
    {
        return sizeof(AtmosphereViewPushConstants);
    }

    bool SkyViewLutPass::ShouldRegisterCompute(const RenderGraphFrameContext& frame_context) const
    {
        return IsRenderableView(frame_context);
    }

    void SkyViewLutPass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const RGSubresourceRange range = MakeColorSubresourceRange();
        res_builder->ReadTexture(res_builder->ImportTexture("SkyViewTransmittance", m_atmosphere.Transmittance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Transmittance", range);
        res_builder->ReadTexture(res_builder->ImportTexture("SkyViewMultiscattering", m_atmosphere.Multiscattering, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Multiscattering", range);
        res_builder->WriteStorageImage(RendererResourceName::FrameSkyViewLutName, MakeSkyViewSpecification(), "Destination", range);
    }

    void SkyViewLutPass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !m_compute_pass || !IsViewActive())
            return;

        const AtmosphereViewPushConstants push = MakePushConstants();
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((kSkyViewWidth + kLocalSize2D - 1) / kLocalSize2D, (kSkyViewHeight + kLocalSize2D - 1) / kLocalSize2D, 1);
    }

    cstring AerialPerspectivePass::GetShaderName() const
    {
        return "sky_atmosphere_aerial_perspective";
    }

    uint32_t AerialPerspectivePass::GetPushConstantSize() const
    {
        return sizeof(AtmosphereViewPushConstants);
    }

    bool AerialPerspectivePass::ShouldRegisterCompute(const RenderGraphFrameContext& frame_context) const
    {
        return IsRenderableView(frame_context);
    }

    void AerialPerspectivePass::RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const res_builder)
    {
        const RGSubresourceRange range = MakeColorSubresourceRange();
        res_builder->ReadTexture(res_builder->ImportTexture("AerialTransmittanceStatic", m_atmosphere.Transmittance, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Transmittance", range);
        res_builder->ReadTexture(res_builder->ImportTexture("AerialMultiscatteringStatic", m_atmosphere.Multiscattering, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL), "Multiscattering", range);
        res_builder->WriteStorageImage(RendererResourceName::FrameAerialInscatterName, MakeAerialSpecification(), "InscatterDestination", range);
        res_builder->WriteStorageImage(RendererResourceName::FrameAerialTransmittanceName, MakeAerialSpecification(), "TransmittanceDestination", range);
    }

    void AerialPerspectivePass::ExecuteCompute(Hardwares::VulkanDevicePtr const device, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const command_buffer)
    {
        if (!device || !device->SwapchainPtr || !device->SwapchainPtr->CurrentFrame || !m_compute_pass || !IsViewActive())
            return;

        const AtmosphereViewPushConstants push = MakePushConstants();
        command_buffer->BindDescriptorSets(device->SwapchainPtr->CurrentFrame->Index);
        command_buffer->PushConstants(VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        command_buffer->Dispatch((kAerialResolution + kLocalSize2D - 1) / kLocalSize2D, (kAerialResolution + kLocalSize2D - 1) / kLocalSize2D, (kAerialResolution + kLocalSizeAerialZ - 1) / kLocalSizeAerialZ);
    }
} // namespace ZEngine::Rendering::Renderers
