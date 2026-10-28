#include <ZEngine/Applications/AppRenderPipeline.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>

using namespace ZEngine::Applications;

TEST(AppRenderPipelineMailboxTest, RenderStateProvidesLatestCoherentCameraSnapshot)
{
    AppRenderPipeline pipeline{};

    RenderFrameState  first        = {};
    first.Camera.Position.x        = 1.0f;
    first.ResizeSequence           = 1;
    first.RenderTargetW            = 1280;
    first.RenderTargetH            = 720;
    first.FramebufferW             = 1500;
    first.FramebufferH             = 800;
    first.SkyRevision              = 4;
    first.Sky.EnvironmentIntensity = 1.0f;
    pipeline.PublishFrameState(first);

    RenderFrameState latest         = first;
    latest.Camera.Position.x        = 3.0f;
    latest.ResizeSequence           = 2;
    latest.RenderTargetW            = 1920;
    latest.RenderTargetH            = 1080;
    latest.FramebufferW             = 2478;
    latest.FramebufferH             = 1514;
    latest.SkyRevision              = 5;
    latest.Sky.EnvironmentIntensity = 2.0f;
    pipeline.PublishFrameState(latest);

    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_FLOAT_EQ(read.Camera.Position.x, 3.0f);
    EXPECT_EQ(read.ResizeSequence, 2u);
    EXPECT_EQ(read.RenderTargetW, 1920u);
    EXPECT_EQ(read.RenderTargetH, 1080u);
    EXPECT_EQ(read.FramebufferW, 2478u);
    EXPECT_EQ(read.FramebufferH, 1514u);
    EXPECT_EQ(read.SkyRevision, 5u);
    EXPECT_FLOAT_EQ(read.Sky.EnvironmentIntensity, 2.0f);
}

TEST(AppRenderPipelineMailboxTest, RetainsImmutableSelectionOutlineMembership)
{
    AppRenderPipeline pipeline = {};
    RenderFrameState  state    = {};
    ASSERT_TRUE(state.SelectionOutline.Add({.Index = 8, .Generation = 2}));
    ASSERT_TRUE(state.SelectionOutline.Add({.Index = 3, .Generation = 4}));
    state.SelectionOutline.SortAndUnique();
    state.SelectionOutline.Hovered = {.Index = 11, .Generation = 7};

    ASSERT_TRUE(pipeline.PublishFrameState(state));

    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_TRUE(read.SelectionOutline.Enabled());
    EXPECT_TRUE(read.SelectionOutline.Contains({.Index = 3, .Generation = 4}));
    EXPECT_TRUE(read.SelectionOutline.Contains({.Index = 8, .Generation = 2}));
    EXPECT_FALSE(read.SelectionOutline.Contains({.Index = 8, .Generation = 3}));
    EXPECT_EQ(read.SelectionOutline.Hovered, (ZEngine::ECS::EntityID{.Index = 11, .Generation = 7}));
    pipeline.ReleaseFrameState();
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

class AppRenderSnapshotTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::ArenaAllocator   arena    = {};
    AppRenderPipeline                       pipeline = {};
    ZEngine::Rendering::Scenes::RenderScene source   = {};

    void                                    SetUp() override
    {
        arena.Initialize(ZMega(4), 0);
        pipeline.InitializeFrameStorage(&arena, 4);
        source.Instances.init(&arena, 8);
        source.Instances.push({});
    }

    void Stamp(float value)
    {
        const bool rotated                            = (static_cast<uint32_t>(value) % 2u) != 0;
        source.Instances[0].Transform(0, 3)           = value;
        source.Grid.Origin                            = {value * 2.f, value, -value};
        source.Grid.Orientation                       = rotated ? ZEngine::Core::Maths::Quaternion<float>{0.f, .6f, 0.f, .8f} : ZEngine::Core::Maths::Quaternion<float>{};
        source.Grid.MajorLineEvery                    = static_cast<uint32_t>(value);
        source.Grid.Labels                            = rotated ? 1u : 0u;
        source.PendingLights.PointCount               = 1;
        source.PendingLights.PointLights[0].Intensity = value;
        source.Sky.EnvironmentIntensity               = value;
    }
};

TEST_F(AppRenderSnapshotTest, CapturesDrawLightGridAndSkyWithThePublishedCamera)
{
    Stamp(7.f);
    RenderFrameState state  = {};
    state.Camera.Position.x = 7.f;
    ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
    Stamp(9.f);
    source.Instances.clear();
    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    ASSERT_TRUE(read.Scene.HasScene);
    ASSERT_EQ(read.Scene.InstanceCount, 1u);
    EXPECT_FLOAT_EQ(read.Camera.Position.x, 7.f);
    EXPECT_FLOAT_EQ(read.Scene.Instances[0].Transform(0, 3), 7.f);
    EXPECT_FLOAT_EQ(read.Scene.Grid.Origin.y, 7.f);
    EXPECT_FLOAT_EQ(read.Scene.Lights.PointLights[0].Intensity, 7.f);
    EXPECT_FLOAT_EQ(read.Sky.EnvironmentIntensity, 7.f);
    EXPECT_NE(read.Scene.Instances, reinterpret_cast<const ZEngine::Rendering::Scenes::SceneDrawInstance*>(source.Instances.data()));
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, ViewportGridVisibilityFiltersOnlyThePublishedSnapshot)
{
    for (bool authored : {false, true})
        for (bool shown : {false, true})
        {
            source.Grid.Enabled    = authored;
            RenderFrameState state = {};
            state.ShowGrid         = shown;
            ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
            RenderFrameState read = {};
            ASSERT_TRUE(pipeline.TryReadFrameState(read));
            EXPECT_EQ(read.Scene.Grid.Enabled, authored && shown);
            EXPECT_EQ(read.ShowGrid, shown);
            EXPECT_EQ(source.Grid.Enabled, authored);
            pipeline.ReleaseFrameState();
        }
}

TEST_F(AppRenderSnapshotTest, GridVisibilityIsImmutableWhileTheConsumerRetainsItsFrame)
{
    source.Grid.Enabled    = true;
    RenderFrameState state = {};
    state.ShowGrid         = false;
    ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
    RenderFrameState retained = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    state.ShowGrid = true;
    ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
    EXPECT_FALSE(retained.Scene.Grid.Enabled);
    EXPECT_TRUE(source.Grid.Enabled);
    pipeline.ReleaseFrameState();
    RenderFrameState next = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(next));
    EXPECT_TRUE(next.Scene.Grid.Enabled);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, FullOrientedGridRemainsCoherentWithItsCameraAcrossRepublishingAndOwnerRetirement)
{
    source.Grid.Origin         = {2.f, 3.f, 4.f};
    source.Grid.Orientation    = {.5f, -.5f, .5f, .5f};
    source.Grid.MajorLineEvery = 7;
    source.Grid.Labels         = 1;
    source.Grid.ColorUAxis     = {.1f, .2f, .3f, .4f};
    RenderFrameState state     = {};
    state.Camera.Position.x    = 23.f;
    ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
    RenderFrameState retained = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    for (uint32_t i = 0; i < 100; ++i)
    {
        source.Grid             = {};
        source.Grid.Origin.x    = float(i);
        state.Camera.Position.x = float(i);
        ASSERT_TRUE(pipeline.PublishFrameState(state, &source));
        EXPECT_EQ(retained.Camera.Position.x, 23.f);
        EXPECT_EQ(retained.Scene.Grid.Origin.x, 2.f);
        EXPECT_EQ(retained.Scene.Grid.Origin.y, 3.f);
        EXPECT_EQ(retained.Scene.Grid.Origin.z, 4.f);
        EXPECT_EQ(retained.Scene.Grid.Orientation.x, .5f);
        EXPECT_EQ(retained.Scene.Grid.Orientation.y, -.5f);
        EXPECT_EQ(retained.Scene.Grid.Orientation.z, .5f);
        EXPECT_EQ(retained.Scene.Grid.Orientation.w, .5f);
        EXPECT_EQ(retained.Scene.Grid.MajorLineEvery, 7u);
        EXPECT_EQ(retained.Scene.Grid.Labels, 1u);
        EXPECT_EQ(retained.Scene.Grid.ColorUAxis[3], .4f);
    }
    // Publishing a scene replacement cannot invalidate an acquired value copy.
    ASSERT_TRUE(pipeline.PublishFrameState(state, nullptr));
    EXPECT_EQ(retained.Scene.Grid.Origin.z, 4.f);
    EXPECT_EQ(retained.Scene.Grid.Orientation.y, -.5f);
    RenderFrameState replacement = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(replacement));
    EXPECT_FALSE(replacement.Scene.HasScene);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, RetainedSceneCannotBeOverwrittenBySubsequentPublications)
{
    Stamp(1.f);
    ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
    RenderFrameState retained = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    for (int i = 2; i <= 100; ++i)
    {
        Stamp(static_cast<float>(i));
        ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
        EXPECT_FLOAT_EQ(retained.Scene.Instances[0].Transform(0, 3), 1.f);
        EXPECT_FLOAT_EQ(retained.Scene.Grid.Origin.y, 1.f);
    }
    RenderFrameState latest = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(latest));
    EXPECT_FLOAT_EQ(latest.Scene.Instances[0].Transform(0, 3), 100.f);
    EXPECT_FLOAT_EQ(latest.Scene.Grid.Origin.y, 100.f);
    EXPECT_FALSE(pipeline.TryReadFrameState(latest));
    EXPECT_FLOAT_EQ(latest.Scene.Instances[0].Transform(0, 3), 100.f);
    pipeline.ReleaseFrameState();
    for (auto& slot : pipeline.FrameStateSlots)
        EXPECT_EQ(slot.value.load(), static_cast<uint32_t>(PayloadSlotState::Free));
}

TEST_F(AppRenderSnapshotTest, OverCapacityNeverPublishesAPartialSceneOrChangesRetainedData)
{
    Stamp(1.f);
    ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
    RenderFrameState retained = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(retained));
    for (int i = 0; i < 4; ++i)
        source.Instances.push({});
    EXPECT_FALSE(pipeline.PublishFrameState({}, &source));
    RenderFrameState next = {};
    EXPECT_FALSE(pipeline.TryReadFrameState(next));
    EXPECT_EQ(retained.Scene.InstanceCount, 1u);
    EXPECT_FLOAT_EQ(retained.Scene.Instances[0].Transform(0, 3), 1.f);
    source.Instances.clear();
    ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
    ASSERT_TRUE(pipeline.TryReadFrameState(next));
    EXPECT_TRUE(next.Scene.HasScene);
    EXPECT_EQ(next.Scene.InstanceCount, 0u);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, NullSceneClearsPreviousSnapshotAndDoesNotExposeCallerPointers)
{
    ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    ASSERT_TRUE(read.Scene.HasScene);
    // Republishing a read state is not permission to retain its borrowed slot pointer.
    ASSERT_TRUE(pipeline.PublishFrameState(read));
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_FALSE(read.Scene.HasScene);
    EXPECT_EQ(read.Scene.InstanceCount, 0u);
    EXPECT_EQ(read.Scene.Instances, nullptr);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, InvalidPublicationDoesNotEvictUnreadValidFrames)
{
    // Fill every slot so rejection must happen before a Ready slot is replaced.
    for (int i = 1; i <= 3; ++i)
    {
        Stamp(static_cast<float>(i));
        ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
    }
    for (int i = 0; i < 4; ++i)
        source.Instances.push({});
    EXPECT_FALSE(pipeline.PublishFrameState({}, &source));
    for (const auto& slot : pipeline.FrameStateSlots)
        EXPECT_EQ(slot.value.load(), static_cast<uint32_t>(PayloadSlotState::Ready));
    RenderFrameState read = {};
    ASSERT_TRUE(pipeline.TryReadFrameState(read));
    EXPECT_FLOAT_EQ(read.Scene.Instances[0].Transform(0, 3), 3.f);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, RepeatedSnapshotsDoNotAllocateOrGrowTheArena)
{
    const auto offset = arena.m_current_offset;
    for (int i = 1; i <= 100; ++i)
    {
        Stamp(static_cast<float>(i));
        ASSERT_TRUE(pipeline.PublishFrameState({}, &source));
        RenderFrameState read = {};
        ASSERT_TRUE(pipeline.TryReadFrameState(read));
        ASSERT_EQ(read.Scene.InstanceCount, 1u);
    }
    EXPECT_EQ(arena.m_current_offset, offset);
    pipeline.ReleaseFrameState();
}

TEST_F(AppRenderSnapshotTest, ConcurrentProducerAndConsumerObserveCoherentRetainedSnapshots)
{
    std::atomic<bool> finished = false;
    uint32_t          observed = 0;
    std::thread       producer([&] {
        RenderFrameState state = {};
        for (int i = 1; i <= 2000; ++i)
        {
            const float stamp = static_cast<float>(i);
            Stamp(stamp);
            state.Camera.Position.x   = stamp;
            state.Gizmo.LineCount     = 2;
            state.Gizmo.TriangleCount = i % 2;
            state.Gizmo.SpriteCount   = 1;
            state.Gizmo.Lines[0]      = {
                .A = {stamp, 0.f, 0.f},
                  .Width = 2.5f
            };
            state.Gizmo.Lines[1] = {
                .B = {stamp, 0.f, 0.f},
                  .Width = 3.5f
            };
            state.Gizmo.Triangles[0].C.x = stamp;
            state.Gizmo.Sprites[0]       = {
                .Position = {stamp, 0.f, 0.f},
                  .Size = 12.f
            };
            pipeline.PublishFrameState(state, &source);
        }
        finished.store(true, std::memory_order_release);
    });
    std::thread       consumer([&] {
        float            last_stamp = 0.f;
        RenderFrameState read       = {};
        for (;;)
        {
            if (pipeline.TryReadFrameState(read))
            {
                ++observed;
                const float stamp = read.Camera.Position.x;
                EXPECT_GT(stamp, last_stamp);
                last_stamp = stamp;
                EXPECT_EQ(read.Scene.InstanceCount, 1u);
                EXPECT_FLOAT_EQ(read.Scene.Instances[0].Transform(0, 3), stamp);
                EXPECT_FLOAT_EQ(read.Scene.Grid.Origin.y, stamp);
                EXPECT_FLOAT_EQ(read.Scene.Grid.Origin.x, stamp * 2.f);
                EXPECT_FLOAT_EQ(read.Scene.Grid.Origin.z, -stamp);
                const bool rotated = (static_cast<uint32_t>(stamp) % 2u) != 0;
                EXPECT_FLOAT_EQ(read.Scene.Grid.Orientation.y, rotated ? .6f : 0.f);
                EXPECT_FLOAT_EQ(read.Scene.Grid.Orientation.w, rotated ? .8f : 1.f);
                EXPECT_EQ(read.Scene.Grid.MajorLineEvery, static_cast<uint32_t>(stamp));
                EXPECT_EQ(read.Scene.Grid.Labels, rotated ? 1u : 0u);
                EXPECT_FLOAT_EQ(read.Scene.Lights.PointLights[0].Intensity, stamp);
                EXPECT_FLOAT_EQ(read.Sky.EnvironmentIntensity, stamp);
                ASSERT_EQ(read.Gizmo.LineCount, 2u);
                EXPECT_FLOAT_EQ(read.Gizmo.Lines[0].A.x, stamp);
                EXPECT_FLOAT_EQ(read.Gizmo.Lines[1].B.x, stamp);
                EXPECT_EQ(read.Gizmo.TriangleCount, uint32_t(stamp) % 2);
                if (read.Gizmo.TriangleCount)
                    EXPECT_FLOAT_EQ(read.Gizmo.Triangles[0].C.x, stamp);
                ASSERT_EQ(read.Gizmo.SpriteCount, 1u);
                EXPECT_FLOAT_EQ(read.Gizmo.Sprites[0].Position.x, stamp);
            }
            else if (finished.load(std::memory_order_acquire))
                break;
        }
        pipeline.ReleaseFrameState();
    });
    producer.join();
    consumer.join();
    EXPECT_GT(observed, 0u);
}
