#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Tools/TransformEditSession.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::ECS::Tools;

namespace
{
    GizmoLocalTransform Local(float x, float y = 0.f, float z = 0.f)
    {
        return {
            .Position = {x, y, z},
            .Rotation = {},
            .Scale    = {1.f, 1.f, 1.f},
        };
    }
} // namespace

class TransformEditSessionFixture : public ::testing::Test
{
protected:
    MemoryManager        m_memory;
    Scene                m_scene;
    TransformEditSession m_session;

    void                 SetUp() override
    {
        m_memory.Initialize(ZMega(32), {});
        m_scene.Initialize(&m_memory.MainArena);
        m_session.Initialize(&m_memory.MainArena, 16, 2, 16);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }

    EntityID CreateTransform(Vec3f position = {})
    {
        const EntityID entity = m_scene.CreateEntity();
        m_scene.AddComponent<TransformComponent>(entity, {.Position = position});
        return entity;
    }

    bool Select(const EntityID* entities, size_t count, EntityID active = INVALID_ENTITY)
    {
        return m_session.SetSelection(m_scene, {entities, count}, active);
    }
};

TEST_F(TransformEditSessionFixture, ManipulatesSelectedRootsOnlyAndSupportsUndoRedo)
{
    const EntityID parent = CreateTransform({1.f, 0.f, 0.f});
    const EntityID child  = CreateTransform({2.f, 0.f, 0.f});
    m_scene.AddComponent<ParentComponent>(child, {.Parent = parent});
    Systems::SyncHierarchy(m_scene);

    EntityID selection[] = {parent, child};
    ASSERT_TRUE(Select(selection, 2, child));
    ASSERT_TRUE(m_session.Begin(m_scene));
    ASSERT_EQ(m_session.ActiveTargets().size(), 1u);
    EXPECT_EQ(m_session.ActiveTargets()[0].Entity, parent);

    const GizmoLocalTransform translated[] = {Local(5.f)};
    ASSERT_TRUE(m_session.PreviewLocal(m_scene, {translated, 1}));
    ASSERT_TRUE(m_session.Commit(m_scene));

    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(parent)->Position.x, 5.f);
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 7.f);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(parent)->Position.x, 1.f);
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(child)->WorldTransform(0, 3), 3.f);
    EXPECT_TRUE(m_session.Redo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(parent)->Position.x, 5.f);
}

TEST_F(TransformEditSessionFixture, RejectsWorldPreviewThatWouldCreateLocalShear)
{
    const EntityID parent                                   = CreateTransform();
    const EntityID child                                    = CreateTransform();
    m_scene.GetComponent<TransformComponent>(parent)->Scale = {2.f, 1.f, 1.f};
    m_scene.AddComponent<ParentComponent>(child, {.Parent = parent});
    Systems::SyncHierarchy(m_scene);

    ASSERT_TRUE(Select(&child, 1));
    ASSERT_TRUE(m_session.Begin(m_scene));
    const Mat4f invalid_world[] = {ComposeTransformMatrix({}, {0.f, 0.f, 0.5f}, {1.f, 1.f, 1.f}) * m_scene.GetComponent<TransformComponent>(parent)->WorldTransform};
    EXPECT_FALSE(m_session.PreviewWorld(m_scene, {invalid_world, 1}));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(child)->Rotation.z, 0.f);
    EXPECT_TRUE(m_session.Cancel(m_scene));
}

TEST_F(TransformEditSessionFixture, NeverPartiallyAppliesAStaleMultiTargetPreview)
{
    const EntityID first       = CreateTransform({1.f, 0.f, 0.f});
    const EntityID second      = CreateTransform({2.f, 0.f, 0.f});
    EntityID       selection[] = {first, second};
    ASSERT_TRUE(Select(selection, 2));
    ASSERT_TRUE(m_session.Begin(m_scene));

    m_scene.DestroyEntity(second);
    const GizmoLocalTransform updated[] = {Local(10.f), Local(20.f)};
    EXPECT_FALSE(m_session.PreviewLocal(m_scene, {updated, 2}));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(first)->Position.x, 1.f);
    EXPECT_FALSE(m_session.Cancel(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(first)->Position.x, 1.f);
}

TEST_F(TransformEditSessionFixture, NewCommitDiscardsRedoAndHistoryUsesBoundedCommandCapacity)
{
    const EntityID entity = CreateTransform();
    ASSERT_TRUE(Select(&entity, 1));

    const auto commit_position = [&](float position) {
        const GizmoLocalTransform transform[] = {Local(position)};
        EXPECT_TRUE(m_session.Begin(m_scene));
        EXPECT_TRUE(m_session.PreviewLocal(m_scene, {transform, 1}));
        EXPECT_TRUE(m_session.Commit(m_scene));
    };

    commit_position(1.f);
    commit_position(2.f);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f);

    commit_position(3.f);
    EXPECT_FALSE(m_session.History().CanRedo());
    EXPECT_EQ(m_session.History().UndoCount(), 2u);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(entity)->Position.x, 1.f);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(entity)->Position.x, 0.f);

    commit_position(4.f);
    commit_position(5.f);
    commit_position(6.f);
    EXPECT_EQ(m_session.History().UndoCount(), 2u);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(entity)->Position.x, 5.f);
    EXPECT_TRUE(m_session.Undo(m_scene));
    EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(entity)->Position.x, 4.f);
}

TEST_F(TransformEditSessionFixture, CommitReportsWhetherTheTransactionAuthoredAChange)
{
    const EntityID entity = CreateTransform();
    ASSERT_TRUE(Select(&entity, 1));

    ASSERT_TRUE(m_session.Begin(m_scene));
    ASSERT_TRUE(m_session.Commit(m_scene));
    EXPECT_FALSE(m_session.LastCommitChanged());
    EXPECT_FALSE(m_session.History().CanUndo());

    const GizmoLocalTransform translated[] = {Local(3.f)};
    ASSERT_TRUE(m_session.Begin(m_scene));
    ASSERT_TRUE(m_session.PreviewLocal(m_scene, {translated, 1}));
    ASSERT_TRUE(m_session.Commit(m_scene));
    EXPECT_TRUE(m_session.LastCommitChanged());
    EXPECT_TRUE(m_session.History().CanUndo());

    ASSERT_TRUE(m_session.Begin(m_scene));
    ASSERT_TRUE(m_session.Cancel(m_scene));
    EXPECT_FALSE(m_session.LastCommitChanged());
}
