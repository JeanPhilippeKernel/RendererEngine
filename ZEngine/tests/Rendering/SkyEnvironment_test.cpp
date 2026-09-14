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

    EnvironmentLightingResources Lighting(uint64_t first_index)
    {
        return {
            .DiffuseIrradiance   = Texture(first_index),
            .SpecularEnvironment = Texture(first_index + 1),
            .BrdfIntegrationLut  = Texture(first_index + 2),
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
