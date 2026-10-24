#include <Tetragrama/EditorScene.h>
#include <Tetragrama/EditorSession.h>
#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Systems/LightSyncSystem.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <ZEngine/Engine.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <thread>
#include <type_traits>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Applications;

/// Composes the production mutation -> hierarchy/draw/light sync -> mailbox
/// boundary. This is CPU lifetime evidence, not a native/GPU acceptance test.
class EditorRenderHandoffTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager               Memory      = {};
    ZEngine::Core::Memory::ArenaAllocator              SourceArena = {};
    Scene                                              World       = {};
    ActorManager                                       Actors      = {};
    EditorScene                                        Owner       = {};
    EditorSession                                      Session     = {};
    AppRenderPipeline                                  Pipeline    = {};
    std::unique_ptr<ZEngine::EngineContext>            Engine      = {};
    ZEngine::Core::Containers::FixedArray<EntityID, 2> Entities    = {};
    bool                                               OwnersLive  = false;

    void                                               SetUp() override
    {
        Memory.Initialize(ZMega(64), {});
        SourceArena.Initialize(ZMega(64), 0, "Test/OutgoingScene");
        World.Initialize(&SourceArena);
        Actors.Initialize(&SourceArena, World);
        OwnersLive = true;
        Owner.Instances.init(&SourceArena, 4);
        Owner.UUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        Pipeline.InitializeFrameStorage(&Memory.MainArena, 4);
        Entities[0]       = World.CreateEntity();
        const auto handle = Actors.Create();
        ASSERT_TRUE(handle.Valid());
        Entities[1] = Actors.Access(handle)->GetEntityID();
        for (uint32_t i = 0; i < 2; ++i)
        {
            const auto uuid = i == 0 ? *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222") : *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
            World.AddComponent<UUIDComponent>(Entities[i], {uuid});
            World.AddComponent<TransformComponent>(Entities[i], {});
            World.AddComponent<MeshComponent>(Entities[i], {.MeshUUID = uuid, .RenderInstanceId = Owner.AddMeshInstance(uuid, "Mesh")});
        }
        World.AddComponent<LightComponent>(Entities[0], {.LightType = LightComponent::Type::Point, .Intensity = 7.f, .Range = 11.f});
        Engine               = std::make_unique<ZEngine::EngineContext>();
        Engine->Scene        = &World;
        Engine->ActorManager = &Actors;
        ASSERT_TRUE(Session.Initialize(&Memory.MainArena, {.MaxTargets = 4, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(Session.BeginMutationPhase());
        ASSERT_TRUE(Session.BindScene(&World));
        ASSERT_TRUE(Owner.AttachSession(Session, *Engine));
        Session.EndMutationPhase();
    }

    void TearDown() override
    {
        Pipeline.ReleaseFrameState();
        Session.Shutdown();
        if (OwnersLive)
        {
            Actors.Shutdown();
            World.Shutdown();
        }
    }

    bool Publish(float camera)
    {
        // Same order as Engine::Run: no runtime waves, no borrowed scene on the
        // consumer side, and no dependence on whether an overlay could build.
        World.SynchronizeHierarchy();
        ZEngine::ECS::Systems::SyncECSToRenderScene(World, 1.f, Owner);
        ZEngine::ECS::Systems::SyncECSToLights(World, Owner);
        RenderFrameState state  = {};
        state.Camera.Position.x = camera;
        return Pipeline.PublishFrameState(state, &Owner);
    }

    void BlockOverlayBuild()
    {
        OverlayPayload* payload = nullptr;
        uint32_t        slot    = 0;
        ASSERT_TRUE(Pipeline.BeginOverlayWrite(payload, slot));
        Pipeline.PublishOverlay(slot);
        EXPECT_FALSE(Pipeline.BeginOverlayWrite(payload, slot));
    }

    void PreviewBatch(float first, float second)
    {
        ASSERT_TRUE(Session.BeginMutationPhase());
        const ZEngine::Core::Containers::FixedArray targets = {Session.TargetFor(Entities[0]), Session.TargetFor(Entities[1])};
        ASSERT_TRUE(Session.BeginTransform({targets.data(), targets.size()}));
        ZEngine::Core::Containers::FixedArray<LocalTransformValue, 2> values = {};
        values[0].Position.x                                                 = first;
        values[1].Position.x                                                 = second;
        ASSERT_TRUE(Session.PreviewTransforms({values.data(), values.size()}));
        Session.EndMutationPhase();
    }

    void ClearPendingLights()
    {
        static_assert(std::is_trivially_copyable_v<ZEngine::Rendering::Scenes::LightArrayUBO>);
        std::memset(&Owner.PendingLights, 0, sizeof(Owner.PendingLights));
    }
};

TEST_F(EditorRenderHandoffTest, CancelRestoresBothEntityKindsWhileTheRendererRetainsThePreview)
{
    BlockOverlayBuild();
    PreviewBatch(5.f, 9.f);
    ASSERT_TRUE(Publish(23.f));
    RenderFrameState retained = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(retained));
    ASSERT_EQ(retained.Scene.InstanceCount, 2u);
    EXPECT_EQ(retained.Scene.Instances[0].Transform(0, 3), 5.f);
    EXPECT_EQ(retained.Scene.Instances[1].Transform(0, 3), 9.f);
    ASSERT_EQ(retained.Scene.Lights.PointCount, 1u);
    EXPECT_EQ(retained.Scene.Lights.PointLights[0].Position.x, 5.f);

    ASSERT_TRUE(Session.BeginMutationPhase());
    Session.Cancel();
    Session.EndMutationPhase();
    EXPECT_FALSE(Session.Dirty());
    EXPECT_EQ(Session.History().UndoCount(), 0u);
    ASSERT_TRUE(Publish(29.f));
    // Cancellation may update scene storage, but never an in-flight frame.
    EXPECT_EQ(retained.Camera.Position.x, 23.f);
    EXPECT_EQ(retained.Scene.Instances[0].Transform(0, 3), 5.f);
    EXPECT_EQ(retained.Scene.Lights.PointLights[0].Position.x, 5.f);
    RenderFrameState restored = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(restored));
    EXPECT_EQ(restored.Camera.Position.x, 29.f);
    EXPECT_EQ(restored.Scene.Instances[0].Transform(0, 3), 0.f);
    EXPECT_EQ(restored.Scene.Instances[1].Transform(0, 3), 0.f);
    EXPECT_EQ(restored.Scene.Lights.PointLights[0].Position.x, 0.f);
}

TEST_F(EditorRenderHandoffTest, CommittedBatchAndTypedFieldReplayPublishCompleteFramesWithoutAnOverlay)
{
    BlockOverlayBuild();
    PreviewBatch(5.f, 9.f);
    ASSERT_TRUE(Session.BeginMutationPhase());
    ASSERT_TRUE(Session.Commit());
    ASSERT_TRUE(Session.BeginField(Session.TargetFor(Entities[0]), AuthoredFloatField::LightRange));
    ASSERT_TRUE(Session.PreviewField(17.f));
    ASSERT_TRUE(Session.Commit());
    ASSERT_TRUE(Session.MarkSaved());
    Session.EndMutationPhase();
    ASSERT_EQ(Session.History().UndoCount(), 2u);
    ASSERT_TRUE(Publish(3.f));
    RenderFrameState retained = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(retained));
    EXPECT_EQ(retained.Scene.Instances[1].Transform(0, 3), 9.f);
    EXPECT_EQ(retained.Scene.Lights.PointLights[0].Radius, 17.f);

    ASSERT_TRUE(Session.BeginMutationPhase());
    ASSERT_TRUE(Session.Undo());
    ASSERT_TRUE(Session.Undo());
    Session.EndMutationPhase();
    ASSERT_TRUE(Publish(4.f));
    EXPECT_EQ(retained.Scene.Instances[0].Transform(0, 3), 5.f);
    EXPECT_EQ(retained.Scene.Lights.PointLights[0].Radius, 17.f);
    RenderFrameState undone = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(undone));
    EXPECT_EQ(undone.Scene.Instances[0].Transform(0, 3), 0.f);
    EXPECT_EQ(undone.Scene.Instances[1].Transform(0, 3), 0.f);
    EXPECT_EQ(undone.Scene.Lights.PointLights[0].Radius, 11.f);
    EXPECT_TRUE(Session.Dirty());

    ASSERT_TRUE(Session.BeginMutationPhase());
    ASSERT_TRUE(Session.Redo());
    ASSERT_TRUE(Session.Redo());
    Session.EndMutationPhase();
    ASSERT_TRUE(Publish(5.f));
    RenderFrameState redone = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(redone));
    EXPECT_EQ(redone.Scene.Instances[0].Transform(0, 3), 5.f);
    EXPECT_EQ(redone.Scene.Instances[1].Transform(0, 3), 9.f);
    EXPECT_EQ(redone.Scene.Lights.PointLights[0].Radius, 17.f);
    EXPECT_FALSE(Session.Dirty());
    EXPECT_EQ(Session.History().UndoCount(), 2u);
}

TEST_F(EditorRenderHandoffTest, ReplacementAndOutgoingOwnerTeardownCannotInvalidateTheRetainedFrame)
{
    PreviewBatch(5.f, 9.f);
    ASSERT_TRUE(Publish(13.f));
    RenderFrameState retained = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(retained));
    const auto epoch = Session.SceneEpoch();
    ASSERT_TRUE(Session.BeginMutationPhase());
    ASSERT_TRUE(Session.BindScene(nullptr));
    Session.EndMutationPhase();
    EXPECT_GT(Session.SceneEpoch(), epoch);
    EXPECT_EQ(World.GetComponent<TransformComponent>(Entities[0])->Position.x, 0.f);
    EXPECT_EQ(World.GetComponent<TransformComponent>(Entities[1])->Position.x, 0.f);
    Actors.Shutdown();
    World.Shutdown();
    OwnersLive = false;
    Owner.Instances.clear();
    Owner.Grid = {};
    ClearPendingLights();
    // Actually unmap the outgoing world's component/instance backing. The
    // session and retained mailbox slots have independent storage owners.
    SourceArena.Shutdown();
    ASSERT_TRUE(Pipeline.PublishFrameState({}));
    EXPECT_EQ(retained.Camera.Position.x, 13.f);
    EXPECT_EQ(retained.Scene.InstanceCount, 2u);
    EXPECT_EQ(retained.Scene.Instances[0].Transform(0, 3), 5.f);
    EXPECT_EQ(retained.Scene.Instances[1].Transform(0, 3), 9.f);
    EXPECT_EQ(retained.Scene.Lights.PointLights[0].Position.x, 5.f);
    RenderFrameState replacement = {};
    ASSERT_TRUE(Pipeline.TryReadFrameState(replacement));
    EXPECT_FALSE(replacement.Scene.HasScene);
    EXPECT_EQ(replacement.Scene.InstanceCount, 0u);
}

TEST_F(EditorRenderHandoffTest, ConcurrentConsumerRetainsPreviewAcrossCancelHistoryReplayAndOwnerUnmap)
{
    BlockOverlayBuild();
    auto grid        = Owner.AuthoredGrid;
    grid.Origin      = {2.f, 3.f, 4.f};
    grid.Orientation = {.5f, -.5f, .5f, .5f};
    ASSERT_TRUE(Session.BeginMutationPhase());
    ASSERT_TRUE(Session.SetGridSettings(Owner.AuthoredGrid, grid));
    ASSERT_TRUE(Session.MarkSaved());
    Session.EndMutationPhase();
    PreviewBatch(5.f, 9.f);
    ASSERT_TRUE(Publish(13.f));

    // Only this consumer accesses TryRead/Release. The test thread remains the
    // actual session/source owner, just as it is in the production main loop.
    std::atomic<bool> consumer_ready = false;
    std::atomic<bool> producer_done  = false;
    bool              acquired       = false;
    uint32_t          samples        = 0;
    std::thread       consumer([&] {
        RenderFrameState retained = {};
        acquired                  = Pipeline.TryReadFrameState(retained);
        if (!acquired)
        {
            consumer_ready.store(true, std::memory_order_release);
            return;
        }
        const auto intact = [&] {
            const auto& orientation = retained.Scene.Grid.Orientation;
            const bool  draws       = retained.Scene.HasScene && retained.Scene.InstanceCount == 2 && retained.Scene.Instances && retained.Scene.Instances[0].Transform(0, 3) == 5.f && retained.Scene.Instances[1].Transform(0, 3) == 9.f;
            const bool  lights      = retained.Scene.Lights.PointCount == 1 && retained.Scene.Lights.PointLights[0].Position.x == 5.f && retained.Scene.Lights.PointLights[0].Radius == 11.f;
            const bool  plane       = retained.Scene.Grid.Origin == grid.Origin && orientation.x == grid.Orientation.x && orientation.y == grid.Orientation.y && orientation.z == grid.Orientation.z && orientation.w == grid.Orientation.w;
            return draws && lights && plane && retained.Camera.Position.x == 13.f;
        };
        bool coherent = intact();
        ++samples;
        consumer_ready.store(true, std::memory_order_release);
        while (!producer_done.load(std::memory_order_acquire))
        {
            coherent &= intact();
            ++samples;
            std::this_thread::yield();
        }
        // This check is ordered after the source arena has actually been
        // unmapped, not merely after its logical scene pointers were cleared.
        EXPECT_TRUE(intact());
        EXPECT_TRUE(coherent);
        RenderFrameState replacement = {};
        const bool       replaced    = Pipeline.TryReadFrameState(replacement);
        EXPECT_TRUE(replaced);
        if (replaced)
        {
            EXPECT_EQ(replacement.Camera.Position.x, 37.f);
            EXPECT_FALSE(replacement.Scene.HasScene);
            EXPECT_EQ(replacement.Scene.InstanceCount, 0u);
            EXPECT_EQ(replacement.Scene.Instances, nullptr);
        }
        Pipeline.ReleaseFrameState();
    });

    // Bound the only startup wait. No fatal assertion may abandon a joinable
    // consumer; every path below signals completion and joins before teardown.
    const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!consumer_ready.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::yield();
    }
    const bool ready = consumer_ready.load(std::memory_order_acquire);
    EXPECT_TRUE(ready);
    if (ready && acquired)
    {
        const auto offset = Memory.MainArena.m_current_offset;
        EXPECT_TRUE(Session.BeginMutationPhase());
        Session.Cancel();
        EXPECT_FALSE(Session.Dirty());
        Session.EndMutationPhase();
        EXPECT_TRUE(Publish(29.f));
        for (uint32_t i = 0; i < 64; ++i)
        {
            PreviewBatch(5.f + float(i), 9.f + float(i));
            EXPECT_TRUE(Session.BeginMutationPhase());
            if (i % 2 == 0)
            {
                Session.Cancel();
            }
            else
            {
                EXPECT_TRUE(Session.Commit());
                EXPECT_TRUE(Session.BeginField(Session.TargetFor(Entities[0]), AuthoredFloatField::LightRange));
                EXPECT_TRUE(Session.PreviewField(17.f + float(i)));
                EXPECT_TRUE(Session.Commit());
                EXPECT_TRUE(Session.MarkSaved());
                EXPECT_TRUE(Session.Undo());
                EXPECT_TRUE(Session.Dirty());
                EXPECT_TRUE(Session.Redo());
                EXPECT_FALSE(Session.Dirty());
            }
            Session.EndMutationPhase();
            EXPECT_TRUE(Publish(float(i)));
        }
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
        EXPECT_TRUE(Session.BeginMutationPhase());
        const bool detached = Session.BindScene(nullptr);
        EXPECT_TRUE(detached);
        Session.EndMutationPhase();
        if (detached)
        {
            Actors.Shutdown();
            World.Shutdown();
            OwnersLive = false;
            Owner.Instances.clear();
            Owner.Grid = {};
            ClearPendingLights();
            SourceArena.Shutdown();
        }
        RenderFrameState replacement  = {};
        replacement.Camera.Position.x = 37.f;
        EXPECT_TRUE(Pipeline.PublishFrameState(replacement));
    }
    producer_done.store(true, std::memory_order_release);
    consumer.join();
    EXPECT_TRUE(acquired);
    EXPECT_GT(samples, 0u);
    for (const auto& slot : Pipeline.FrameStateSlots)
    {
        EXPECT_EQ(slot.value.load(), static_cast<uint32_t>(PayloadSlotState::Free));
    }
}
