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

TEST(SkyEnvironmentTest, StaleCompletionIsDiscardedAndDoesNotReplaceFallback)
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

    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(request.Revision, 2u);
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    EXPECT_EQ(environment.CompleteBake(2, Texture(3), true), SkyEnvironmentBakeResult::Published);
    EXPECT_EQ(environment.GetPublishedSnapshot()->Revision, 2u);
    EXPECT_EQ(environment.GetPublishedSnapshot()->SourceRadiance.Index, 3u);
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

TEST(SkyEnvironmentTest, FailedReplacementRetainsPreviouslyReadyEnvironment)
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
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    EXPECT_EQ(environment.CompleteBake(2, {}, false), SkyEnvironmentBakeResult::Failed);

    const SkyEnvironmentSnapshot* snapshot = environment.GetPublishedSnapshot();
    ASSERT_NE(snapshot, nullptr);
    EXPECT_EQ(snapshot->Revision, 1u);
    EXPECT_EQ(snapshot->SourceRadiance.Index, 2u);
    EXPECT_EQ(environment.GetState(), SkyEnvironmentState::Failed);
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
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(3), true), SkyEnvironmentBakeResult::Published);

    Textures::TextureHandle retired = {};
    EXPECT_FALSE(environment.TakeRetiredSnapshot(0, retired));
    environment.ReleaseSubmittedFrame(9);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(8, retired));
    ASSERT_TRUE(environment.TakeRetiredSnapshot(9, retired));
    EXPECT_EQ(retired.Index, 2u);
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
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(3), true), SkyEnvironmentBakeResult::Published);

    Textures::TextureHandle retired = {};
    environment.ReleaseSubmittedFrame(7);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(7, retired));
    environment.ReleaseSubmittedFrame(8);
    EXPECT_FALSE(environment.TakeRetiredSnapshot(7, retired));
    ASSERT_TRUE(environment.TakeRetiredSnapshot(8, retired));
    EXPECT_EQ(retired.Index, 2u);
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
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(3), true), SkyEnvironmentBakeResult::Published);
    environment.ReleaseCancelledFrame();

    Textures::TextureHandle retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(0, retired));
    EXPECT_EQ(retired.Index, 2u);
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
    ASSERT_TRUE(environment.TakeBakeRequest(request));
    ASSERT_TRUE(environment.AttachBakeResource(2, Texture(3)));
    ASSERT_EQ(environment.CompleteBake(2, Texture(3), true, Lighting(30)), SkyEnvironmentBakeResult::Published);
    environment.ReleaseSubmittedFrame(9);

    SkyEnvironmentResources retired = {};
    ASSERT_TRUE(environment.TakeRetiredSnapshot(9, retired));
    EXPECT_EQ(retired.SourceRadiance.Index, 2u);
    EXPECT_EQ(retired.Lighting.DiffuseIrradiance.Index, 20u);
    EXPECT_EQ(retired.Lighting.SpecularEnvironment.Index, 21u);
    EXPECT_EQ(retired.Lighting.BrdfIntegrationLut.Index, 22u);
}
