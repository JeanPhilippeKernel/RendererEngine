#include <Tetragrama/EditorSession.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <gtest/gtest.h>
#include <array>
#include <cstdio>
#include <limits>
#include <string>
#include <thread>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using ZEngine::Core::Containers::FixedArray;

class EditorSessionTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory  = {};
    Scene                                scene   = {};
    ActorManager                         actors  = {};
    EditorSession                        session = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, scene);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
    }

    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
    }

    static uuids::uuid Identity(uint8_t value)
    {
        std::array<uint8_t, 16> bytes = {};
        bytes.back()                  = value;
        return uuids::uuid(bytes);
    }

    EntityID Entity(uint8_t value)
    {
        const auto entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {Identity(value)});
        scene.AddComponent<TransformComponent>(entity, {});
        return entity;
    }

    LocalTransformValue Value(EntityID entity)
    {
        return CaptureLocalTransform(*scene.GetComponent<TransformComponent>(entity));
    }

    bool Begin(EntityID entity)
    {
        const auto target = session.TargetFor(entity);
        return session.BeginTransform({&target, 1});
    }

    bool Preview(EntityID entity, float x)
    {
        auto value       = Value(entity);
        value.Position.x = x;
        return session.PreviewTransforms({&value, 1});
    }
};

TEST_F(EditorSessionTest, SameEntityTransformReplacementIsNotTheCapturedComponentEvenWithIdenticalValues)
{
    const auto         a         = Entity(1);
    const auto         b         = Entity(2);
    const EntityTarget targets[] = {session.TargetFor(a), session.TargetFor(b)};
    ASSERT_TRUE(session.BeginTransform(targets));
    LocalTransformValue values[] = {Value(a), Value(b)};
    values[0].Position.x         = 3.f;
    values[1].Position.x         = 4.f;
    ASSERT_TRUE(session.PreviewTransforms(values));
    auto replacement             = *scene.GetComponent<TransformComponent>(a);
    replacement.PreviousPosition = {17.f, 18.f, 19.f};
    scene.RemoveComponent<TransformComponent>(a);
    scene.AddComponent<TransformComponent>(a, replacement);
    session.Cancel();
    EXPECT_FALSE(session.Active());
    EXPECT_TRUE(SameLocalTransform(Value(a), values[0]));
    EXPECT_EQ(scene.GetComponent<TransformComponent>(a)->PreviousPosition, replacement.PreviousPosition);
    EXPECT_TRUE(SameLocalTransform(Value(b), LocalTransformValue{}));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, EveryTypedFieldReplacementRejectsRollbackPreviewAndCommitEvenWithIdenticalValues)
{
    struct FieldCase
    {
        AuthoredFloatField Field   = {};
        float              Preview = 0.f;
    };
    constexpr FieldCase cases[] = {
        {          AuthoredFloatField::LightRange,   12.f},
        {      AuthoredFloatField::LightSpotAngle,   50.f},
        {           AuthoredFloatField::CameraFar, 1200.f},
        {          AuthoredFloatField::CameraFovY,   70.f},
        {      AuthoredFloatField::LightIntensity,    2.f},
        {          AuthoredFloatField::CameraNear,    .2f},
        {   AuthoredFloatField::CameraAspectRatio,    2.f},
        {       AuthoredFloatField::RigidBodyMass,    2.f},
        {   AuthoredFloatField::RigidBodyFriction,    .7f},
        {AuthoredFloatField::RigidBodyRestitution,    .3f},
    };
    static_assert(std::size(cases) == static_cast<size_t>(AuthoredFloatField::Count));
    uint8_t identity = 0;
    for (const auto& item : cases)
    {
        for (uint32_t termination = 0; termination < 3; ++termination)
        {
            SCOPED_TRACE(static_cast<uint32_t>(item.Field));
            SCOPED_TRACE(termination);
            const auto entity = Entity(++identity);
            scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Spot});
            scene.AddComponent<CameraComponent>(entity, {});
            scene.AddComponent<RigidBodyComponent>(entity, {});
            ASSERT_TRUE(session.BeginField(session.TargetFor(entity), item.Field));
            ASSERT_TRUE(session.PreviewField(item.Preview));
            const auto lifetime = AuthoredFloatFieldLifetime(scene, entity, item.Field);
            const auto replace  = [&]<typename T>() {
                const auto value = *scene.GetComponent<T>(entity);
                scene.RemoveComponent<T>(entity);
                scene.AddComponent<T>(entity, value);
            };
            switch (item.Field)
            {
                case AuthoredFloatField::LightRange:
                case AuthoredFloatField::LightSpotAngle:
                case AuthoredFloatField::LightIntensity:
                    replace.template operator()<LightComponent>();
                    break;
                case AuthoredFloatField::CameraFar:
                case AuthoredFloatField::CameraFovY:
                case AuthoredFloatField::CameraNear:
                case AuthoredFloatField::CameraAspectRatio:
                    replace.template operator()<CameraComponent>();
                    break;
                case AuthoredFloatField::RigidBodyMass:
                case AuthoredFloatField::RigidBodyFriction:
                case AuthoredFloatField::RigidBodyRestitution:
                    replace.template operator()<RigidBodyComponent>();
                    break;
                default:
                    FAIL() << "Uncovered authored field";
            }
            EXPECT_NE(AuthoredFloatFieldLifetime(scene, entity, item.Field), lifetime);
            if (termination == 0)
            {
                session.Cancel();
            }
            else if (termination == 1)
            {
                EXPECT_FALSE(session.PreviewField(item.Preview));
            }
            else
            {
                EXPECT_FALSE(session.Commit());
            }
            float current = 0.f;
            ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, item.Field, current));
            EXPECT_FLOAT_EQ(current, item.Preview);
            EXPECT_FALSE(session.Active());
            EXPECT_EQ(session.History().UndoCount(), 0u);
            EXPECT_TRUE(session.Dirty());
        }
    }
}

TEST_F(EditorSessionTest, OwnedTransformRollbackSurvivesUnrelatedSwapRemovalAndDenseGrowth)
{
    const auto         unrelated  = Entity(1);
    const auto         a          = Entity(2);
    const auto         b          = Entity(3);
    const auto         lifetime_a = scene.ComponentLifetime<TransformComponent>(a);
    const auto         lifetime_b = scene.ComponentLifetime<TransformComponent>(b);
    const EntityTarget targets[]  = {session.TargetFor(a), session.TargetFor(b)};
    ASSERT_TRUE(session.BeginTransform(targets));
    LocalTransformValue values[] = {Value(a), Value(b)};
    values[0].Position.x         = 3.f;
    values[1].Position.x         = 4.f;
    ASSERT_TRUE(session.PreviewTransforms(values));
    scene.RemoveComponent<TransformComponent>(unrelated); // moves b's dense slot
    for (uint32_t i = 0; i < 600; ++i)
    {
        scene.AddComponent<TransformComponent>(scene.CreateEntity(), {});
    }
    EXPECT_EQ(scene.ComponentLifetime<TransformComponent>(a), lifetime_a);
    EXPECT_EQ(scene.ComponentLifetime<TransformComponent>(b), lifetime_b);
    session.Cancel();
    EXPECT_TRUE(SameLocalTransform(Value(a), LocalTransformValue{}));
    EXPECT_TRUE(SameLocalTransform(Value(b), LocalTransformValue{}));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, OwnedFieldRollbackSurvivesUnrelatedSwapRemovalAndDenseGrowthForEveryComponentKind)
{
    uint8_t    identity = 0;
    const auto exercise = [&]<typename T>(AuthoredFloatField field, float preview) {
        SCOPED_TRACE(static_cast<uint32_t>(field));
        const auto unrelated = Entity(++identity);
        const auto target    = Entity(++identity);
        scene.AddComponent<T>(unrelated, {});
        scene.AddComponent<T>(target, {});
        float original = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, target, field, original));
        const auto lifetime = scene.ComponentLifetime<T>(target);
        ASSERT_TRUE(session.BeginField(session.TargetFor(target), field));
        ASSERT_TRUE(session.PreviewField(preview));
        scene.RemoveComponent<T>(unrelated);
        for (uint32_t i = 0; i < 600; ++i)
        {
            scene.AddComponent<T>(scene.CreateEntity(), {});
        }
        EXPECT_EQ(scene.ComponentLifetime<T>(target), lifetime);
        session.Cancel();
        float current = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, target, field, current));
        EXPECT_FLOAT_EQ(current, original);
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.History().UndoCount(), 0u);
    };
    exercise.template operator()<LightComponent>(AuthoredFloatField::LightIntensity, 2.f);
    exercise.template operator()<CameraComponent>(AuthoredFloatField::CameraFar, 1200.f);
    exercise.template operator()<RigidBodyComponent>(AuthoredFloatField::RigidBodyMass, 2.f);
}

TEST_F(EditorSessionTest, AuthoredMutationRequiresTheOwnerAndExclusivePhase)
{
    const auto entity = Entity(1);
    session.EndMutationPhase();
    EXPECT_FALSE(session.Select(entity));
    EXPECT_FALSE(Begin(entity));
    EXPECT_FALSE(session.Undo());
    bool        worker_accepted = true;
    std::thread worker([&] { worker_accepted = session.BeginMutationPhase(); });
    worker.join();
    EXPECT_FALSE(worker_accepted);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Select(entity));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, ChangedPreviewCommitsExactlyOneOperationAndUndoRedo)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    ASSERT_TRUE(Preview(entity, 2.f));
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 2.f);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 2.f);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, UIRequestsOnlyApplyAtThePerUpdateMutationPoint)
{
    const auto entity = Entity(1);
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelection(entity));
    EXPECT_TRUE(session.Selection().Entities().empty());
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    session.EndMutationPhase();
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_TRUE(session.CanRedo());
    ASSERT_TRUE(session.Request(EditorCommand::Redo));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_TRUE(session.CanUndo());
}

TEST_F(EditorSessionTest, RequestQueueIsBoundedAndReusesConsumedSlots)
{
    for (uint32_t round = 0; round < 4; ++round)
    {
        session.EndMutationPhase();
        for (uint32_t i = 0; i < EditorSession::MaxPendingRequests; ++i)
        {
            ASSERT_TRUE(session.Request(EditorCommand::Cancel));
        }
        EXPECT_FALSE(session.Request(EditorCommand::ClearSelection));
        EXPECT_EQ(session.PendingRequestCount(), EditorSession::MaxPendingRequests);
        ASSERT_TRUE(session.BeginMutationPhase());
        EXPECT_TRUE(session.ProcessRequests());
        EXPECT_EQ(session.PendingRequestCount(), 0u);
    }
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, QueuedHistoryExhaustionIsANoOpWithoutSuppressingLaterCommands)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.MarkSaved());
    const auto bytes  = session.History().BytesUsed();
    const auto offset = memory.MainArena.m_current_offset;
    session.EndMutationPhase();
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    ASSERT_TRUE(session.Request(EditorCommand::Redo));
    ASSERT_TRUE(session.Request(EditorCommand::Redo));
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_EQ(session.History().BytesUsed(), bytes);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(session.Redo()); // direct API still reports no operation
}

TEST_F(EditorSessionTest, EmptyHistoryDoesNotTurnReadOnlyRequestsIntoSuccessfulCommands)
{
    session.EndMutationPhase();
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    ASSERT_TRUE(session.Request(EditorCommand::Redo));
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, InvalidHistoryReplayIsNotMistakenForBenignQueueExhaustion)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    scene.GetComponent<TransformComponent>(entity)->Position.x = 99.f;
    session.EndMutationPhase();
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 99.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, FailedPreviewRollbackDoesNotBecomeASuccessfulEmptyHistoryCommand)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 8.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.Request(EditorCommand::Undo));
    ASSERT_TRUE(session.BeginMutationPhase());
    // A foreign write within the phase invalidates rollback and history.
    // Empty-history handling must not hide Undo's failed cancellation/replay.
    scene.GetComponent<TransformComponent>(entity)->Position.x = 99.f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 99.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, QueuedAuthoredValuesApplyOnlyInTheMutationPhaseAndRoundTrip)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Spot});
    auto value       = Value(entity);
    value.Position.y = 7.f;
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestTransformValue(entity, value));
    EXPECT_FLOAT_EQ(Value(entity).Position.y, 0.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.y, 7.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, 12.f));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 12.f);
    EXPECT_EQ(session.History().UndoCount(), 2u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(SameLocalTransform(Value(entity), {}));
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(Value(entity).Position.y, 7.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 12.f);
}

TEST_F(EditorSessionTest, CommittedTransformPublishesFrameBeforeUIFieldCapture)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 4.f);
    ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, 12.f));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 12.f);
    EXPECT_EQ(session.History().UndoCount(), 2u);
}

TEST_F(EditorSessionTest, CancelledTransformPublishesRestoredFrameBeforeUIFieldCapture)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 4.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 0.f);
    ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, 12.f));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(EditorSessionTest, HistoryReplayPublishesFrameBeforeUIFieldCapture)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    for (const bool undo : {true, false})
    {
        ASSERT_TRUE(undo ? session.Undo() : session.Redo());
        session.EndMutationPhase();
        EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), undo ? 0.f : 4.f);
        // A semantic no-op validates the captured frame without replacing redo.
        ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, 10.f));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.ProcessRequests());
    }
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FALSE(session.Active());
}

namespace
{
    enum class QueuedFrameEdit : uint8_t
    {
        Transform,
        Field,
        TransformControl,
        FieldControl,
    };

    struct QueuedFrameCase
    {
        QueuedFrameEdit Edit = QueuedFrameEdit::Transform;
        bool            Undo = true;
    };
} // namespace

class EditorQueuedFrameTest : public EditorSessionTest, public testing::WithParamInterface<QueuedFrameCase>
{
protected:
    EntityID Parent = {};
    EntityID Target = {};

    void     PrepareReplay(bool affected)
    {
        Parent           = Entity(1);
        Target           = Entity(2);
        const auto other = Entity(3);
        scene.AddComponent<LightComponent>(Target, {.LightType = LightComponent::Type::Point});
        ASSERT_TRUE(scene.SetParent(Target, affected ? Parent : other));
        ASSERT_TRUE(Begin(Parent));
        ASSERT_TRUE(Preview(Parent, 4.f));
        ASSERT_TRUE(session.Commit());
        if (!GetParam().Undo)
        {
            ASSERT_TRUE(session.Undo());
        }
        session.EndMutationPhase();
    }

    bool QueueEdit()
    {
        switch (GetParam().Edit)
        {
            case QueuedFrameEdit::Transform:
            {
                auto value       = Value(Target);
                value.Position.x = 2.f;
                return session.RequestTransformValue(Target, value);
            }
            case QueuedFrameEdit::Field:
                return session.RequestFieldValue(Target, AuthoredFloatField::LightRange, 12.f);
            case QueuedFrameEdit::TransformControl:
            case QueuedFrameEdit::FieldControl:
                return session.RequestControlDrag(Target, {.Key = 91, .PointerSerial = 31, .StartX = 100.f, .IsTransform = GetParam().Edit == QueuedFrameEdit::TransformControl});
        }
        return false;
    }
};

TEST_P(EditorQueuedFrameTest, ReplayBeforeQueuedEditRejectsTheStaleParentFrameWithoutRebasing)
{
    ASSERT_NO_FATAL_FAILURE(PrepareReplay(true));
    const auto frame = scene.Hierarchy().ParentWorldRevision(Target);
    ASSERT_TRUE(session.Request(GetParam().Undo ? EditorCommand::Undo : EditorCommand::Redo));
    ASSERT_TRUE(QueueEdit());
    const auto offset = memory.MainArena.m_current_offset;
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_FALSE(session.ProcessRequests());
    EXPECT_NE(scene.Hierarchy().ParentWorldRevision(Target), frame);
    EXPECT_TRUE(SameLocalTransform(Value(Target), {}));
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(Target)->Range, 10.f);
    EXPECT_FLOAT_EQ(Value(Parent).Position.x, GetParam().Undo ? 0.f : 4.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.ControlKey(), 0u);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), GetParam().Undo ? 0u : 1u);
    EXPECT_EQ(session.History().RedoCount(), GetParam().Undo ? 1u : 0u);
    EXPECT_EQ(session.Dirty(), !GetParam().Undo);
    // A fresh intent against the newly published frame still works. The stale
    // request is retired, not postponed, and cannot later replace this edit.
    ASSERT_TRUE(session.RequestFieldValue(Target, AuthoredFloatField::LightRange, 12.f));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(Target)->Range, 12.f);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(Target)->Range, 12.f);
}

TEST_P(EditorQueuedFrameTest, ReplayOnAnotherBranchPreservesValidQueuedEditsAndReusesStorage)
{
    ASSERT_NO_FATAL_FAILURE(PrepareReplay(false));
    const auto frame = scene.Hierarchy().ParentWorldRevision(Target);
    ASSERT_TRUE(session.Request(GetParam().Undo ? EditorCommand::Undo : EditorCommand::Redo));
    ASSERT_TRUE(QueueEdit());
    const auto offset = memory.MainArena.m_current_offset;
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    const bool control   = GetParam().Edit == QueuedFrameEdit::TransformControl || GetParam().Edit == QueuedFrameEdit::FieldControl;
    const bool transform = GetParam().Edit == QueuedFrameEdit::Transform || GetParam().Edit == QueuedFrameEdit::TransformControl;
    EXPECT_EQ(session.Active(), control);
    if (control)
    {
        ASSERT_TRUE(session.UpdateControlDrag(120.f, false));
    }
    EXPECT_EQ(scene.Hierarchy().ParentWorldRevision(Target), frame);
    EXPECT_FLOAT_EQ(Value(Target).Position.x, transform ? (control ? 1.f : 2.f) : 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(Target)->Range, transform ? 10.f : (control ? 11.f : 12.f));
    EXPECT_FLOAT_EQ(Value(Parent).Position.x, GetParam().Undo ? 0.f : 4.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), GetParam().Undo ? 1u : 2u);
    EXPECT_EQ(session.History().RedoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

INSTANTIATE_TEST_SUITE_P(OrderedReplay, EditorQueuedFrameTest, testing::Values(QueuedFrameCase{QueuedFrameEdit::Transform, true}, QueuedFrameCase{QueuedFrameEdit::Transform, false}, QueuedFrameCase{QueuedFrameEdit::Field, true}, QueuedFrameCase{QueuedFrameEdit::Field, false}, QueuedFrameCase{QueuedFrameEdit::TransformControl, true}, QueuedFrameCase{QueuedFrameEdit::TransformControl, false}, QueuedFrameCase{QueuedFrameEdit::FieldControl, true}, QueuedFrameCase{QueuedFrameEdit::FieldControl, false}));

TEST_F(EditorSessionTest, QueuedRenameCancelsBeforeMutationAndInvalidatesIncompatibleHistory)
{
    const auto    entity = Entity(1);
    NameComponent name   = {};
    ZEngine::Helpers::secure_strcpy(name.Value, sizeof(name.Value), "Before");
    scene.AddComponent<NameComponent>(entity, name);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestRename(session.TargetFor(entity), "Before", "After"));
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Before");
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "After");
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, UnchangedAndConflictingRenamesPreserveTheLiveInteraction)
{
    const auto    entity = Entity(1);
    NameComponent name   = {};
    ZEngine::Helpers::secure_strcpy(name.Value, sizeof(name.Value), "Before");
    scene.AddComponent<NameComponent>(entity, name);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.RequestRename(session.TargetFor(entity), "Before", "Before"));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.RequestRename(session.TargetFor(entity), "WrongBefore", "After"));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "Before");
    session.Cancel();
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RenameRejectsReplacementEntitiesEpochsAndUnboundedText)
{
    const auto entity = Entity(1);
    scene.AddComponent<NameComponent>(entity, {});
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.RequestRename(target, "", "After"));
    scene.DestroyEntity(entity);
    const auto replacement = Entity(1);
    scene.AddComponent<NameComponent>(replacement, {});
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_STREQ(scene.GetComponent<NameComponent>(replacement)->Value, "");
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_FALSE(session.RequestRename(target, "", "After"));
    const auto current = session.TargetFor(replacement);
    EXPECT_FALSE(session.RequestRename(current, "", ""));
    EXPECT_FALSE(session.RequestRename(current, "", std::string(128, 'x')));
    EXPECT_FALSE(session.RequestRename(current, "", std::string_view("a\0b", 3)));
    EXPECT_FALSE(session.RequestRename(current, std::string(128, 'x'), "After"));
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, MissingSceneOwnerAdaptersRejectDestructiveRequestsWithoutCancelling)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.RequestDelete(entity));
    ASSERT_TRUE(session.RequestCollection("Collection"));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    EXPECT_TRUE(scene.IsAlive(entity));
    EXPECT_EQ(scene.AliveCount(), 1u);
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    session.Cancel();
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(session.RequestCollection(""));
    EXPECT_FALSE(session.RequestCollection(std::string(128, 'x')));
    EXPECT_FALSE(session.RequestCollection(std::string_view("a\0b", 3)));
}

namespace
{
    struct MutationHookOwner
    {
        uint32_t                 Validations = 0;
        uint32_t                 Applies     = 0;

        EditorSceneMutationHooks Hooks()
        {
            return {
                .Context = this,
                .Validate =
                    [](void* context, const Scene&, EditorSceneMutation, EntityID) {
                        ++static_cast<MutationHookOwner*>(context)->Validations;
                        return true;
                    },
                .Apply = [](void* context, Scene&, EditorSceneMutation, EntityID, cstring) { ++static_cast<MutationHookOwner*>(context)->Applies; },
            };
        }
    };
} // namespace

TEST_F(EditorSessionTest, StructuralOwnerCannotBeReplacedWhileSameEpochCommandsAreQueued)
{
    MutationHookOwner original = {}, foreign = {};
    const auto        entity = Entity(1);
    const auto        hooks  = original.Hooks();
    ASSERT_TRUE(session.SetSceneMutationHooks(hooks));
    ASSERT_TRUE(session.RequestDelete(entity));
    ASSERT_TRUE(session.RequestCollection("Collection"));
    EXPECT_FALSE(session.SetSceneMutationHooks(foreign.Hooks()));
    auto changed     = hooks;
    changed.Validate = [](void*, const Scene&, EditorSceneMutation, EntityID) { return false; };
    EXPECT_FALSE(session.SetSceneMutationHooks(changed));
    changed       = hooks;
    changed.Apply = [](void*, Scene&, EditorSceneMutation, EntityID, cstring) {};
    EXPECT_FALSE(session.SetSceneMutationHooks(changed));
    EXPECT_TRUE(session.SetSceneMutationHooks(hooks)); // Exact rebinding is harmless.
    EXPECT_TRUE(session.ProcessRequests());
    EXPECT_EQ(original.Validations, 2u);
    EXPECT_EQ(original.Applies, 2u);
    EXPECT_EQ(foreign.Validations, 0u);
    EXPECT_EQ(foreign.Applies, 0u);
}

TEST_F(EditorSessionTest, StructuralOwnerChangesRequireANewEpochWhichRetiresOldCommands)
{
    MutationHookOwner original = {}, replacement = {};
    ASSERT_TRUE(session.SetSceneMutationHooks(original.Hooks()));
    ASSERT_TRUE(session.RequestDelete(Entity(1)));
    const auto epoch = session.SceneEpoch();
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_GT(session.SceneEpoch(), epoch);
    ASSERT_TRUE(session.SetSceneMutationHooks(replacement.Hooks()));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(original.Applies, 0u);
    EXPECT_EQ(replacement.Applies, 0u);
    ASSERT_TRUE(session.RequestCollection("New owner"));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(replacement.Applies, 1u);
}

TEST_F(EditorSessionTest, InvalidOrWrongThreadStructuralHookBindingPreservesTheOriginalOwnerAndPreview)
{
    MutationHookOwner original = {}, foreign = {};
    ASSERT_TRUE(session.SetSceneMutationHooks(original.Hooks()));
    ASSERT_TRUE(Begin(Entity(1)));
    EXPECT_FALSE(session.SetSceneMutationHooks(original.Hooks()));
    EXPECT_FALSE(session.SetSceneMutationHooks(foreign.Hooks()));
    EXPECT_TRUE(session.Active());
    session.Cancel();
    auto invalid  = original.Hooks();
    invalid.Apply = nullptr;
    EXPECT_FALSE(session.SetSceneMutationHooks(invalid));
    session.EndMutationPhase();
    EXPECT_FALSE(session.SetSceneMutationHooks(original.Hooks()));
    std::thread worker([&] { EXPECT_FALSE(session.SetSceneMutationHooks(foreign.Hooks())); });
    worker.join();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.RequestCollection("Original owner"));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(original.Applies, 1u);
    EXPECT_EQ(foreign.Applies, 0u);
}

TEST_F(EditorSessionTest, ReadOnlyPhaseRejectsAuthoredCommandsRatherThanTreatingThemAsCancel)
{
    const auto entity = Entity(1);
    scene.AddComponent<NameComponent>(entity, {});
    ASSERT_TRUE(session.RequestRename(session.TargetFor(entity), "", "NewName"));
    ASSERT_TRUE(session.RequestDelete(entity));
    ASSERT_TRUE(session.RequestCollection("Collection"));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "");
    EXPECT_EQ(scene.AliveCount(), 1u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RepeatedQueuedRenamesReuseBoundedRequestAndHierarchyStorage)
{
    const auto entity = Entity(1);
    scene.AddComponent<NameComponent>(entity, {});
    const auto target = session.TargetFor(entity);
    const auto bytes  = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        char name[128] = {};
        snprintf(name, sizeof(name), "Name_%u", i);
        ASSERT_TRUE(session.RequestRename(target, scene.GetComponent<NameComponent>(entity)->Value, name));
        ASSERT_TRUE(session.ProcessRequests());
        EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, name);
        EXPECT_EQ(memory.MainArena.m_current_offset, bytes);
        EXPECT_EQ(session.PendingRequestCount(), 0u);
        EXPECT_EQ(session.History().UndoCount(), 0u);
    }
}

TEST_F(EditorSessionTest, NumericPointerPreviewAlwaysUsesTheCapturedBaseAndMapping)
{
    const auto entity            = Entity(1);
    auto*      component         = scene.GetComponent<TransformComponent>(entity);
    component->Position.y        = 3.f;
    const EditorControlDrag drag = {.Key = 42, .StartX = 100.f, .UnitsPerPixel = .5f, .Axis = 1};
    ASSERT_TRUE(session.RequestControlDrag(entity, drag));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.ControlKey(), 42u);
    ASSERT_TRUE(session.UpdateControlDrag(110.f, true));
    EXPECT_FLOAT_EQ(component->Position.y, 8.f);
    ASSERT_TRUE(session.RequestControlDrag(entity, drag));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateControlDrag(120.f, true));
    EXPECT_FLOAT_EQ(component->Position.y, 13.f);
    auto incompatible   = drag;
    incompatible.StartX = 120.f;
    ASSERT_TRUE(session.RequestControlDrag(entity, incompatible));
    EXPECT_FALSE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateControlDrag(104.f, false));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.ControlKey(), 0u);
    EXPECT_FLOAT_EQ(component->Position.y, 5.f);
    EXPECT_FLOAT_EQ(component->Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(component->Position.y, 3.f);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(component->Position.y, 5.f);
}

TEST_F(EditorSessionTest, NumericRotationAndScaleTouchOnlyTheirCapturedProperty)
{
    const auto        entity = Entity(1);
    EditorControlDrag drag   = {.Key = 42, .UnitsPerPixel = .01f, .Property = TransformProperty::Rotation, .Axis = 2};
    ASSERT_TRUE(session.RequestControlDrag(entity, drag));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateControlDrag(10.f, false));
    EXPECT_FLOAT_EQ(Value(entity).Rotation.z, .1f);
    EXPECT_FLOAT_EQ(Value(entity).Scale.z, 1.f);
    drag.Property = TransformProperty::Scale;
    ASSERT_TRUE(session.RequestControlDrag(entity, drag));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateControlDrag(-200.f, false));
    EXPECT_FLOAT_EQ(Value(entity).Scale.z, -1.f);
    EXPECT_FLOAT_EQ(Value(entity).Rotation.z, .1f);
    EXPECT_EQ(session.History().UndoCount(), 2u);
}

TEST_F(EditorSessionTest, NumericFieldRejectsInvalidMotionButReleaseCommitsTheLastValidPreview)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Spot});
    const EditorControlDrag drag = {.Key = 42, .UnitsPerPixel = 1.f, .Field = AuthoredFloatField::LightRange, .IsTransform = false};
    ASSERT_TRUE(session.RequestControlDrag(entity, drag));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_TRUE(session.UpdateControlDrag(5.f, true));
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 15.f);
    EXPECT_FALSE(session.UpdateControlDrag(-100.f, true));
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 15.f);
    ASSERT_TRUE(session.UpdateControlDrag(std::numeric_limits<float>::infinity(), false));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.ControlKey(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, NumericCancelAndNoOpRestoreInterpolationWithoutHistory)
{
    const auto entity            = Entity(1);
    auto*      component         = scene.GetComponent<TransformComponent>(entity);
    component->PreviousPosition  = {4.f, 5.f, 6.f};
    const EditorControlDrag drag = {.Key = 42, .StartX = 10.f};
    for (uint32_t reason = 0; reason < 4; ++reason)
    {
        ASSERT_TRUE(session.RequestControlDrag(entity, drag));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.UpdateControlDrag(20.f, true));
        ASSERT_TRUE(session.UpdateControlDrag(10.f, reason != 0, reason != 1, reason == 2, reason == 3));
        EXPECT_FALSE(session.Active());
        EXPECT_EQ(session.ControlKey(), 0u);
        EXPECT_FLOAT_EQ(component->Position.x, 0.f);
        EXPECT_FLOAT_EQ(component->PreviousPosition.x, 4.f);
        EXPECT_FLOAT_EQ(component->PreviousPosition.y, 5.f);
        EXPECT_FLOAT_EQ(component->PreviousPosition.z, 6.f);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_FALSE(session.Dirty());
    }
}

TEST_F(EditorSessionTest, InvalidNumericControlDescriptorsAndValuesAreRejectedBeforeQueueing)
{
    const auto        entity = Entity(1);
    EditorControlDrag drag   = {};
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.Key  = 42;
    drag.Axis = 3;
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.Axis          = 0;
    drag.UnitsPerPixel = 0.f;
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.UnitsPerPixel = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.UnitsPerPixel = .05f;
    drag.StartX        = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.StartX   = 0.f;
    drag.Property = static_cast<TransformProperty>(99);
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    drag.IsTransform = false;
    drag.Field       = static_cast<AuthoredFloatField>(99);
    EXPECT_FALSE(session.RequestControlDrag(entity, drag));
    auto value    = Value(entity);
    value.Scale.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(session.RequestTransformValue(entity, value));
    EXPECT_FALSE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, std::numeric_limits<float>::infinity()));
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, DelayedValueAndControlRequestsRejectSelectionChangesAndRecycledTargets)
{
    const auto original = Entity(1);
    const auto other    = Entity(2);
    ASSERT_TRUE(session.Select(original));
    auto value       = Value(original);
    value.Position.x = 8.f;
    ASSERT_TRUE(session.RequestTransformValue(original, value));
    ASSERT_TRUE(session.Select(other));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(original).Position.x, 0.f);
    ASSERT_TRUE(session.RequestControlDrag(original, {.Key = 42}));
    scene.DestroyEntity(original);
    const auto replacement = Entity(1);
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(replacement).Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, DiscardingDelayedEditsPreservesOrderedSelectionAndHistoryCommandsAcrossRingWrap)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    for (uint32_t round = 0; round < 3; ++round)
    {
        for (uint32_t i = 0; i < EditorSession::MaxPendingRequests / 4; ++i)
        {
            ASSERT_TRUE(session.RequestControlDrag(first, {.Key = 42}));
            ASSERT_TRUE(session.RequestSelection(first));
            ASSERT_TRUE(session.RequestTransformValue(first, {}));
            ASSERT_TRUE(session.RequestSelection(second));
        }
        EXPECT_FALSE(session.RequestControlDrag(first, {.Key = 42}));
        ASSERT_TRUE(session.DiscardEditRequests());
        EXPECT_EQ(session.PendingRequestCount(), EditorSession::MaxPendingRequests / 2);
        ASSERT_TRUE(session.ProcessRequests());
        EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
        EXPECT_FALSE(session.Active());
        EXPECT_FALSE(session.DiscardEditRequests());
        // Advance the ring start by an odd count before the next full queue.
        ASSERT_TRUE(session.Request(EditorCommand::Cancel));
        ASSERT_TRUE(session.ProcessRequests());
    }
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, CancellationAndExternalMutationDiscardDelayedNumericInput)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(session.Request(EditorCommand::Cancel));
    ASSERT_TRUE(session.RequestControlDrag(entity, {.Key = 42}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    auto value       = Value(entity);
    value.Position.x = 8.f;
    ASSERT_TRUE(session.RequestTransformValue(entity, value));
    session.InvalidateForExternalMutation();
    scene.GetComponent<TransformComponent>(entity)->Position.x = 3.f;
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 3.f);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, DelayedNumericRequestsCannotOverwriteValuesChangedSinceTheControlWasBuilt)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    auto value       = Value(entity);
    value.Position.x = 8.f;
    ASSERT_TRUE(session.RequestTransformValue(entity, value));
    scene.GetComponent<TransformComponent>(entity)->Position.y = 3.f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(entity).Position.y, 3.f);
    ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::LightRange, 20.f));
    scene.GetComponent<LightComponent>(entity)->Range = 15.f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 15.f);
    ASSERT_TRUE(session.RequestControlDrag(entity, {.Key = 42}));
    scene.GetComponent<TransformComponent>(entity)->Scale.z = 2.f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, InvalidQueuedComponentCandidateLeavesAuthoredStateAndHistoryUnchanged)
{
    const auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    ASSERT_TRUE(session.RequestFieldValue(entity, AuthoredFloatField::CameraFovY, 180.f));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, StaleQueuedSelectionCannotSelectAReplacementWithTheSameUUID)
{
    const auto original = Entity(1);
    const auto other    = Entity(2);
    ASSERT_TRUE(session.Select(other));
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelection(original));
    scene.DestroyEntity(original);
    const auto replacement = Entity(1);
    ASSERT_NE(original, replacement);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
}

TEST_F(EditorSessionTest, SceneRebindDiscardsRequestsAndSelectionRequestsCancelPreview)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    ASSERT_TRUE(Begin(first));
    ASSERT_TRUE(Preview(first, 5.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelection(second));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.RequestSceneSelection(SceneTarget::Grid));
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::None);
}

TEST_F(EditorSessionTest, QueuedSceneSelectionAndClearPreserveDocumentState)
{
    const auto entity = Entity(1);
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelection(entity));
    ASSERT_TRUE(session.RequestSceneSelection(SceneTarget::Grid));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(session.Selection().Entities().empty());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    ASSERT_TRUE(session.Request(EditorCommand::ClearSelection));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::None);
    EXPECT_FALSE(session.Dirty());
    EXPECT_FALSE(session.Request(static_cast<EditorCommand>(255)));
    EXPECT_FALSE(session.RequestSceneSelection(static_cast<SceneTarget>(255)));
    EXPECT_FALSE(session.RequestSelection(entity, static_cast<SelectionMode>(255)));
}

TEST_F(EditorSessionTest, FocusLossCanDiscardQueuedInputAndWorkerRequestsAreRejected)
{
    const auto entity = Entity(1);
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelection(entity));
    bool        accepted = true;
    std::thread worker([&] {
        accepted = session.Request(EditorCommand::ClearSelection);
        session.DiscardRequests();
    });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    session.DiscardRequests();
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(session.Selection().Entities().empty());
}

TEST_F(EditorSessionTest, CancelRestoresAuthoredAndInterpolationValuesExactly)
{
    const auto entity           = Entity(1);
    auto*      component        = scene.GetComponent<TransformComponent>(entity);
    component->Position         = {3.f, -0.f, 8.f};
    component->Rotation         = {1.f, 2.f, 3.f};
    component->Scale            = {-2.f, 0.f, 4.f};
    component->PreviousPosition = {7.f, 6.f, 5.f};
    const auto before           = Value(entity);
    ASSERT_TRUE(Begin(entity));
    EXPECT_FALSE(session.MarkSaved());
    ASSERT_TRUE(Preview(entity, 12.f));
    session.Cancel();
    EXPECT_TRUE(SameLocalTransform(before, Value(entity)));
    EXPECT_EQ(component->PreviousPosition.x, 7.f);
    EXPECT_EQ(component->PreviousPosition.y, 6.f);
    EXPECT_EQ(component->PreviousPosition.z, 5.f);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, NoOpAndReturnedToStartPreviewsCreateNoHistory)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    ASSERT_TRUE(Preview(entity, 0.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SubEpsilonAuthoredEditsAreNotDiscardedAsNoOps)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.e-9f));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Undo());
    EXPECT_EQ(Value(entity).Position.x, 0.f);
}

TEST_F(EditorSessionTest, BatchValidationAndHistoryApplicationAreAtomic)
{
    const auto       first   = Entity(1);
    const auto       second  = Entity(2);
    const std::array targets = {session.TargetFor(first), session.TargetFor(second)};
    ASSERT_TRUE(session.BeginTransform(targets));
    std::array values    = {Value(first), Value(second)};
    values[0].Position.x = 1.f;
    values[1].Position.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(session.PreviewTransforms(values));
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(second).Position.x, 0.f);
    values[1].Position.x = 2.f;
    ASSERT_TRUE(session.PreviewTransforms(values));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    scene.GetComponent<TransformComponent>(second)->Position.x = 99.f;
    EXPECT_FALSE(session.Undo());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 1.f);
    EXPECT_FLOAT_EQ(Value(second).Position.x, 99.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
    scene.GetComponent<TransformComponent>(second)->Position.x = 2.f;
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(second).Position.x, 0.f);
}

TEST_F(EditorSessionTest, DuplicateAndInvalidTargetsCannotBeginAPartialBatch)
{
    const auto       first     = session.TargetFor(Entity(1));
    const std::array duplicate = {first, first};
    EXPECT_FALSE(session.BeginTransform(duplicate));
    const std::array invalid = {first, EntityTarget{}};
    EXPECT_FALSE(session.BeginTransform(invalid));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SelectionChangeCancelsBeforeReplacingTheTarget)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    ASSERT_TRUE(session.Select(first));
    ASSERT_TRUE(Begin(first));
    ASSERT_TRUE(Preview(first, 3.f));
    ASSERT_TRUE(session.Select(second));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, ActiveLifecycleContinuesAcrossUpdatesWithoutAnyUIBuild)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(Preview(entity, 4.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    session.Cancel();
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, EditModeExitCancelsAndDisablesAuthoredEdits)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FALSE(Begin(entity));
    EXPECT_FALSE(session.Undo());
    EXPECT_FALSE(session.Redo());
}

TEST_F(EditorSessionTest, TargetLossRestoresSurvivorsAndNeverWritesRecycledEntity)
{
    const auto       first   = Entity(1);
    const auto       second  = Entity(2);
    const std::array targets = {session.TargetFor(first), session.TargetFor(second)};
    ASSERT_TRUE(session.BeginTransform(targets));
    std::array values    = {Value(first), Value(second)};
    values[0].Position.x = 1.f;
    values[1].Position.x = 2.f;
    ASSERT_TRUE(session.PreviewTransforms(values));
    scene.DestroyEntity(second);
    const auto recycled                                          = Entity(2);
    scene.GetComponent<TransformComponent>(recycled)->Position.x = 99.f;
    EXPECT_FALSE(session.Commit());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(recycled).Position.x, 99.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, SceneReplacementRestoresOriginalAndInvalidatesOldEpoch)
{
    const auto entity = Entity(1);
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 5.f));
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 5.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.BindScene(nullptr));
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(entity)->WorldTransform(0, 3), 0.f);
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_FALSE(session.Resolve(target).IsValid());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, ShutdownRestoresTheOriginalDerivedFrameBeforeECSOwnerTeardown)
{
    const auto entity             = Entity(1);
    auto*      transform          = scene.GetComponent<TransformComponent>(entity);
    transform->PreviousPosition.x = 41.f;
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 5.f));
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 5.f);
    session.Shutdown();
    EXPECT_FLOAT_EQ(transform->Position.x, 0.f);
    EXPECT_FLOAT_EQ(transform->PreviousPosition.x, 41.f);
    EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 0.f);
    EXPECT_FALSE(session.Ready());
}

TEST_F(EditorSessionTest, ReparentingWithoutMembershipChangeIsDetected)
{
    const auto entity = Entity(1);
    const auto parent = Entity(2);
    scene.AddComponent<ParentComponent>(entity, {});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 3.f));
    scene.GetComponent<ParentComponent>(entity)->Parent = parent;
    EXPECT_FALSE(session.Commit());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, AncestorRotationCancelsBeforeTheNextPreviewAndRestoresOnlyTheCapturedTargets)
{
    const auto child       = Entity(1);
    const auto parent      = Entity(2);
    const auto grandparent = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, parent));
    ASSERT_TRUE(scene.SetParent(parent, grandparent));
    scene.GetComponent<TransformComponent>(parent)->Position.y      = 1.f;
    scene.GetComponent<TransformComponent>(child)->PreviousPosition = {2.f, 3.f, 4.f};
    ASSERT_TRUE(Begin(child));
    ASSERT_TRUE(Preview(child, 5.f));
    scene.GetComponent<TransformComponent>(grandparent)->Rotation.x = .7f;
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(child).Position.x, 0.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->PreviousPosition.y, 3.f);
    EXPECT_FLOAT_EQ(Value(grandparent).Rotation.x, .7f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, UnrelatedBranchMotionDoesNotCancelAnActiveTransform)
{
    const auto child     = Entity(1);
    const auto parent    = Entity(2);
    const auto unrelated = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, parent));
    ASSERT_TRUE(Begin(child));
    ASSERT_TRUE(Preview(child, 5.f));
    scene.GetComponent<TransformComponent>(unrelated)->Position.x = 7.f;
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.Commit());
    EXPECT_FLOAT_EQ(Value(child).Position.x, 5.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(EditorSessionTest, LegacyAncestorReparentIsDetectedEvenWhenTheTargetParentAndMembershipStayUnchanged)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    const auto first  = Entity(3);
    const auto second = Entity(4);
    ASSERT_TRUE(scene.SetParent(child, parent));
    ASSERT_TRUE(scene.SetParent(parent, first));
    ASSERT_TRUE(Begin(child));
    ASSERT_TRUE(Preview(child, 5.f));
    const auto structural                               = scene.StructuralRevision();
    scene.GetComponent<ParentComponent>(parent)->Parent = second;
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_EQ(scene.StructuralRevision(), structural);
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(child).Position.x, 0.f);
    EXPECT_EQ(scene.GetComponent<ParentComponent>(parent)->Parent, second);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, RequestedReparentCancelsBeforeMutationAndInvalidatesIncompatibleLocalHistory)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    ASSERT_TRUE(Begin(child));
    ASSERT_TRUE(Preview(child, 2.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(child));
    ASSERT_TRUE(Preview(child, 8.f));
    ASSERT_TRUE(session.Reparent(child, parent));
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(child).Position.x, 2.f);
    EXPECT_EQ(scene.GetComponent<ParentComponent>(child)->Parent, parent);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, InvalidOrNoOpReparentDoesNotCancelTheCurrentInteraction)
{
    const auto root  = Entity(1);
    const auto child = Entity(2);
    ASSERT_TRUE(scene.SetParent(child, root));
    ASSERT_TRUE(Begin(root));
    ASSERT_TRUE(Preview(root, 3.f));
    EXPECT_FALSE(session.Reparent(root, child));
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(Value(root).Position.x, 3.f);
    ASSERT_TRUE(session.Reparent(child, root));
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
}

TEST_F(EditorSessionTest, QueuedReparentWorksForPureECSEntitiesOnlyAtTheMutationPoint)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestReparent(child, parent));
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(child));
    EXPECT_FALSE(session.ProcessRequests());
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(scene.GetComponent<ParentComponent>(child)->Parent, parent);
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.RequestReparent(child, INVALID_ENTITY));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(child));
}

TEST_F(EditorSessionTest, QueuedReparentRejectsStaleParentsAndConflictingHierarchyEdits)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    const auto other  = Entity(3);
    ASSERT_TRUE(session.RequestReparent(child, parent));
    ASSERT_TRUE(scene.SetParent(child, other));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(scene.GetComponent<ParentComponent>(child)->Parent, other);
    ASSERT_TRUE(session.RequestReparent(child, parent));
    scene.DestroyEntity(parent);
    const auto replacement = Entity(2);
    ASSERT_NE(parent, replacement);
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(scene.GetComponent<ParentComponent>(child)->Parent, other);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, InvalidHierarchyFramesCannotBeginTransformOrComponentHandleEdits)
{
    const auto a = Entity(1);
    const auto b = Entity(2);
    scene.AddComponent<ParentComponent>(a, {b});
    scene.AddComponent<ParentComponent>(b, {a});
    scene.AddComponent<LightComponent>(a, {.LightType = LightComponent::Type::Point});
    EXPECT_FALSE(Begin(a));
    EXPECT_FALSE(session.BeginField(session.TargetFor(a), AuthoredFloatField::LightRange));
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, ComponentHandleCancelsIfItsCapturedWorldFrameMoves)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    ASSERT_TRUE(scene.SetParent(child, parent));
    scene.AddComponent<LightComponent>(child, {.LightType = LightComponent::Type::Point});
    ASSERT_TRUE(session.BeginField(session.TargetFor(child), AuthoredFloatField::LightRange));
    ASSERT_TRUE(session.PreviewField(20.f));
    scene.GetComponent<TransformComponent>(parent)->Position.y = 3.f;
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(child)->Range, 10.f);
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, FieldCancelPreservesANewerExternalValueInsteadOfRestoringOverIt)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    ASSERT_TRUE(session.BeginField(session.TargetFor(entity), AuthoredFloatField::LightRange));
    ASSERT_TRUE(session.PreviewField(20.f));
    scene.GetComponent<LightComponent>(entity)->Range = 30.f;
    session.Cancel();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 30.f);
    EXPECT_FALSE(session.Active());
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, FieldCancelDoesNotRestoreAValueInvalidatedByAnotherComponentConstraint)
{
    const auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {.Near = 1.f, .Far = 10.f});
    ASSERT_TRUE(session.BeginField(session.TargetFor(entity), AuthoredFloatField::CameraFar));
    ASSERT_TRUE(session.PreviewField(20.f));
    scene.GetComponent<CameraComponent>(entity)->Near = 15.f;
    session.Cancel();
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 20.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Near, 15.f);
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, DelayedControlCannotBeginAgainstADifferentParentFrame)
{
    const auto child  = Entity(1);
    const auto parent = Entity(2);
    ASSERT_TRUE(scene.SetParent(child, parent));
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(session.RequestControlDrag(child, {.Key = 42}));
    scene.GetComponent<TransformComponent>(parent)->Rotation.x = .7f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(child).Position.x, 0.f);
}

TEST_F(EditorSessionTest, ActorFacadeAndPureECSTargetShareTheSameTransaction)
{
    const auto pure   = Entity(1);
    const auto handle = actors.Create();
    auto*      actor  = actors.Access(handle);
    actor->AddComponent<UUIDComponent>({Identity(2)});
    actor->AddComponent<TransformComponent>({});
    const auto       actor_entity = actor->GetEntityID();
    const std::array targets      = {session.TargetFor(pure), session.TargetFor(actor_entity)};
    ASSERT_TRUE(session.BeginTransform(targets));
    std::array values    = {Value(pure), Value(actor_entity)};
    values[0].Position.x = 1.f;
    values[1].Position.x = 2.f;
    ASSERT_TRUE(session.PreviewTransforms(values));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Value(pure).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(actor_entity).Position.x, 0.f);
}

TEST_F(EditorSessionTest, TypedFieldEditsValidatePreserveOtherFieldsAndUndoExactly)
{
    const auto     entity = Entity(1);
    LightComponent light  = {};
    light.LightType       = LightComponent::Type::Spot;
    light.Intensity       = 7.f;
    scene.AddComponent<LightComponent>(entity, light);
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.BeginField(target, AuthoredFloatField::LightRange));
    EXPECT_FALSE(session.PreviewField(-1.f));
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    ASSERT_TRUE(session.PreviewField(20.f));
    ASSERT_TRUE(session.PreviewField(30.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 10.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Intensity, 7.f);
    ASSERT_TRUE(session.Redo());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Range, 30.f);
    ASSERT_TRUE(session.BeginField(target, AuthoredFloatField::LightSpotAngle));
    EXPECT_FALSE(session.PreviewField(180.f));
    ASSERT_TRUE(session.PreviewField(60.f));
    session.Cancel();
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->SpotAngle, 45.f);
}

TEST_F(EditorSessionTest, CameraFieldsUseTheirComponentConstraints)
{
    const auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.BeginField(target, AuthoredFloatField::CameraFar));
    EXPECT_FALSE(session.PreviewField(0.05f));
    ASSERT_TRUE(session.PreviewField(2000.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->Far, 1000.f);
    ASSERT_TRUE(session.BeginField(target, AuthoredFloatField::CameraFovY));
    EXPECT_FALSE(session.PreviewField(std::numeric_limits<float>::quiet_NaN()));
    ASSERT_TRUE(session.PreviewField(90.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().RedoCount(), 0u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(entity)->FovY, 60.f);
}

TEST_F(EditorSessionTest, CancelRestoresPreexistingDirtyAndCheckpointState)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.MarkSaved());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    session.Cancel();
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 3.f));
    session.Cancel();
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Undo());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, ExternalMutationCancelsAndInvalidatesIncompatibleHistory)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    session.InvalidateForExternalMutation();
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 1.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, RepeatedDragsEvictHistoryWithoutGrowingArenaStorage)
{
    const auto entity = Entity(1);
    const auto offset = memory.MainArena.m_current_offset;
    for (int i = 1; i <= 1000; ++i)
    {
        ASSERT_TRUE(Begin(entity));
        ASSERT_TRUE(Preview(entity, static_cast<float>(i)));
        ASSERT_TRUE(session.Commit());
        EXPECT_LE(session.History().BytesUsed(), session.History().ByteCapacity());
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.History().UndoCount(), 8u);
    for (int i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(session.Undo());
    }
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 992.f);
}

TEST_F(EditorSessionTest, RepeatedQueuedNumericDragsReuseRequestScratchAndBoundedHistoryStorage)
{
    const auto              entity = Entity(1);
    const auto              offset = memory.MainArena.m_current_offset;
    const EditorControlDrag drag   = {.Key = 42, .StartX = 100.f};
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.RequestControlDrag(entity, drag));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.UpdateControlDrag(110.f, true));
        ASSERT_TRUE(session.UpdateControlDrag(120.f, false));
        EXPECT_EQ(session.PendingRequestCount(), 0u);
        EXPECT_LE(session.History().BytesUsed(), session.History().ByteCapacity());
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.History().UndoCount(), 8u);
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 1000.f);
    for (uint32_t i = 0; i < 8; ++i)
    {
        ASSERT_TRUE(session.Undo());
    }
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 992.f);
}

TEST_F(EditorSessionTest, StableFieldKeysDoNotExposeOffsetsOrRuntimeComponentIDs)
{
    for (uint8_t index = 0; index < static_cast<uint8_t>(AuthoredFloatField::Count); ++index)
    {
        const auto         field   = static_cast<AuthoredFloatField>(index);
        AuthoredFloatField decoded = {};
        EXPECT_TRUE(FindAuthoredFloatField(ComponentSchemaKey(field), ComponentFieldKey(field), decoded));
        EXPECT_EQ(field, decoded);
    }
    AuthoredFloatField field = {};
    EXPECT_FALSE(FindAuthoredFloatField("TransformComponent", "position", field));
    EXPECT_EQ(ComponentSchemaKey(AuthoredFloatField::Count), nullptr);
    EXPECT_EQ(ComponentFieldKey(static_cast<AuthoredFloatField>(255)), nullptr);
}

TEST_F(EditorSessionTest, AllAdditionalNumericFieldsUseSharedHistoryAndExactSavedContentTracking)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {});
    scene.AddComponent<CameraComponent>(entity, {});
    scene.AddComponent<RigidBodyComponent>(entity, {.BodyID = 42u});
    for (const auto field : {AuthoredFloatField::LightIntensity, AuthoredFloatField::CameraNear, AuthoredFloatField::CameraAspectRatio, AuthoredFloatField::RigidBodyMass, AuthoredFloatField::RigidBodyFriction, AuthoredFloatField::RigidBodyRestitution})
    {
        SCOPED_TRACE(ComponentFieldKey(field));
        float before = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, before));
        ASSERT_TRUE(session.RequestFieldValue(entity, field, before + .125f));
        ASSERT_TRUE(session.ProcessRequests());
        EXPECT_TRUE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 1u);
        float after = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, after));
        EXPECT_FLOAT_EQ(after, before + .125f);
        ASSERT_TRUE(session.Undo());
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, after));
        EXPECT_EQ(after, before);
        EXPECT_FALSE(session.Dirty());
        ASSERT_TRUE(session.Redo());
        EXPECT_TRUE(session.Dirty());
        ASSERT_TRUE(session.Undo());
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->BodyID, 42u);
    }
}

TEST_F(EditorSessionTest, AdditionalNumericValidationRetainsTheLastValidPreviewAndDoesNotTouchRuntimeFields)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {});
    scene.AddComponent<CameraComponent>(entity, {});
    scene.AddComponent<RigidBodyComponent>(entity, {.BodyID = 1729u});
    const std::array cases = {
        std::pair{      AuthoredFloatField::LightIntensity,   -1.f},
        std::pair{          AuthoredFloatField::CameraNear,    0.f},
        std::pair{          AuthoredFloatField::CameraNear, 1000.f},
        std::pair{   AuthoredFloatField::CameraAspectRatio,   -1.f},
        std::pair{       AuthoredFloatField::RigidBodyMass,    0.f},
        std::pair{   AuthoredFloatField::RigidBodyFriction,   -1.f},
        std::pair{AuthoredFloatField::RigidBodyRestitution,  -0.1f},
        std::pair{AuthoredFloatField::RigidBodyRestitution,   1.1f},
    };
    for (const auto& [field, invalid] : cases)
    {
        SCOPED_TRACE(ComponentFieldKey(field));
        float before = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, before));
        ASSERT_TRUE(session.BeginField(session.TargetFor(entity), field));
        ASSERT_TRUE(session.PreviewField(before + .125f));
        EXPECT_FALSE(session.PreviewField(invalid));
        EXPECT_FALSE(session.PreviewField(std::numeric_limits<float>::quiet_NaN()));
        float after = 0.f;
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, after));
        EXPECT_FLOAT_EQ(after, before + .125f);
        session.Cancel();
        ASSERT_TRUE(ReadAuthoredFloatField(scene, entity, field, after));
        EXPECT_EQ(after, before);
        EXPECT_FALSE(session.Dirty());
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->BodyID, 1729u);
    }
}

TEST_F(EditorSessionTest, RepeatedAdditionalNumericDragsReuseStorageAndCommitOnlyOncePerRelease)
{
    const auto entity = Entity(1);
    scene.AddComponent<RigidBodyComponent>(entity, {.BodyID = 12u});
    ASSERT_TRUE(session.TargetFor(entity).CachedEntity.IsValid());
    scene.SynchronizeHierarchy();
    const auto              offset = memory.MainArena.m_current_offset;
    const EditorControlDrag drag   = {.Key = 42, .UnitsPerPixel = .125f, .Field = AuthoredFloatField::RigidBodyMass, .IsTransform = false};
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.RequestControlDrag(entity, drag));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.UpdateControlDrag(1.f, true));
        ASSERT_TRUE(session.UpdateControlDrag(2.f, false));
        EXPECT_EQ(session.PendingRequestCount(), 0u);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.History().UndoCount(), 8u);
    EXPECT_FLOAT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->Mass, 251.f);
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->BodyID, 12u);
}

class EditorAddComponentTest : public EditorSessionTest
{
protected:
    void SetUp() override
    {
        // Reflection metadata is process-owned, never backed by a fixture's arena.
        struct ReflectionOwner
        {
            ZEngine::Core::Memory::MemoryManager Memory = {};
            ReflectionOwner()
            {
                Memory.Initialize(ZMega(1), {});
                ComponentReflectionRegistry::Get().Initialize(&Memory.MainArena);
                RegisterBuiltInComponentReflection();
            }
        };
        static ReflectionOwner owner = {};
        EditorSessionTest::SetUp();
    }
};

TEST_F(EditorAddComponentTest, QueuedAddUsesTypedDefaultsAndCancelsBeforeChangingTheArchetype)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 9.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestAddComponent(entity, "RigidBodyComponent"));
    EXPECT_FALSE(scene.HasComponent<RigidBodyComponent>(entity));
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 9.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    const auto* body = scene.GetComponent<RigidBodyComponent>(entity);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->MotionKind, RigidBodyComponent::MotionType::Dynamic);
    EXPECT_FLOAT_EQ(body->Mass, 1.f);
    EXPECT_FLOAT_EQ(body->Friction, .5f);
    EXPECT_FLOAT_EQ(body->Restitution, 0.f);
    EXPECT_EQ(body->BodyID, std::numeric_limits<uint32_t>::max());
    for (const auto pad : body->_pad)
    {
        EXPECT_EQ(pad, 0u);
    }
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorAddComponentTest, IndependentAddsAreNotDiscardedWhenAnEarlierAddInvalidatesHistory)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(session.RequestAddComponent(entity, "CameraComponent"));
    ASSERT_TRUE(session.RequestAddComponent(entity, "LightComponent"));
    ASSERT_TRUE(session.RequestAddComponent(entity, "NameComponent"));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(scene.HasComponent<CameraComponent>(entity));
    EXPECT_TRUE(scene.HasComponent<LightComponent>(entity));
    ASSERT_NE(scene.GetComponent<NameComponent>(entity), nullptr);
    EXPECT_STREQ(scene.GetComponent<NameComponent>(entity)->Value, "");
}

TEST_F(EditorAddComponentTest, InvalidDuplicateAndStaleAddsCannotMutateOrCancelAValidPreview)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    EXPECT_FALSE(session.RequestAddComponent(entity, "TransformComponent"));
    EXPECT_FALSE(session.RequestAddComponent(entity, "NotRegistered"));
    EXPECT_FALSE(session.RequestAddComponent(entity, ""));
    EXPECT_FALSE(session.RequestAddComponent(entity, std::string(128, 'x')));
    EXPECT_FALSE(session.RequestAddComponent(entity, std::string_view("Light\0Component", 15)));
    EXPECT_TRUE(session.Active());
    const auto stale = Entity(2);
    ASSERT_TRUE(session.RequestAddComponent(stale, "LightComponent"));
    scene.DestroyEntity(stale);
    const auto replacement = Entity(2);
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(scene.HasComponent<LightComponent>(replacement));
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
}

TEST_F(EditorAddComponentTest, ReadOnlyAndNewSceneRejectQueuedAdds)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(session.RequestAddComponent(entity, "CameraComponent"));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(scene.HasComponent<CameraComponent>(entity));
    EXPECT_FALSE(session.Dirty());
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.RequestAddComponent(entity, "CameraComponent"));
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(scene.HasComponent<CameraComponent>(entity));
}

TEST_F(EditorSessionTest, UntrackedPropertiesCancelBeforeMutationAndPreserveEveryUnrelatedValue)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {.Intensity = 4.f});
    scene.AddComponent<CameraComponent>(entity, {.FovY = 90.f});
    scene.AddComponent<RigidBodyComponent>(entity, {.Mass = 3.f, .BodyID = 42u});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 9.f));
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestUntrackedProperty(
        entity,
        EditorUntrackedProperty::LightColor,
        {
        .Color = {.18f, 4.5f, .25f}
    }));
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {.MainCamera = true}));
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::RigidBodyMotion, {.Motion = RigidBodyComponent::MotionType::Static}));
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Color[0], 1.f);
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_EQ(scene.GetComponent<RigidBodyComponent>(entity)->MotionKind, RigidBodyComponent::MotionType::Dynamic);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_TRUE(session.Dirty());
    const auto* light = scene.GetComponent<LightComponent>(entity);
    EXPECT_FLOAT_EQ(light->Color[0], .18f);
    EXPECT_FLOAT_EQ(light->Color[1], 4.5f);
    EXPECT_FLOAT_EQ(light->Color[2], .25f);
    EXPECT_FLOAT_EQ(light->Intensity, 4.f);
    const auto* camera = scene.GetComponent<CameraComponent>(entity);
    EXPECT_TRUE(camera->IsMain);
    EXPECT_FLOAT_EQ(camera->FovY, 90.f);
    const auto* body = scene.GetComponent<RigidBodyComponent>(entity);
    EXPECT_EQ(body->MotionKind, RigidBodyComponent::MotionType::Static);
    EXPECT_FLOAT_EQ(body->Mass, 3.f);
    EXPECT_EQ(body->BodyID, 42u);
}

TEST_F(EditorSessionTest, NoOpUntrackedPropertiesDoNotCancelPreviewInvalidateHistoryOrDirtyTheCheckpoint)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {});
    scene.AddComponent<CameraComponent>(entity, {});
    scene.AddComponent<RigidBodyComponent>(entity, {});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(session.MarkSaved());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 8.f));
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::LightColor, {}));
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {}));
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::RigidBodyMotion, {}));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 8.f);
    session.Cancel();
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, InvalidAndStaleUntrackedPropertiesCannotCancelOrOverwriteCurrentContent)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {});
    scene.AddComponent<RigidBodyComponent>(entity, {});
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 4.f));
    EXPECT_FALSE(session.RequestUntrackedProperty(
        entity,
        EditorUntrackedProperty::LightColor,
        {
        .Color = {-1.f, 1.f, 1.f}
    }));
    EXPECT_FALSE(session.RequestUntrackedProperty(
        entity,
        EditorUntrackedProperty::LightColor,
        {
        .Color = {1.f, std::numeric_limits<float>::infinity(), 1.f}
    }));
    EXPECT_FALSE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::RigidBodyMotion, {.Motion = static_cast<RigidBodyComponent::MotionType>(99)}));
    EXPECT_FALSE(session.RequestUntrackedProperty(entity, static_cast<EditorUntrackedProperty>(99), {}));
    EXPECT_FALSE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {}));
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.RequestUntrackedProperty(
        entity,
        EditorUntrackedProperty::LightColor,
        {
        .Color = {.5f, 1.f, 1.f}
    }));
    scene.GetComponent<LightComponent>(entity)->Color[0] = .75f;
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FLOAT_EQ(scene.GetComponent<LightComponent>(entity)->Color[0], .75f);
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 4.f);
    EXPECT_TRUE(session.Active());
}

TEST_F(EditorSessionTest, ReadOnlyAndCancelledUntrackedInputCannotApplyButIndependentSelectionSurvives)
{
    const auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {.MainCamera = true}));
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_FALSE(session.Dirty());
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {.MainCamera = true}));
    ASSERT_TRUE(session.RequestSelection(entity));
    ASSERT_TRUE(session.DiscardEditRequests());
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RecycledAndReboundUntrackedTargetsNeverChangeReplacementEntities)
{
    auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {.MainCamera = true}));
    scene.DestroyEntity(entity);
    entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    ASSERT_TRUE(session.RequestUntrackedProperty(entity, EditorUntrackedProperty::CameraMain, {.MainCamera = true}));
    ASSERT_TRUE(session.BindScene(&scene));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(scene.GetComponent<CameraComponent>(entity)->IsMain);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RepeatedUntrackedPropertiesReuseQueueAndHierarchyStorage)
{
    const auto entity = Entity(1);
    scene.AddComponent<LightComponent>(entity, {});
    ASSERT_TRUE(session.TargetFor(entity).CachedEntity.IsValid());
    scene.SynchronizeHierarchy();
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.RequestUntrackedProperty(
            entity,
            EditorUntrackedProperty::LightColor,
            {
            .Color = {i % 2 ? .5f : .75f, 4.5f, .18f}
        }));
        ASSERT_TRUE(session.ProcessRequests());
        EXPECT_EQ(session.PendingRequestCount(), 0u);
        EXPECT_EQ(session.History().UndoCount(), 0u);
        EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    }
}

TEST_F(EditorSessionTest, InsufficientHistoryCapacityRejectsBeforeTheFirstPreview)
{
    EditorSession limited = {};
    ASSERT_TRUE(limited.Initialize(&memory.MainArena, {.MaxTargets = 8, .HistoryEntries = 8, .HistoryBytes = 128}));
    ASSERT_TRUE(limited.BeginMutationPhase());
    ASSERT_TRUE(limited.BindScene(&scene));
    const auto entity = Entity(1);
    const auto target = limited.TargetFor(entity);
    EXPECT_FALSE(limited.BeginTransform({&target, 1}));
    EXPECT_FALSE(limited.Active());
    EXPECT_FALSE(limited.Dirty());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    limited.Shutdown();
}

TEST_F(EditorSessionTest, InvalidCandidatePreservesTheLastValidPreviewAndHistoryState)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    EXPECT_FALSE(Preview(entity, std::numeric_limits<float>::infinity()));
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 1.f);
    EXPECT_TRUE(session.Active());
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
}

TEST_F(EditorSessionTest, UndoCancelsTheCurrentPreviewBeforeRevertingTheLastCommittedEdit)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    ASSERT_TRUE(session.Commit());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 2.f));
    ASSERT_TRUE(session.Undo());
    EXPECT_FLOAT_EQ(Value(entity).Position.x, 0.f);
    EXPECT_FALSE(session.Active());
    EXPECT_EQ(session.History().RedoCount(), 1u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RemovedFieldComponentCancelsSafelyOnTheNextUpdate)
{
    const auto entity = Entity(1);
    scene.AddComponent<CameraComponent>(entity, {});
    ASSERT_TRUE(session.BeginField(session.TargetFor(entity), AuthoredFloatField::CameraFar));
    ASSERT_TRUE(session.PreviewField(2000.f));
    session.EndMutationPhase();
    scene.RemoveComponent<CameraComponent>(entity);
    ASSERT_TRUE(session.BeginMutationPhase());
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, RestoringSavedContentThroughANewEditClearsDirtyWithoutDiscardingHistory)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 0.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 2u);
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(session.Redo());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SavedContentFingerprintSurvivesHistoryEvictionAndFieldEdits)
{
    const auto      entity = Entity(1);
    CameraComponent camera = {};
    scene.AddComponent<CameraComponent>(entity, camera);
    for (int i = 1; i <= 20; ++i)
    {
        ASSERT_TRUE(Begin(entity));
        ASSERT_TRUE(Preview(entity, static_cast<float>(i)));
        ASSERT_TRUE(session.Commit());
    }
    ASSERT_TRUE(session.BeginField(session.TargetFor(entity), AuthoredFloatField::CameraFar));
    ASSERT_TRUE(session.PreviewField(2000.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_TRUE(session.Dirty());
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 0.f));
    ASSERT_TRUE(session.Commit());
    EXPECT_TRUE(session.Dirty()); // camera field still differs from saved content
    ASSERT_TRUE(session.BeginField(session.TargetFor(entity), AuthoredFloatField::CameraFar));
    ASSERT_TRUE(session.PreviewField(camera.Far));
    ASSERT_TRUE(session.Commit());
    EXPECT_FALSE(session.Dirty());
    ASSERT_TRUE(session.Undo());
    EXPECT_TRUE(session.Dirty());
}

TEST_F(EditorSessionTest, UnexpectedTargetLossCannotRestoreACleanCheckpoint)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(Begin(entity));
    ASSERT_TRUE(Preview(entity, 1.f));
    scene.DestroyEntity(entity);
    session.Cancel();
    EXPECT_FALSE(session.Active());
    EXPECT_TRUE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
    ASSERT_TRUE(session.MarkSaved());
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SelectionRangeCopiesEveryTargetAndAppliesOnlyAtTheMutationPoint)
{
    const auto                  first = Entity(1), second = Entity(2);
    FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(first), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.Select(first));
    const auto revision = session.Selection().Revision();
    session.EndMutationPhase();
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    range = FixedArray<EntityTarget, 2>{};
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Revision(), revision);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_EQ(session.Selection().Entities().size(), 2u);
    EXPECT_EQ(session.Selection().Entities()[0].UUID, Identity(1));
    EXPECT_EQ(session.Selection().Entities()[1].UUID, Identity(2));
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    EXPECT_EQ(session.Selection().Revision(), revision + 1);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SelectionRangeRestoresTheActivePreviewBeforePublishingTheWholeSelection)
{
    const auto                        first = Entity(1), second = Entity(2), third = Entity(3);
    const FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(third), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.Select(first));
    ASSERT_TRUE(Begin(first));
    ASSERT_TRUE(Preview(first, 7.f));
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    ASSERT_EQ(session.Selection().Entities().size(), 2u);
    EXPECT_EQ(session.Selection().Entities()[0].UUID, Identity(3));
    EXPECT_EQ(session.Selection().Entities()[1].UUID, Identity(2));
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, InvalidSelectionSetAndOverCapacityRangeDoNotCancelTheLivePreview)
{
    FixedArray<EntityTarget, 8> all = {};
    for (uint8_t i = 0; i < all.size(); ++i)
    {
        all[i] = session.TargetFor(Entity(i + 1));
    }
    const auto extra = session.TargetFor(Entity(9));
    ASSERT_TRUE(session.SetSelection({all.data(), all.size()}, all[0].UUID));
    ASSERT_TRUE(Begin(all[0].CachedEntity));
    ASSERT_TRUE(Preview(all[0].CachedEntity, 3.f));
    const auto                        revision = session.Selection().Revision();
    const FixedArray<EntityTarget, 2> invalid  = {
        {all[0], {}}
    };
    EXPECT_FALSE(session.SetSelection({invalid.data(), invalid.size()}));
    EXPECT_TRUE(session.Active());
    ASSERT_TRUE(session.RequestSelectionRange({&extra, 1}, extra.UUID, true));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(Value(all[0].CachedEntity).Position.x, 3.f);
    EXPECT_EQ(session.Selection().Revision(), revision);
    EXPECT_EQ(session.Selection().Entities().size(), all.size());
    EXPECT_FALSE(session.Selection().Contains(extra.UUID));
    session.Cancel();
    EXPECT_FLOAT_EQ(Value(all[0].CachedEntity).Position.x, 0.f);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, RejectedSingleRowSelectionDoesNotCancelAnOtherwiseValidPreview)
{
    FixedArray<EntityTarget, 8> all = {};
    for (uint8_t i = 0; i < all.size(); ++i)
    {
        all[i] = session.TargetFor(Entity(i + 1));
    }
    const auto extra = Entity(9);
    ASSERT_TRUE(session.SetSelection({all.data(), all.size()}, all[0].UUID));
    ASSERT_TRUE(Begin(all[0].CachedEntity));
    ASSERT_TRUE(Preview(all[0].CachedEntity, 3.f));
    const auto revision = session.Selection().Revision();
    EXPECT_FALSE(session.Select(INVALID_ENTITY));
    EXPECT_FALSE(session.Select(extra, static_cast<SelectionMode>(255)));
    EXPECT_FALSE(session.Select(extra, SelectionMode::Add));
    ASSERT_TRUE(session.RequestSelection(extra, SelectionMode::Toggle));
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_TRUE(session.Active());
    EXPECT_FLOAT_EQ(Value(all[0].CachedEntity).Position.x, 3.f);
    EXPECT_EQ(session.Selection().Revision(), revision);
    ASSERT_TRUE(session.RequestSelection(all[1].CachedEntity, SelectionMode::Toggle));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_FALSE(session.Active());
    EXPECT_FLOAT_EQ(Value(all[0].CachedEntity).Position.x, 0.f);
    EXPECT_FALSE(session.Selection().Contains(all[1].UUID));
    EXPECT_EQ(session.Selection().Entities().size(), all.size() - 1);
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(EditorSessionTest, AdditiveRangesRespectQueuedCommandOrderAndDoNotReintroduceRemovedTargetsEarly)
{
    const auto                        first = Entity(1), second = Entity(2), third = Entity(3), fourth = Entity(4);
    const FixedArray<EntityTarget, 2> a = {
        {session.TargetFor(second), session.TargetFor(third)}
    };
    const FixedArray<EntityTarget, 2> b = {
        {session.TargetFor(fourth), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.Select(first));
    ASSERT_TRUE(session.RequestSelectionRange(a, a[1].UUID, true));
    ASSERT_TRUE(session.RequestSelection(second, SelectionMode::Toggle));
    ASSERT_TRUE(session.RequestSelectionRange(b, b[1].UUID, true));
    ASSERT_TRUE(session.ProcessRequests());
    ASSERT_EQ(session.Selection().Entities().size(), 4u);
    EXPECT_EQ(session.Selection().Entities()[0].UUID, Identity(1));
    EXPECT_EQ(session.Selection().Entities()[1].UUID, Identity(3));
    EXPECT_EQ(session.Selection().Entities()[2].UUID, Identity(4));
    EXPECT_EQ(session.Selection().Entities()[3].UUID, Identity(2));
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, StaleAndDuplicateRangeTargetsRejectTheWholeRequestInsteadOfSelectingTheirReplacements)
{
    const auto                  first = Entity(1), second = Entity(2), third = Entity(3);
    FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(second), session.TargetFor(third)}
    };
    ASSERT_TRUE(session.Select(first));
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    scene.DestroyEntity(third);
    const auto replacement = Entity(3);
    EXPECT_NE(replacement, third);
    EXPECT_FALSE(session.ProcessRequests());
    ASSERT_EQ(session.Selection().Entities().size(), 1u);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    range[1] = session.TargetFor(replacement);
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    const auto duplicate = Entity(2);
    EXPECT_FALSE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    scene.DestroyEntity(duplicate);
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
}

TEST_F(EditorSessionTest, InvalidRangeEnqueueNeverConsumesPayloadBudgetOrChangesSelection)
{
    const auto                        first = Entity(1), second = Entity(2);
    const FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(first), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.Select(first));
    EXPECT_FALSE(session.RequestSelectionRange({}, range[0].UUID));
    EXPECT_FALSE(session.RequestSelectionRange(range, Identity(9)));
    const FixedArray<EntityTarget, 2> invalid = {
        {range[0], {}}
    };
    EXPECT_FALSE(session.RequestSelectionRange(invalid, range[0].UUID));
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
}

TEST_F(EditorSessionTest, SelectionPayloadBudgetAndFullCommandQueueFailWithoutPartialRequestsAndRecoverAfterDiscard)
{
    FixedArray<EntityTarget, 8> range = {};
    for (uint8_t i = 0; i < range.size(); ++i)
    {
        range[i] = session.TargetFor(Entity(i + 1));
    }
    ASSERT_TRUE(session.RequestSelectionRange(range, range[0].UUID));
    EXPECT_FALSE(session.RequestSelectionRange({range.data(), 1}, range[0].UUID));
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Entities().size(), range.size());
    for (uint32_t i = 0; i < EditorSession::MaxPendingRequests; ++i)
    {
        ASSERT_TRUE(session.Request(EditorCommand::Undo));
    }
    EXPECT_FALSE(session.RequestSelectionRange(range, range[0].UUID));
    EXPECT_EQ(session.PendingRequestCount(), EditorSession::MaxPendingRequests);
    session.DiscardRequests();
    ASSERT_TRUE(session.RequestSelectionRange(range, range[0].UUID));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Entities().size(), range.size());
}

TEST_F(EditorSessionTest, EditRequestDiscardKeepsTheCopiedSelectionRangeAndItsEndpoints)
{
    const auto          first = Entity(1), second = Entity(2);
    const auto          target = session.TargetFor(second);
    LocalTransformValue value  = {};
    value.Position.x           = 6.f;
    ASSERT_TRUE(session.RequestTransformValue(first, value));
    ASSERT_TRUE(session.RequestSelectionRange({&target, 1}, target.UUID));
    ASSERT_TRUE(session.RequestTransformValue(second, value));
    ASSERT_TRUE(session.DiscardEditRequests());
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), target.UUID);
    EXPECT_FLOAT_EQ(Value(first).Position.x, 0.f);
    EXPECT_FLOAT_EQ(Value(second).Position.x, 0.f);
}

TEST_F(EditorSessionTest, SceneEpochChangesDiscardCopiedRangesAndPermitFreshRequests)
{
    const auto                  first = Entity(1), second = Entity(2);
    FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(first), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    ASSERT_TRUE(session.BindScene(nullptr));
    ASSERT_TRUE(session.BindScene(&scene));
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_FALSE(session.RequestSelectionRange(range, range[1].UUID));
    range = {
        {session.TargetFor(first), session.TargetFor(second)}
    };
    ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().Entities().size(), 2u);
    EXPECT_EQ(session.Selection().Entities()[0].SceneEpoch, session.SceneEpoch());
}

TEST_F(EditorSessionTest, RepeatedRangeEnqueueProcessAndDiscardReuseBoundedStorage)
{
    const auto                        first = Entity(1), second = Entity(2);
    const FixedArray<EntityTarget, 2> range = {
        {session.TargetFor(first), session.TargetFor(second)}
    };
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(session.RequestSelectionRange(range, range[1].UUID, (i & 1) != 0));
        ASSERT_TRUE(session.ProcessRequests());
        ASSERT_TRUE(session.RequestSelectionRange(range, range[0].UUID));
        session.DiscardRequests();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    EXPECT_EQ(session.History().UndoCount(), 0u);
    EXPECT_FALSE(session.Dirty());
}

TEST_F(EditorSessionTest, SelectionRangeRejectsWrongThreadAndAllowsReadOnlySelectionWithoutAuthoredChanges)
{
    const auto  target   = session.TargetFor(Entity(1));
    bool        accepted = true;
    std::thread worker([&] { accepted = session.RequestSelectionRange({&target, 1}, target.UUID); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_EQ(session.PendingRequestCount(), 0u);
    session.EndMutationPhase();
    ASSERT_TRUE(session.BeginMutationPhase(false));
    ASSERT_TRUE(session.RequestSelectionRange({&target, 1}, target.UUID));
    ASSERT_TRUE(session.ProcessRequests());
    EXPECT_EQ(session.Selection().ActiveUUID(), target.UUID);
    EXPECT_FALSE(session.Editable());
    EXPECT_FALSE(session.Dirty());
}
