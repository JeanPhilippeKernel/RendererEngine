#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <ZEngine/Controllers/FlyCameraController.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Engine/FixedTimestepAccumulator.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <ZEngine/Windows/Events/KeyEvent.h>
#include <ZEngine/Windows/Events/WindowEvent.h>
#include <ZEngine/Windows/Inputs/KeyCodeDefinition.h>
#include <gtest/gtest.h>
#include <cmath>
#include <memory>
#include <numbers>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::UI;

class EditorIntegrationTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory = {};
    Scene                                scene  = {};
    std::unique_ptr<Editor>              editor = {};
    ZUIContext                           ctx    = {};
    EntityID                             entity = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45293").value()});
        scene.AddComponent<TransformComponent>(entity, {});
        editor = std::make_unique<Editor>();
        ASSERT_TRUE(editor->Session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(editor->Session.BeginMutationPhase());
        ASSERT_TRUE(editor->Session.BindScene(&scene));
        const auto target = editor->Session.TargetFor(entity);
        ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
        LocalTransformValue value = {};
        value.Position.x          = 5.f;
        ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
        ASSERT_TRUE(editor->Session.Commit());
        editor->Session.EndMutationPhase();
    }

    void TearDown() override
    {
        editor->Session.Shutdown();
        scene.Shutdown();
        memory.Shutdown();
    }

    void Process()
    {
        ASSERT_TRUE(editor->Session.BeginMutationPhase());
        EXPECT_TRUE(editor->Session.ProcessRequests());
        editor->Session.EndMutationPhase();
    }
};

/// Exercises native event dispatch and the actual UI modifier adapter without
/// a window or an overlay build. This is not native OS/GPU acceptance.
class EditorHistoryInputTest : public EditorIntegrationTest
{
protected:
    std::unique_ptr<ZEngine::Applications::AppRenderPipeline> pipeline = {};
    Tetragrama::Layers::ZUILayer                              layer    = {};

    void                                                      SetUp() override
    {
        EditorIntegrationTest::SetUp();
        ASSERT_FALSE(HasFatalFailure());
        pipeline         = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
        pipeline->ZUICtx = &ctx;
        pipeline->OverlayBuildAvailable.value.store(false);
        editor->RenderPipeline = pipeline.get();
        layer.Initialize(&memory.MainArena, editor.get());
        editor->ZUIUILayer = &layer;
    }

    void TearDown() override
    {
        if (editor)
        {
            editor->ZUIUILayer     = nullptr;
            editor->RenderPipeline = nullptr;
            editor->CurrentScene   = nullptr;
        }
        layer.LocalArena.Shutdown();
        EditorIntegrationTest::TearDown();
    }

    void Key(ZENGINE_KEYCODE code, int repeat = 0)
    {
        ZEngine::Windows::Events::KeyPressedEvent event{code, repeat};
        editor->ProcessEvent(event);
    }

    void Release(ZENGINE_KEYCODE code)
    {
        ZEngine::Windows::Events::KeyReleasedEvent event{code};
        editor->ProcessEvent(event);
    }

    void ShortcutModifier()
    {
#if defined(__APPLE__)
        Key(ZENGINE_KEY_LEFT_SUPER);
#else
        Key(ZENGINE_KEY_LEFT_CONTROL);
#endif
    }

    bool QueuePosition(float x)
    {
        LocalTransformValue value = {};
        value.Position.x          = x;
        return editor->Session.RequestTransformValue(entity, value);
    }
};

class EditorFocusInputTest : public EditorHistoryInputTest
{
protected:
    void Focus(bool focused)
    {
        if (focused)
        {
            ZEngine::Windows::Events::WindowFocusGainedEvent event = {};
            editor->ProcessEvent(event);
        }
        else
        {
            ZEngine::Windows::Events::WindowFocusLostEvent event = {};
            editor->ProcessEvent(event);
        }
    }

    void QueueNumeric()
    {
        ZUIFeedMousePos(&ctx, 100.f, 0.f);
        ZUIFeedMouseButton(&ctx, 0, true);
        ctx.ActiveKey = 42;
        ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42, .PointerSerial = ctx.MousePressSerial[0], .StartX = 100.f, .UnitsPerPixel = .5f}));
    }
};

TEST_F(EditorFocusInputTest, FocusLossAndRegainBetweenUpdatesCancelRatherThanCommitASyntheticRelease)
{
    QueueNumeric();
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    ASSERT_TRUE(editor->Session.Active());
    ASSERT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    Focus(false);
    ZUIFeedMousePos(&ctx, 120.f, 0.f);
    ZEngine::Windows::Events::MouseButtonReleasedEvent release{ZENGINE_KEY_MOUSE_LEFT};
    editor->ProcessEvent(release); // GLFW synthesizes releases on focus loss.
    Focus(true);
    // Event callbacks cannot mutate the authored scene or cancel outside the phase.
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
}

TEST_F(EditorFocusInputTest, FocusLossAndRegainRetireAnUnstartedNumericPressBeforeTheMutationPoint)
{
    QueueNumeric();
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    Focus(false);
    Focus(true);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_FALSE(ctx.MousePressed[0]);
    EXPECT_FALSE(ctx.MouseDown[0]);
    EXPECT_EQ(ctx.ActiveKey, 0u);
}

TEST_F(EditorFocusInputTest, TransientNativeFocusLossDiscardsQueuedHistoryAndModeChanges)
{
    ctx.ViewportInputKey = 42;
    Key(ZENGINE_KEY_W);
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    ASSERT_EQ(editor->Session.PendingRequestCount(), 2u);
    Focus(false);
    Focus(true);
    editor->OnUpdate(1.f / 60.f);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(ctx.CtrlDown);
}

TEST_F(EditorFocusInputTest, MinimizeAndRestoreWithinOnePollCannotHideTheCancellationBoundary)
{
    QueueNumeric();
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    ZEngine::Windows::Events::WindowMinimizedEvent minimized = {};
    editor->ProcessEvent(minimized);
    ZEngine::Windows::Events::WindowRestoredEvent restored = {};
    editor->ProcessEvent(restored);
    editor->OnUpdate(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
}

TEST_F(EditorFocusInputTest, FailedNestedMutationEntryCannotConsumeThePendingFocusLoss)
{
    QueueNumeric();
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    Focus(false);
    Focus(true);
    editor->UpdateSessionInput(&ctx, true); // nested mutation entry must fail
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    editor->Session.EndMutationPhase();
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorFocusInputTest, FocusGainedWithoutALossDoesNotDiscardAValidQueuedEdit)
{
    ASSERT_TRUE(QueuePosition(8.f));
    Focus(true);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 8.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
}

TEST_F(EditorFocusInputTest, FreshPhysicalInputWorksAfterTheCancellationPhaseWithoutReusingItsSerial)
{
    QueueNumeric();
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    const auto serial = ctx.MousePressSerial[0];
    Focus(false);
    Focus(true);
    editor->UpdateSessionInput(&ctx, true);
    ASSERT_FALSE(editor->Session.Active());
    QueueNumeric();
    EXPECT_EQ(ctx.MousePressSerial[0], serial + 1);
    ZUIFeedMousePos(&ctx, 120.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 15.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().RedoCount(), 1u);
}

TEST_F(EditorFocusInputTest, RepeatedTransientFocusLossReusesStorageWithoutHistoryOrSerialAliasing)
{
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t cycle = 0; cycle < 512; ++cycle)
    {
        QueueNumeric();
        ZUIFeedMousePos(&ctx, 110.f, 0.f);
        editor->UpdateSessionInput(&ctx, true);
        ASSERT_TRUE(editor->Session.Active());
        Focus(false);
        Focus(true);
        editor->UpdateSessionInput(&ctx, true);
        ASSERT_FALSE(editor->Session.Active());
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
        EXPECT_EQ(ctx.MousePressSerial[0], cycle + 1u);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
}

TEST_F(EditorFocusInputTest, NewShortcutEdgesBeforeTheCancellationPhaseCannotQueueAnotherMutation)
{
    Focus(false);
    Focus(true);
    ctx.ViewportInputKey = 42;
    Key(ZENGINE_KEY_W);
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorIntegrationTest, NativeFocusLossWithoutAUiContextStillCancelsAtTheMutationBoundary)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 8.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    ZEngine::Windows::Events::WindowFocusLostEvent lost = {};
    editor->ProcessEvent(lost);
    ZEngine::Windows::Events::WindowFocusGainedEvent gained = {};
    editor->ProcessEvent(gained);
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 8.f);
    editor->UpdateSessionInput(nullptr, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, DistinctUndoPressesAreQueuedInOrderWithoutAnOverlayBuild)
{
    ASSERT_TRUE(QueuePosition(8.f));
    Process();
    ASSERT_EQ(editor->Session.History().UndoCount(), 2u);
    const auto offset = memory.MainArena.m_current_offset;
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    Release(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 8.f);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 2u);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_FALSE(pipeline->OverlayBuildAvailable.value.load());
}

TEST_F(EditorHistoryInputTest, UndoThenRedoBeforeAnUpdateReturnsToTheSavedCheckpoint)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.MarkSaved());
    editor->Session.EndMutationPhase();
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_LEFT_SHIFT);
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_FALSE(ctx.CtrlYPressed);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_FALSE(editor->Session.Dirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
}

TEST_F(EditorHistoryInputTest, RedoThenUndoBeforeAnUpdatePreservesEventOrderAtTheHistoryBoundary)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Undo());
    ASSERT_TRUE(editor->Session.MarkSaved());
    editor->Session.EndMutationPhase();
    ShortcutModifier();
    Key(ZENGINE_KEY_Y);
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_FALSE(editor->Session.Dirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, UndoUsesHistoryProducedByAnEarlierQueuedEditRatherThanEventTimeCounts)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Undo());
    editor->Session.EndMutationPhase();
    ASSERT_EQ(editor->Session.History().UndoCount(), 0u);
    ASSERT_TRUE(QueuePosition(8.f));
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, NativeHistoryKeyRepeatsRetainTheirOrderAndDoNotReplayOnLaterUpdates)
{
    ASSERT_TRUE(QueuePosition(8.f));
    Process();
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_Z, 1);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    for (uint32_t update = 0; update < 8; ++update)
    {
        editor->UpdateSessionInput(&ctx, true);
    }
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 2u);
}

TEST_F(EditorHistoryInputTest, TextAndPopupOwnedNativeShortcutsRemainAvailableToTheirWidgets)
{
    ShortcutModifier();
    ctx.TextInputActive = true;
    Key(ZENGINE_KEY_Z);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    ctx.CtrlZPressed    = false; // widget consumption
    ctx.TextInputActive = false;
    ctx.PopupStackSize  = 1;
    Key(ZENGINE_KEY_Y);
    EXPECT_TRUE(ctx.CtrlYPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ctx.CtrlYPressed   = false; // popup consumption
    ctx.PopupStackSize = 0;
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, ActiveInteractionConsumesNativeHistoryWithoutCancellingOrDeferringIt)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    editor->Session.EndMutationPhase();
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_TRUE(editor->Session.Active());
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    editor->Session.Cancel();
    editor->Session.EndMutationPhase();
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorHistoryInputTest, QueuePressureRetiresTheRejectedPhysicalShortcutInsteadOfReplayingIt)
{
    for (uint32_t i = 0; i < EditorSession::MaxPendingRequests; ++i)
    {
        ASSERT_TRUE(editor->Session.Request(EditorCommand::Cancel));
    }
    const auto offset = memory.MainArena.m_current_offset;
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), EditorSession::MaxPendingRequests);
    editor->UpdateSessionInput(&ctx, true);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(EditorHistoryInputTest, FocusLossDiscardsTheQueuedHistoryBurst)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_Y);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 2u);
    editor->UpdateSessionInput(&ctx, false);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorHistoryInputTest, ExtraHistoryRepeatsAreNoOpsAndDoNotDropTheFollowingRedo)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_Z, 1);
    Key(ZENGINE_KEY_Z, 1);
    Key(ZENGINE_KEY_Y);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 4u);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, ReadOnlySourceRetiresNativeHistoryBeforeTheNextMutationPhase)
{
    EditorScene owner    = {};
    owner.SourceReadOnly = true;
    editor->CurrentScene = &owner;
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    owner.SourceReadOnly = false;
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    editor->CurrentScene = nullptr;
}

TEST_F(EditorHistoryInputTest, ModalAndPendingPopupOwnNativeShortcutsEvenWithoutAnOverlayBuild)
{
    ShortcutModifier();
    ctx.ActiveModalKey = 123;
    Key(ZENGINE_KEY_Z);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ctx.CtrlZPressed    = false; // modal consumption
    ctx.ActiveModalKey  = 0;
    ctx.PendingPopupKey = 456;
    Key(ZENGINE_KEY_Y);
    EXPECT_TRUE(ctx.CtrlYPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_TRUE(ctx.CtrlYPressed);
    ctx.CtrlYPressed    = false; // popup consumption
    ctx.PendingPopupKey = 0;
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorHistoryInputTest, SceneReplacementRetiresQueuedNativeHistoryBeforeTheNewEpoch)
{
    ShortcutModifier();
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    const auto epoch = editor->Session.SceneEpoch();
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.BindScene(&scene));
    editor->Session.EndMutationPhase();
    EXPECT_GT(editor->Session.SceneEpoch(), epoch);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(editor->Session.Dirty());
}

TEST_F(EditorHistoryInputTest, UnmodifiedAndAltModifiedHistoryKeysDoNotBecomeSceneCommands)
{
    Key(ZENGINE_KEY_Z);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ShortcutModifier();
    Key(ZENGINE_KEY_RIGHT_ALT);
    Key(ZENGINE_KEY_Z);
    Key(ZENGINE_KEY_Y);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_FALSE(ctx.CtrlYPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    Release(ZENGINE_KEY_RIGHT_ALT);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorIntegrationTest, GlobalUndoRedoAreConsumedOnceWithoutAnOverlayBuild)
{
    ctx.CtrlZPressed = true;
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    for (uint32_t update = 0; update < 10; ++update)
    {
        editor->ProcessHistoryShortcuts(&ctx);
    }
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ctx.CtrlYPressed = true;
    editor->ProcessHistoryShortcuts(&ctx);
    Process();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorIntegrationTest, AuthoringWorldCannotRunRegisteredRuntimeSystemsOrQueuedCommands)
{
    auto                        engine   = std::make_unique<ZEngine::EngineContext>();
    ZEngine::ECS::WorldTick     tick     = {};
    ZEngine::ECS::WorldCommands commands = {};
    ZEngine::ECS::ActorManager  actors   = {};
    tick.Initialize(&memory.MainArena);
    commands.Initialize(&memory.MainArena);
    actors.Initialize(&memory.MainArena, scene);
    (void) tick.RegisterSystem([](Scene& world, float, ZEngine::ECS::WorldCommands&) { world.ForEach<TransformComponent>([](EntityID, TransformComponent& transform) { transform.Position.x = 99.f; }); }, {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    tick.Commit();
    engine->App           = editor.get();
    engine->Scene         = &scene;
    engine->WorldTick     = &tick;
    engine->WorldCommands = &commands;
    engine->ActorManager  = &actors;
    commands.DestroyEntity(entity);
    ZEngine::Timing::FixedTimestepAccumulator accumulator{};
    EXPECT_FALSE(engine->App->ShouldSimulate());
    for (uint32_t frame = 0; frame < 100; ++frame)
    {
        EXPECT_FLOAT_EQ(ZEngine::Engine::AdvanceSimulation(*engine, accumulator, 1.f), 1.f);
    }
    EXPECT_TRUE(scene.IsAlive(entity));
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(accumulator.StepCount(), 0u);
    EXPECT_FALSE(commands.IsEmpty());
    // The ordinary editor mutation phase and hierarchy synchronization still work.
    EXPECT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->UpdateSessionInput(&ctx, true, engine.get());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    ZEngine::ECS::Systems::SyncHierarchy(scene);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 0.f);
    EXPECT_TRUE(editor->Session.Request(EditorCommand::Redo));
    editor->UpdateSessionInput(&ctx, true, engine.get());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    ZEngine::ECS::Systems::SyncHierarchy(scene);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 5.f);
    commands.Clear();
    actors.Shutdown();
}

TEST_F(EditorIntegrationTest, TextAndPopupOwnershipPreserveWidgetUndo)
{
    ctx.TextInputActive = ctx.CtrlZPressed = true;
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ctx.TextInputActive = false;
    ctx.PopupStackSize  = 1;
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_TRUE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
}

TEST_F(EditorIntegrationTest, GizmoModeShortcutsKeepEventOrderAndApplyWithoutAnOverlay)
{
    ctx.ViewportInputKey = 42;
    EXPECT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    EXPECT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::E));
    EXPECT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::R));
    EXPECT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::Q));
    EXPECT_EQ(editor->Session.PendingRequestCount(), 4u);
    Process();
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::A));
}

TEST_F(EditorIntegrationTest, GizmoModeShortcutsDoNotStealTextFlightOrModifiedKeys)
{
    ctx.ViewportInputKey         = 42;
    bool ZUIContext::* blocked[] = {&ZUIContext::TextInputActive, &ZUIContext::CtrlDown, &ZUIContext::ShiftDown, &ZUIContext::AltDown};
    for (auto field : blocked)
    {
        ctx.*field = true;
        EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
        ctx.*field = false;
    }
    ctx.MouseDown[1] = true;
    EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    ctx.MouseDown[1]   = false;
    ctx.PopupStackSize = 1;
    EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    ctx.PopupStackSize = 0;
    ctx.ActiveModalKey = 42;
    EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    ctx.ActiveModalKey   = 0;
    ctx.ViewportInputKey = 0;
    EXPECT_FALSE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    EXPECT_FALSE(editor->ProcessGizmoShortcut(nullptr, ZUIKey::W));
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
}

TEST_F(EditorHistoryInputTest, PendingPopupRetiresAllNativeModeEdgesWithoutPublicationOrDeferredReplay)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.MarkSaved());
    editor->Session.EndMutationPhase();
    ctx.ViewportInputKey = ctx.HotKey = 42;
    const auto offset                 = memory.MainArena.m_current_offset;
    const auto revision               = editor->Session.GizmoSettings().Revision;
    const auto frame                  = ctx.FrameIndex;
    ZUIOpenPopup(&ctx, "ModeShortcutPopup", 30.f, 40.f);
    const auto popup = ctx.PendingPopupKey;
    ASSERT_NE(popup, 0u);
    const ZENGINE_KEYCODE keys[] = {ZENGINE_KEY_Q, ZENGINE_KEY_W, ZENGINE_KEY_E, ZENGINE_KEY_R};
    for (const auto code : keys)
    {
        Key(code);
        Release(code);
        Key(code, 1);
        Release(code);
        EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    }
    EXPECT_EQ(ctx.PendingPopupKey, popup);
    EXPECT_EQ(ctx.PopupStackSize, 0u);
    EXPECT_EQ(ctx.FrameIndex, frame);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, revision);
    ZEngine::Applications::OverlayPayload* overlay = nullptr;
    uint32_t                               slot    = 0;
    EXPECT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
    ZUIClosePopup(&ctx);
    for (uint32_t update = 0; update < 16; ++update)
    {
        editor->UpdateSessionInput(&ctx, true);
    }
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, revision);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_FALSE(editor->Session.Dirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
    Key(ZENGINE_KEY_E);
    Release(ZENGINE_KEY_E);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Rotate);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, revision + 1);
    EXPECT_FALSE(editor->Session.Dirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(EditorHistoryInputTest, PopupPublicationAndCloseRestoreOnlyFreshNativeModeEdges)
{
    ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 16);
    ZUIBeginFrame(&ctx, 1.f / 60.f);
    ctx.ViewportInputKey = ctx.HotKey = 42;
    const auto offset                 = memory.MainArena.m_current_offset;
    const auto revision               = editor->Session.GizmoSettings().Revision;
    ZUIOpenPopup(&ctx, "ModeShortcutPopup", 30.f, 40.f);
    const auto popup = ctx.PendingPopupKey;
    Key(ZENGINE_KEY_W);
    Release(ZENGINE_KEY_W);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ZUIEndFrame(&ctx);
    EXPECT_EQ(ctx.PendingPopupKey, 0u);
    EXPECT_EQ(ctx.PopupStackSize, 1u);
    EXPECT_EQ(ctx.PopupStack[0].Key, popup);
    Key(ZENGINE_KEY_E);
    Release(ZENGINE_KEY_E);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ZUIClosePopup(&ctx);
    Key(ZENGINE_KEY_R);
    Release(ZENGINE_KEY_R);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    ZUIOpenPopup(&ctx, "ModeShortcutPopup", 30.f, 40.f);
    Key(ZENGINE_KEY_Q);
    Release(ZENGINE_KEY_Q);
    ZUIClosePopup(&ctx);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Scale);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, revision + 1);
    Key(ZENGINE_KEY_Q, 1);
    Release(ZENGINE_KEY_Q);
    for (uint32_t update = 0; update < 16; ++update)
    {
        editor->UpdateSessionInput(&ctx, true);
    }
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Scale);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    Key(ZENGINE_KEY_Q);
    Release(ZENGINE_KEY_Q);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, revision + 2);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    ZUIContextDestroy(&ctx);
}

TEST_F(EditorIntegrationTest, BackpressuredModeTransitionCancelsPreviewAndReleasesCapture)
{
    auto pipeline    = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx = &ctx;
    pipeline->OverlayBuildAvailable.value.store(false);
    editor->RenderPipeline = pipeline.get();
    ctx.ViewportInputKey = ctx.HotKey = 42;
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 8.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    ctx.InteractionCapture = {true, true};
    ASSERT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    ZEngine::Applications::OverlayPayload* overlay = nullptr;
    uint32_t                               slot    = 0;
    EXPECT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Translate);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    editor->RenderPipeline = nullptr;
}

TEST_F(EditorIntegrationTest, NativeModeEventsAreOneShotAndIgnoreKeyRepeats)
{
    auto pipeline          = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx       = &ctx;
    editor->RenderPipeline = pipeline.get();
    ctx.ViewportInputKey   = 42;
    ZEngine::Windows::Events::KeyPressedEvent repeat{ZENGINE_KEY_W, 1};
    editor->ProcessEvent(repeat);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ZEngine::Windows::Events::KeyPressedEvent press{ZENGINE_KEY_W, 0};
    editor->ProcessEvent(press);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Translate);
    for (uint32_t update = 0; update < 10; ++update)
    {
        editor->UpdateSessionInput(&ctx, true);
    }
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.GizmoSettings().Revision, 2u);
    editor->RenderPipeline = nullptr;
}

TEST_F(EditorIntegrationTest, FocusLossDiscardsDelayedModeEventsRatherThanReplayingThem)
{
    ctx.ViewportInputKey = 42;
    ASSERT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    editor->UpdateSessionInput(&ctx, false);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Select);
}

TEST_F(EditorIntegrationTest, ModeChangeRetiresAnUnstartedNumericPressSoItCannotRearm)
{
    ctx.ViewportInputKey = 42;
    ctx.ActiveKey        = 99;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ASSERT_TRUE(editor->ProcessGizmoShortcut(&ctx, ZUIKey::W));
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 99, .UnitsPerPixel = 1.f}));
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.MousePressed[0]);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorIntegrationTest, ActiveInteractionConsumesGlobalShortcutWithoutReplayingAfterCancel)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    editor->Session.EndMutationPhase();
    ctx.CtrlZPressed = true;
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    editor->Session.Cancel();
    editor->Session.EndMutationPhase();
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorIntegrationTest, ReadOnlyModeDisablesHistoryAndConsumesTheShortcut)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase(false));
    editor->Session.EndMutationPhase();
    EXPECT_FALSE(editor->Session.CanUndo());
    ctx.CtrlZPressed = true;
    editor->ProcessHistoryShortcuts(&ctx);
    EXPECT_FALSE(ctx.CtrlZPressed);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
}

TEST_F(EditorIntegrationTest, EditorUpdateDrainsCommandsWithoutRenderingUI)
{
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->OnUpdate(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Redo));
    editor->OnUpdate(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
}

TEST_F(EditorIntegrationTest, ClosingRestoresPreviewBeforeShuttingDownTheSession)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 8.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    editor->OnClosing();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_FALSE(editor->Session.Ready());
    EXPECT_FALSE(editor->Session.Request(EditorCommand::Undo));
}

TEST_F(EditorIntegrationTest, BackpressuredOverlayDoesNotDelayCancellationOrCameraCapture)
{
    struct CaptureCamera : ZEngine::Controllers::FlyCameraController
    {
        ZUIInputCapture Captured = {};
        ZUIInputCapture Used     = {};

        void            SetInputCapture(bool pointer, bool keyboard) override
        {
            Captured = {pointer, keyboard};
        }
        void Update(ZEngine::Core::TimeStep) override
        {
            Used = Captured;
        }
    } camera         = {};

    auto pipeline    = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx = &ctx;
    pipeline->OverlayBuildAvailable.value.store(false);
    editor->RenderPipeline   = pipeline.get();
    editor->CameraController = &camera;
    ctx.ViewportInputKey = ctx.HotKey = 42;
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 8.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();

    for (uint32_t update = 0; update < 3; ++update)
    {
        ZEngine::Applications::OverlayPayload* overlay = nullptr;
        uint32_t                               slot    = 0;
        EXPECT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
        editor->Update(1.f / 60.f);
        EXPECT_TRUE(camera.Used.Pointer);
        EXPECT_TRUE(camera.Used.Keyboard);
    }
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_FALSE(ctx.EscapePressed);
    editor->RenderPipeline   = nullptr;
    editor->CameraController = nullptr;
}

TEST_F(EditorIntegrationTest, BackpressuredNumericControlPreviewsAndCommitsTheFinalPointerWithoutAnyUIBuild)
{
    auto pipeline    = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx = &ctx;
    pipeline->OverlayBuildAvailable.value.store(false);
    editor->RenderPipeline = pipeline.get();
    ctx.ActiveKey          = 42;
    ctx.MouseDown[0]       = true;
    ctx.MousePos[0]        = 110.f;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42, .StartX = 100.f, .UnitsPerPixel = .5f}));
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    EXPECT_TRUE(ctx.InteractionCapture.Pointer);
    EXPECT_TRUE(ctx.InteractionCapture.Keyboard);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    ctx.MousePos[0] = 116.f;
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 13.f);
    ctx.MousePos[0]  = 120.f;
    ctx.MouseDown[0] = false;
    ctx.ActiveKey    = 0; // UI may already have observed release; it does not own commit.
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.ControlKey(), 0u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 15.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_FALSE(ctx.InteractionCapture.Pointer);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    editor->RenderPipeline = nullptr;
}

TEST_F(EditorIntegrationTest, EscapeDiscardsPendingNumericPressAndPreventsRetainedUIRearmingIt)
{
    auto pipeline    = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx = &ctx;
    pipeline->OverlayBuildAvailable.value.store(false);
    editor->RenderPipeline = pipeline.get();
    ctx.ActiveKey          = 42;
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    ctx.MousePos[0]                        = 100.f;
    ctx.EscapePressed                      = true;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42}));
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.MousePressed[0]);
    EXPECT_FALSE(ctx.EscapePressed);
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    editor->RenderPipeline = nullptr;
}

TEST_F(EditorIntegrationTest, LostNumericCaptureCancelsAndRestoresBeforeCameraNavigation)
{
    auto pipeline          = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
    pipeline->ZUICtx       = &ctx;
    editor->RenderPipeline = pipeline.get();
    ctx.ActiveKey          = 42;
    ctx.MouseDown[0]       = true;
    ctx.MousePos[0]        = 100.f;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42}));
    editor->OnUpdate(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    ctx.ActiveKey = 99; // capture transferred to another control while still held
    editor->OnUpdate(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 99u);
    EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
    editor->RenderPipeline = nullptr;
}

TEST_F(EditorIntegrationTest, NumericCommitUsesPhysicalReleasePositionInsteadOfLaterUncapturedMotion)
{
    ZUIFeedMousePos(&ctx, 100.f, 0.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.ActiveKey = 42;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42, .PointerSerial = ctx.MousePressSerial[0], .StartX = 100.f, .UnitsPerPixel = .5f}));
    ZUIFeedMousePos(&ctx, 110.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 10.f);
    ZUIFeedMousePos(&ctx, 120.f, 0.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 180.f, 0.f);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 15.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
}

TEST_F(EditorIntegrationTest, ReleaseAndAnotherPressBetweenUpdatesCannotExtendThePreviousTransaction)
{
    ZUIFeedMousePos(&ctx, 100.f, 0.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    ctx.ActiveKey = 42;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42, .PointerSerial = ctx.MousePressSerial[0], .StartX = 100.f, .UnitsPerPixel = .5f}));
    editor->UpdateSessionInput(&ctx, true);
    ZUIFeedMousePos(&ctx, 120.f, 0.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 200.f, 0.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 15.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.MousePressed[0]);
}

TEST_F(EditorIntegrationTest, FocusLossDiscardsPendingOrActiveNumericInputAndRetiresThePhysicalPress)
{
    for (bool begin : {false, true})
    {
        ZUIFeedMousePos(&ctx, 100.f, 0.f);
        ZUIFeedMouseButton(&ctx, 0, true);
        ctx.ActiveKey = 42;
        ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42, .PointerSerial = ctx.MousePressSerial[0], .UnitsPerPixel = .5f}));
        if (begin)
        {
            editor->UpdateSessionInput(&ctx, true);
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 55.f);
        }
        editor->UpdateSessionInput(&ctx, false);
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
        EXPECT_FALSE(editor->Session.Active());
        EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_FALSE(ctx.MousePressed[0]);
        EXPECT_FALSE(ctx.MouseDown[0]);
        EXPECT_FALSE(ctx.InteractionCapture.Keyboard);
        editor->UpdateSessionInput(&ctx, true);
        EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    }
}

TEST_F(EditorIntegrationTest, RejectedNumericTargetCannotRearmItsControlAgainstAReplacementEntity)
{
    ctx.ActiveKey    = 42;
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    ASSERT_TRUE(editor->Session.RequestControlDrag(entity, {.Key = 42}));
    const auto identity = scene.GetComponent<UUIDComponent>(entity)->Value;
    scene.DestroyEntity(entity);
    const auto replacement = scene.CreateEntity();
    scene.AddComponent<UUIDComponent>(replacement, {identity});
    scene.AddComponent<TransformComponent>(replacement, {});
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(ctx.MousePressed[0]);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(replacement)->Position.x, 0.f);
}

TEST_F(EditorIntegrationTest, RejectedDelayedSelectionDoesNotStealAnotherWidgetsPointerCapture)
{
    ASSERT_TRUE(editor->Session.RequestSelection(entity));
    scene.DestroyEntity(entity);
    ctx.ActiveKey    = 77;
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_EQ(ctx.ActiveKey, 77u);
    EXPECT_TRUE(ctx.MousePressed[0]);
    EXPECT_FALSE(editor->Session.Active());
}

namespace
{
    class EditorGizmoIntegrationTest : public EditorIntegrationTest
    {
    protected:
        struct CaptureCamera : ZEngine::Controllers::FlyCameraController
        {
            mutable ZEngine::Rendering::Cameras::FlyCamera Camera{4.f / 3.f, {}};
            ZUIInputCapture                                Captured = {}, Used = {};
            uint32_t                                       Updates       = 0;
            bool                                           AdvanceCamera = false;

            ZEngine::Rendering::Cameras::CameraPtr         GetCamera() const override
            {
                return &Camera;
            }
            void SetInputCapture(bool pointer, bool keyboard) override
            {
                Captured = {pointer, keyboard};
            }
            void Update(ZEngine::Core::TimeStep dt) override
            {
                Used = Captured;
                ++Updates;
                if (AdvanceCamera && !Used.Pointer && !Used.Keyboard)
                {
                    Camera.OnUpdate(dt.GetSecond());
                }
            }
        } camera                                                           = {};
        std::unique_ptr<ZEngine::Applications::AppRenderPipeline> pipeline = {};
        ZEngine::Applications::ApplicationState                   state    = {};
        const float                                               rect[4]  = {37.f, 61.f, 837.f, 661.f};

        void                                                      SetUp() override
        {
            EditorIntegrationTest::SetUp();
            ZUIContextInit(&ctx, &memory.MainArena, ZKilo(128), ZKilo(64), 64, 32);
            pipeline         = std::make_unique<ZEngine::Applications::AppRenderPipeline>();
            pipeline->ZUICtx = &ctx;
            pipeline->OverlayBuildAvailable.value.store(false);
            editor->RenderPipeline   = pipeline.get();
            editor->CameraController = &camera;
            editor->State            = &state;
            camera.Camera.SetPosition({5.f, 0.f, 10.f});
            camera.Camera.SetOrientation(0.f, 0.f);
            ASSERT_TRUE(editor->Session.BeginMutationPhase());
            ASSERT_TRUE(editor->Session.Select(entity));
            ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Translate}));
            ASSERT_TRUE(editor->Session.ProcessRequests());
            editor->Session.EndMutationPhase();
            BindUI();
        }

        void TearDown() override
        {
            editor->GizmoViewport.Unbind();
            editor->RenderPipeline   = nullptr;
            editor->CameraController = nullptr;
            editor->State            = nullptr;
            pipeline.reset();
            ZUIContextDestroy(&ctx);
            EditorIntegrationTest::TearDown();
        }

        void BindUI()
        {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            auto* root         = ZUIPushBox(&ctx, "", 0, ZUI_None);
            root->Key          = 1;
            root->ScreenMax[0] = 1024.f;
            root->ScreenMax[1] = 768.f;
            for (uint64_t key : {42u, 99u})
            {
                ctx.Current       = root;
                auto* box         = ZUIPushBox(&ctx, "", 0, ZUI_Clickable | ZUI_NoKeyboardFocus);
                box->Key          = key;
                box->ScreenMin[0] = rect[0];
                box->ScreenMin[1] = rect[1];
                box->ScreenMax[0] = rect[2];
                box->ScreenMax[1] = rect[3];
                if (key == 99)
                {
                    box->CustomHitTest     = Gizmo::Viewport::Filter;
                    box->CustomHitTestData = &editor->GizmoViewport;
                }
            }
            ctx.ViewportInputKey = ctx.HotKey = 42;
            ctx.MousePos[0]                   = 437.f;
            ctx.MousePos[1]                   = 361.f;
            editor->GizmoViewport.Bind(&ctx, rect, 42, 99);
            editor->RefreshGizmoHitFrame(&ctx);
        }

        bool MovePointerToWorld(const ZEngine::Core::Maths::Vec3f& world)
        {
            using namespace ZEngine::Core::Maths;
            const auto                frame      = camera.Camera.CaptureFrameData();
            Geometry::ProjectionFrame projection = {};
            Geometry::ProjectedPoint  point      = {};
            const Geometry::Viewport  bounds     = {
                {          rect[0],           rect[1]},
                {rect[2] - rect[0], rect[3] - rect[1]}
            };
            if (!Geometry::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, Geometry::ProjectionKind::Perspective, false, &projection) || !Geometry::Project(world, projection, bounds, &point))
            {
                return false;
            }
            ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
            return true;
        }

        bool BeginPhysicalViewRotation(Gizmo::RotationSnapshot& snapshot, ZEngine::Core::Maths::Vec3f& grab)
        {
            if (!editor->Session.BeginMutationPhase())
            {
                return false;
            }
            const bool changed = editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Rotate}) && editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Rotation, .Enabled = false}) && editor->Session.ProcessRequests();
            editor->Session.EndMutationPhase();
            if (!changed)
            {
                return false;
            }
            editor->RefreshGizmoHitFrame(&ctx);
            using namespace ZEngine::Core::Maths;
            const auto                frame      = camera.Camera.CaptureFrameData();
            Geometry::ProjectionFrame projection = {};
            if (!Geometry::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, Geometry::ProjectionKind::Perspective, false, &projection))
            {
                return false;
            }
            for (const auto& shape : editor->GizmoViewport.Hits().View())
            {
                if (shape.Handle != uint32_t(Gizmo::Handle::RotateView))
                {
                    continue;
                }
                const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                if (editor->GizmoViewport.Hits().Pick(point).Handle != shape.Handle || !editor->GizmoViewport.Hits().PickRingPoint(Gizmo::Handle::RotateView, point, projection, grab))
                {
                    continue;
                }
                ZUIFeedMousePos(&ctx, point.x, point.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                editor->Update(1.f / 60.f);
                return editor->Session.ReadRotationSnapshot(snapshot);
            }
            return false;
        }

        bool BeginPhysicalUniformScale(ZEngine::Core::Maths::Vec2f& pointer)
        {
            if (!editor->Session.BeginMutationPhase())
            {
                return false;
            }
            const bool changed = editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Scale}) && editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Scale, .Enabled = false}) && editor->Session.ProcessRequests();
            editor->Session.EndMutationPhase();
            if (!changed)
            {
                return false;
            }
            editor->RefreshGizmoHitFrame(&ctx);
            for (const auto& shape : editor->GizmoViewport.Hits().View())
            {
                if (shape.Handle != uint32_t(Gizmo::Handle::ScaleUniform))
                {
                    continue;
                }
                pointer = (shape.Points[0] + shape.Points[1]) * .5f;
                ZUIFeedMousePos(&ctx, pointer.x, pointer.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                editor->Update(1.f / 60.f);
                return editor->Session.ScaleActive();
            }
            return false;
        }

        bool BeginPhysicalLight()
        {
            if (!editor->Session.BeginMutationPhase())
            {
                return false;
            }
            if (!scene.GetComponent<LightComponent>(entity))
            {
                scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point, .Range = 2.f});
            }
            const bool changed = editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Select}) && editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Translation, .Enabled = false}) && editor->Session.ProcessRequests();
            scene.SynchronizeHierarchy();
            editor->Session.EndMutationPhase();
            if (!changed)
            {
                return false;
            }
            editor->RefreshGizmoHitFrame(&ctx);
            for (const auto& shape : editor->GizmoViewport.Hits().View())
            {
                if (shape.Handle != uint32_t(Gizmo::Handle::LightRangePositiveX))
                {
                    continue;
                }
                const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                ZUIFeedMousePos(&ctx, point.x, point.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                editor->UpdateGizmoInput(&ctx, true);
                editor->Update(1.f / 60.f);
                return editor->Session.LightActive();
            }
            return false;
        }

        bool BeginPhysicalTranslation()
        {
            scene.SynchronizeHierarchy();
            editor->RefreshGizmoHitFrame(&ctx);
            for (const auto& shape : editor->GizmoViewport.Hits().View())
            {
                if (shape.Handle != uint32_t(Gizmo::Handle::TranslateX))
                {
                    continue;
                }
                const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                if (editor->GizmoViewport.Hits().Pick(point).Handle != shape.Handle)
                {
                    continue;
                }
                ZUIFeedMousePos(&ctx, point.x, point.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                editor->Update(1.f / 60.f);
                return editor->Session.TranslationActive();
            }
            return false;
        }

        bool BeginPhysicalCamera(Gizmo::CameraSnapshot& snapshot)
        {
            if (!editor->Session.BeginMutationPhase())
            {
                return false;
            }
            if (!scene.GetComponent<CameraComponent>(entity))
            {
                scene.AddComponent<CameraComponent>(entity, {.Near = 1.f, .Far = 4.f, .AspectRatio = 1.5f});
            }
            const bool changed = editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Select}) && editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Translation, .Enabled = false}) && editor->Session.ProcessRequests();
            scene.SynchronizeHierarchy();
            editor->Session.EndMutationPhase();
            if (!changed)
            {
                return false;
            }
            editor->RefreshGizmoHitFrame(&ctx);
            for (const auto& shape : editor->GizmoViewport.Hits().View())
            {
                if (shape.Handle != uint32_t(Gizmo::Handle::CameraFarLowerLeft))
                {
                    continue;
                }
                const auto point = (shape.Points[0] + shape.Points[1]) * .5f;
                ZUIFeedMousePos(&ctx, point.x, point.y);
                ZUIFeedMouseButton(&ctx, 0, true);
                editor->UpdateGizmoInput(&ctx, true);
                editor->Update(1.f / 60.f);
                return editor->Session.ReadCameraSnapshot(snapshot);
            }
            return false;
        }

        bool BeginPhysicalPivot()
        {
            using namespace ZEngine::Core::Maths;
            ctx.AltDown = true;
            editor->RefreshGizmoHitFrame(&ctx);
            const auto                frame      = camera.Camera.CaptureFrameData();
            Geometry::ProjectionFrame projection = {};
            Geometry::ProjectedPoint  point      = {};
            if (!Geometry::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, Geometry::ProjectionKind::Perspective, false, &projection))
            {
                return false;
            }
            Vec3f pivot = {};
            if (!editor->Session.ReadCustomPivot(pivot))
            {
                pivot = {5.f, 0.f, 0.f};
            }
            if (!Geometry::Project(
                    pivot,
                    projection,
                    {
                    { 37.f,  61.f},
                    {800.f, 600.f}
            },
                    &point))
            {
                return false;
            }
            ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
            ZUIFeedMouseButton(&ctx, 0, true);
            editor->Update(1.f / 60.f);
            return editor->Session.PivotActive();
        }

        void MovePhysicalScale(const ZEngine::Core::Maths::Vec2f& pointer, float ratio)
        {
            ZUIFeedMousePos(&ctx, pointer.x + float(std::log(double(ratio)) * std::numbers::sqrt2 * Gizmo::Constants::UniformScalePixelsPerEfold), pointer.y);
        }

        bool MovePhysicalRotation(const Gizmo::RotationSnapshot& snapshot, const ZEngine::Core::Maths::Vec3f& grab, double angle)
        {
            using namespace ZEngine::Core::Maths;
            const auto                frame      = camera.Camera.CaptureFrameData();
            Geometry::ProjectionFrame projection = {};
            if (!Geometry::MakeProjectionFrame(frame.View, frame.Projection, frame.Position, Geometry::ProjectionKind::Perspective, false, &projection))
            {
                return false;
            }
            const float              radius  = (grab - snapshot.Pivot).magnitude();
            const auto               tangent = cross3d(snapshot.Axis, snapshot.StartDirection);
            const auto               world   = snapshot.Pivot + (snapshot.StartDirection * float(std::cos(angle)) + tangent * float(std::sin(angle))) * radius;
            Geometry::ProjectedPoint point   = {};
            if (!Geometry::Project(
                    world,
                    projection,
                    {
                    { 37.f,  61.f},
                    {800.f, 600.f}
            },
                    &point))
            {
                return false;
            }
            ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
            return true;
        }
    };
} // namespace

// Exercise the actual retained-hit/physical-input route with both a held press
// and an already queued matching release. This is not native OS acceptance.
class EditorComponentReplacementInputTest : public EditorGizmoIntegrationTest, public testing::WithParamInterface<bool>
{
protected:
    template <typename T>
    void ReplaceDuringPreview(const T& replacement)
    {
        const auto ui_frame = ctx.FrameIndex;
        const auto bytes    = ctx.FrameArena.m_current_offset;
        const auto lifetime = scene.ComponentLifetime<T>(entity);
        scene.RemoveComponent<T>(entity);
        scene.AddComponent<T>(entity, replacement);
        EXPECT_NE(scene.ComponentLifetime<T>(entity), lifetime);
        if (GetParam())
        {
            ZUIFeedMouseButton(&ctx, 0, false);
        }
        ZUIFeedMousePos(&ctx, 2000.f, 100.f);
        editor->Update(1.f / 60.f);
        EXPECT_FALSE(editor->Session.Active());
        EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
        EXPECT_EQ(editor->Session.History().RedoCount(), 0u);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        // A press that began on a handle stays excluded from camera navigation
        // until physically released, but no longer owns a transaction/keyboard.
        EXPECT_EQ(camera.Used.Pointer, !GetParam());
        EXPECT_FALSE(camera.Used.Keyboard);
        EXPECT_TRUE(editor->Session.Dirty());
        for (uint32_t i = 0; i < 3; ++i)
        {
            editor->Update(1.f / 60.f);
        }
        EXPECT_FALSE(editor->Session.Active());
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_EQ(ctx.FrameIndex, ui_frame);
        EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
        EXPECT_FALSE(pipeline->OverlayBuildAvailable.value.load());
        if (!GetParam())
        {
            ZUIFeedMouseButton(&ctx, 0, false);
            editor->Update(1.f / 60.f);
        }
        EXPECT_FALSE(camera.Used.Pointer);
        EXPECT_FALSE(camera.Used.Keyboard);
    }

    void CancelFreshGesture()
    {
        EXPECT_TRUE(camera.Used.Keyboard);
        ctx.EscapePressed = true;
        editor->Update(1.f / 60.f);
        EXPECT_FALSE(editor->Session.Active());
        EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
        EXPECT_EQ(ctx.ActiveKey, 0u);
        EXPECT_FALSE(camera.Used.Keyboard);
        ZUIFeedMouseButton(&ctx, 0, false);
        editor->Update(1.f / 60.f);
    }
};

TEST_P(EditorComponentReplacementInputTest, TransformReplacementRetiresCaptureBeforeCameraWithoutOverwritingInterpolation)
{
    ASSERT_TRUE(BeginPhysicalTranslation());
    ZUIFeedMousePos(&ctx, ctx.MousePos[0] + 30.f, ctx.MousePos[1]);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    auto replacement = *scene.GetComponent<TransformComponent>(entity);
    ASSERT_NE(replacement.Position.x, 5.f);
    replacement.PreviousPosition = {17.f, 18.f, 19.f};
    ReplaceDuringPreview(replacement);
    const auto* current = scene.GetComponent<TransformComponent>(entity);
    ASSERT_NE(current, nullptr);
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*current), CaptureLocalTransform(replacement)));
    EXPECT_EQ(current->PreviousPosition, replacement.PreviousPosition);
    ASSERT_TRUE(BeginPhysicalTranslation());
    CancelFreshGesture();
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), CaptureLocalTransform(replacement)));
}

TEST_P(EditorComponentReplacementInputTest, LightReplacementRetiresCaptureBeforeCameraWithoutRollingBackItsValue)
{
    ASSERT_TRUE(BeginPhysicalLight());
    ZUIFeedMousePos(&ctx, ctx.MousePos[0] + 20.f, ctx.MousePos[1]);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.LightActive());
    const auto replacement = *scene.GetComponent<LightComponent>(entity);
    ASSERT_GT(replacement.Range, 2.f);
    ReplaceDuringPreview(replacement);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, replacement.Range);
    ASSERT_TRUE(BeginPhysicalLight());
    CancelFreshGesture();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, replacement.Range);
}

TEST_P(EditorComponentReplacementInputTest, CameraReplacementRetiresCaptureBeforeCameraWithoutRollingBackItsValue)
{
    Gizmo::CameraSnapshot start = {};
    ASSERT_TRUE(BeginPhysicalCamera(start));
    Gizmo::CameraHandle handle = {};
    ASSERT_TRUE(Gizmo::ResolveCameraHandle(start.Frame, start.Constraint, handle));
    ASSERT_TRUE(MovePointerToWorld(handle.Position + handle.Direction * float(handle.Factor)));
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.CameraActive());
    const auto replacement = *scene.GetComponent<CameraComponent>(entity);
    ASSERT_NEAR(replacement.Far, 5.f, .001f);
    ReplaceDuringPreview(replacement);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, replacement.Far);
    ASSERT_TRUE(BeginPhysicalCamera(start));
    CancelFreshGesture();
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, replacement.Far);
}

INSTANTIATE_TEST_SUITE_P(HeldAndReleased, EditorComponentReplacementInputTest, testing::Values(false, true));

TEST_F(EditorGizmoIntegrationTest, QueuedSelectionCancelsBeforeMatchingReleaseAndRetiresTheOldPhysicalOwner)
{
    ASSERT_TRUE(BeginPhysicalTranslation());
    ZUIFeedMousePos(&ctx, ctx.MousePos[0] + 30.f, ctx.MousePos[1]);
    editor->Update(1.f / 60.f);
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    ASSERT_TRUE(editor->Session.RequestSceneSelection(SceneTarget::Grid));
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorGizmoIntegrationTest, QueuedReparentCancelsBeforeMatchingReleaseAndRetiresTheOldPhysicalOwner)
{
    const auto parent = scene.CreateEntity();
    scene.AddComponent<UUIDComponent>(parent, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45294").value()});
    scene.AddComponent<TransformComponent>(
        parent,
        {
        .Position = {2.f, 3.f, 4.f}
    });
    ASSERT_TRUE(BeginPhysicalTranslation());
    ZUIFeedMousePos(&ctx, ctx.MousePos[0] + 30.f, ctx.MousePos[1]);
    editor->Update(1.f / 60.f);
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    ASSERT_TRUE(editor->Session.RequestReparent(entity, parent));
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    ASSERT_NE(scene.GetComponent<ParentComponent>(entity), nullptr);
    EXPECT_EQ(scene.GetComponent<ParentComponent>(entity)->Parent, parent);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorGizmoIntegrationTest, RetainedHandleCapturesBeforeCameraUpdateWithNoOverlaySlot)
{
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    ctx.AltDown         = true;
    for (uint32_t update = 0; update < 10; ++update)
    {
        ZEngine::Applications::OverlayPayload* overlay = nullptr;
        uint32_t                               slot    = 0;
        ASSERT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
        editor->Update(1.f / 60.f);
        EXPECT_EQ(editor->GizmoViewport.Hovered(), Gizmo::Handle::TranslateView);
        EXPECT_TRUE(camera.Used.Pointer);
        EXPECT_FALSE(camera.Used.Keyboard);
    }
    EXPECT_EQ(camera.Updates, 10u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 100.f, 100.f);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
}

TEST_F(EditorGizmoIntegrationTest, ProductionPressAndReleaseBetweenUpdatesCommitsOnlyTheLatchedReleaseOnce)
{
    using namespace ZEngine::Core::Maths;
    const auto                camera_frame = camera.Camera.CaptureFrameData();
    Geometry::ProjectionFrame projection   = {};
    ASSERT_TRUE(Geometry::MakeProjectionFrame(camera_frame.View, camera_frame.Projection, camera_frame.Position, Geometry::ProjectionKind::Perspective, false, &projection));
    Geometry::ProjectedPoint point = {};
    ASSERT_TRUE(
        Geometry::Project(
            {
            5.5f, 0.f, 0.f
    },
            projection,
            {{37.f, 61.f}, {800.f, 600.f}},
            &point));
    ZUIFeedMouseButton(&ctx, 0, true);
    ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    const auto ui_frame = ctx.FrameIndex;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.5f, .0002f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    const auto arena_bytes = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 100; ++i)
    {
        editor->Update(1.f / 60.f);
    }
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_EQ(memory.MainArena.m_current_offset, arena_bytes);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    editor->Session.EndMutationPhase();
}

TEST_F(EditorGizmoIntegrationTest, ProductionStartsRespectTextPopupAndDebugOwnershipAndNeverReplayARejectedPress)
{
    for (uint32_t owner = 0; owner < 3; ++owner)
    {
        SCOPED_TRACE(owner);
        ctx.TextInputActive      = owner == 0;
        ctx.PendingPopupKey      = owner == 1 ? 123 : 0;
        editor->EnableGizmoDebug = owner == 2;
        ZUIFeedMouseButton(&ctx, 0, true);
        editor->Update(1.f / 60.f);
        EXPECT_FALSE(editor->Session.Active());
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
        ctx.TextInputActive      = false;
        ctx.PendingPopupKey      = 0;
        editor->EnableGizmoDebug = false;
        if (owner == 2)
        {
            BindUI();
        }
        editor->Update(1.f / 60.f);
        EXPECT_FALSE(editor->Session.Active());
        ZUIFeedMouseButton(&ctx, 0, false);
        editor->Update(1.f / 60.f);
    }
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorGizmoIntegrationTest, ProductionBatchTranslationPrunesSelectedDescendantsAndCommitsWholeSetOnce)
{
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    editor->Session.InvalidateForExternalMutation();
    const auto child = scene.CreateEntity();
    const auto peer  = scene.CreateEntity();
    scene.AddComponent<UUIDComponent>(child, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45294").value()});
    scene.AddComponent<UUIDComponent>(peer, {uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45295").value()});
    scene.AddComponent<TransformComponent>(
        child,
        {
        .Position = {1.f, 0.f, 0.f}
    });
    scene.AddComponent<TransformComponent>(
        peer,
        {
        .Position = {2.f, 0.f, 0.f}
    });
    ASSERT_TRUE(scene.SetParent(child, entity));
    const EntityTarget targets[] = {editor->Session.TargetFor(entity), editor->Session.TargetFor(child), editor->Session.TargetFor(peer)};
    ASSERT_TRUE(editor->Session.SetSelection(targets, targets[1].UUID));
    ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Translation, .Enabled = true}));
    ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapIncrement, .Target = Gizmo::SnapTarget::Translation, .Increment = .1f}));
    ASSERT_TRUE(editor->Session.ProcessRequests());
    ASSERT_TRUE(editor->Session.MarkSaved());
    editor->Session.EndMutationPhase();

    ASSERT_TRUE(MovePointerToWorld({6.f, 0.f, 0.f}));
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    EXPECT_EQ(editor->Session.TransformCount(), 2u);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ASSERT_TRUE(MovePointerToWorld({6.27f, .08f, 0.f}));
    editor->Update(1.f / 60.f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.3f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.x, 2.3f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.y, .1f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.y, .1f, .0002f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->Position.x, 1.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    ASSERT_TRUE(MovePointerToWorld({6.42f, .17f, 0.f}));
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    ASSERT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.4f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.x, 2.4f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.y, .2f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.y, .2f, .0002f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->Position.x, 1.f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 6.4f, .0002f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(editor->Session.Selection().ActiveUUID(), targets[1].UUID);

    ASSERT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(peer)->Position.x, 2.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 6.f);
    EXPECT_FALSE(editor->Session.Dirty());
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Redo));
    editor->Update(1.f / 60.f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.4f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.x, 2.4f, .0002f);
    EXPECT_TRUE(editor->Session.Dirty());
    ASSERT_TRUE(MovePointerToWorld({6.4f, .2f, 0.f}));
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    ASSERT_TRUE(MovePointerToWorld({7.f, .6f, 0.f}));
    editor->Update(1.f / 60.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.4f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(peer)->Position.x, 2.4f, .0002f);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 6.4f, .0002f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(camera.Used.Keyboard);
}

namespace
{
    class EditorGridGizmoIntegrationTest : public EditorGizmoIntegrationTest
    {
    protected:
        EditorScene                             owner  = {};
        std::unique_ptr<ZEngine::EngineContext> engine = {};

        void                                    SetUp() override
        {
            EditorGizmoIntegrationTest::SetUp();
            ASSERT_FALSE(HasFatalFailure());
            engine                         = std::make_unique<ZEngine::EngineContext>();
            engine->Scene                  = &scene;
            owner.UUID                     = uuids::uuid::from_string("780d9696-105b-4fe1-86cd-8435bdf45296").value();
            owner.AuthoredGrid.Origin      = {5.f, 0.f, 0.f};
            owner.AuthoredGrid.Orientation = {0.f, 0.f, float(std::sin(.35)), float(std::cos(.35))};
            ASSERT_TRUE(CanonicalizeSceneGridSettings(owner.AuthoredGrid));
            owner.SyncGridFromAuthored();
            editor->CurrentScene = &owner;
            ASSERT_TRUE(editor->Session.BeginMutationPhase());
            ASSERT_TRUE(owner.AttachSession(editor->Session, *engine));
            editor->Session.InvalidateForExternalMutation(false);
            ASSERT_TRUE(editor->Session.SelectScene(SceneTarget::Grid));
            ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapEnabled, .Target = Gizmo::SnapTarget::Translation, .Enabled = true}));
            ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::SnapIncrement, .Target = Gizmo::SnapTarget::Translation, .Increment = .1f}));
            ASSERT_TRUE(editor->Session.ProcessRequests());
            ASSERT_TRUE(editor->Session.MarkSaved());
            editor->Session.EndMutationPhase();
        }

        void TearDown() override
        {
            editor->CurrentScene = nullptr;
            EditorGizmoIntegrationTest::TearDown();
        }

        void SameGrid(const SceneGridSettings& expected)
        {
            EXPECT_TRUE(EqualSceneGridSettings(owner.AuthoredGrid, expected));
            SceneGridSettings rendered = {};
            ASSERT_TRUE(ZEngine::Rendering::Scenes::ReadSceneGridRenderSnapshot(owner.Grid, rendered));
            EXPECT_TRUE(EqualSceneGridSettings(rendered, expected));
            EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
            EXPECT_EQ(editor->Session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
            EXPECT_TRUE(editor->Session.Selection().Entities().empty());
        }
    };
} // namespace

TEST_F(EditorGridGizmoIntegrationTest, ProductionGridTranslationUsesItsTiltedUAxisAndNeverMutatesEntitiesOrAllowsScale)
{
    const auto                        before = owner.AuthoredGrid;
    const auto                        matrix = ZEngine::Core::Maths::quaternionToMat4(before.Orientation);
    const ZEngine::Core::Maths::Vec3f axis   = {matrix(0, 0), matrix(1, 0), matrix(2, 0)};
    ASSERT_TRUE(MovePointerToWorld(before.Origin + axis * .7f));
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    EXPECT_EQ(editor->Session.TransformCount(), 0u);
    ASSERT_TRUE(MovePointerToWorld(before.Origin + axis * 1.07f));
    editor->Update(1.f / 60.f);
    const float start_coordinate = ZEngine::Core::Maths::dot(axis, before.Origin);
    const float snapped_delta    = std::round((start_coordinate + .37f) / .1f) * .1f - start_coordinate;
    auto        after            = before;
    after.Origin                 = before.Origin + axis * snapped_delta;
    for (uint32_t component = 0; component < 3; ++component)
    {
        EXPECT_NEAR(owner.AuthoredGrid.Origin[component], after.Origin[component], .0002f);
    }
    after = owner.AuthoredGrid;
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    ASSERT_FALSE(editor->Session.Active());
    SameGrid(after);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FALSE(camera.Used.Keyboard);
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->Update(1.f / 60.f);
    SameGrid(before);
    EXPECT_FALSE(editor->Session.Dirty());
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Redo));
    editor->Update(1.f / 60.f);
    SameGrid(after);
    EXPECT_TRUE(editor->Session.Dirty());
    ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Scale}));
    editor->Update(1.f / 60.f);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Translate);
    EXPECT_FALSE(editor->Session.Active());
    SameGrid(after);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorGridGizmoIntegrationTest, ProductionGridSelectionRetiresEntityScaleHitsAndNeverReplaysRejectedPresses)
{
    const auto before = owner.AuthoredGrid;
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Select(entity));
    ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Scale}));
    ASSERT_TRUE(editor->Session.ProcessRequests());
    editor->Session.EndMutationPhase();
    editor->Update(1.f / 60.f);
    ASSERT_FALSE(editor->GizmoViewport.Hits().View().empty());
    ASSERT_TRUE(MovePointerToWorld(before.Origin));
    ASSERT_TRUE(editor->Session.RequestSceneSelection(SceneTarget::Grid));
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    EXPECT_EQ(editor->Session.GizmoSettings().Mode, Gizmo::Operation::Scale);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_TRUE(editor->GizmoViewport.Hits().View().empty());
    SameGrid(before);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = Gizmo::Operation::Translate}));
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active()); // A rejected held Scale press cannot become a Translate press.
    SameGrid(before);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    SameGrid(before);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_FALSE(editor->Session.Dirty());
}

TEST_F(EditorGridGizmoIntegrationTest, ProductionGridViewRotationCommitsOnceAndEscapePreservesExactGridHistory)
{
    const auto                  before = owner.AuthoredGrid;
    Gizmo::RotationSnapshot     start  = {};
    ZEngine::Core::Maths::Vec3f grab   = {};
    ASSERT_TRUE(BeginPhysicalViewRotation(start, grab));
    ASSERT_EQ(start.Scene.Kind, SceneTarget::Grid);
    EXPECT_FLOAT_EQ(start.Axis.z, 1.f);
    ASSERT_TRUE(MovePhysicalRotation(start, grab, .5));
    editor->Update(1.f / 60.f);
    EXPECT_NEAR(owner.AuthoredGrid.Orientation.z, std::sin(.6), .0002);
    EXPECT_NEAR(owner.AuthoredGrid.Orientation.w, std::cos(.6), .0002);
    const auto after = owner.AuthoredGrid;
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    ASSERT_FALSE(editor->Session.Active());
    SameGrid(after);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Undo));
    editor->Update(1.f / 60.f);
    SameGrid(before);
    EXPECT_FALSE(editor->Session.Dirty());
    ASSERT_TRUE(BeginPhysicalViewRotation(start, grab));
    ASSERT_TRUE(MovePhysicalRotation(start, grab, -.7));
    editor->Update(1.f / 60.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    SameGrid(before);
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
    EXPECT_EQ(editor->Session.History().RedoCount(), 1u);
    EXPECT_FALSE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(editor->Session.Request(EditorCommand::Redo));
    editor->Update(1.f / 60.f);
    SameGrid(after);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
}

TEST_F(EditorGizmoIntegrationTest, PrepareSceneBuildsFromItsCapturedCameraAndFinalTargetExtent)
{
    camera.Camera.SetPosition({6.f, 1.f, 20.f}); // different from the retained hit frame
    ASSERT_TRUE(state.RenderTargetResizeRequests.push({800, 600}));
    ASSERT_TRUE(state.RenderTargetResizeRequests.push({1600, 1200}));
    auto frame = std::make_unique<ZEngine::Applications::RenderFrameState>();
    editor->PrepareScene(*frame);
    EXPECT_FLOAT_EQ(frame->Camera.Position.x, 6.f);
    EXPECT_FLOAT_EQ(frame->Camera.Position.y, 1.f);
    EXPECT_FLOAT_EQ(frame->Camera.Position.z, 20.f);
    EXPECT_EQ(frame->RenderTargetW, 1600u);
    EXPECT_EQ(frame->RenderTargetH, 1200u);
    EXPECT_EQ(frame->ResizeSequence, 2u);
    ASSERT_EQ(frame->Gizmo.SpriteCount, 1u);
    EXPECT_FLOAT_EQ(frame->Gizmo.Sprites[0].Position.x, 5.f);
    EXPECT_FLOAT_EQ(frame->Gizmo.Sprites[0].Size, 20.f);
    ZEngine::Core::Maths::Geometry::ProjectionFrame projection = {};
    ASSERT_TRUE(ZEngine::Core::Maths::Geometry::MakeProjectionFrame(frame->Camera.View, frame->Camera.Projection, frame->Camera.Position, ZEngine::Core::Maths::Geometry::ProjectionKind::Perspective, false, &projection));
    ZEngine::Core::Maths::Geometry::ProjectedPoint point = {};
    ASSERT_TRUE(
        ZEngine::Core::Maths::Geometry::Project(
            frame->Gizmo.Sprites[0].Position,
            projection,
            {
            { 37.f,  61.f},
            {800.f, 600.f}
    },
            &point));
    EXPECT_EQ(editor->GizmoViewport.Hits().Pick(point.Screen).Handle, uint32_t(Gizmo::Handle::TranslateView));
}

TEST_F(EditorGizmoIntegrationTest, MovingFlyAndOrbitCamerasPublishCoherentGizmosWithoutAnOverlayAndRetainOlderFrames)
{
    using namespace ZEngine::Core::Maths;
    using namespace ZEngine::Rendering::Cameras;
    using namespace ZEngine::Rendering::Renderers;
    using namespace ZEngine::Applications;
    const auto find_center = [](const GizmoFrameData& gizmo) -> const GizmoSprite* {
        for (uint32_t i = 0; i < gizmo.SpriteCount; ++i)
        {
            if (gizmo.Sprites[i].Position == Vec3f{5.f, 0.f, 0.f})
            {
                return &gizmo.Sprites[i];
            }
        }
        return nullptr;
    };
    camera.AdvanceCamera = true;
    pipeline->InitializeFrameStorage(&memory.MainArena, 1);
    pipeline->OverlayBuildAvailable.value.store(true);
    OverlayPayload* overlay = nullptr;
    uint32_t        slot    = 0;
    ASSERT_TRUE(pipeline->BeginOverlayWrite(overlay, slot));
    pipeline->PublishOverlay(slot);
    ASSERT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
    EditorScene owner = {};
    owner.Instances.init(&memory.MainArena, 1);
    const auto mesh = scene.GetComponent<UUIDComponent>(entity)->Value;
    scene.AddComponent<MeshComponent>(entity, {.MeshUUID = mesh, .RenderInstanceId = owner.AddMeshInstance(mesh, "Target")});
    auto            frame            = std::make_unique<RenderFrameState>();
    auto            retained         = std::make_unique<RenderFrameState>();
    CameraFrameData retained_camera  = {};
    GizmoSprite     retained_center  = {};
    GizmoLine       retained_line    = {};
    uint32_t        retained_lines   = 0;
    uint32_t        retained_sprites = 0;
    bool            reading          = false;
    const auto      ui_frame         = ctx.FrameIndex;
    const auto      scene_bytes      = memory.MainArena.m_current_offset;
    const auto      ui_bytes         = ctx.FrameArena.m_current_offset;
    const auto      undo             = editor->Session.History().UndoCount();
    const auto      redo             = editor->Session.History().RedoCount();
    const auto      authored         = *scene.GetComponent<TransformComponent>(entity);
    uint32_t        changed_views    = 0;
    ZUIFeedMousePos(&ctx, 100.f, 100.f); // Navigation starts away from every handle.
    for (const auto kind : {CameraType::PERSPECTIVE, CameraType::ORTHOGRAPHIC})
    {
        for (const auto mode : {Gizmo::Operation::Translate, Gizmo::Operation::Rotate, Gizmo::Operation::Scale})
        {
            for (bool orbit : {false, true})
            {
                SCOPED_TRACE(testing::Message() << "projection " << uint32_t(kind) << " mode " << uint32_t(mode) << " orbit " << orbit);
                camera.Camera.StopMotion();
                camera.Camera.SetProjectionType(kind);
                camera.Camera.SetPosition({5.f, 0.f, 10.f});
                camera.Camera.SetOrientation(0.f, 0.f);
                ASSERT_TRUE(editor->Session.BeginMutationPhase());
                ASSERT_TRUE(editor->Session.RequestGizmoChange({.Kind = Gizmo::ChangeKind::Operation, .Mode = mode}));
                ASSERT_TRUE(editor->Session.ProcessRequests());
                editor->Session.EndMutationPhase();
                ctx.AltDown = orbit;
                ZUIFeedMouseButton(&ctx, 0, orbit);
                ZUIFeedMouseButton(&ctx, 1, !orbit);
                ASSERT_TRUE(state.RenderTargetResizeRequests.push({1600, 1200}));
                for (uint32_t step = 0; step < 64; ++step)
                {
                    SCOPED_TRACE(step);
                    const auto before   = camera.Camera.CaptureFrameData();
                    camera.Camera.Input = {.RightDown = !orbit, .LeftDown = orbit, .AltDown = orbit, .ShiftDown = true, .MouseDeltaX = step % 16 < 8 ? 24.f : -24.f, .MouseDeltaY = step % 8 < 4 ? 8.f : -8.f};
                    if (!orbit)
                    {
                        camera.Camera.Input.Keys['W']                       = true;
                        camera.Camera.Input.Keys[step % 16 < 8 ? 'A' : 'D'] = true;
                        // Event-time shortcut gating must not change the mode
                        // or queue a key to replay when presentation resumes.
                        for (const auto key : {ZENGINE_KEY_Q, ZENGINE_KEY_W, ZENGINE_KEY_E})
                        {
                            ZEngine::Windows::Events::KeyPressedEvent event{key, 0};
                            editor->ProcessEvent(event);
                        }
                    }
                    editor->Update(1.f / 120.f);
                    ASSERT_FALSE(camera.Used.Pointer);
                    ASSERT_FALSE(camera.Used.Keyboard);
                    ASSERT_FALSE(editor->Session.Active());
                    ASSERT_EQ(editor->Session.GizmoSettings().Mode, mode);
                    const auto current = camera.Camera.CaptureFrameData();
                    bool       changed = false;
                    for (uint32_t row = 0; row < 4; ++row)
                    {
                        for (uint32_t col = 0; col < 4; ++col)
                        {
                            changed |= current.View(row, col) != before.View(row, col);
                        }
                    }
                    changed_views += changed;
                    editor->PrepareScene(*frame);
                    EXPECT_EQ(frame->RenderTargetW, 1600u);
                    EXPECT_EQ(frame->RenderTargetH, 1200u);
                    for (uint32_t row = 0; row < 4; ++row)
                    {
                        for (uint32_t col = 0; col < 4; ++col)
                        {
                            EXPECT_FLOAT_EQ(frame->Camera.View(row, col), current.View(row, col));
                            EXPECT_FLOAT_EQ(frame->Camera.Projection(row, col), current.Projection(row, col));
                            if (reading)
                            {
                                EXPECT_FLOAT_EQ(retained->Camera.View(row, col), retained_camera.View(row, col));
                                EXPECT_FLOAT_EQ(retained->Camera.Projection(row, col), retained_camera.Projection(row, col));
                            }
                        }
                    }
                    ASSERT_TRUE(frame->Gizmo.ValidCounts());
                    const auto* center = find_center(frame->Gizmo);
                    ASSERT_NE(center, nullptr);
                    EXPECT_FLOAT_EQ(center->Size, (mode == Gizmo::Operation::Scale ? Gizmo::Constants::ScaleCubeSizePx : Gizmo::Constants::CenterSizePx) * 2.f);
                    Geometry::ProjectionFrame projection = {};
                    ASSERT_TRUE(Geometry::MakeProjectionFrame(current.View, current.Projection, current.Position, kind == CameraType::ORTHOGRAPHIC ? Geometry::ProjectionKind::Orthographic : Geometry::ProjectionKind::Perspective, false, &projection));
                    Geometry::ProjectedPoint pivot = {};
                    ASSERT_TRUE(
                        Geometry::Project(
                            center->Position,
                            projection,
                            {
                            {rect[0], rect[1]},
                            {  800.f,   600.f}
                    },
                            &pivot));
                    bool matched = false;
                    for (const auto& shape : editor->GizmoViewport.Hits().View())
                    {
                        if (mode == Gizmo::Operation::Rotate && !orbit)
                        {
                            if (shape.Handle != uint32_t(Gizmo::Handle::RotateView))
                            {
                                continue;
                            }
                            ASSERT_NEAR(std::hypot(double(shape.Points[0].x) - pivot.Screen.x, double(shape.Points[0].y) - pivot.Screen.y), Gizmo::Constants::ViewRingRadiusPx, .003);
                        }
                        else
                        {
                            if (shape.Priority != Gizmo::HitPriority::Center)
                            {
                                continue;
                            }
                            ASSERT_NEAR((shape.Points[0].x + shape.Points[1].x) * .5f, pivot.Screen.x, .003f);
                            ASSERT_NEAR((shape.Points[0].y + shape.Points[1].y) * .5f, pivot.Screen.y, .003f);
                        }
                        matched = true;
                    }
                    ASSERT_TRUE(matched);
                    scene.SynchronizeHierarchy();
                    ZEngine::ECS::Systems::SyncECSToRenderScene(scene, 1.f, owner);
                    ASSERT_TRUE(pipeline->PublishFrameState(*frame, &owner));
                    if (reading)
                    {
                        // Producer updates cannot overwrite the consumer's
                        // retained draw storage or sampled gizmo primitives.
                        ASSERT_EQ(retained->Scene.InstanceCount, 1u);
                        EXPECT_EQ(retained->Scene.Instances[0].MeshUUID, mesh);
                        EXPECT_FLOAT_EQ(retained->Scene.Instances[0].Transform(0, 3), 5.f);
                        ASSERT_EQ(retained->Gizmo.LineCount, retained_lines);
                        ASSERT_EQ(retained->Gizmo.SpriteCount, retained_sprites);
                        const auto* held_center = find_center(retained->Gizmo);
                        ASSERT_NE(held_center, nullptr);
                        EXPECT_FLOAT_EQ(held_center->Size, retained_center.Size);
                        EXPECT_EQ(held_center->Fill, retained_center.Fill);
                        EXPECT_EQ(retained->Gizmo.Lines[0].A, retained_line.A);
                        EXPECT_EQ(retained->Gizmo.Lines[0].B, retained_line.B);
                        EXPECT_FLOAT_EQ(retained->Gizmo.Lines[0].Width, retained_line.Width);
                    }
                    if (step % 8 == 0)
                    {
                        ASSERT_TRUE(pipeline->TryReadFrameState(*retained));
                        retained_camera = retained->Camera;
                        reading         = true;
                        ASSERT_EQ(retained->Scene.InstanceCount, 1u);
                        EXPECT_FLOAT_EQ(retained->Scene.Instances[0].Transform(0, 3), 5.f);
                        ASSERT_TRUE(retained->Gizmo.ValidCounts());
                        const auto* held_center = find_center(retained->Gizmo);
                        ASSERT_NE(held_center, nullptr);
                        retained_center  = *held_center;
                        retained_lines   = retained->Gizmo.LineCount;
                        retained_sprites = retained->Gizmo.SpriteCount;
                        ASSERT_GT(retained_lines, 0u);
                        retained_line = retained->Gizmo.Lines[0];
                    }
                    ASSERT_FALSE(pipeline->BeginOverlayWrite(overlay, slot));
                    ZUIFeedBeginFrame(&ctx);
                }
                ZUIFeedMouseButton(&ctx, 0, false);
                ZUIFeedMouseButton(&ctx, 1, false);
                ctx.AltDown         = false;
                camera.Camera.Input = {};
                editor->Update(1.f / 120.f);
                EXPECT_EQ(editor->Session.GizmoSettings().Mode, mode);
                EXPECT_FALSE(editor->Session.Active());
            }
        }
    }
    pipeline->ReleaseFrameState();
    EXPECT_EQ(changed_views, 768u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(memory.MainArena.m_current_offset, scene_bytes);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
    EXPECT_EQ(editor->Session.History().UndoCount(), undo);
    EXPECT_EQ(editor->Session.History().RedoCount(), redo);
    const auto& final = *scene.GetComponent<TransformComponent>(entity);
    EXPECT_EQ(final.Position, authored.Position);
    EXPECT_EQ(final.Rotation, authored.Rotation);
    EXPECT_EQ(final.Scale, authored.Scale);
    EXPECT_EQ(final.PreviousPosition, authored.PreviousPosition);
}

TEST_F(EditorGizmoIntegrationTest, GridLabelsCaptureOnlyTheExistingBodyGlyphValuesAndAtlasGeneration)
{
    using namespace ZEngine::Rendering::Renderers;
    ZUIGlyph glyphs[2] = {};
    glyphs[0]          = {.U0 = .1f, .V0 = .2f, .U1 = .15f, .V1 = .3f, .Width = 14.f, .Height = 22.f};
    glyphs[1]          = {.U0 = .4f, .V0 = .5f, .U1 = .45f, .V1 = .6f, .Width = 16.f, .Height = 20.f};
    ZUIFont      font  = {.Glyphs = glyphs, .GlyphCount = 2, .FirstCodepoint = 'U', .FontScale = .5f};
    ZUIFontAtlas atlas = {
        .Body = &font, .Handle = {71, 9}
    };
    ctx.Atlas                          = &atlas;
    editor->GizmoLogicalViewportHeight = 600.f;
    ASSERT_TRUE(state.RenderTargetResizeRequests.push({1600, 1200}));
    auto frame = std::make_unique<ZEngine::Applications::RenderFrameState>();
    editor->PrepareScene(*frame);
    EXPECT_EQ(frame->GridLabelFont.Atlas.Index, 71u);
    EXPECT_EQ(frame->GridLabelFont.Atlas.Generation, 9u);
    EXPECT_EQ(frame->GridLabelFont.LogicalViewportHeight, 600.f);
    EXPECT_EQ(frame->GridLabelFont.Glyphs[0].Size.x, 7.f);
    EXPECT_EQ(frame->GridLabelFont.Glyphs[0].Size.y, 11.f);
    EXPECT_EQ(frame->GridLabelFont.Glyphs[1].Size.x, 8.f);
    EXPECT_EQ(frame->GridLabelFont.Glyphs[1].Size.y, 10.f);
    EXPECT_EQ(frame->GridLabelFont.Glyphs[0].UV, (ZEngine::Core::Containers::FixedArray<float, 4>{.1f, .2f, .15f, .3f}));
    // Retained drawing cannot dereference the source glyphs/font/UI context.
    glyphs[0] = {};
    font      = {};
    atlas     = {};
    ctx.Atlas = nullptr;
    EXPECT_EQ(frame->GridLabelFont.Glyphs[0].Size.x, 7.f);
    EXPECT_EQ(frame->GridLabelFont.Atlas.Generation, 9u);
    ZEngine::Rendering::Scenes::SceneGridRenderSnapshot grid = {};
    grid.Origin                                              = {5.f, 0.f, 0.f};
    grid.Orientation                                         = {std::sqrt(.5f), 0.f, 0.f, std::sqrt(.5f)};
    grid.Labels                                              = 1;
    GridLabelFrameData labels                                = {};
    ASSERT_TRUE(BuildGridLabelFrame(grid, frame->Camera, frame->GridLabelFont, frame->RenderTargetW, frame->RenderTargetH, labels));
    ASSERT_EQ(labels.Count, 2u);
    EXPECT_NEAR(labels.Push.Rects[0][2] * 800.f, 14.f, .0001f);
    editor->PrepareScene(*frame);
    EXPECT_FALSE(frame->GridLabelFont.Atlas.Valid());
}

TEST_F(EditorGizmoIntegrationTest, MissingOrIncompleteGridLabelFontRetiresEarlierCapture)
{
    ZUIGlyph     glyphs[2] = {};
    ZUIFont      font      = {.Glyphs = glyphs, .GlyphCount = 2, .FirstCodepoint = 'U'};
    ZUIFontAtlas atlas     = {
        .Body = &font, .Handle = {71, 9}
    };
    auto frame = std::make_unique<ZEngine::Applications::RenderFrameState>();
    for (uint32_t mode = 0; mode < 6; ++mode)
    {
        ctx.Atlas           = &atlas;
        atlas.Body          = &font;
        atlas.Handle        = {71, 9};
        font.Glyphs         = glyphs;
        font.GlyphCount     = 2;
        font.FirstCodepoint = 'U';
        editor->PrepareScene(*frame);
        ASSERT_TRUE(frame->GridLabelFont.Atlas.Valid());
        if (mode == 0)
        {
            ctx.Atlas = nullptr;
        }
        if (mode == 1)
        {
            atlas.Body = nullptr;
        }
        if (mode == 2)
        {
            atlas.Handle = {};
        }
        if (mode == 3)
        {
            font.Glyphs = nullptr;
        }
        if (mode == 4)
        {
            font.GlyphCount = 1; // U only: never read V out of bounds
        }
        if (mode == 5)
        {
            font.FirstCodepoint = 'W'; // subtraction must not underflow
        }
        editor->PrepareScene(*frame);
        EXPECT_FALSE(frame->GridLabelFont.Atlas.Valid()) << mode;
        EXPECT_EQ(frame->GridLabelFont.Glyphs[0].Size.x, 0.f);
        EXPECT_EQ(frame->GridLabelFont.Glyphs[1].UV[3], 0.f);
    }
    ctx.Atlas = nullptr;
}

TEST_F(EditorGizmoIntegrationTest, LegacyRawTrsWriteIsSynchronizedBeforeHoverAndRender)
{
    scene.GetComponent<TransformComponent>(entity)->Position.x = 0.f;
    camera.Camera.SetPosition({0.f, 0.f, 10.f});
    editor->Update(1.f / 60.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 0.f);
    EXPECT_TRUE(camera.Used.Pointer);
    ASSERT_TRUE(state.RenderTargetResizeRequests.push({800, 600}));
    auto frame = std::make_unique<ZEngine::Applications::RenderFrameState>();
    editor->PrepareScene(*frame);
    ASSERT_EQ(frame->Gizmo.SpriteCount, 1u);
    EXPECT_FLOAT_EQ(frame->Gizmo.Sprites[0].Position.x, 0.f);
}

TEST_F(EditorGizmoIntegrationTest, ResizeAndMinimizeRetireLayoutUntilItIsRebound)
{
    ZEngine::Windows::Events::WindowResizedEvent   resize{1280, 720};
    ZEngine::Windows::Events::WindowMinimizedEvent minimize{};
    for (auto* event : {static_cast<ZEngine::Core::CoreEvent*>(&resize), static_cast<ZEngine::Core::CoreEvent*>(&minimize)})
    {
        editor->Update(1.f / 60.f);
        ASSERT_TRUE(camera.Used.Pointer);
        editor->ProcessEvent(*event);
        EXPECT_FALSE(editor->GizmoViewport.Bound(&ctx));
        editor->Update(1.f / 60.f);
        EXPECT_FALSE(camera.Used.Pointer);
        EXPECT_EQ(editor->GizmoViewport.Hits().Count, 0u);
        BindUI();
    }
}

TEST_F(EditorGizmoIntegrationTest, OwnedPhysicalDragUpdatesAndReleasesBeforeCameraWithOverlayBackpressure)
{
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    using namespace ZEngine::Core::Maths;
    const auto                captured   = camera.Camera.CaptureFrameData();
    Geometry::ProjectionFrame projection = {};
    ASSERT_TRUE(Geometry::MakeProjectionFrame(captured.View, captured.Projection, captured.Position, Geometry::ProjectionKind::Perspective, false, &projection));
    Geometry::ProjectedPoint point = {};
    ASSERT_TRUE(
        Geometry::Project(
            {
            5.5f, 0.f, 0.f
    },
            projection,
            {{37.f, 61.f}, {800.f, 600.f}},
            &point));
    ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.5f, .0002f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_NEAR(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.5f, .0002f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorGizmoIntegrationTest, PivotInputUpdatesAndDurablyReleasesBeforeCameraDespiteOverlayBackpressure)
{
    ASSERT_TRUE(BeginPhysicalPivot());
    const auto ui_frame = ctx.FrameIndex;
    const auto ui_bytes = ctx.FrameArena.m_current_offset;
    const auto revision = editor->Session.DocumentRevision();
    ZUIFeedMousePos(&ctx, 467.f, 331.f);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.PivotActive());
    ZEngine::Core::Maths::Vec3f point = {};
    ASSERT_TRUE(editor->Session.ReadCustomPivot(point));
    EXPECT_GT(point.x, 5.f);
    EXPECT_GT(point.y, 0.f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    ZEngine::Core::Maths::Vec3f committed = {};
    ASSERT_TRUE(editor->Session.ReadCustomPivot(committed));
    EXPECT_EQ(committed, point);
    EXPECT_EQ(editor->Session.DocumentRevision(), revision);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, ui_bytes);
}

TEST_F(EditorGizmoIntegrationTest, EscapeCancelsPivotDragThenClearsSavedPivotBeforeSelection)
{
    ASSERT_TRUE(BeginPhysicalPivot());
    ZUIFeedMousePos(&ctx, 467.f, 331.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    ZEngine::Core::Maths::Vec3f saved = {};
    ASSERT_TRUE(editor->Session.ReadCustomPivot(saved));
    ASSERT_TRUE(BeginPhysicalPivot());
    ZUIFeedMousePos(&ctx, 497.f, 301.f);
    editor->Update(1.f / 60.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    ZEngine::Core::Maths::Vec3f restored = {};
    ASSERT_TRUE(editor->Session.ReadCustomPivot(restored));
    EXPECT_EQ(restored, saved);
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 1u);
    EXPECT_FALSE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.ReadCustomPivot(restored));
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 1u);
    // With no pivot/interaction, Escape is left for normal selection handling.
    ctx.EscapePressed = true;
    editor->UpdateSessionInput(&ctx, true);
    EXPECT_TRUE(ctx.EscapePressed);
}

TEST_F(EditorGizmoIntegrationTest, TextPopupAndFocusOwnershipCannotClearSavedPivotWithEscape)
{
    ASSERT_TRUE(BeginPhysicalPivot());
    ZUIFeedMousePos(&ctx, 467.f, 331.f);
    ZUIFeedMouseButton(&ctx, 0, false);
    editor->Update(1.f / 60.f);
    ZEngine::Core::Maths::Vec3f saved = {};
    ASSERT_TRUE(editor->Session.ReadCustomPivot(saved));
    for (uint32_t blocker = 0; blocker < 3; ++blocker)
    {
        ctx.TextInputActive = blocker == 0;
        ctx.PendingPopupKey = blocker == 1 ? 123 : 0;
        ctx.EscapePressed   = true;
        editor->UpdateSessionInput(&ctx, blocker != 2);
        ZEngine::Core::Maths::Vec3f actual = {};
        ASSERT_TRUE(editor->Session.ReadCustomPivot(actual));
        EXPECT_EQ(actual, saved);
    }
}

TEST_F(EditorGizmoIntegrationTest, EditorEscapeCancelsOwnedPhysicalDragBeforeCameraWithoutAnOverlay)
{
    ZUIFeedMouseButton(&ctx, 0, true);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.TranslationActive());
    ZUIFeedMousePos(&ctx, 467.f, 361.f);
    editor->Update(1.f / 60.f);
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Keyboard);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
}

TEST_F(EditorGizmoIntegrationTest, OwnedRotationUpdatesAndReleasesBeforeCameraWithOverlayBackpressure)
{
    Gizmo::RotationSnapshot     start = {}, preview = {};
    ZEngine::Core::Maths::Vec3f grab = {};
    ASSERT_TRUE(BeginPhysicalViewRotation(start, grab));
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    ASSERT_TRUE(MovePhysicalRotation(start, grab, .4));
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.ReadRotationSnapshot(preview));
    EXPECT_NEAR(preview.Angle, .4, .0001);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ASSERT_TRUE(MovePhysicalRotation(start, grab, .7));
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    const auto committed = scene.GetComponent<TransformComponent>(entity)->Rotation;
    // Authored Euler Z uses the opposite sign to right-handed world motion.
    EXPECT_NEAR(committed.z, float(-.7 * start.Axis.z), .0001f);
    scene.SynchronizeHierarchy();
    const auto world = scene.GetComponent<TransformComponent>(entity)->WorldTransform;
    EXPECT_NEAR(world(0, 0), float(std::cos(.7)), .0001f);
    EXPECT_NEAR(world(1, 0), float(std::sin(.7) * start.Axis.z), .0001f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.z, 0.f);
    ASSERT_TRUE(editor->Session.Redo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.z, committed.z);
    editor->Session.EndMutationPhase();
}

TEST_F(EditorGizmoIntegrationTest, OwnedLightReleaseAndEscapeRunBeforeCameraDespiteOverlayBackpressure)
{
    ASSERT_TRUE(BeginPhysicalLight());
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    const auto press_x  = ctx.MousePos[0];
    ZUIFeedMousePos(&ctx, press_x + 20.f, ctx.MousePos[1]);
    editor->Update(1.f / 60.f);
    ASSERT_TRUE(editor->Session.LightActive());
    EXPECT_GT(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
    EXPECT_FALSE(ctx.EscapePressed);
    EXPECT_FALSE(camera.Used.Keyboard);
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(BeginPhysicalLight());
    ZUIFeedMousePos(&ctx, press_x + 30.f, ctx.MousePos[1]);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_GT(scene.GetComponent<LightComponent>(entity)->Range, 2.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorGizmoIntegrationTest, OwnedCameraFarReleaseAndEscapeRunBeforeNavigationWithoutAnOverlaySlot)
{
    Gizmo::CameraSnapshot start = {};
    ASSERT_TRUE(BeginPhysicalCamera(start));
    Gizmo::CameraHandle handle = {};
    ASSERT_TRUE(Gizmo::ResolveCameraHandle(start.Frame, start.Constraint, handle));
    using namespace ZEngine::Core::Maths;
    const auto                camera_frame = camera.Camera.CaptureFrameData();
    Geometry::ProjectionFrame projection   = {};
    ASSERT_TRUE(Geometry::MakeProjectionFrame(camera_frame.View, camera_frame.Projection, camera_frame.Position, Geometry::ProjectionKind::Perspective, false, &projection));
    Geometry::ProjectedPoint point = {};
    ASSERT_TRUE(
        Geometry::Project(
            handle.Position + handle.Direction * float(handle.Factor),
            projection,
            {
            { 37.f,  61.f},
            {800.f, 600.f}
    },
            &point));
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
    editor->Update(1.f / 60.f);
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Far, 5.f, .001f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 4.f);
    EXPECT_FALSE(ctx.EscapePressed);
    ZUIFeedMouseButton(&ctx, 0, false);
    ASSERT_TRUE(BeginPhysicalCamera(start));
    ZUIFeedMousePos(&ctx, point.Screen.x, point.Screen.y);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_NEAR(scene.GetComponent<CameraComponent>(entity)->Far, 5.f, .001f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
}

TEST_F(EditorGizmoIntegrationTest, OwnedScaleUpdatesAndReleasesBeforeCameraWithOverlayBackpressure)
{
    ZEngine::Core::Maths::Vec2f pointer = {};
    ASSERT_TRUE(BeginPhysicalUniformScale(pointer));
    const auto ui_frame = ctx.FrameIndex;
    const auto bytes    = ctx.FrameArena.m_current_offset;
    MovePhysicalScale(pointer, 1.5f);
    editor->Update(1.f / 60.f);
    Gizmo::ScaleSnapshot preview = {};
    ASSERT_TRUE(editor->Session.ReadScaleSnapshot(preview));
    EXPECT_NEAR(preview.Ratio, 1.5f, .0001f);
    EXPECT_TRUE(camera.Used.Pointer);
    EXPECT_TRUE(camera.Used.Keyboard);
    ASSERT_TRUE(state.RenderTargetResizeRequests.push({1600, 1200}));
    auto frame = std::make_unique<ZEngine::Applications::RenderFrameState>();
    editor->PrepareScene(*frame);
    EXPECT_GT(frame->Gizmo.SpriteCount, 1u);
    MovePhysicalScale(pointer, 1.75f);
    ZUIFeedMouseButton(&ctx, 0, false);
    ZUIFeedMousePos(&ctx, 2000.f, 100.f);
    ZUIFeedBeginFrame(&ctx);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_EQ(editor->Session.History().UndoCount(), 2u);
    const auto committed = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    EXPECT_NEAR(committed.Scale.x, 1.75f, .0001f);
    EXPECT_FLOAT_EQ(committed.Position.x, 5.f);
    EXPECT_FALSE(camera.Used.Pointer);
    EXPECT_FALSE(camera.Used.Keyboard);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_EQ(ctx.FrameIndex, ui_frame);
    EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.f);
    ASSERT_TRUE(editor->Session.Redo());
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity)), committed));
    editor->Session.EndMutationPhase();
}

TEST_F(EditorGizmoIntegrationTest, EditorEscapeCancelsOwnedScaleBeforeCameraWithoutAnOverlay)
{
    ZEngine::Core::Maths::Vec2f pointer = {};
    ASSERT_TRUE(BeginPhysicalUniformScale(pointer));
    MovePhysicalScale(pointer, 1.5f);
    editor->Update(1.f / 60.f);
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Scale.x, 1.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    const auto& value = *scene.GetComponent<TransformComponent>(entity);
    EXPECT_FLOAT_EQ(value.Scale.x, 1.f);
    EXPECT_FLOAT_EQ(value.Scale.y, 1.f);
    EXPECT_FLOAT_EQ(value.Scale.z, 1.f);
    EXPECT_FLOAT_EQ(value.Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Keyboard);
}

TEST_F(EditorGizmoIntegrationTest, EditorEscapeCancelsOwnedRotationBeforeCameraWithoutAnOverlay)
{
    Gizmo::RotationSnapshot     start = {};
    ZEngine::Core::Maths::Vec3f grab  = {};
    ASSERT_TRUE(BeginPhysicalViewRotation(start, grab));
    ASSERT_TRUE(MovePhysicalRotation(start, grab, .4));
    editor->Update(1.f / 60.f);
    ASSERT_NE(scene.GetComponent<TransformComponent>(entity)->Rotation.z, 0.f);
    ctx.EscapePressed = true;
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.x, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.y, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Rotation.z, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_EQ(editor->Session.History().UndoCount(), 1u);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    EXPECT_FALSE(camera.Used.Keyboard);
    editor->Update(1.f / 60.f);
    EXPECT_FALSE(editor->Session.Active());
}
