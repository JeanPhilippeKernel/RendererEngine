#include <ZEngine/Rendering/Renderers/Compute/SkyAtmosphereViewPass.h>
#include <ZEngine/Rendering/Scenes/SkyEnvironment.h>
#include <gtest/gtest.h>

using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Scenes;

namespace
{
    Textures::TextureHandle Texture(uint64_t index)
    {
        return {.Index = index, .Generation = 1};
    }

    EnvironmentLightingResources Lighting(uint64_t first_index, const EnvironmentLightingBakeSettings& bake_settings = ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard))
    {
        return {
            .DiffuseIrradiance   = Texture(first_index),
            .SpecularEnvironment = Texture(first_index + 1),
            .BrdfIntegrationLut  = Texture(first_index + 2),
            .BakeSettings        = bake_settings,
        };
    }

    SkyConfig HDRIConfig()
    {
        SkyConfig config = {};
        config.Mode      = SkyMode::HDRI;
        return config;
    }

    SkyConfig SkySphereConfig()
    {
        SkyConfig config = {};
        config.Mode      = SkyMode::SkySphere;
        return config;
    }

    AtmosphereStaticResources Atmosphere(uint64_t first_index)
    {
        return {
            .Transmittance   = Texture(first_index),
            .Multiscattering = Texture(first_index + 1),
        };
    }

    struct SkyAtmosphereViewPassProbe final : Renderers::SkyAtmosphereViewPass
    {
        using Renderers::SkyAtmosphereViewPass::MakePushConstants;

        void    RegisterCompute(ZEngine::Hardwares::VulkanDevicePtr const /*device*/, const Renderers::RenderGraphFrameContext& /*frame_context*/, Renderers::RenderGraphResourceBuilderPtr const /*res_builder*/) override {}
        void    ExecuteCompute(ZEngine::Hardwares::VulkanDevicePtr const /*device*/, Renderers::RenderGraphResourceInspectorPtr /*res_inspector*/, SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, ZEngine::Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
        cstring GetShaderName() const override
        {
            return "";
        }
    };
} // namespace

TEST(SkyEnvironmentTest, FirstFramePinsTheValidFallbackSnapshot)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    const SkyEnvironmentSnapshot* snapshot = environment.AcquireForFrame();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->IsFallback);
    EXPECT_EQ(snapshot->State, SkyEnvironmentState::Fallback);
    EXPECT_EQ(snapshot->SourceRadiance.Index, 1u);
    EXPECT_TRUE(snapshot->Lighting.Valid());
    EXPECT_EQ(snapshot->PinCount, 1u);

    environment.ReleaseSubmittedFrame(7);
    EXPECT_EQ(snapshot->PinCount, 0u);
    EXPECT_EQ(snapshot->LastUseTimeline, 7u);
}

TEST(SkyEnvironmentTest, RapidEditsCoalesceToTheNewestRevision)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyConfig first             = HDRIConfig();
    first.EnvironmentIntensity  = 1.0f;
    SkyConfig latest            = first;
    latest.EnvironmentIntensity = 3.0f;

    EXPECT_TRUE(environment.SubmitConfig(first, 10));
    EXPECT_TRUE(environment.SubmitConfig(latest, 12));
    EXPECT_FALSE(environment.SubmitConfig(first, 11));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, 12u);
    EXPECT_FLOAT_EQ(request.Config.EnvironmentIntensity, 3.0f);
    EXPECT_FALSE(environment.TakeBakeRequest(request));
}

TEST(SkyEnvironmentTest, PresentationOnlyChangesDoNotScheduleAnotherBake)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);

    SkyConfig presentation             = HDRIConfig();
    presentation.EnvironmentIntensity  = 2.0f;
    presentation.EnvironmentTint[0]    = 0.5f;
    presentation.EnvironmentYawRadians = 1.0f;
    ASSERT_TRUE(environment.SubmitConfig(presentation, 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_EQ(environment.GetPublishedSnapshot()->Revision, 1u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 2u);
    EXPECT_FLOAT_EQ(environment.GetPresentationConfig().EnvironmentIntensity, 2.0f);
    EXPECT_FLOAT_EQ(environment.GetPresentationConfig().EnvironmentTint[0], 0.5f);
    EXPECT_FLOAT_EQ(environment.GetPresentationConfig().EnvironmentYawRadians, 1.0f);
}

TEST(SkyEnvironmentTest, SkySphereUsesFallbackResourcesWithoutSchedulingABake)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyConfig sky_sphere                = SkySphereConfig();
    sky_sphere.EnvironmentIntensity     = 2.0f;
    SkyCelestialLight celestial_light   = {};
    celestial_light.DirectionToLight[0] = 1.0f;
    celestial_light.IsAvailable         = true;

    ASSERT_TRUE(environment.SubmitConfig(sky_sphere, 1, {}, celestial_light));
    SkyEnvironmentBakeRequest request = {};
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 1u);
    EXPECT_EQ(environment.GetFallbackLighting().DiffuseIrradiance.Index, 10u);
    EXPECT_TRUE(environment.GetPresentationConfig().IsSkySphere());
    EXPECT_TRUE(environment.GetPresentationCelestialLight().IsAvailable);
    EXPECT_FLOAT_EQ(environment.GetPresentationCelestialLight().DirectionToLight[0], 1.0f);
    EXPECT_EQ(environment.GetState(), SkyEnvironmentState::Fallback);
}

TEST(SkyEnvironmentTest, QualityChangeDiscardsThePreviousRevisionAndSchedulesNewResources)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    const EnvironmentLightingBakeSettings low      = ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Low);
    const EnvironmentLightingBakeSettings standard = ResolveEnvironmentLightingQuality(EnvironmentLightingQualityTier::Standard);
    ASSERT_FALSE(low.Matches(standard));
    ASSERT_EQ(low.DiffuseSampleCount, 16u);
    ASSERT_EQ(standard.SpecularSampleCount, 128u);

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, low));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(request.BakeSettings.Matches(low));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(20, low)));

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, standard));
    SkyConfig presentation            = HDRIConfig();
    presentation.EnvironmentIntensity = 2.0f;
    ASSERT_TRUE(environment.SubmitConfig(presentation, 3, standard));
    EXPECT_EQ(environment.CompleteBake(1, Texture(2), true, Lighting(20, low)), SkyEnvironmentBakeResult::Discarded);

    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, 3u);
    EXPECT_TRUE(request.BakeSettings.Matches(standard));
}

TEST(SkyEnvironmentTest, PresentationChangesKeepAnInFlightBakeCurrent)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));

    SkyConfig presentation            = HDRIConfig();
    presentation.EnvironmentIntensity = 2.0f;
    ASSERT_TRUE(environment.SubmitConfig(presentation, 2));
    EXPECT_EQ(environment.CompleteBake(2, Texture(2), true), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Revision, 2u);
    EXPECT_FLOAT_EQ(environment.GetPublishedSnapshot()->Config.EnvironmentIntensity, 2.0f);
}

TEST(SkyEnvironmentTest, SkySphereCancelsStaleBakeAndKeepsTheFallback)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyConfig config = HDRIConfig();
    ASSERT_TRUE(environment.SubmitConfig(config, 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));

    config = SkySphereConfig();
    ASSERT_TRUE(environment.SubmitConfig(config, 2));
    EXPECT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Discarded);
    ASSERT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPresentationConfig().IsSkySphere());
}

TEST(SkyEnvironmentTest, FailedBakeRetainsVisibleFallback)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Failed);

    const SkyEnvironmentSnapshot* snapshot = environment.GetPublishedSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->IsFallback);
    EXPECT_TRUE(snapshot->SourceRadiance.Valid());
    EXPECT_EQ(environment.GetState(), SkyEnvironmentState::Failed);
}

TEST(SkyEnvironmentTest, SkySphereReplacesReadyEnvironmentWithFallback)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);

    SkyConfig replacement = SkySphereConfig();
    ASSERT_TRUE(environment.SubmitConfig(replacement, 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));

    const SkyEnvironmentSnapshot* snapshot = environment.GetPublishedSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_TRUE(snapshot->IsFallback);
    EXPECT_EQ(snapshot->SourceRadiance.Index, 1u);
    EXPECT_EQ(environment.GetState(), SkyEnvironmentState::Fallback);

    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
}

TEST(SkyEnvironmentTest, ReplacedSnapshotWaitsForItsSubmittedFrameTimeline)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);

    const SkyEnvironmentSnapshot* first = environment.AcquireForFrame();
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first->SourceRadiance.Index, 2u);

    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);

    SkyEnvironmentResources retired = {};
    EXPECT_FALSE(environment.TakeRetiredSnapshot(0, retired));
    environment.ReleaseSubmittedFrame(9);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(8, retired));
    ASSERT_TRUE(environment.TakeRetiredSnapshot(9, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
}

TEST(SkyEnvironmentTest, ReplacedSnapshotWaitsForEverySubmittedFrameConsumer)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);

    ASSERT_NE(environment.AcquireForFrame(), nullptr);
    ASSERT_NE(environment.AcquireForFrame(), nullptr);

    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);

    SkyEnvironmentResources retired = {};
    environment.ReleaseSubmittedFrame(7);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(7, retired));
    environment.ReleaseSubmittedFrame(8);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(7, retired));
    ASSERT_TRUE(environment.TakeRetiredSnapshot(8, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
}

TEST(SkyEnvironmentTest, CancelledFrameDoesNotLeaveAReplacementPinned)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);
    ASSERT_NE(environment.AcquireForFrame(), nullptr);

    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    environment.ReleaseCancelledFrame();

    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(0, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
}

TEST(SkyEnvironmentTest, GpuBakeStagesAdvanceOnlyAfterTheirSubmittedTimelineCompletes)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(20)));
    ASSERT_TRUE(environment.BeginGpuBake(1));

    EXPECT_EQ(environment.GetActiveBakeStage(), SkyEnvironmentBakeStage::SourceMipChain);
    ASSERT_TRUE(environment.CanRecordGpuBakeStage());
    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, SkyEnvironmentBakeStage::SourceMipChain));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 5));
    EXPECT_FALSE(environment.CanRecordGpuBakeStage());
    EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(4));
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(5));
    EXPECT_EQ(environment.GetActiveBakeStage(), SkyEnvironmentBakeStage::DiffuseIrradiance);

    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, SkyEnvironmentBakeStage::DiffuseIrradiance));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 6));
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(6));
    EXPECT_EQ(environment.GetActiveBakeStage(), SkyEnvironmentBakeStage::SpecularEnvironment);

    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, SkyEnvironmentBakeStage::SpecularEnvironment));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 7));
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(7));
    EXPECT_TRUE(environment.IsGpuBakeReadyToPublish());
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true, Lighting(20)), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Lighting.DiffuseIrradiance.Index, 20u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Lighting.SpecularEnvironment.Index, 21u);
}

TEST(SkyEnvironmentTest, AtmosphereBakeIncludesStaticLutsBeforeSourceRadiance)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyCelestialLight sun = {};
    sun.IsAvailable       = true;
    ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(request.BakeInputsValid);
    ASSERT_TRUE(environment.AttachBakeAtmosphere(1, Atmosphere(20)));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(30)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(40)));
    ASSERT_TRUE(environment.BeginGpuBake(1));

    constexpr SkyEnvironmentBakeStage stages[] = {
        SkyEnvironmentBakeStage::AtmosphereTransmittance,
        SkyEnvironmentBakeStage::AtmosphereMultiscattering,
        SkyEnvironmentBakeStage::AtmosphereSourceRadiance,
        SkyEnvironmentBakeStage::SourceMipChain,
        SkyEnvironmentBakeStage::DiffuseIrradiance,
        SkyEnvironmentBakeStage::SpecularEnvironment,
    };
    for (uint64_t index = 0; index < sizeof(stages) / sizeof(stages[0]); ++index)
    {
        const SkyEnvironmentBakeStage stage = stages[index];
        ASSERT_EQ(environment.GetActiveBakeStage(), stage);
        ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, stage));
        ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, index + 1));
        ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(index + 1));
    }

    ASSERT_TRUE(environment.IsGpuBakeReadyToPublish());
    ASSERT_EQ(environment.CompleteBake(1, Texture(30), true, Lighting(40), Atmosphere(20)), SkyEnvironmentBakeResult::Published);
    ASSERT_NE(environment.GetPublishedSnapshot(), nullptr);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Atmosphere.Transmittance.Index, 20u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Atmosphere.Multiscattering.Index, 21u);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->CelestialLight.IsAvailable);
    EXPECT_FLOAT_EQ(environment.GetPublishedSnapshot()->CelestialLight.DirectionToLight[1], 1.0f);
}

TEST(SkyEnvironmentTest, PerViewAtmospherePassOmitsMinimizedViews)
{
    SkyEnvironmentSnapshot snapshot     = {};
    snapshot.Config.Mode                = SkyMode::Atmosphere;
    snapshot.Atmosphere                 = Atmosphere(20);
    snapshot.CelestialLight.IsAvailable = true;

    Renderers::SkyViewLutPass sky_view  = {};
    sky_view.SetEnvironment(&snapshot, snapshot.Config);
    sky_view.SetCameraPosition({0.0f, 5.0f, 0.0f});

    Renderers::RenderGraphFrameContext minimized = {};
    minimized.RenderWidth                        = 0;
    minimized.RenderHeight                       = 720;
    EXPECT_FALSE(sky_view.ShouldRegisterCompute(minimized));

    Renderers::RenderGraphFrameContext renderable = {};
    renderable.RenderWidth                        = 1280;
    renderable.RenderHeight                       = 720;
    EXPECT_TRUE(sky_view.ShouldRegisterCompute(renderable));
}

TEST(SkyEnvironmentTest, AtmosphereViewClassificationUsesTheConfiguredPlanetSurface)
{
    const AtmosphereSettings atmosphere = {};

    EXPECT_EQ(ClassifyAtmosphereView(atmosphere, {0.0f, 5.0f, 0.0f}), AtmosphereViewClass::InsideAtmosphere);
    EXPECT_EQ(ClassifyAtmosphereView(atmosphere, {0.0f, 0.0f, 0.0f}), AtmosphereViewClass::BelowGround);
    EXPECT_EQ(ClassifyAtmosphereView(atmosphere, {0.0f, -5.0f, 0.0f}), AtmosphereViewClass::BelowGround);
    EXPECT_EQ(ClassifyAtmosphereView(atmosphere, {0.0f, 101000.0f, 0.0f}), AtmosphereViewClass::OutsideAtmosphere);

    AtmosphereSettings invalid_scale = atmosphere;
    invalid_scale.WorldUnitsPerMeter = 0.0f;
    EXPECT_EQ(ClassifyAtmosphereView(invalid_scale, {0.0f, 5.0f, 0.0f}), AtmosphereViewClass::Invalid);

    AtmosphereSettings configured         = {};
    configured.PlanetCenterWorld[0]       = 100.0f;
    configured.PlanetCenterWorld[1]       = 200.0f;
    configured.PlanetCenterWorld[2]       = 300.0f;
    configured.WorldUnitsPerMeter         = 2.0f;
    configured.PlanetRadiusKilometers     = 2.0f;
    configured.AtmosphereRadiusKilometers = 3.0f;
    EXPECT_EQ(ClassifyAtmosphereView(configured, {100.0f, 4200.0f, 300.0f}), AtmosphereViewClass::BelowGround);
    EXPECT_EQ(ClassifyAtmosphereView(configured, {100.0f, 4201.0f, 300.0f}), AtmosphereViewClass::InsideAtmosphere);
    EXPECT_EQ(ClassifyAtmosphereView(configured, {100.0f, 7000.0f, 300.0f}), AtmosphereViewClass::OutsideAtmosphere);
    EXPECT_EQ(ClassifyAtmosphereView(configured, {2100.0f, 200.0f, 300.0f}), AtmosphereViewClass::BelowGround);
    EXPECT_EQ(ClassifyAtmosphereView(configured, {-1900.0f, 200.0f, 300.0f}), AtmosphereViewClass::BelowGround);
}

TEST(SkyEnvironmentTest, PerViewAtmospherePassSkipsBelowGroundViews)
{
    SkyEnvironmentSnapshot snapshot     = {};
    snapshot.Config.Mode                = SkyMode::Atmosphere;
    snapshot.Atmosphere                 = Atmosphere(20);
    snapshot.CelestialLight.IsAvailable = true;

    Renderers::SkyViewLutPass sky_view  = {};
    sky_view.SetEnvironment(&snapshot, snapshot.Config);

    Renderers::RenderGraphFrameContext frame_context = {};
    frame_context.RenderWidth                        = 1280;
    frame_context.RenderHeight                       = 720;

    sky_view.SetCameraPosition({0.0f, -5.0f, 0.0f});
    EXPECT_FALSE(sky_view.ShouldRegisterCompute(frame_context));

    sky_view.SetCameraPosition({0.0f, 5.0f, 0.0f});
    EXPECT_TRUE(sky_view.ShouldRegisterCompute(frame_context));
}

TEST(SkyAtmosphereViewPassTest, GroundValuesStayBoundToThePublishedSnapshot)
{
    SkyEnvironmentSnapshot snapshot                    = {};
    snapshot.Config.Mode                               = SkyMode::Atmosphere;
    snapshot.Config.Atmosphere.GroundAlbedo[0]         = 0.1f;
    snapshot.Config.Atmosphere.GroundAlbedo[1]         = 0.2f;
    snapshot.Config.Atmosphere.GroundAlbedo[2]         = 0.3f;
    snapshot.Config.Atmosphere.GroundAmbientIrradiance = 0.4f;
    snapshot.Atmosphere                                = Atmosphere(20);
    snapshot.CelestialLight.IsAvailable                = true;

    SkyConfig presentation                             = snapshot.Config;
    presentation.Atmosphere.PlanetCenterWorld[0]       = 4000.0f;
    presentation.Atmosphere.WorldUnitsPerMeter         = 2.0f;
    presentation.Atmosphere.GroundAlbedo[0]            = 0.8f;
    presentation.Atmosphere.GroundAlbedo[1]            = 0.7f;
    presentation.Atmosphere.GroundAlbedo[2]            = 0.6f;
    presentation.Atmosphere.GroundAmbientIrradiance    = 0.9f;

    SkyAtmosphereViewPassProbe sky_view                = {};
    sky_view.SetEnvironment(&snapshot, presentation);
    const Renderers::AtmosphereViewPushConstants push = sky_view.MakePushConstants();

    EXPECT_FLOAT_EQ(push.PlanetCenterRelativeAndMaxDistance[0], 2.0f);
    EXPECT_FLOAT_EQ(push.RayleighScatteringAndGroundAlbedoR[3], 0.1f);
    EXPECT_FLOAT_EQ(push.SunRadianceAvailabilityAndGroundAlbedoGB[2], 0.2f);
    EXPECT_FLOAT_EQ(push.SunRadianceAvailabilityAndGroundAlbedoGB[3], 0.3f);
    EXPECT_FLOAT_EQ(push.PresentationTintIntensityAndGroundAmbient[3], 0.4f);
}

TEST(SkyEnvironmentTest, InvalidInputsAreMarkedForFallbackInsteadOfRebakingDefaults)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyConfig invalid                             = {};
    invalid.Atmosphere.AtmosphereRadiusKilometers = invalid.Atmosphere.PlanetRadiusKilometers;
    ASSERT_TRUE(environment.SubmitConfig(invalid, 1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_FALSE(request.BakeInputsValid);
    EXPECT_TRUE(request.Config.IsValid());
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Failed);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentTest, AtmosphereWithoutSelectedSunKeepsTheFallbackSnapshot)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    ASSERT_TRUE(environment.SubmitConfig({}, 1));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_FALSE(request.BakeInputsValid);
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Failed);
    ASSERT_NE(environment.GetPublishedSnapshot(), nullptr);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentTest, AtmosphereSourceBakeIgnoresScenePlacementInputs)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyCelestialLight sun = {};
    sun.IsAvailable       = true;
    ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));

    SkyConfig moved                       = {};
    moved.Atmosphere.PlanetCenterWorld[0] = 250000.0f;
    moved.Atmosphere.PlanetCenterWorld[1] = -1000.0f;
    moved.Atmosphere.WorldUnitsPerMeter   = 100.0f;
    ASSERT_TRUE(environment.SubmitConfig(moved, 2, {}, sun));

    ASSERT_NE(environment.GetActiveBake(), nullptr);
    EXPECT_EQ(environment.GetActiveBake()->Revision, 2u);
    EXPECT_FALSE(environment.TakeBakeRequest(request));
}

TEST(SkyEnvironmentTest, GroundChangeRebakesSourceRadianceAndReusesStaticAtmosphere)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyCelestialLight sun = {};
    sun.IsAvailable       = true;
    ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeAtmosphere(1, Atmosphere(20)));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(30)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(40)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(30), true, Lighting(40), Atmosphere(20)), SkyEnvironmentBakeResult::Published);

    SkyConfig ground_change                          = {};
    ground_change.Atmosphere.GroundAlbedo[1]         = 0.4f;
    ground_change.Atmosphere.GroundAmbientIrradiance = 0.75f;
    ASSERT_TRUE(environment.SubmitConfig(ground_change, 2, {}, sun));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, 2u);
    EXPECT_FLOAT_EQ(request.Config.Atmosphere.GroundAlbedo[1], 0.4f);
    EXPECT_FLOAT_EQ(request.Config.Atmosphere.GroundAmbientIrradiance, 0.75f);

    const AtmosphereStaticResources* const reusable = environment.FindReusableAtmosphere(request.Config);
    ASSERT_NE(reusable, nullptr);
    EXPECT_EQ(reusable->Transmittance.Index, 20u);
    EXPECT_EQ(reusable->Multiscattering.Index, 21u);

    ASSERT_TRUE(environment.AttachBakeAtmosphere(2, *reusable, false));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(50)));
    ASSERT_TRUE(environment.AttachBakeLighting(2, Lighting(60)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(50), true, Lighting(60), *reusable), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 50u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Lighting.DiffuseIrradiance.Index, 60u);
    EXPECT_FLOAT_EQ(environment.GetPublishedSnapshot()->Config.Atmosphere.GroundAlbedo[1], 0.4f);
}

TEST(SkyEnvironmentTest, DirectionOnlyAtmosphereUpdateReusesStaticLutsUntilTheLastReferenceRetires)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyCelestialLight noon = {};
    noon.IsAvailable       = true;
    ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, noon));

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeAtmosphere(1, Atmosphere(20)));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(30)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(40)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(30), true, Lighting(40), Atmosphere(20)), SkyEnvironmentBakeResult::Published);

    SkyCelestialLight sunset   = noon;
    sunset.DirectionToLight[0] = 1.0f;
    sunset.DirectionToLight[1] = 0.0f;
    ASSERT_TRUE(environment.SubmitConfig({}, 2, {}, sunset));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    const AtmosphereStaticResources* const reusable = environment.FindReusableAtmosphere(request.Config);
    ASSERT_NE(reusable, nullptr);
    EXPECT_EQ(reusable->Transmittance.Index, 20u);
    EXPECT_EQ(reusable->Multiscattering.Index, 21u);
    ASSERT_TRUE(environment.AttachBakeAtmosphere(2, *reusable, false));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(50)));
    ASSERT_TRUE(environment.AttachBakeLighting(2, Lighting(60)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(50), true, Lighting(60), *reusable), SkyEnvironmentBakeResult::Published);

    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 30u);
    EXPECT_FALSE(retired.Atmosphere.Valid());

    SkyConfig static_change                             = {};
    static_change.Atmosphere.MieScatteringPerKilometer *= 1.1f;
    ASSERT_TRUE(environment.SubmitConfig(static_change, 3, {}, sunset));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_EQ(environment.FindReusableAtmosphere(request.Config), nullptr);
    ASSERT_TRUE(environment.AttachBakeAtmosphere(3, Atmosphere(70)));
    ASSERT_TRUE(environment.AttachBakeResource(3, Texture(80)));
    ASSERT_TRUE(environment.AttachBakeLighting(3, Lighting(90)));
    ASSERT_EQ(environment.CompleteBake(3, Texture(80), true, Lighting(90), Atmosphere(70)), SkyEnvironmentBakeResult::Published);

    retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 50u);
    EXPECT_EQ(retired.Atmosphere.Transmittance.Index, 20u);
    EXPECT_EQ(retired.Atmosphere.Multiscattering.Index, 21u);
}

TEST(SkyEnvironmentTest, RetiringRevisionReturnsItsFullOwnedLightingSet)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true, Lighting(20)), SkyEnvironmentBakeResult::Published);
    ASSERT_NE(environment.AcquireForFrame(), nullptr);

    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2));
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    environment.ReleaseSubmittedFrame(9);

    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(9, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
    EXPECT_EQ(retired.Lighting.DiffuseIrradiance.Index, 20u);
    EXPECT_EQ(retired.Lighting.SpecularEnvironment.Index, 21u);
    EXPECT_EQ(retired.Lighting.BrdfIntegrationLut.Index, 22u);
}
