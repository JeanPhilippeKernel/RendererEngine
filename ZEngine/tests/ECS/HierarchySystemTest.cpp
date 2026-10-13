#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::ECS::Systems;

class HierarchySystemFixture : public ::testing::Test
{
protected:
    MemoryManager m_memory;
    Scene         m_scene;

    void          SetUp() override
    {
        m_memory.Initialize(ZMega(16), {});
        m_scene.Initialize(&m_memory.MainArena);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }
};

TEST_F(HierarchySystemFixture, PropagatesEveryMatrixElementAfterParentScaleChanges)
{
    const EntityID parent = m_scene.CreateEntity();
    const EntityID child  = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(parent, {});
    m_scene.AddComponent<TransformComponent>(child, {});
    m_scene.AddComponent<ParentComponent>(child, {.Parent = parent});

    SyncHierarchy(m_scene);
    TransformComponent* parent_transform = m_scene.GetComponent<TransformComponent>(parent);
    TransformComponent* child_transform  = m_scene.GetComponent<TransformComponent>(child);
    ASSERT_NE(parent_transform, nullptr);
    ASSERT_NE(child_transform, nullptr);
    EXPECT_FLOAT_EQ(child_transform->WorldTransform(1, 1), 1.f);

    // This was previously missed because the change detector checked only m00 and
    // translation. A gizmo scale edit must propagate all matrix terms.
    parent_transform->Scale.y = 2.f;
    SyncHierarchy(m_scene);
    EXPECT_FLOAT_EQ(child_transform->WorldTransform(1, 1), 2.f);
}

TEST_F(HierarchySystemFixture, TreatsInvalidParentLinkAsRoot)
{
    const EntityID entity = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(
        entity,
        {
        .Position = {3.f, 4.f, 5.f}
    });
    m_scene.AddComponent<ParentComponent>(entity, {.Parent = INVALID_ENTITY});

    SyncHierarchy(m_scene);
    const TransformComponent* transform = m_scene.GetComponent<TransformComponent>(entity);
    ASSERT_NE(transform, nullptr);
    EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 3.f);
    EXPECT_FLOAT_EQ(transform->WorldTransform(1, 3), 4.f);
    EXPECT_FLOAT_EQ(transform->WorldTransform(2, 3), 5.f);
}
