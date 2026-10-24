#include <Tetragrama/EditorSession.h>
#include <Tetragrama/SelectedRootCache.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <gtest/gtest.h>
#include <initializer_list>
#include <limits>
#include <thread>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using ZEngine::Core::Containers::ArrayView;
using ZEngine::Core::Containers::FixedArray;

class SelectedRootCacheTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory  = {};
    Scene                                scene   = {};
    ActorManager                         actors  = {};
    EditorSession                        session = {};
    SelectedRootCache                    cache   = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, scene);
        ASSERT_TRUE(session.Initialize(&memory.MainArena, {.MaxTargets = 4096, .HistoryEntries = 8, .HistoryBytes = ZMega(1)}));
        ASSERT_TRUE(cache.Initialize(&memory.MainArena, 4096));
        ASSERT_TRUE(session.BeginMutationPhase());
        ASSERT_TRUE(session.BindScene(&scene));
    }
    void TearDown() override
    {
        session.Shutdown();
        actors.Shutdown();
        scene.Shutdown();
    }
    static uuids::uuid Identity(uint32_t value)
    {
        FixedArray<uint8_t, 16> bytes = {};
        for (uint32_t i = 0; i < 4; ++i)
        {
            bytes[15 - i] = uint8_t(value >> (i * 8));
        }
        return uuids::uuid(bytes.begin(), bytes.end());
    }
    EntityID Entity(uint32_t value, bool transform = true)
    {
        const auto entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {Identity(value)});
        if (transform)
        {
            scene.AddComponent<TransformComponent>(entity, {});
        }
        return entity;
    }
    void Choose(std::initializer_list<EntityID> entities, EntityID active = {})
    {
        FixedArray<EntityTarget, 16> targets = {};
        ASSERT_LE(entities.size(), targets.size());
        uint32_t count = 0;
        for (const auto entity : entities)
        {
            targets[count++] = session.TargetFor(entity);
        }
        ASSERT_TRUE(session.SetSelection({targets.data(), count}, active.IsValid() ? session.TargetFor(active).UUID : uuids::uuid{}));
        scene.SynchronizeHierarchy();
    }
};

TEST_F(SelectedRootCacheTest, ParentPruningCrossesUnselectedAndTransformlessIntermediatesWithoutChangingActiveChild)
{
    const auto child = Entity(1), independent = Entity(2), group = Entity(3, false), parent = Entity(4);
    ASSERT_TRUE(scene.SetParent(child, group));
    ASSERT_TRUE(scene.SetParent(group, parent));
    Choose({child, independent, parent}, child);
    const auto                    revision = session.Selection().Revision();
    ArrayView<const EntityTarget> roots    = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].CachedEntity, independent);
    EXPECT_EQ(roots[1].CachedEntity, parent);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    EXPECT_EQ(session.Selection().Revision(), revision);
    EXPECT_EQ(session.Selection().Entities().size(), 3u);
}

TEST_F(SelectedRootCacheTest, SiblingsAreSeparateRootsWhenTheirParentIsNotSelected)
{
    const auto first = Entity(1), parent = Entity(2), second = Entity(3);
    ASSERT_TRUE(scene.SetParent(first, parent));
    ASSERT_TRUE(scene.SetParent(second, parent));
    Choose({second, first});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(session.ReadSelectedRoots(roots));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].CachedEntity, second);
    EXPECT_EQ(roots[1].CachedEntity, first);
}

TEST_F(SelectedRootCacheTest, ActorAndPureECSLightCameraAndEmptyTargetsUseTheSamePath)
{
    const auto actor_handle = actors.Create();
    auto*      actor        = actors.Access(actor_handle);
    actor->AddComponent<UUIDComponent>({Identity(1)});
    const auto light = Entity(2), camera = Entity(3), empty = Entity(4);
    scene.AddComponent<LightComponent>(light, {});
    scene.AddComponent<CameraComponent>(camera, {});
    Choose({camera, actor->GetEntityID(), light, empty});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 4u);
    for (uint32_t i = 0; i < roots.size(); ++i)
    {
        EXPECT_EQ(roots[i].UUID, session.Selection().Entities()[i].UUID);
    }
}

TEST_F(SelectedRootCacheTest, TopologyPruningDoesNotSilentlyDiscardTargetsWithoutTransforms)
{
    const auto group = Entity(1, false), child = Entity(2), other = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, group));
    Choose({other, child, group}, child);
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[1].CachedEntity, group);
    EXPECT_FALSE(session.BeginTransform({roots.data(), roots.size()}));
    EXPECT_FALSE(session.Active());
    EXPECT_FALSE(session.Dirty());
    EXPECT_EQ(session.History().UndoCount(), 0u);
}

TEST_F(SelectedRootCacheTest, UnchangedReadsAndWorldPoseChangesReuseTheRootSetWithoutAllocation)
{
    const auto parent = Entity(1), child = Entity(2);
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({child, parent}, child);
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    const auto* data   = roots.data();
    const auto  builds = cache.BuildCount();
    const auto  offset = memory.MainArena.m_current_offset;
    session.EndMutationPhase();
    for (uint32_t i = 0; i < 1000; ++i)
    {
        scene.GetComponent<TransformComponent>(parent)->Position.x = float(i);
        scene.SynchronizeHierarchy();
        ASSERT_TRUE(cache.Read(session, roots));
        ASSERT_EQ(roots.size(), 1u);
        EXPECT_EQ(roots.data(), data);
        EXPECT_EQ(roots[0].CachedEntity, parent);
    }
    EXPECT_EQ(cache.BuildCount(), builds);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(SelectedRootCacheTest, SelectionAndActiveRevisionChangesRefreshDeterministically)
{
    const auto parent = Entity(1), child = Entity(2);
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({parent, child}, child);
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    const auto builds = cache.BuildCount();
    ASSERT_TRUE(session.Select(parent, SelectionMode::Add));
    ASSERT_TRUE(cache.Read(session, roots));
    EXPECT_EQ(cache.BuildCount(), builds + 1);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(1));
    ASSERT_TRUE(session.Select(parent, SelectionMode::Toggle));
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, child);
    EXPECT_EQ(cache.BuildCount(), builds + 2);
}

TEST_F(SelectedRootCacheTest, ReparentInvalidatesBeforeSynchronizationAndNeverPublishesStaleOrPartialRoots)
{
    const auto parent = Entity(1), child = Entity(2);
    Choose({child, parent});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    const auto         builds   = cache.BuildCount();
    const EntityTarget sentinel = {.UUID = Identity(77)};
    roots                       = {&sentinel, 1};
    ASSERT_TRUE(scene.SetParent(child, parent));
    EXPECT_FALSE(cache.Read(session, roots));
    EXPECT_EQ(roots.data(), &sentinel);
    EXPECT_EQ(roots.size(), 1u);
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, parent);
    EXPECT_EQ(cache.BuildCount(), builds + 1);
}

TEST_F(SelectedRootCacheTest, RawLegacyParentChangesAreDetectedAfterHierarchySynchronization)
{
    const auto parent = Entity(1), child = Entity(2);
    scene.AddComponent<ParentComponent>(child, {});
    Choose({child, parent});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    scene.GetComponent<ParentComponent>(child)->Parent = parent;
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, parent);
}

TEST_F(SelectedRootCacheTest, InvalidSelectedTopologyRejectsTheWholeSetWhileUnselectedInvalidBranchesDoNot)
{
    const auto valid = Entity(1), a = Entity(2), b = Entity(3);
    scene.AddComponent<ParentComponent>(a, {b});
    scene.AddComponent<ParentComponent>(b, {a});
    Choose({valid, a});
    const EntityTarget            sentinel = {.UUID = Identity(77)};
    ArrayView<const EntityTarget> roots    = {&sentinel, 1};
    EXPECT_FALSE(cache.Read(session, roots));
    EXPECT_EQ(roots.data(), &sentinel);
    EXPECT_EQ(cache.BuildCount(), 0u);
    Choose({valid});
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, valid);
    ASSERT_TRUE(scene.SetParent(a, INVALID_ENTITY));
    Choose({b, a}, b);
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, a);
}

TEST_F(SelectedRootCacheTest, DeletedAndDuplicateTargetsCannotReuseCachedIdentityAndRestorationRefreshesGenerations)
{
    const auto original = Entity(1), other = Entity(2);
    Choose({other, original});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    scene.DestroyEntity(original);
    scene.SynchronizeHierarchy();
    EXPECT_FALSE(cache.Read(session, roots));
    const auto replacement = Entity(1);
    EXPECT_EQ(replacement.Index, original.Index);
    EXPECT_NE(replacement.Generation, original.Generation);
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[1].CachedEntity, replacement);
    const auto duplicate = Entity(1);
    scene.SynchronizeHierarchy();
    EXPECT_FALSE(cache.Read(session, roots));
    scene.DestroyEntity(duplicate);
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(cache.Read(session, roots));
    EXPECT_EQ(roots[1].CachedEntity, replacement);
}

TEST_F(SelectedRootCacheTest, EmptyGridAndSceneEpochChangesRetireAllEntityRoots)
{
    const auto entity = Entity(1);
    Choose({entity});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_TRUE(session.SelectScene(SceneTarget::Grid));
    ASSERT_TRUE(cache.Read(session, roots));
    EXPECT_TRUE(roots.empty());
    EXPECT_EQ(session.Selection().ActiveSceneTarget(), SceneTarget::Grid);
    ASSERT_TRUE(session.BindScene(&scene));
    ASSERT_TRUE(cache.Read(session, roots));
    EXPECT_TRUE(roots.empty());
    ASSERT_TRUE(session.Select(entity));
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].SceneEpoch, session.SceneEpoch());
    session.ClearSelection();
    ASSERT_TRUE(cache.Read(session, roots));
    EXPECT_TRUE(roots.empty());
    ASSERT_TRUE(session.BindScene(nullptr));
    EXPECT_FALSE(cache.Read(session, roots));
}

TEST_F(SelectedRootCacheTest, CapacityAndInitializationFailuresDoNotTruncateOrConsumeFailedReservations)
{
    SelectedRootCache limited = {};
    EXPECT_FALSE(limited.Initialize(nullptr, 1));
    EXPECT_FALSE(limited.Initialize(&memory.MainArena, 0));
    EXPECT_FALSE(limited.Initialize(&memory.MainArena, EntityRegistry::MAX_ENTITIES + 1));
    ASSERT_TRUE(limited.Initialize(&memory.MainArena, 1));
    EXPECT_FALSE(limited.Initialize(&memory.MainArena, 1));
    Choose({Entity(1), Entity(2)});
    const EntityTarget            sentinel = {.UUID = Identity(77)};
    ArrayView<const EntityTarget> roots    = {&sentinel, 1};
    EXPECT_FALSE(limited.Read(session, roots));
    EXPECT_EQ(roots.data(), &sentinel);
    EXPECT_EQ(limited.BuildCount(), 0u);
    alignas(16) FixedArray<std::byte, 128> bytes = {};
    ZEngine::Core::Memory::ArenaAllocator  tiny  = {};
    ASSERT_TRUE(tiny.InitializeBorrowed(bytes.data(), bytes.size(), "SelectedRootCacheTest"));
    const auto        offset = tiny.m_current_offset;
    SelectedRootCache failed = {};
    EXPECT_FALSE(failed.Initialize(&tiny, 1));
    EXPECT_EQ(tiny.m_current_offset, offset);
    EXPECT_FALSE(failed.Read(session, roots));
    EXPECT_EQ(roots.data(), &sentinel);
    ASSERT_TRUE(failed.Initialize(&memory.MainArena, 2));
    ASSERT_TRUE(failed.Read(session, roots));
    EXPECT_EQ(roots.size(), 2u);
}

TEST_F(SelectedRootCacheTest, OwnerThreadAndPendingHierarchyGuardsPreserveCallerOutput)
{
    const auto entity = Entity(1);
    Choose({entity});
    const EntityTarget            sentinel = {.UUID = Identity(77)};
    ArrayView<const EntityTarget> roots    = {&sentinel, 1};
    bool                          accepted = true;
    std::thread                   worker([&] { accepted = session.ReadSelectedRoots(roots); });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_EQ(roots.data(), &sentinel);
    const auto target = session.TargetFor(entity);
    ASSERT_TRUE(session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 3.f;
    ASSERT_TRUE(session.PreviewTransforms({&value, 1}));
    EXPECT_FALSE(session.ReadSelectedRoots(roots));
    EXPECT_EQ(roots.data(), &sentinel);
    session.EndMutationPhase();
    ASSERT_TRUE(session.ReadSelectedRoots(roots));
    EXPECT_EQ(roots[0].CachedEntity, entity);
}

TEST_F(SelectedRootCacheTest, DeepFullSelectionUsesOneRootAndKeepsTheExplicitActiveLeaf)
{
    constexpr uint32_t              count    = 2048;
    FixedArray<EntityTarget, count> targets  = {};
    EntityID                        previous = {}, first = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto entity = Entity(i + 1);
        if (previous.IsValid())
        {
            scene.AddComponent<ParentComponent>(entity, {previous});
        }
        else
        {
            first = entity;
        }
        targets[i] = {.UUID = Identity(i + 1), .CachedEntity = entity, .SceneEpoch = session.SceneEpoch()};
        previous   = entity;
    }
    ASSERT_TRUE(session.SetSelection({targets.data(), targets.size()}, targets[count - 1].UUID));
    scene.SynchronizeHierarchy();
    const auto                    offset = memory.MainArena.m_current_offset;
    ArrayView<const EntityTarget> roots  = {};
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 1u);
    EXPECT_EQ(roots[0].CachedEntity, first);
    EXPECT_EQ(session.Selection().ActiveUUID(), targets[count - 1].UUID);
    EXPECT_EQ(session.Selection().Entities().size(), count);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(SelectedRootCacheTest, FullCapacityIndependentRootsPreserveEveryTargetWithinTheReservationBound)
{
    constexpr uint32_t              count   = 4096;
    FixedArray<EntityTarget, count> targets = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        const auto entity      = Entity(i + 1);
        targets[count - i - 1] = {.UUID = Identity(i + 1), .CachedEntity = entity, .SceneEpoch = session.SceneEpoch()};
    }
    ASSERT_TRUE(session.RequestSelectionRange(targets, targets[0].UUID));
    EXPECT_EQ(session.PendingRequestCount(), 1u);
    EXPECT_TRUE(session.Selection().Entities().empty());
    ASSERT_TRUE(session.ProcessRequests());
    scene.SynchronizeHierarchy();
    SelectedRootCache bounded = {};
    const auto        before  = memory.MainArena.m_current_offset;
    ASSERT_TRUE(bounded.Initialize(&memory.MainArena, count));
    const auto offset = memory.MainArena.m_current_offset;
    EXPECT_LE(offset - before, sizeof(EntityTarget) * count + EntityRegistry::MAX_ENTITIES + alignof(EntityTarget) - 1);
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(bounded.Read(session, roots));
    ASSERT_EQ(roots.size(), count);
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(roots[i].UUID, targets[i].UUID);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(session.Selection().ActiveUUID(), targets[0].UUID);
}

TEST_F(SelectedRootCacheTest, ARecycledParentSlotDoesNotTurnADanglingChildIntoAnEditableRoot)
{
    const auto parent = Entity(1), child = Entity(2), other = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, parent));
    Choose({other, child});
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(cache.Read(session, roots));
    scene.DestroyEntity(parent);
    const auto replacement = Entity(4);
    EXPECT_EQ(replacement.Index, parent.Index);
    EXPECT_NE(replacement.Generation, parent.Generation);
    scene.SynchronizeHierarchy();
    EXPECT_FALSE(cache.Read(session, roots));
    ASSERT_TRUE(scene.SetParent(child, replacement));
    scene.SynchronizeHierarchy();
    ASSERT_TRUE(cache.Read(session, roots));
    ASSERT_EQ(roots.size(), 2u);
    EXPECT_EQ(roots[0].CachedEntity, other);
    EXPECT_EQ(roots[1].CachedEntity, child);
}

TEST_F(SelectedRootCacheTest, RandomizedForestsMatchAnIndependentAncestorWalkInSelectionOrder)
{
    constexpr uint32_t          count    = 128;
    FixedArray<EntityID, count> entities = {};
    uint32_t                    random   = 0x87AF1329u;
    const auto                  next     = [&]() {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        return random;
    };
    for (uint32_t i = 0; i < count; ++i)
    {
        entities[i] = Entity(i + 1, i % 7 != 0);
        if (i && next() % 3)
        {
            scene.AddComponent<ParentComponent>(entities[i], {entities[next() % i]});
        }
    }
    scene.SynchronizeHierarchy();
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t sample = 0; sample < 100; ++sample)
    {
        FixedArray<bool, count>      chosen   = {};
        FixedArray<EntityTarget, 32> targets  = {};
        uint32_t                     selected = 0;
        for (uint32_t i = 0; i < targets.size(); ++i)
        {
            const uint32_t index = next() % count;
            if (!chosen[index])
            {
                chosen[index]       = true;
                targets[selected++] = session.TargetFor(entities[index]);
            }
        }
        ASSERT_TRUE(session.SetSelection({targets.data(), selected}, targets[0].UUID));
        ArrayView<const EntityTarget> roots = {};
        ASSERT_TRUE(cache.Read(session, roots));
        uint32_t expected = 0;
        for (uint32_t i = 0; i < selected; ++i)
        {
            const auto* parent                = scene.GetComponent<ParentComponent>(targets[i].CachedEntity);
            bool        has_selected_ancestor = false;
            while (parent)
            {
                const auto* identity = scene.GetComponent<UUIDComponent>(parent->Parent);
                if (identity && session.Selection().Contains(identity->Value))
                {
                    has_selected_ancestor = true;
                }
                parent = scene.GetComponent<ParentComponent>(parent->Parent);
            }
            if (!has_selected_ancestor)
            {
                ASSERT_LT(expected, roots.size());
                EXPECT_EQ(roots[expected++].UUID, targets[i].UUID);
            }
        }
        EXPECT_EQ(roots.size(), expected);
        EXPECT_EQ(session.Selection().ActiveUUID(), targets[0].UUID);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(SelectedRootCacheTest, OneSharedBatchMovesSelectedRootsWithoutDoubleTransformingTheActiveChild)
{
    const auto parent = Entity(1), child = Entity(2), other = Entity(3);
    ASSERT_TRUE(scene.SetParent(child, parent));
    scene.GetComponent<TransformComponent>(child)->Position.x = 3.f;
    scene.GetComponent<TransformComponent>(other)->Position.x = 10.f;
    Choose({child, other, parent}, child);
    ArrayView<const EntityTarget> roots = {};
    ASSERT_TRUE(session.ReadSelectedRoots(roots));
    ASSERT_EQ(roots.size(), 2u);
    FixedArray<LocalTransformValue, 2> values = {};
    for (uint32_t i = 0; i < values.size(); ++i)
    {
        values[i]             = CaptureLocalTransform(*scene.GetComponent<TransformComponent>(roots[i].CachedEntity));
        values[i].Position.x += 2.f;
    }
    ASSERT_TRUE(session.BeginTransform({roots.data(), roots.size()}));
    const auto valid     = values[1];
    values[1].Position.x = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(session.PreviewTransforms({values.data(), values.size()}));
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->Position.x, 10.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(parent)->Position.x, 0.f);
    values[1] = valid;
    ASSERT_TRUE(session.PreviewTransforms({values.data(), values.size()}));
    ASSERT_TRUE(session.Commit());
    EXPECT_EQ(session.History().UndoCount(), 1u);
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->Position.x, 3.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 5.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->WorldTransform(0, 3), 12.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Undo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 3.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->WorldTransform(0, 3), 10.f);
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.Redo());
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 5.f);
    EXPECT_EQ(session.Selection().ActiveUUID(), Identity(2));
    ASSERT_TRUE(session.BeginMutationPhase());
    ASSERT_TRUE(session.ReadSelectedRoots(roots));
    ASSERT_TRUE(session.BeginTransform({roots.data(), roots.size()}));
    for (auto& value : values)
    {
        value.Position.x += 5.f;
    }
    ASSERT_TRUE(session.PreviewTransforms({values.data(), values.size()}));
    session.Cancel();
    session.EndMutationPhase();
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 5.f);
    EXPECT_FLOAT_EQ(scene.GetComponent<TransformComponent>(other)->WorldTransform(0, 3), 12.f);
    EXPECT_EQ(session.History().UndoCount(), 1u);
}
