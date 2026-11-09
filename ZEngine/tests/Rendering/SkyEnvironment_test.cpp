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

    bool FinishSkyGpuStages(SkyEnvironment& environment, uint64_t revision, uint64_t first_timeline = 1)
    {
        // Atmosphere has three whole-image stages and eighteen face batches;
        // HDRI has one whole-image stage and twelve face batches. Never spin.
        for (uint64_t index = 0; index < 21 && !environment.IsGpuBakeReadyToPublish(); ++index)
        {
            if (!environment.NotifyGpuBakeStageRecorded(revision, environment.GetActiveBakeStage(), environment.GetActiveBakeFace()) || !environment.MarkGpuBakeStageSubmitted(revision, first_timeline + index) || !environment.AdvanceCompletedGpuBakeStage(first_timeline + index))
            {
                return false;
            }
        }
        return environment.IsGpuBakeReadyToPublish();
    }

    bool RunSkyGpuBake(SkyEnvironment& environment, uint64_t revision, uint64_t source_index = 20)
    {
        SkyEnvironmentBakeRequest request = {};
        if (!environment.TakeBakeRequest(request) || request.Revision != revision || !environment.AttachBakeResource(revision, Texture(source_index)) || !environment.AttachBakeLighting(revision, Lighting(source_index + 1)))
        {
            return false;
        }
        if (request.Config.IsAtmosphere() && !environment.AttachBakeAtmosphere(revision, Atmosphere(source_index + 4)))
        {
            return false;
        }
        return environment.BeginGpuBake(revision) && FinishSkyGpuStages(environment, revision);
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

TEST(SkyEnvironmentPreparationTest, CandidateRemainsInvisibleUntilExactRevisionIsActivated)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    const SkyConfig original = SkySphereConfig();
    ASSERT_TRUE(environment.SubmitConfig(original, 1));
    const SkyEnvironmentSnapshot* const original_snapshot = environment.AcquireForFrame();
    ASSERT_NE(original_snapshot, nullptr);

    SkyConfig candidate            = HDRIConfig();
    candidate.EnvironmentIntensity = 3.0f;
    ASSERT_TRUE(environment.SubmitConfig(candidate, 2, {}, {}, 0, true, SkyEnvironmentPublication::Explicit));
    EXPECT_EQ(environment.GetPresentationConfig(), original);
    EXPECT_EQ(environment.GetPreparedSnapshot(2), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(2));
    ASSERT_TRUE(RunSkyGpuBake(environment, 2));
    ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Prepared);

    const SkyEnvironmentSnapshot* const prepared = environment.GetPreparedSnapshot(2);
    ASSERT_NE(prepared, nullptr);
    EXPECT_EQ(prepared->Config, candidate);
    EXPECT_EQ(prepared->Revision, 2u);
    EXPECT_EQ(environment.GetPublishedSnapshot(), original_snapshot);
    EXPECT_EQ(environment.GetPresentationConfig(), original);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(1));
    EXPECT_FALSE(environment.CancelPrepared(1));
    SkyEnvironmentResources retired = {};
    EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));

    ASSERT_TRUE(environment.ActivatePrepared(2));
    EXPECT_EQ(environment.GetPublishedSnapshot(), prepared);
    EXPECT_EQ(environment.GetPresentationConfig(), candidate);
    EXPECT_EQ(environment.GetPreparedSnapshot(2), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(2));
    EXPECT_FALSE(environment.CancelPrepared(2));
    EXPECT_EQ(original_snapshot->PinCount, 1u);
    environment.ReleaseSubmittedFrame(50);
    EXPECT_EQ(original_snapshot->PinCount, 0u);
}

TEST(SkyEnvironmentPreparationTest, EarlySuccessCannotBypassSubmittedGpuCompletion)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 0, true, SkyEnvironmentPublication::Explicit));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(20)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(21)));
    ASSERT_TRUE(environment.BeginGpuBake(1));
    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, environment.GetActiveBakeStage()));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 7));
    EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(6));
    EXPECT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Ignored);
    ASSERT_NE(environment.GetActiveBake(), nullptr);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(7));
    ASSERT_TRUE(FinishSkyGpuStages(environment, 1, 8));
    ASSERT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Prepared);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentPreparationTest, CandidateAndOutgoingSnapshotsShareOneMemoryBudget)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    environment.ConfigureMemoryBudget(100, 10);
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.ReserveActiveBakeMemory(1, 60));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);
    ASSERT_NE(environment.AcquireForFrame(), nullptr);

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, {}, {}, 2, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_FALSE(environment.ReserveActiveBakeMemory(2, 31));
    ASSERT_TRUE(environment.ReserveActiveBakeMemory(2, 30));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(20)));
    ASSERT_TRUE(environment.AttachBakeLighting(2, Lighting(21)));
    ASSERT_TRUE(environment.BeginGpuBake(2));
    ASSERT_TRUE(FinishSkyGpuStages(environment, 2));
    ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Prepared);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 100u);
    SkyEnvironmentResources retired = {};
    EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));

    ASSERT_TRUE(environment.ActivatePrepared(2));
    EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    environment.ReleaseSubmittedFrame(50);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(49, retired));
    ASSERT_TRUE(environment.TakeRetiredSnapshot(50, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 40u);
}

TEST(SkyEnvironmentPreparationTest, CancellingCompletedCandidateReleasesOnlyItsResources)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);
    const SkyEnvironmentSnapshot* const visible = environment.GetPublishedSnapshot();
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, {}, {}, 2, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 2));
    ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Prepared);
    ASSERT_TRUE(environment.CancelPrepared(2));
    EXPECT_EQ(environment.GetPreparedSnapshot(2), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(2));
    EXPECT_FALSE(environment.CancelPrepared(2));
    EXPECT_EQ(environment.GetPublishedSnapshot(), visible);
    EXPECT_FALSE(visible->Retired);
    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 20u);
    EXPECT_EQ(retired.Lighting.DiffuseIrradiance.Index, 21u);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
}

TEST(SkyEnvironmentPreparationTest, CancellingSubmittedCandidateKeepsItsGpuWorkOwnedUntilDrained)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 0, true, SkyEnvironmentPublication::Explicit));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(20)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(21)));
    ASSERT_TRUE(environment.BeginGpuBake(1));
    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, environment.GetActiveBakeStage()));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 7));
    ASSERT_TRUE(environment.CancelPrepared(1));
    EXPECT_FALSE(environment.IsActiveBakeCurrent());
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Ignored);
    EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(6));
    ASSERT_NE(environment.GetActiveBake(), nullptr);
    EXPECT_EQ(environment.GetActiveBakeSource().Index, 20u);
    EXPECT_EQ(environment.GetActiveBakeLighting().DiffuseIrradiance.Index, 21u);
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(7));
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Discarded);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentPreparationTest, SupersededCandidateCannotBeActivatedAndItsSlotIsRetired)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 1));
    ASSERT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Prepared);
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, {}, {}, 2, true, SkyEnvironmentPublication::Explicit));
    EXPECT_FALSE(environment.ActivatePrepared(1));
    EXPECT_FALSE(environment.CancelPrepared(1));
    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 20u);
    ASSERT_TRUE(RunSkyGpuBake(environment, 2, 30));
    ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Prepared);
    ASSERT_TRUE(environment.ActivatePrepared(2));
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 30u);
}

TEST(SkyEnvironmentPreparationTest, ImmediateEditCannotReuseAnUnpublishedCandidateAsVisibleState)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 1));
    ASSERT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Prepared);
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, {}, {}, 1));
    EXPECT_FALSE(environment.ActivatePrepared(1));
    ASSERT_TRUE(RunSkyGpuBake(environment, 2, 30));
    ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 30u);
}

TEST(SkyEnvironmentPreparationTest, SkySphereIsPreparedWithoutBakingButDoesNotHideTheOutgoingSky)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);
    const SkyEnvironmentSnapshot* const visible  = environment.GetPublishedSnapshot();
    const SkyConfig                     original = environment.GetPresentationConfig();
    SkyCelestialLight                   sun      = {};
    sun.IsAvailable                              = true;
    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2, {}, sun, 0, true, SkyEnvironmentPublication::Explicit));
    ASSERT_NE(environment.GetPreparedSnapshot(2), nullptr);
    EXPECT_FALSE(environment.TakeBakeRequest(request));
    EXPECT_EQ(environment.GetPublishedSnapshot(), visible);
    EXPECT_EQ(environment.GetPresentationConfig(), original);
    EXPECT_FALSE(environment.GetPresentationCelestialLight().IsAvailable);
    ASSERT_TRUE(environment.ActivatePrepared(2));
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    EXPECT_TRUE(environment.GetPresentationConfig().IsSkySphere());
    EXPECT_TRUE(environment.GetPresentationCelestialLight().IsAvailable);
    EXPECT_TRUE(visible->Retired);
}

TEST(SkyEnvironmentPreparationTest, MissingEngineFallbackCannotSatisfySkySpherePreparation)
{
    SkyEnvironment environment = {};
    environment.Initialize({}, {});
    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 1, {}, {}, 0, true, SkyEnvironmentPublication::Explicit));
    EXPECT_EQ(environment.GetState(), SkyEnvironmentState::Failed);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(1));
}

TEST(SkyEnvironmentPreparationTest, RequiredHdrSourceCannotUseTheEngineFallback)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 0, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 1, 1));
    EXPECT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Failed);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentPreparationTest, InvalidRequiredInputsCannotBePreparedEvenAfterGpuStages)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 0, false, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 1));
    EXPECT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Failed);
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(1));
}

TEST(SkyEnvironmentPreparationTest, CompletionCannotSubstituteUnbakedResourceGenerationsOrMetadata)
{
    for (uint32_t changed_field = 0; changed_field < 8; ++changed_field)
    {
        SCOPED_TRACE(changed_field);
        SkyEnvironment environment = {};
        environment.Initialize(Texture(1), Lighting(10));
        SkyCelestialLight sun = {};
        sun.IsAvailable       = true;
        ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun, 0, true, SkyEnvironmentPublication::Explicit));
        ASSERT_TRUE(RunSkyGpuBake(environment, 1));
        Textures::TextureHandle      source     = Texture(20);
        EnvironmentLightingResources lighting   = Lighting(21);
        AtmosphereStaticResources    atmosphere = Atmosphere(24);
        switch (changed_field)
        {
            case 0:
                ++source.Generation;
                break;
            case 1:
                ++lighting.DiffuseIrradiance.Generation;
                break;
            case 2:
                ++lighting.SpecularEnvironment.Generation;
                break;
            case 3:
                ++lighting.BrdfIntegrationLut.Generation;
                break;
            case 4:
                ++lighting.SpecularMipCount;
                break;
            case 5:
                ++lighting.BrdfIntegrationKey.ShaderVersion;
                break;
            case 6:
                ++atmosphere.Transmittance.Generation;
                break;
            case 7:
                ++atmosphere.Multiscattering.Generation;
                break;
        }
        EXPECT_EQ(environment.CompleteBake(1, source, true, lighting, atmosphere), SkyEnvironmentBakeResult::Failed);
        EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
        EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    }
}

TEST(SkyEnvironmentPreparationTest, StaleSubmittedBakeCannotReleaseOwnershipBeforeItsTimeline)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1, true, SkyEnvironmentPublication::Explicit));
    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(20)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(21)));
    ASSERT_TRUE(environment.BeginGpuBake(1));
    ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, environment.GetActiveBakeStage()));
    ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, 50));
    ASSERT_TRUE(environment.SubmitConfig(SkySphereConfig(), 2));
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Ignored);
    ASSERT_NE(environment.GetActiveBake(), nullptr);
    EXPECT_EQ(environment.GetActiveBake()->Publication, SkyEnvironmentPublication::Explicit);
    ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(50));
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Discarded);
    EXPECT_TRUE(environment.GetPresentationConfig().IsSkySphere());
    EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
}

TEST(SkyEnvironmentPreparationTest, SnapshotCapacityFailurePreservesTheOutgoingFrame)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    for (uint64_t revision = 1; revision < SkyEnvironment::MaxSnapshots; ++revision)
    {
        ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), revision, {}, {}, revision));
        SkyEnvironmentBakeRequest request = {};
        ASSERT_TRUE(environment.TakeBakeRequest(request));
        ASSERT_EQ(environment.CompleteBake(revision, Texture(revision + 1), true), SkyEnvironmentBakeResult::Published);
        ASSERT_NE(environment.AcquireForFrame(), nullptr);
    }
    const SkyEnvironmentSnapshot* const visible            = environment.GetPublishedSnapshot();
    const uint64_t                      candidate_revision = SkyEnvironment::MaxSnapshots;
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), candidate_revision, {}, {}, candidate_revision, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, candidate_revision));
    EXPECT_EQ(environment.CompleteBake(candidate_revision, {}, true), SkyEnvironmentBakeResult::Failed);
    EXPECT_EQ(environment.GetPreparedSnapshot(candidate_revision), nullptr);
    EXPECT_EQ(environment.GetPublishedSnapshot(), visible);
    EXPECT_FALSE(visible->Retired);
    for (uint32_t index = 1; index < SkyEnvironment::MaxSnapshots; ++index)
    {
        environment.ReleaseSubmittedFrame(50);
    }
}

TEST(SkyEnvironmentPreparationTest, RepeatedPreparationAndCancellationRecoverTheBoundedReservation)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    environment.ConfigureMemoryBudget(100, 10);
    for (uint64_t revision = 1; revision <= 1100; ++revision)
    {
        SCOPED_TRACE(revision);
        ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), revision, {}, {}, revision, true, SkyEnvironmentPublication::Explicit));
        SkyEnvironmentBakeRequest request = {};
        ASSERT_TRUE(environment.TakeBakeRequest(request));
        ASSERT_TRUE(environment.ReserveActiveBakeMemory(revision, 90));
        ASSERT_TRUE(environment.AttachBakeResource(revision, Texture(20)));
        ASSERT_TRUE(environment.AttachBakeLighting(revision, Lighting(21)));
        ASSERT_TRUE(environment.BeginGpuBake(revision));
        ASSERT_TRUE(FinishSkyGpuStages(environment, revision));
        ASSERT_EQ(environment.CompleteBake(revision, {}, true), SkyEnvironmentBakeResult::Prepared);
        EXPECT_EQ(environment.GetReservedMemoryBytes(), 100u);
        ASSERT_TRUE(environment.CancelPrepared(revision));
        SkyEnvironmentResources retired = {};
        ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
        EXPECT_EQ(environment.GetReservedMemoryBytes(), 10u);
        EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
        EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
    }
}

TEST(SkyEnvironmentPreparationTest, ShutdownRetiresPreparedAndPublishedSnapshotsWithoutActivation)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    SkyCelestialLight sun = {};
    sun.IsAvailable       = true;
    ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun, 0, true, SkyEnvironmentPublication::Explicit));
    ASSERT_TRUE(RunSkyGpuBake(environment, 1));
    ASSERT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Prepared);
    EXPECT_FALSE(environment.Shutdown().SourceRadiance.Valid());
    EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
    EXPECT_FALSE(environment.ActivatePrepared(1));
    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 20u);
    EXPECT_EQ(retired.Atmosphere.Transmittance.Index, 24u);
    EXPECT_EQ(retired.Lighting.DiffuseIrradiance.Index, 21u);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(UINT64_MAX, retired));
}

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

TEST(SkyEnvironmentTest, MemoryBudgetDefersBakesUntilRetiredSnapshotsReleaseReservations)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));
    environment.ConfigureMemoryBudget(100, 10);
    EXPECT_EQ(environment.GetMemoryBudgetBytes(), 100u);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 10u);

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 1, {}, {}, 1));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.ReserveActiveBakeMemory(1, 60));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 70u);
    ASSERT_NE(environment.AcquireForFrame(), nullptr);

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 2, {}, {}, 2));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.ReserveActiveBakeMemory(2, 30));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(3), true), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 100u);

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 3, {}, {}, 3));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_FALSE(environment.ReserveActiveBakeMemory(3, 1));
    ASSERT_EQ(environment.CompleteBake(3, {}, false), SkyEnvironmentBakeResult::Failed);

    environment.ReleaseSubmittedFrame(9);
    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(9, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 40u);

    ASSERT_TRUE(environment.SubmitConfig(HDRIConfig(), 4, {}, {}, 4));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.ReserveActiveBakeMemory(4, 60));
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 100u);
    ASSERT_EQ(environment.CompleteBake(4, {}, false), SkyEnvironmentBakeResult::Failed);
    EXPECT_EQ(environment.GetReservedMemoryBytes(), 40u);
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

TEST(SkyEnvironmentTest, HdrSourceReloadAtTheSameSceneRevisionDiscardsStaleWork)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyConfig config                  = HDRIConfig();
    config.EnvironmentMap             = uuids::uuid::from_string("550e8400-e29b-41d4-a716-446655440000").value();

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(config, 1, {}, {}, 0x1111ULL, true));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(2)));
    EXPECT_TRUE(environment.IsActiveBakeCurrent());

    // The source enters the stale/importing state before its new artifact is
    // ready. Its scene UUID and scene revision intentionally remain unchanged.
    ASSERT_TRUE(environment.SubmitConfig(config, 1, {}, {}, 0x2222ULL, false));
    EXPECT_FALSE(environment.IsActiveBakeCurrent());
    EXPECT_EQ(environment.CompleteBake(1, Texture(2), true), SkyEnvironmentBakeResult::Discarded);

    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, 1u);
    EXPECT_EQ(request.HDRISourceHash, 0x2222ULL);
    EXPECT_FALSE(request.HDRIArtifactReady);
    EXPECT_FALSE(request.BakeInputsValid);
    EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Failed);

    // An unchanged unavailable artifact does not cause per-frame retry spam.
    EXPECT_FALSE(environment.SubmitConfig(config, 1, {}, {}, 0x2222ULL, false));

    // Import completion uses the same UUID/hash but marks the cooked artifact
    // ready, which schedules exactly one fresh bake.
    ASSERT_TRUE(environment.SubmitConfig(config, 1, {}, {}, 0x2222ULL, true));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_TRUE(request.HDRIArtifactReady);
    EXPECT_TRUE(request.BakeInputsValid);
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

    uint64_t timeline = 6;
    for (const auto stage : {SkyEnvironmentBakeStage::DiffuseIrradiance, SkyEnvironmentBakeStage::SpecularEnvironment})
    {
        for (uint32_t face = 0; face < SkyEnvironment::BakeFaceCount; ++face, ++timeline)
        {
            ASSERT_EQ(environment.GetActiveBakeStage(), stage);
            ASSERT_EQ(environment.GetActiveBakeFace(), face);
            EXPECT_FALSE(environment.MarkGpuBakeStageSubmitted(1, timeline));
            EXPECT_FALSE(environment.NotifyGpuBakeStageRecorded(2, stage, face));
            EXPECT_FALSE(environment.NotifyGpuBakeStageRecorded(1, stage, SkyEnvironment::BakeFaceCount));
            ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, stage, face));
            EXPECT_FALSE(environment.MarkGpuBakeStageSubmitted(1, 0));
            EXPECT_EQ(environment.GetActiveBakeFace(), face);
            ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, timeline));
            EXPECT_FALSE(environment.BeginGpuBake(1));
            EXPECT_FALSE(environment.CanRecordGpuBakeStage());
            EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(timeline - 1));
            EXPECT_EQ(environment.GetActiveBakeFace(), face);
            EXPECT_FALSE(environment.IsGpuBakeReadyToPublish());
            ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(timeline));
            EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(timeline));
            if (face + 1 < SkyEnvironment::BakeFaceCount)
            {
                EXPECT_FALSE(environment.NotifyGpuBakeStageRecorded(1, stage, face));
            }
        }
    }
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
    uint64_t timeline = 1;
    for (const SkyEnvironmentBakeStage stage : stages)
    {
        const bool     face_stage  = stage == SkyEnvironmentBakeStage::AtmosphereSourceRadiance || stage == SkyEnvironmentBakeStage::DiffuseIrradiance || stage == SkyEnvironmentBakeStage::SpecularEnvironment;
        const uint32_t batch_count = face_stage ? SkyEnvironment::BakeFaceCount : 1;
        for (uint32_t face = 0; face < batch_count; ++face, ++timeline)
        {
            ASSERT_EQ(environment.GetActiveBakeStage(), stage);
            ASSERT_EQ(environment.GetActiveBakeFace(), face);
            ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, stage, face));
            ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, timeline));
            ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(timeline));
        }
    }
    EXPECT_EQ(timeline, 22u);

    ASSERT_TRUE(environment.IsGpuBakeReadyToPublish());
    ASSERT_EQ(environment.CompleteBake(1, Texture(30), true, Lighting(40), Atmosphere(20)), SkyEnvironmentBakeResult::Published);
    ASSERT_NE(environment.GetPublishedSnapshot(), nullptr);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Atmosphere.Transmittance.Index, 20u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Atmosphere.Multiscattering.Index, 21u);
    EXPECT_TRUE(environment.GetPublishedSnapshot()->CelestialLight.IsAvailable);
    EXPECT_FLOAT_EQ(environment.GetPublishedSnapshot()->CelestialLight.DirectionToLight[1], 1.0f);
}

TEST(SkyEnvironmentPreparationTest, EveryFaceBoundaryPreservesPublicationAndCancellationOwnership)
{
    // Exercise cancellation during every submitted batch, including the final
    // face. The previous environment must remain visible throughout.
    for (uint64_t cancelled_batch = 0; cancelled_batch < 21; ++cancelled_batch)
    {
        SCOPED_TRACE(cancelled_batch);
        SkyEnvironment environment = {};
        environment.Initialize(Texture(1), Lighting(10));
        SkyCelestialLight sun = {};
        sun.IsAvailable       = true;
        ASSERT_TRUE(environment.SubmitConfig({}, 1, {}, sun, 0, true, SkyEnvironmentPublication::Explicit));
        SkyEnvironmentBakeRequest request = {};
        ASSERT_TRUE(environment.TakeBakeRequest(request));
        ASSERT_TRUE(environment.AttachBakeAtmosphere(1, Atmosphere(20)));
        ASSERT_TRUE(environment.AttachBakeResource(1, Texture(30)));
        ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(40)));
        ASSERT_TRUE(environment.BeginGpuBake(1));
        for (uint64_t batch = 0; batch <= cancelled_batch; ++batch)
        {
            const auto stage = environment.GetActiveBakeStage();
            const auto face  = environment.GetActiveBakeFace();
            EXPECT_EQ(environment.CompleteBake(1, {}, true), SkyEnvironmentBakeResult::Ignored);
            EXPECT_FALSE(environment.ActivatePrepared(1));
            ASSERT_TRUE(environment.NotifyGpuBakeStageRecorded(1, stage, face));
            ASSERT_TRUE(environment.MarkGpuBakeStageSubmitted(1, batch + 1));
            if (batch == cancelled_batch)
            {
                ASSERT_TRUE(environment.CancelPrepared(1));
                EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Ignored);
                EXPECT_FALSE(environment.AdvanceCompletedGpuBakeStage(batch));
                EXPECT_EQ(environment.GetActiveBakeFace(), face);
                EXPECT_EQ(environment.GetActiveBakeSource().Index, 30u);
            }
            ASSERT_TRUE(environment.AdvanceCompletedGpuBakeStage(batch + 1));
        }
        EXPECT_EQ(environment.CompleteBake(1, {}, false), SkyEnvironmentBakeResult::Discarded);
        EXPECT_EQ(environment.GetActiveBake(), nullptr);
        EXPECT_EQ(environment.GetActiveBakeFace(), 0u);
        EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
        EXPECT_EQ(environment.GetPreparedSnapshot(1), nullptr);
        // A retry starts at the first batch, never at the discarded face.
        ASSERT_TRUE(environment.SubmitConfig({}, 2, {}, sun, 0, true, SkyEnvironmentPublication::Explicit));
        ASSERT_TRUE(RunSkyGpuBake(environment, 2, 50));
        ASSERT_EQ(environment.CompleteBake(2, {}, true), SkyEnvironmentBakeResult::Prepared);
        EXPECT_TRUE(environment.GetPublishedSnapshot()->IsFallback);
        ASSERT_TRUE(environment.ActivatePrepared(2));
        EXPECT_EQ(environment.GetPublishedSnapshot()->Revision, 2u);
    }
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

TEST(SkyEnvironmentTest, DynamicCelestialUpdatesCoalesceUntilTheBakeBudgetOpens)
{
    SkyEnvironment environment = {};
    environment.Initialize(Texture(1), Lighting(10));

    SkyConfig         config          = {};
    SkyCelestialLight sun             = {};
    sun.IsAvailable                   = true;

    SkyEnvironmentBakeRequest request = {};
    ASSERT_TRUE(environment.SubmitConfig(config, 1, {}, sun));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeAtmosphere(1, Atmosphere(20)));
    ASSERT_TRUE(environment.AttachBakeResource(1, Texture(30)));
    ASSERT_TRUE(environment.AttachBakeLighting(1, Lighting(40)));
    ASSERT_EQ(environment.CompleteBake(1, Texture(30), true, Lighting(40), Atmosphere(20)), SkyEnvironmentBakeResult::Published);

    for (uint64_t revision = 2; revision < SkyEnvironment::DynamicCelestialBakeRevisionInterval + 1; ++revision)
    {
        sun.DirectionToLight[0] = static_cast<float>(revision) * 0.001f;
        ASSERT_TRUE(environment.SubmitConfig(config, revision, {}, sun));
        EXPECT_FALSE(environment.TakeBakeRequest(request));
    }

    const uint64_t accepted_revision = SkyEnvironment::DynamicCelestialBakeRevisionInterval + 1;
    sun.DirectionToLight[0]          = static_cast<float>(accepted_revision) * 0.001f;
    ASSERT_TRUE(environment.SubmitConfig(config, accepted_revision, {}, sun));
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, accepted_revision);
    EXPECT_FLOAT_EQ(request.CelestialLight.DirectionToLight[0], sun.DirectionToLight[0]);
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
