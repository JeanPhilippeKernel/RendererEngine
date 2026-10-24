#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorGridTest : public testing::Test
{
protected:
    struct Owner
    {
        const Scene*   World    = nullptr;
        EditorGridView View     = {};
        uint32_t       Writes   = 0;
        bool           Readable = true;
    };
    ZEngine::Core::Memory::MemoryManager memory  = {};
    Scene                                scene   = {};
    EditorSession                        session = {};
    Owner                                owner   = {};

    EditorSceneSettingsHooks             Hooks(Owner& value)
    {
        return {
            .Context = &value,
            .ReadGrid =
                [](void* context, const Scene& scene, EditorGridView& output) {
                    const auto& owner = *static_cast<const Owner*>(context);
                    if (!owner.Readable || owner.World != &scene)
                    {
                        return false;
                    }
                    output = owner.View;
                    return true;
                },
            .ApplyGrid =
                [](void* context, Scene&, const SceneGridSettings& value) {
                    auto& owner      = *static_cast<Owner*>(context);
                    owner.View.Value = value;
                    ++owner.Writes;
                },
        };
    }

    void SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
        owner.World          = &scene;
        owner.View.SceneUUID = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        owner.View.Editable  = true;
        ASSERT_TRUE(session.SetSceneSettingsHooks(Hooks(owner)));
    }
    void TearDown() override
    {
        session.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }
    SceneGridSettings At(float x) const
    {
        auto result     = owner.View.Value;
        result.Origin.x = x;
        return result;
    }
    bool Set(float x)
    {
        return session.SetGridSettings(owner.View.Value, At(x));
    }
    EntityID Entity()
    {
        const auto result = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(result, {*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")});
        scene.AddComponent<TransformComponent>(result, {});
        return result;
    }
    void Transform(EntityID entity, float x, bool commit)
    {
        const auto target = session.TargetFor(entity);
        ASSERT_TRUE(session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = x;
        ASSERT_TRUE(session.PreviewTransforms({&value, 1}));
        if (commit)
        {
            ASSERT_TRUE(session.Commit());
        }
    }
};

TEST_F(EditorGridTest, FullTypedPreviewCommitsOnceAndUndoRedoRestoreEveryFieldAndCheckpoint)
{
    const auto original = owner.View.Value;
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    ASSERT_TRUE(session.BeginGridSettings());
    auto changed           = At(3.f);
    changed.Origin.y       = -2.f;
    changed.Origin.z       = 7.f;
    changed.Orientation    = {.2f, -.3f, .4f, .5f};
    changed.Enabled        = false;
    changed.MajorLineEvery = 7;
    changed.BaseSpacing    = .5f;
    changed.LineWidth      = 2.f;
    changed.MinorColor     = {.1f, .2f, .3f, .4f};
    changed.MajorColor     = {.2f, .3f, .4f, .5f};
    changed.UColor         = {.3f, .4f, .5f, .6f};
    changed.VColor         = {.4f, .5f, .6f, .7f};
    changed.FadeRadius     = 2000.f;
    changed.FadeStrength   = 1.5f;
    changed.MaxLOD         = 16;
    changed.Labels         = SceneGridLabels::UV;
    ASSERT_TRUE(session.PreviewGridSettings(changed));
    const auto actual = owner.View.Value;
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_STREQ(session.History().UndoName(), "Grid settings");
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(EqualSceneGridSettings(owner.View.Value, original));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_TRUE(EqualSceneGridSettings(owner.View.Value, actual));
    ASSERT_TRUE(session.MarkSaved());
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
}

TEST_F(EditorGridTest, NoopsAndReturningToStartKeepRedoHistoryAndDoNotWriteUnusedLanes)
{
    ASSERT_TRUE(Set(2.f));
    ASSERT_TRUE(session.Undo());
    const auto revision = session.DocumentRevision(), writes = uint64_t(owner.Writes);
    auto       same  = owner.View.Value;
    same.Origin.x    = -0.f;
    same.Origin.w    = std::numeric_limits<float>::quiet_NaN();
    same.Orientation = {0.f, -0.f, 0.f, -2.f};
    ASSERT_TRUE(session.SetGridSettings(owner.View.Value, same));
    EXPECT_EQ(session.DocumentRevision(), revision);
    EXPECT_EQ(owner.Writes, writes);
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(3.f)));
    ASSERT_TRUE(session.PreviewGridSettings({}));
    ASSERT_TRUE(session.Commit());
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 1u);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.w, 0.f);
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 2.f);
}

TEST_F(EditorGridTest, InvalidSamplesPreserveTheLastAcceptedPreviewAndReleaseCommitsOnlyThatState)
{
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(2.f)));
    const auto accepted = owner.View.Value;
    const auto revision = session.DocumentRevision(), writes = uint64_t(owner.Writes);
    for (uint32_t fault = 0; fault < 5; ++fault)
    {
        auto invalid = At(9.f);
        if (fault == 0)
        {
            invalid.BaseSpacing = 0.f;
        }
        if (fault == 1)
        {
            invalid.Orientation = {0.f, 0.f, 0.f, 0.f};
        }
        if (fault == 2)
        {
            invalid.Origin.z = std::numeric_limits<float>::infinity();
        }
        if (fault == 3)
        {
            invalid.VColor[2] = 2.f;
        }
        if (fault == 4)
        {
            invalid.Labels = SceneGridLabels(2);
        }
        EXPECT_FALSE(session.PreviewGridSettings(invalid));
        EXPECT_TRUE(session.Active());
        EXPECT_TRUE(EqualSceneGridSettings(accepted, owner.View.Value));
        EXPECT_EQ(session.DocumentRevision(), revision);
        EXPECT_EQ(owner.Writes, writes);
    }
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 0.f);
}

TEST_F(EditorGridTest, CancellationNeverOverwritesANewerForeignGridOrSceneIdentity)
{
    ASSERT_TRUE(Set(1.f));
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(2.f)));
    owner.View.Value.Origin.y = 9.f;
    const auto writes         = owner.Writes;
    EXPECT_FALSE(session.PreviewGridSettings(At(3.f)));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 2.f);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.y, 9.f);
    EXPECT_EQ(owner.Writes, writes);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(4.f)));
    owner.View.SceneUUID = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    session.Cancel();
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 4.f);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorGridTest, EditabilityLossRestoresOwnedPreviewButMissingOwnerIsNeverWritten)
{
    for (bool writable : {false, true})
    {
        ASSERT_TRUE(session.BeginGridSettings());
        ASSERT_TRUE(session.PreviewGridSettings(At(3.f)));
        owner.View.Editable = writable;
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase(!writable));
        EXPECT_FALSE(session.Active());
        EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 0.f);
        EXPECT_FALSE(session.Dirty());
        owner.View.Editable = true;
        session.EndMutationPhase();
        ASSERT_TRUE(session.BeginMutationPhase());
    }
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(5.f)));
    const auto writes = owner.Writes;
    owner.Readable    = false;
    session.Cancel();
    EXPECT_EQ(owner.Writes, writes);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 5.f);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorGridTest, WrongPhaseAndThreadRejectBeforeReadingOrWritingTheSceneOwner)
{
    const auto expected = owner.View.Value, changed = At(2.f);
    session.EndMutationPhase();
    EXPECT_FALSE(session.BeginGridSettings());
    EXPECT_FALSE(session.SetGridSettings(expected, changed));
    EditorGridView view = {.Value = At(77.f)};
    std::thread    worker([&] {
        EXPECT_FALSE(session.ReadGridSettings(view));
        EXPECT_FALSE(session.SetSceneSettingsHooks(Hooks(owner)));
        EXPECT_FALSE(session.BeginGridSettings());
        EXPECT_FALSE(session.PreviewGridSettings(changed));
        EXPECT_FALSE(session.SetGridSettings(expected, changed));
    });
    worker.join();
    EXPECT_FLOAT_EQ(view.Value.Origin.x, 77.f);
    EXPECT_EQ(owner.Writes, 0u);
    ASSERT_TRUE(session.ReadGridSettings(view));
    EXPECT_FLOAT_EQ(view.Value.Origin.x, 0.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(changed));
    session.EndMutationPhase();
    EXPECT_FALSE(session.PreviewGridSettings(expected));
    EXPECT_FALSE(session.Commit());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 2.f);
}

TEST_F(EditorGridTest, HookOwnerCannotBeReplacedWithinAnEpochOrReplayHistoryIntoAnotherOwner)
{
    ASSERT_TRUE(Set(1.f));
    Owner replacement = owner;
    ASSERT_TRUE(session.SetSceneSettingsHooks(Hooks(owner)));
    EXPECT_FALSE(session.SetSceneSettingsHooks(Hooks(replacement)));
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 0.f);
    EXPECT_FLOAT_EQ(replacement.View.Value.Origin.x, 1.f);
    owner.View.SceneUUID = replacement.View.SceneUUID = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    const auto writes                                 = owner.Writes;
    EXPECT_FALSE(session.Redo());
    EXPECT_EQ(owner.Writes, writes);
    EXPECT_EQ(session.History().RedoCount(), 1u);
}

TEST_F(EditorGridTest, InvalidOrStaleCopiedIntentDoesNotCancelAnUnrelatedPreviewOrHistory)
{
    const auto entity = Entity();
    Transform(entity, 1.f, true);
    Transform(entity, 2.f, false);
    const auto revision = session.DocumentRevision();
    auto       wrong    = At(9.f);
    EXPECT_FALSE(session.SetGridSettings(wrong, At(3.f)));
    auto invalid        = At(3.f);
    invalid.BaseSpacing = 0.f;
    EXPECT_FALSE(session.SetGridSettings(owner.View.Value, invalid));
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 2.f);
    EXPECT_EQ(session.DocumentRevision(), revision);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(Set(3.f));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 3.f);
    EXPECT_EQ(session.History().UndoCount(), 2u);
    ASSERT_TRUE(session.Undo());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 0.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorGridTest, CompleteHistoryBudgetIsPreflightedBeforeCancellingAnotherInteraction)
{
    EditorSession small = {};
    ASSERT_TRUE(small.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 192}));
    ASSERT_TRUE(small.BeginMutationPhase());
    ASSERT_TRUE(small.BindScene(&scene));
    ASSERT_TRUE(small.SetSceneSettingsHooks(Hooks(owner)));
    const auto entity = Entity();
    const auto target = small.TargetFor(entity);
    ASSERT_TRUE(small.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 2.f;
    ASSERT_TRUE(small.PreviewTransforms({&value, 1}));
    const auto revision = small.DocumentRevision();
    EXPECT_TRUE(small.SetGridSettings(owner.View.Value, owner.View.Value));
    EXPECT_FALSE(small.SetGridSettings(owner.View.Value, At(4.f)));
    EXPECT_TRUE(small.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 2.f);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 0.f);
    EXPECT_EQ(small.DocumentRevision(), revision);
    small.Shutdown();
}

TEST_F(EditorGridTest, InterleavedEntityAndGridHistoryShareOneSemanticSavedCheckpoint)
{
    const auto entity = Entity();
    ASSERT_TRUE(Set(1.f));
    Transform(entity, 2.f, true);
    ASSERT_TRUE(session.MarkSaved());
    ASSERT_TRUE(Set(3.f));
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(Set(1.f));
    EXPECT_FALSE(session.Dirty()); // same source semantics, different operation chain
    ASSERT_TRUE(session.Undo());
    ASSERT_TRUE(session.Undo());
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorGridTest, RebindingCancelsOnOriginalOwnerThenRetiresHooksAndHistory)
{
    ASSERT_TRUE(Set(1.f));
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(3.f)));
    const auto epoch = session.SceneEpoch();
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 1.f);
    EXPECT_NE(session.SceneEpoch(), epoch);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Active());
    EditorGridView output = {.Value = At(9.f)};
    EXPECT_FALSE(session.ReadGridSettings(output));
    EXPECT_FLOAT_EQ(output.Value.Origin.x, 9.f);
    EXPECT_FALSE(session.BeginGridSettings());
    ASSERT_TRUE(session.SetSceneSettingsHooks(Hooks(owner)));
    ASSERT_TRUE(Set(4.f));
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 1.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorGridTest, RepeatedPreviewCommitUndoAndEvictionReuseBoundedStorage)
{
    const auto bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 1; i <= 2000; ++i)
    {
        ASSERT_TRUE(session.BeginGridSettings());
        const auto before = owner.View.Value;
        ASSERT_TRUE(session.PreviewGridSettings(At(float(i))));
        ASSERT_TRUE(session.PreviewGridSettings(At(float(i) + .25f)));
        ASSERT_TRUE(session.Commit());
        ASSERT_TRUE(session.Undo());
        EXPECT_TRUE(EqualSceneGridSettings(owner.View.Value, before));
        ASSERT_TRUE(session.Redo());
        EXPECT_LE(session.History().UndoCount(), 8u);
        EXPECT_LE(session.History().BytesUsed(), session.History().ByteCapacity());
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
    const auto history = session.History().BytesUsed();
    ASSERT_TRUE(session.BeginGridSettings());
    ASSERT_TRUE(session.PreviewGridSettings(At(5000.f)));
    session.Cancel();
    EXPECT_EQ(session.History().BytesUsed(), history);
    EXPECT_FLOAT_EQ(owner.View.Value.Origin.x, 2000.25f);
}
