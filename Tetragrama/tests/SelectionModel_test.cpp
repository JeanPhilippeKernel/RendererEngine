#include <Tetragrama/SelectionModel.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <gtest/gtest.h>
#include <array>

using namespace Tetragrama;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class SelectionModelTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory    = {};
    Scene                                scene     = {};
    ActorManager                         actors    = {};
    EntityTargetResolver                 resolver  = {};
    SelectionModel                       selection = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, scene);
        resolver.Initialize(&memory.MainArena);
        resolver.Bind(&scene, 1);
        selection.Initialize(&memory.MainArena, 8);
    }

    void TearDown() override
    {
        actors.Shutdown();
        scene.Shutdown();
    }

    static uuids::uuid Identity(uint8_t value)
    {
        std::array<uint8_t, 16> bytes = {};
        bytes.back()                  = value;
        return uuids::uuid(bytes);
    }

    EntityID Entity(uint8_t identity)
    {
        const auto entity = scene.CreateEntity();
        scene.AddComponent<UUIDComponent>(entity, {Identity(identity)});
        scene.AddComponent<TransformComponent>(entity, {});
        return entity;
    }
};

TEST_F(SelectionModelTest, UninitializedAndNilTargetsFailSafely)
{
    EntityTargetResolver empty = {};
    EXPECT_FALSE(empty.Resolve(Identity(1)).IsValid());
    EXPECT_TRUE(empty.TargetFor({}).UUID.is_nil());
    EXPECT_FALSE(resolver.Resolve(uuids::uuid{}).IsValid());
    EXPECT_FALSE(selection.Select(resolver, {}));
    const auto no_identity = scene.CreateEntity();
    EXPECT_FALSE(selection.Select(resolver, no_identity));
    EXPECT_EQ(selection.Revision(), 0u);
}

TEST_F(SelectionModelTest, InvalidModesAndSceneTagsDoNotMutateSelection)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(selection.Select(resolver, entity));
    const auto revision = selection.Revision();
    EXPECT_FALSE(selection.Select(resolver, entity, static_cast<SelectionMode>(255)));
    selection.SelectScene(static_cast<SceneTarget>(255));
    EXPECT_EQ(selection.Revision(), revision);
    EXPECT_EQ(selection.ActiveUUID(), Identity(1));
    EXPECT_EQ(selection.ActiveSceneTarget(), SceneTarget::None);
}

TEST_F(SelectionModelTest, HoverIsTransientAndPrunesWithoutChangingSelectionMembership)
{
    const auto selected = Entity(1);
    const auto hovered  = Entity(2);
    ASSERT_TRUE(selection.Select(resolver, selected));
    const uint64_t selection_revision = selection.Revision();
    const uint64_t hover_revision     = selection.HoverRevision();

    ASSERT_TRUE(selection.SetHovered(resolver, hovered));
    EXPECT_EQ(selection.Revision(), selection_revision);
    EXPECT_GT(selection.HoverRevision(), hover_revision);
    EXPECT_EQ(resolver.Resolve(selection.Hovered()), hovered);

    scene.DestroyEntity(hovered);
    EXPECT_TRUE(selection.PruneInvalid(resolver));
    EXPECT_EQ(selection.Revision(), selection_revision);
    EXPECT_TRUE(selection.Hovered().UUID.is_nil());
}

TEST_F(SelectionModelTest, ActorAndPureECSEntitiesUseTheSameIdentityPath)
{
    const auto pure   = Entity(1);
    const auto handle = actors.Create();
    auto*      actor  = actors.Access(handle);
    actor->AddComponent<UUIDComponent>({Identity(2)});
    const auto actor_entity = actor->GetEntityID();
    ASSERT_TRUE(selection.Select(resolver, pure));
    ASSERT_TRUE(selection.Select(resolver, actor_entity, SelectionMode::Add));
    ASSERT_EQ(selection.Entities().size(), 2u);
    EXPECT_EQ(resolver.Resolve(selection.Entities()[0]), pure);
    EXPECT_EQ(resolver.Resolve(selection.Entities()[1]), actor_entity);
    EXPECT_EQ(selection.ActiveUUID(), Identity(2));
}

TEST_F(SelectionModelTest, StructuralChangesInvalidateTheIndexButNoOpRemovalDoesNot)
{
    const auto first = Entity(1);
    ASSERT_EQ(resolver.Resolve(Identity(1)), first);
    const auto indexed_revision = resolver.IndexRevision();
    const auto second           = Entity(2);
    ASSERT_EQ(resolver.Resolve(Identity(2)), second);
    EXPECT_GT(resolver.IndexRevision(), indexed_revision);
    scene.RemoveComponent<UUIDComponent>(second);
    const auto revision = scene.StructuralRevision();
    scene.RemoveComponent<UUIDComponent>(second);
    EXPECT_EQ(scene.StructuralRevision(), revision);
    EXPECT_FALSE(resolver.Resolve(Identity(2)).IsValid());
}

TEST_F(SelectionModelTest, RecycledEntityIndexCannotResolveAnUnrelatedUUID)
{
    const auto original = Entity(1);
    const auto target   = resolver.TargetFor(original);
    scene.DestroyEntity(original);
    const auto replacement = Entity(2);
    EXPECT_EQ(original.Index, replacement.Index);
    EXPECT_NE(original.Generation, replacement.Generation);
    EXPECT_FALSE(resolver.Resolve(target).IsValid());
    EXPECT_EQ(resolver.Resolve(Identity(2)), replacement);
}

TEST_F(SelectionModelTest, SameEpochRestorationResolvesUUIDRatherThanStaleCache)
{
    const auto original = Entity(1);
    const auto target   = resolver.TargetFor(original);
    scene.DestroyEntity(original);
    const auto restored = Entity(1);
    EXPECT_NE(target.CachedEntity, restored);
    EXPECT_EQ(resolver.Resolve(target), restored);
}

TEST_F(SelectionModelTest, SceneEpochRejectsTargetsEvenWhenUUIDAndEntityMatch)
{
    const auto entity = Entity(1);
    const auto before = resolver.TargetFor(entity);
    resolver.Bind(&scene, 2);
    EXPECT_FALSE(resolver.Resolve(before).IsValid());
    EXPECT_EQ(resolver.Resolve(resolver.TargetFor(entity)), entity);
    resolver.Bind(nullptr, 0);
    EXPECT_FALSE(resolver.Resolve(Identity(1)).IsValid());
}

TEST_F(SelectionModelTest, DuplicateIdentityRejectsCachedAndFreshResolution)
{
    const auto first  = Entity(1);
    const auto cached = resolver.TargetFor(first);
    const auto second = Entity(1);
    EXPECT_FALSE(resolver.Resolve(cached).IsValid());
    EXPECT_FALSE(resolver.Resolve(Identity(1)).IsValid());
    EXPECT_TRUE(resolver.TargetFor(first).UUID.is_nil());
    EXPECT_TRUE(resolver.TargetFor(second).UUID.is_nil());
    scene.DestroyEntity(second);
    EXPECT_EQ(resolver.Resolve(cached), first);
}

TEST_F(SelectionModelTest, CachedIndexRefreshAndSelectionReuseExistingStorage)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    const auto offset = memory.MainArena.m_current_offset;
    for (int i = 0; i < 100; ++i)
    {
        resolver.Invalidate();
        ASSERT_EQ(resolver.Resolve(Identity(1)), first);
        ASSERT_TRUE(selection.Select(resolver, first));
        ASSERT_TRUE(selection.Select(resolver, second, SelectionMode::Add));
        ASSERT_TRUE(selection.Select(resolver, first, SelectionMode::Toggle));
        selection.Clear();
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(SelectionModelTest, NoOpReplacementDoesNotChangeRevision)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(selection.Select(resolver, entity));
    const auto revision = selection.Revision();
    ASSERT_TRUE(selection.Select(resolver, entity));
    ASSERT_TRUE(selection.Select(resolver, entity, SelectionMode::Add));
    ASSERT_TRUE(selection.Set(resolver, selection.Entities()));
    EXPECT_EQ(selection.Revision(), revision);
}

TEST_F(SelectionModelTest, OrderedTogglePreservesRemainingOrderAndActiveTarget)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    const auto third  = Entity(3);
    ASSERT_TRUE(selection.Select(resolver, first));
    ASSERT_TRUE(selection.Select(resolver, second, SelectionMode::Add));
    ASSERT_TRUE(selection.Select(resolver, third, SelectionMode::Add));
    ASSERT_TRUE(selection.Select(resolver, second, SelectionMode::Toggle));
    ASSERT_EQ(selection.Entities().size(), 2u);
    EXPECT_EQ(selection.Entities()[0].UUID, Identity(1));
    EXPECT_EQ(selection.Entities()[1].UUID, Identity(3));
    EXPECT_EQ(selection.ActiveUUID(), Identity(3));
    ASSERT_TRUE(selection.Select(resolver, third, SelectionMode::Toggle));
    EXPECT_EQ(selection.ActiveUUID(), Identity(1));
    ASSERT_TRUE(selection.Select(resolver, first, SelectionMode::Toggle));
    EXPECT_TRUE(selection.Entities().empty());
    EXPECT_TRUE(selection.ActiveUUID().is_nil());
}

TEST_F(SelectionModelTest, SetDeduplicatesAndPreservesExplicitActiveTarget)
{
    const auto       first   = resolver.TargetFor(Entity(1));
    const auto       second  = resolver.TargetFor(Entity(2));
    const std::array targets = {first, second, first};
    ASSERT_TRUE(selection.Set(resolver, targets, first.UUID));
    ASSERT_EQ(selection.Entities().size(), 2u);
    EXPECT_EQ(selection.Entities()[0].UUID, first.UUID);
    EXPECT_EQ(selection.Entities()[1].UUID, second.UUID);
    EXPECT_EQ(selection.ActiveUUID(), first.UUID);
}

TEST_F(SelectionModelTest, InvalidBatchOrActiveTargetLeavesSelectionUnchanged)
{
    const auto first  = resolver.TargetFor(Entity(1));
    const auto second = resolver.TargetFor(Entity(2));
    ASSERT_TRUE(selection.Set(resolver, {&first, 1}));
    const auto       revision = selection.Revision();
    const std::array targets  = {second, EntityTarget{}};
    EXPECT_FALSE(selection.Set(resolver, targets));
    EXPECT_FALSE(selection.Set(resolver, {&second, 1}, Identity(9)));
    ASSERT_EQ(selection.Entities().size(), 1u);
    EXPECT_EQ(selection.Entities()[0].UUID, first.UUID);
    EXPECT_EQ(selection.Revision(), revision);
}

TEST_F(SelectionModelTest, CapacityFailureDoesNotTruncateOrPartiallyReplaceSelection)
{
    for (uint8_t i = 1; i <= selection.Capacity(); ++i)
    {
        ASSERT_TRUE(selection.Select(resolver, Entity(i), SelectionMode::Add));
    }
    const auto revision = selection.Revision();
    const auto ninth    = Entity(9);
    EXPECT_FALSE(selection.Select(resolver, ninth, SelectionMode::Add));
    EXPECT_EQ(selection.Entities().size(), selection.Capacity());
    EXPECT_EQ(selection.ActiveUUID(), Identity(8));
    EXPECT_EQ(selection.Revision(), revision);
    EXPECT_FALSE(selection.Contains(Identity(9)));
}

TEST_F(SelectionModelTest, GridAndEntitySelectionsAreMutuallyExclusive)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(selection.Select(resolver, entity));
    selection.SelectScene(SceneTarget::Grid);
    EXPECT_TRUE(selection.Entities().empty());
    EXPECT_TRUE(selection.ActiveUUID().is_nil());
    EXPECT_FALSE(selection.Contains(Identity(1)));
    EXPECT_EQ(selection.ActiveSceneTarget(), SceneTarget::Grid);
    const auto revision = selection.Revision();
    selection.SelectScene(SceneTarget::Grid);
    EXPECT_EQ(selection.Revision(), revision);
    ASSERT_TRUE(selection.Select(resolver, entity, SelectionMode::Add));
    EXPECT_EQ(selection.ActiveSceneTarget(), SceneTarget::None);
    EXPECT_TRUE(selection.Contains(Identity(1)));
}

TEST_F(SelectionModelTest, AddCannotMixSelectionEpochsAndReplacementCanResetTheSelection)
{
    const auto entity = Entity(1);
    ASSERT_TRUE(selection.Select(resolver, entity));
    const auto revision = selection.Revision();
    resolver.Bind(&scene, 2);
    EXPECT_FALSE(selection.Select(resolver, entity, SelectionMode::Add));
    EXPECT_FALSE(selection.Select(resolver, Entity(2), SelectionMode::Add));
    EXPECT_EQ(selection.Revision(), revision);
    ASSERT_TRUE(selection.Select(resolver, entity));
    ASSERT_EQ(selection.Entities().size(), 1u);
    EXPECT_EQ(selection.Entities()[0].SceneEpoch, 2u);
}

TEST_F(SelectionModelTest, PruningDeletedTargetsPreservesValidOrderAndFixesActiveTarget)
{
    const auto first  = Entity(1);
    const auto second = Entity(2);
    ASSERT_TRUE(selection.Select(resolver, first));
    ASSERT_TRUE(selection.Select(resolver, second, SelectionMode::Add));
    scene.DestroyEntity(second);
    EXPECT_TRUE(selection.PruneInvalid(resolver));
    ASSERT_EQ(selection.Entities().size(), 1u);
    EXPECT_EQ(selection.ActiveUUID(), Identity(1));
    EXPECT_FALSE(selection.PruneInvalid(resolver));
    resolver.Bind(&scene, 2);
    EXPECT_TRUE(selection.PruneInvalid(resolver));
    EXPECT_TRUE(selection.Entities().empty());
    EXPECT_TRUE(selection.ActiveUUID().is_nil());
}

TEST_F(SelectionModelTest, AdditiveRangePreservesExistingOrderThenAppendsUniqueTargetsAndUsesTheExplicitEndpoint)
{
    const auto first = resolver.TargetFor(Entity(1)), second = resolver.TargetFor(Entity(2)), third = resolver.TargetFor(Entity(3));
    ASSERT_TRUE(selection.Set(resolver, {&first, 1}));
    const ZEngine::Core::Containers::FixedArray<EntityTarget, 4> range = {
        {second, first, third, second}
    };
    ASSERT_TRUE(selection.Extend(resolver, {range.data(), range.size()}, second.UUID));
    ASSERT_EQ(selection.Entities().size(), 3u);
    EXPECT_EQ(selection.Entities()[0].UUID, first.UUID);
    EXPECT_EQ(selection.Entities()[1].UUID, second.UUID);
    EXPECT_EQ(selection.Entities()[2].UUID, third.UUID);
    EXPECT_EQ(selection.ActiveUUID(), second.UUID);
}

TEST_F(SelectionModelTest, InvalidOrOverCapacityAdditiveRangeDoesNotPublishAnyTargetOrRevision)
{
    ZEngine::Core::Containers::FixedArray<EntityTarget, 8> targets = {};
    for (uint8_t i = 0; i < targets.size(); ++i)
    {
        targets[i] = resolver.TargetFor(Entity(i + 1));
    }
    const auto extra = resolver.TargetFor(Entity(9));
    ASSERT_TRUE(selection.Set(resolver, {targets.data(), targets.size()}, targets[0].UUID));
    const auto revision = selection.Revision();
    EXPECT_FALSE(selection.Extend(resolver, {&extra, 1}));
    EXPECT_EQ(selection.Revision(), revision);
    EXPECT_EQ(selection.ActiveUUID(), targets[0].UUID);
    EXPECT_FALSE(selection.Contains(extra.UUID));
    const ZEngine::Core::Containers::FixedArray<EntityTarget, 2> invalid = {
        {targets[0], {}}
    };
    EXPECT_FALSE(selection.Extend(resolver, {invalid.data(), invalid.size()}));
    EXPECT_FALSE(selection.Extend(resolver, {targets.data(), 1}, extra.UUID));
    EXPECT_EQ(selection.Revision(), revision);
    EXPECT_EQ(selection.Entities().size(), targets.size());
}

TEST_F(SelectionModelTest, EmptyExtensionIsANoOpIncludingGridAndAnExplicitNonLastActiveTarget)
{
    const auto                                                   first = resolver.TargetFor(Entity(1)), second = resolver.TargetFor(Entity(2));
    const ZEngine::Core::Containers::FixedArray<EntityTarget, 2> targets = {
        {first, second}
    };
    ASSERT_TRUE(selection.Set(resolver, {targets.data(), targets.size()}, first.UUID));
    const auto revision = selection.Revision();
    ASSERT_TRUE(selection.Extend(resolver, {}));
    EXPECT_EQ(selection.ActiveUUID(), first.UUID);
    EXPECT_EQ(selection.Revision(), revision);
    selection.SelectScene(SceneTarget::Grid);
    const auto grid_revision = selection.Revision();
    ASSERT_TRUE(selection.Extend(resolver, {}));
    EXPECT_EQ(selection.Revision(), grid_revision);
    EXPECT_EQ(selection.ActiveSceneTarget(), SceneTarget::Grid);
    ASSERT_TRUE(selection.Extend(resolver, {&second, 1}));
    EXPECT_EQ(selection.ActiveSceneTarget(), SceneTarget::None);
    EXPECT_EQ(selection.ActiveUUID(), second.UUID);
}

TEST_F(SelectionModelTest, ExtensionRejectsMixedEpochsWithoutReplacingTheOriginalSelection)
{
    const auto entity = Entity(1), second = Entity(2);
    ASSERT_TRUE(selection.Select(resolver, entity));
    const auto revision = selection.Revision();
    resolver.Bind(&scene, 2);
    const auto target = resolver.TargetFor(second);
    EXPECT_FALSE(selection.Extend(resolver, {&target, 1}));
    EXPECT_EQ(selection.Revision(), revision);
    EXPECT_EQ(selection.Entities()[0].SceneEpoch, 1u);
}

TEST_F(SelectionModelTest, RepeatedRangeReplaceAndExtendReuseReservedStorage)
{
    const auto first = resolver.TargetFor(Entity(1)), second = resolver.TargetFor(Entity(2));
    const auto offset = memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        ASSERT_TRUE(selection.Set(resolver, {&first, 1}));
        ASSERT_TRUE(selection.Extend(resolver, {&second, 1}));
        const auto revision = selection.Revision();
        ASSERT_TRUE(selection.Extend(resolver, selection.Entities(), second.UUID));
        EXPECT_EQ(selection.Revision(), revision);
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}
