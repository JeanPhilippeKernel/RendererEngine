#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <vector>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class HierarchySystemTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory = {};
    Scene                                scene  = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
    }
    void TearDown() override
    {
        scene.Shutdown();
        memory.Shutdown();
    }
    EntityID Entity(float x = 0.f)
    {
        const auto entity = scene.CreateEntity();
        scene.AddComponent<TransformComponent>(
            entity,
            {
            .Position = {x, 0.f, 0.f}
        });
        return entity;
    }
    TransformComponent& Transform(EntityID entity)
    {
        return *scene.GetComponent<TransformComponent>(entity);
    }
    void Sync()
    {
        Systems::SyncHierarchy(scene);
    }
    static void MatrixEquals(const Mat4f& actual, const Mat4f& expected)
    {
        for (uint32_t column = 0; column < 4; ++column)
            for (uint32_t row = 0; row < 4; ++row)
                EXPECT_FLOAT_EQ(actual(row, column), expected(row, column)) << row << "," << column;
    }
};

TEST_F(HierarchySystemTest, RootAndExplicitInvalidParentBothDeriveTheirFullLocalMatrix)
{
    const auto root     = Entity(3.f);
    const auto detached = Entity(4.f);
    scene.AddComponent<ParentComponent>(detached, {});
    Transform(detached).Rotation.x = .7f;
    Transform(detached).Scale      = {1.f, 2.f, 3.f};
    Sync();
    MatrixEquals(Transform(root).WorldTransform, ComposeTransformMatrix(Transform(root).Position, {}, {1.f, 1.f, 1.f}));
    const auto& transform = Transform(detached);
    MatrixEquals(transform.WorldTransform, ComposeTransformMatrix(transform.Position, transform.Rotation, transform.Scale));
    EXPECT_TRUE(scene.Hierarchy().Find(root)->FrameValid);
    EXPECT_TRUE(scene.Hierarchy().Find(detached)->FrameValid);
}

TEST_F(HierarchySystemTest, RotationAndScaleChangesUpdateMatrixLanesThatTheOldPartialComparisonMissed)
{
    const auto child  = Entity();
    const auto parent = Entity();
    ASSERT_TRUE(scene.SetParent(child, parent));
    Sync();
    const auto before           = scene.Hierarchy().Find(child)->WorldRevision;
    Transform(child).Rotation.x = .7f; // leaves (0,0) and all translations unchanged
    Transform(child).Scale.y    = 2.f;
    Transform(child).Scale.z    = 3.f;
    Sync();
    MatrixEquals(Transform(child).WorldTransform, ComposeTransformMatrix(Transform(child).Position, Transform(child).Rotation, Transform(child).Scale));
    EXPECT_GT(scene.Hierarchy().Find(child)->WorldRevision, before);
}

TEST_F(HierarchySystemTest, DeepReverseCreationOrderIsIterativeAndParentBeforeChild)
{
    constexpr uint32_t    count    = 4096;
    std::vector<EntityID> entities = {};
    entities.reserve(count);
    for (uint32_t i = 0; i < count; ++i)
        entities.push_back(Entity(1.f));
    for (uint32_t i = 0; i + 1 < count; ++i)
        ASSERT_TRUE(scene.SetParent(entities[i], entities[i + 1]));
    const auto arena_offset = memory.MainArena.m_current_offset;
    Sync();
    EXPECT_FLOAT_EQ(Transform(entities.front()).WorldTransform(0, 3), static_cast<float>(count));
    EXPECT_FLOAT_EQ(Transform(entities.back()).WorldTransform(0, 3), 1.f);
    const auto order = scene.Hierarchy().Order();
    const auto nodes = scene.Hierarchy().Nodes();
    ASSERT_EQ(order.size(), count);
    EXPECT_EQ(nodes[order.front()].Entity, entities.back());
    EXPECT_EQ(nodes[order.back()].Entity, entities.front());
    EXPECT_EQ(memory.MainArena.m_current_offset, arena_offset);
}

TEST_F(HierarchySystemTest, TransformlessGroupsInheritTheNearestTransformedAncestor)
{
    const auto root  = Entity(3.f);
    const auto group = scene.CreateEntity();
    const auto child = Entity(2.f);
    ASSERT_TRUE(scene.SetParent(group, root));
    ASSERT_TRUE(scene.SetParent(child, group));
    Sync();
    EXPECT_FLOAT_EQ(Transform(child).WorldTransform(0, 3), 5.f);
    EXPECT_TRUE(scene.Hierarchy().Find(group)->FrameValid);
    EXPECT_EQ(scene.Hierarchy().ParentWorldRevision(child), scene.Hierarchy().Find(root)->WorldRevision);
    EXPECT_EQ(scene.Hierarchy().Find(group)->WorldRevision, scene.Hierarchy().Find(root)->WorldRevision);
}

TEST_F(HierarchySystemTest, CyclesAndTheirDescendantsUseStableLocalFallbackWithoutChangingAuthoredLinks)
{
    const auto a           = Entity(1.f);
    const auto b           = Entity(2.f);
    const auto descendant  = Entity(3.f);
    const auto independent = Entity(4.f);
    scene.AddComponent<ParentComponent>(a, {b});
    scene.AddComponent<ParentComponent>(b, {a});
    scene.AddComponent<ParentComponent>(descendant, {b});
    for (uint32_t i = 0; i < 10; ++i)
    {
        Sync();
        for (auto entity : {a, b, descendant})
        {
            EXPECT_FALSE(scene.Hierarchy().Find(entity)->FrameValid);
            EXPECT_FLOAT_EQ(Transform(entity).WorldTransform(0, 3), Transform(entity).Position.x);
        }
        EXPECT_TRUE(scene.Hierarchy().Find(independent)->FrameValid);
    }
    EXPECT_EQ(scene.GetComponent<ParentComponent>(a)->Parent, b);
    EXPECT_EQ(scene.GetComponent<ParentComponent>(b)->Parent, a);
    ASSERT_TRUE(scene.SetParent(a, INVALID_ENTITY));
    Sync();
    EXPECT_TRUE(scene.Hierarchy().Find(descendant)->FrameValid);
    EXPECT_FLOAT_EQ(Transform(descendant).WorldTransform(0, 3), 6.f);
}

TEST_F(HierarchySystemTest, SelfCycleNeverLoopsAndValidatedAPIRejectsItAtomically)
{
    const auto entity   = Entity(1.f);
    const auto revision = scene.Hierarchy().Revision();
    EXPECT_FALSE(scene.SetParent(entity, entity));
    EXPECT_EQ(scene.Hierarchy().Revision(), revision);
    scene.AddComponent<ParentComponent>(entity, {entity});
    Sync();
    EXPECT_FALSE(scene.Hierarchy().Find(entity)->FrameValid);
    EXPECT_FLOAT_EQ(Transform(entity).WorldTransform(0, 3), 1.f);
}

TEST_F(HierarchySystemTest, StaleParentsCannotAttachChildrenToRecycledEntitySlots)
{
    const auto old   = Entity(10.f);
    const auto child = Entity(2.f);
    scene.AddComponent<ParentComponent>(child, {old});
    scene.DestroyEntity(old);
    const auto replacement = Entity(100.f);
    ASSERT_EQ(old.Index, replacement.Index);
    EXPECT_FALSE(scene.SetParent(child, old));
    Sync();
    EXPECT_FALSE(scene.Hierarchy().Find(child)->FrameValid);
    EXPECT_FLOAT_EQ(Transform(child).WorldTransform(0, 3), 2.f);
    EXPECT_EQ(scene.Hierarchy().Find(old), nullptr);
    ASSERT_TRUE(scene.SetParent(child, replacement));
    Sync();
    EXPECT_FLOAT_EQ(Transform(child).WorldTransform(0, 3), 102.f);
}

TEST_F(HierarchySystemTest, ReparentValidationRejectsDescendantCyclesAndMalformedDestinationChains)
{
    const auto root       = Entity(1.f);
    const auto child      = Entity(2.f);
    const auto grandchild = Entity(3.f);
    ASSERT_TRUE(scene.SetParent(child, root));
    ASSERT_TRUE(scene.SetParent(grandchild, child));
    const auto revision = scene.Hierarchy().Revision();
    EXPECT_FALSE(scene.SetParent(root, grandchild));
    EXPECT_EQ(scene.Hierarchy().Revision(), revision);
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(root));
    const auto a = Entity();
    const auto b = Entity();
    scene.AddComponent<ParentComponent>(a, {b});
    scene.AddComponent<ParentComponent>(b, {a});
    EXPECT_FALSE(scene.SetParent(root, a));
    EXPECT_FALSE(scene.SetParent(INVALID_ENTITY, root));
    EXPECT_FALSE(scene.SetParent(root, {123u, 0u}));
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(root));
}

TEST_F(HierarchySystemTest, ReparentPreservesLocalTRSAndNoOpDoesNotInvalidateTheCache)
{
    const auto first  = Entity(10.f);
    const auto second = Entity(20.f);
    const auto child  = Entity(3.f);
    ASSERT_TRUE(scene.SetParent(child, first));
    Sync();
    const auto revision = scene.Hierarchy().Revision();
    const auto builds   = scene.Hierarchy().BuildCount();
    ASSERT_TRUE(scene.SetParent(child, first));
    Sync();
    EXPECT_EQ(scene.Hierarchy().Revision(), revision);
    EXPECT_EQ(scene.Hierarchy().BuildCount(), builds);
    ASSERT_TRUE(scene.SetParent(child, second));
    EXPECT_EQ(scene.Hierarchy().Find(child), nullptr); // stale views are unavailable
    Sync();
    EXPECT_FLOAT_EQ(Transform(child).Position.x, 3.f);
    EXPECT_FLOAT_EQ(Transform(child).WorldTransform(0, 3), 23.f);
}

TEST_F(HierarchySystemTest, LegacyRawReparentIsDetectedWithoutAComponentMembershipChange)
{
    const auto first  = Entity(10.f);
    const auto second = Entity(20.f);
    const auto child  = Entity(3.f);
    ASSERT_TRUE(scene.SetParent(child, first));
    Sync();
    const auto structural                              = scene.StructuralRevision();
    const auto revision                                = scene.Hierarchy().Revision();
    const auto builds                                  = scene.Hierarchy().BuildCount();
    scene.GetComponent<ParentComponent>(child)->Parent = second;
    Sync();
    EXPECT_EQ(scene.StructuralRevision(), structural);
    EXPECT_GT(scene.Hierarchy().Revision(), revision);
    EXPECT_EQ(scene.Hierarchy().BuildCount(), builds + 1);
    EXPECT_FLOAT_EQ(Transform(child).WorldTransform(0, 3), 23.f);
    Sync();
    EXPECT_EQ(scene.Hierarchy().BuildCount(), builds + 1);
}

TEST_F(HierarchySystemTest, AncestorFrameRevisionsChangeOnlyForAffectedBranches)
{
    const auto root      = Entity();
    const auto child     = Entity(2.f);
    const auto unrelated = Entity();
    ASSERT_TRUE(scene.SetParent(child, root));
    Sync();
    const auto parent_revision      = scene.Hierarchy().ParentWorldRevision(child);
    const auto builds               = scene.Hierarchy().BuildCount();
    Transform(unrelated).Position.x = 3.f;
    Sync();
    EXPECT_EQ(scene.Hierarchy().ParentWorldRevision(child), parent_revision);
    Transform(root).Rotation.x = .7f;
    Sync();
    EXPECT_GT(scene.Hierarchy().ParentWorldRevision(child), parent_revision);
    EXPECT_EQ(scene.Hierarchy().BuildCount(), builds);
}

TEST_F(HierarchySystemTest, SteadyStateSynchronizationReusesAllWorkspaceAndTopology)
{
    const auto root  = Entity();
    const auto child = Entity(2.f);
    ASSERT_TRUE(scene.SetParent(child, root));
    Sync();
    const auto offset   = memory.MainArena.m_current_offset;
    const auto builds   = scene.Hierarchy().BuildCount();
    const auto revision = scene.Hierarchy().Find(child)->WorldRevision;
    for (uint32_t i = 0; i < 1000; ++i)
        Sync();
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_EQ(scene.Hierarchy().BuildCount(), builds);
    EXPECT_EQ(scene.Hierarchy().Find(child)->WorldRevision, revision);
}

TEST_F(HierarchySystemTest, FullEntityCapacityIsNotTruncatedAndGrowthHappensOnlyOnCreation)
{
    for (uint32_t i = 0; i < EntityRegistry::MAX_ENTITIES; ++i)
        ASSERT_TRUE(scene.CreateEntity().IsValid());
    const auto offset = memory.MainArena.m_current_offset;
    EXPECT_EQ(scene.CreateEntity(), INVALID_ENTITY);
    Sync();
    EXPECT_EQ(scene.Hierarchy().Nodes().size(), EntityRegistry::MAX_ENTITIES);
    EXPECT_EQ(scene.Hierarchy().Order().size(), EntityRegistry::MAX_ENTITIES);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}

TEST_F(HierarchySystemTest, CreationGrowthFitsItsPreflightBoundAndReusesReservedCapacity)
{
    for (uint32_t capacity : {256u, 512u, 1024u})
    {
        while (scene.AliveCount() < capacity)
        {
            EXPECT_EQ(scene.Hierarchy().ReservationMemoryBound(scene.AliveCount() + 1), 0u);
            ASSERT_TRUE(scene.CreateEntity().IsValid());
        }
        const auto bound  = scene.Hierarchy().ReservationMemoryBound(scene.AliveCount() + 1);
        const auto offset = memory.MainArena.m_current_offset;
        ASSERT_GT(bound, 0u);
        ASSERT_TRUE(scene.CreateEntity().IsValid());
        EXPECT_LE(memory.MainArena.m_current_offset - offset, bound);
        EXPECT_EQ(scene.Hierarchy().ReservationMemoryBound(scene.AliveCount() + 1), 0u);
    }
}

TEST_F(HierarchySystemTest, NonFiniteAndOverflowedFramesStayFiniteButAreNotEditable)
{
    const auto root  = Entity();
    const auto child = Entity(2.f);
    ASSERT_TRUE(scene.SetParent(child, root));
    Transform(root).Scale.x = std::numeric_limits<float>::infinity();
    Sync();
    EXPECT_FALSE(scene.Hierarchy().Find(root)->FrameValid);
    EXPECT_FALSE(scene.Hierarchy().Find(child)->FrameValid);
    EXPECT_TRUE(std::isinf(Transform(root).Scale.x)); // authored input is not silently repaired
    MatrixEquals(Transform(root).WorldTransform, Identity<Mat4f>());
    Transform(root).Scale  = {1e30f, 1.f, 1.f};
    Transform(child).Scale = {1e30f, 1.f, 1.f};
    Sync();
    EXPECT_TRUE(scene.Hierarchy().Find(root)->FrameValid);
    EXPECT_FALSE(scene.Hierarchy().Find(child)->FrameValid);
    Transform(root).Scale = Transform(child).Scale = {1.f, 1.f, 1.f};
    Sync();
    EXPECT_TRUE(scene.Hierarchy().Find(child)->FrameValid);
}
