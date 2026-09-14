#include <ZEngine/Applications/AppRenderPipeline.h>
#include <gtest/gtest.h>

using namespace ZEngine::Applications;

TEST(AppRenderPipelineMailboxTest, RenderStateProvidesLatestCoherentCameraSnapshot)
{
    AppRenderPipeline pipeline{};

    RenderFrameState  first        = {};
    first.Camera.Position.x        = 1.0f;
    first.ResizeSequence           = 1;
    first.RenderTargetW            = 1280;
    first.RenderTargetH            = 720;
    first.SkyRevision              = 4;
    first.Sky.EnvironmentIntensity = 1.0f;
    pipeline.PublishFrameState(first);

    RenderFrameState latest         = first;
    latest.Camera.Position.x        = 3.0f;
    latest.ResizeSequence           = 2;
    latest.RenderTargetW            = 1920;
    latest.RenderTargetH            = 1080;
    latest.SkyRevision              = 5;
    latest.Sky.EnvironmentIntensity = 2.0f;
    pipeline.PublishFrameState(latest);

    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_FLOAT_EQ(read.Camera.Position.x, 3.0f);
    EXPECT_EQ(read.ResizeSequence, 2u);
    EXPECT_EQ(read.RenderTargetW, 1920u);
    EXPECT_EQ(read.RenderTargetH, 1080u);
    EXPECT_EQ(read.SkyRevision, 5u);
    EXPECT_FLOAT_EQ(read.Sky.EnvironmentIntensity, 2.0f);
}

TEST(AppRenderPipelineMailboxTest, OverlayBuildIsPacedAndCanReplaceRetainedOverlay)
{
    AppRenderPipeline pipeline{};

    OverlayPayload*   first      = nullptr;
    uint32_t          first_slot = 0;
    ASSERT_TRUE(pipeline.BeginOverlayWrite(first, first_slot));
    ASSERT_NE(first, nullptr);
    pipeline.PublishOverlay(first_slot);

    // One UI build is permitted for each completed presentation frame.
    OverlayPayload* blocked      = nullptr;
    uint32_t        blocked_slot = 0;
    EXPECT_FALSE(pipeline.BeginOverlayWrite(blocked, blocked_slot));

    OverlayPayload* retained      = nullptr;
    uint32_t        retained_slot = 0;
    ASSERT_TRUE(pipeline.BeginOverlayRead(retained, retained_slot));
    ASSERT_EQ(retained, first);
    EXPECT_EQ(retained->Sequence, 1u);

    pipeline.NotifyOverlayFrameComplete();
    OverlayPayload* replacement      = nullptr;
    uint32_t        replacement_slot = 0;
    ASSERT_TRUE(pipeline.BeginOverlayWrite(replacement, replacement_slot));
    ASSERT_NE(replacement, nullptr);
    pipeline.PublishOverlay(replacement_slot);

    OverlayPayload* next      = nullptr;
    uint32_t        next_slot = 0;
    ASSERT_TRUE(pipeline.BeginOverlayRead(next, next_slot));
    EXPECT_EQ(next->Sequence, 2u);

    pipeline.EndOverlayRead(retained_slot);
    pipeline.EndOverlayRead(next_slot);
}
