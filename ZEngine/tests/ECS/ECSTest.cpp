#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Query.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;

struct VelocityComponent
{
    float X = 0.f;
    float Y = 0.f;
    float Z = 0.f;
};
static_assert(sizeof(VelocityComponent) <= 64, "VelocityComponent exceeds cache line");

class ECSFixture : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    Scene         m_scene;

    void          SetUp() override
    {
        m_manager.Initialize(ZMega(64), {});
        m_scene.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }
};

TEST_F(ECSFixture, CreateEntityIsAlive)
{
    EntityID id = m_scene.CreateEntity();
    EXPECT_TRUE(id.IsValid());
    EXPECT_TRUE(m_scene.IsAlive(id));
    EXPECT_EQ(m_scene.AliveCount(), 1u);
}

TEST_F(ECSFixture, DestroyEntityIsNoLongerAlive)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.DestroyEntity(id);
    EXPECT_FALSE(m_scene.IsAlive(id));
    EXPECT_EQ(m_scene.AliveCount(), 0u);
}

TEST_F(ECSFixture, StaleHandleAfterDestroyAndRecycle)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.DestroyEntity(id);
    EntityID id2 = m_scene.CreateEntity();
    EXPECT_FALSE(m_scene.IsAlive(id));
    EXPECT_TRUE(m_scene.IsAlive(id2));
}

TEST_F(ECSFixture, AddAndGetComponent)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(
        id,
        {
        {1.f, 2.f, 3.f}
    });

    TransformComponent* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->Position.x, 1.f);
    EXPECT_FLOAT_EQ(t->Position.y, 2.f);
    EXPECT_FLOAT_EQ(t->Position.z, 3.f);
}

TEST_F(ECSFixture, HasComponentReturnsTrueAfterAdd)
{
    EntityID id = m_scene.CreateEntity();
    EXPECT_FALSE(m_scene.HasComponent<TransformComponent>(id));
    m_scene.AddComponent<TransformComponent>(id, {});
    EXPECT_TRUE(m_scene.HasComponent<TransformComponent>(id));
}

TEST_F(ECSFixture, RemoveComponentClearsHasComponent)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    m_scene.RemoveComponent<TransformComponent>(id);
    EXPECT_FALSE(m_scene.HasComponent<TransformComponent>(id));
    EXPECT_EQ(m_scene.GetComponent<TransformComponent>(id), nullptr);
}

TEST_F(ECSFixture, DestroyEntityRemovesAllComponents)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    m_scene.AddComponent<VelocityComponent>(id, {});
    m_scene.DestroyEntity(id);
    EXPECT_EQ(m_scene.GetComponent<TransformComponent>(id), nullptr);
}

TEST_F(ECSFixture, ForEachHitsOnlyMatchingEntities)
{
    EntityID with_both  = m_scene.CreateEntity();
    EntityID with_trans = m_scene.CreateEntity();
    EntityID with_vel   = m_scene.CreateEntity();
    EntityID with_none  = m_scene.CreateEntity();
    (void) with_none;

    m_scene.AddComponent<TransformComponent>(with_both, {});
    m_scene.AddComponent<VelocityComponent>(with_both, {});
    m_scene.AddComponent<TransformComponent>(with_trans, {});
    m_scene.AddComponent<VelocityComponent>(with_vel, {});

    uint32_t count = 0;
    m_scene.ForEach<TransformComponent, VelocityComponent>([&](EntityID, TransformComponent&, VelocityComponent&) { ++count; });
    EXPECT_EQ(count, 1u);
}

TEST_F(ECSFixture, ForEachVisitsManyEntities)
{
    constexpr uint32_t N = 1000;
    for (uint32_t i = 0; i < N; ++i)
    {
        EntityID id = m_scene.CreateEntity();
        m_scene.AddComponent<TransformComponent>(
            id,
            {
            {(float) i, 0.f, 0.f}
        });
    }
    uint32_t count = 0;
    m_scene.ForEach<TransformComponent>([&](EntityID, TransformComponent&) { ++count; });
    EXPECT_EQ(count, N);
}

TEST_F(ECSFixture, QueryForEach)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});

    Query<TransformComponent> q(m_scene);
    uint32_t                  count = 0;
    q.ForEach([&](EntityID, TransformComponent&) { ++count; });
    EXPECT_EQ(count, 1u);
}

TEST_F(ECSFixture, WorldCommandsSpawnEntity)
{
    WorldCommands cmds;
    cmds.Initialize(&m_manager.MainArena);

    EntityID spawned = INVALID_ENTITY;
    cmds.SpawnEntity({&spawned, [](void* ctx, EntityID id) { *static_cast<EntityID*>(ctx) = id; }});

    cmds.Flush(m_scene);
    EXPECT_TRUE(spawned.IsValid());
    EXPECT_TRUE(m_scene.IsAlive(spawned));
}

TEST_F(ECSFixture, WorldCommandsDestroyDeduplicates)
{
    EntityID      id = m_scene.CreateEntity();

    WorldCommands cmds;
    cmds.Initialize(&m_manager.MainArena);
    cmds.DestroyEntity(id);
    cmds.DestroyEntity(id);
    cmds.Flush(m_scene);

    EXPECT_FALSE(m_scene.IsAlive(id));
    EXPECT_EQ(m_scene.AliveCount(), 0u);
}

TEST_F(ECSFixture, WorldCommandsAddComponentAppliedOnFlush)
{
    EntityID      id = m_scene.CreateEntity();

    WorldCommands cmds;
    cmds.Initialize(&m_manager.MainArena);
    cmds.AddComponent<TransformComponent>(
        id,
        {
        {5.f, 6.f, 7.f}
    });

    EXPECT_FALSE(m_scene.HasComponent<TransformComponent>(id));
    cmds.Flush(m_scene);

    TransformComponent* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->Position.x, 5.f);
}

class ECSActorFixture : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    Scene         m_scene;
    ActorManager  m_actors;

    void          SetUp() override
    {
        m_manager.Initialize(ZMega(64), {});
        m_scene.Initialize(&m_manager.MainArena);
        m_actors.Initialize(&m_manager.MainArena, m_scene);
    }

    void TearDown() override
    {
        m_actors.Shutdown();
        m_scene.Shutdown();
    }
};

TEST_F(ECSActorFixture, CreateActorIsLive)
{
    ActorHandle h = m_actors.Create();
    EXPECT_TRUE(h.Valid());
    EXPECT_TRUE(m_actors.IsLive(h));
    ASSERT_NE(m_actors.Access(h), nullptr);
    EXPECT_TRUE(m_actors.Access(h)->IsAlive());
}

TEST_F(ECSActorFixture, DestroyActorHandleBecomeStale)
{
    ActorHandle h  = m_actors.Create();
    EntityID    id = m_actors.Access(h)->GetEntityID();

    m_actors.Destroy(h);

    EXPECT_FALSE(m_actors.IsLive(h));
    EXPECT_EQ(m_actors.Access(h), nullptr);
    EXPECT_FALSE(m_scene.IsAlive(id));
}

TEST_F(ECSActorFixture, ActorComponentVisibleToECS)
{
    ActorHandle h = m_actors.Create();
    m_actors.Access(h)->AddComponent<TransformComponent>({
        {3.f, 4.f, 5.f}
    });

    uint32_t count = 0;
    m_scene.ForEach<TransformComponent>([&](EntityID, TransformComponent& t) {
        EXPECT_FLOAT_EQ(t.Position.x, 3.f);
        ++count;
    });
    EXPECT_EQ(count, 1u);
}

TEST_F(ECSActorFixture, Tier1AndTier2VisibleTogether)
{
    ActorHandle h = m_actors.Create();
    m_actors.Access(h)->AddComponent<TransformComponent>({});

    EntityID tier2 = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(tier2, {});

    uint32_t count = 0;
    m_scene.ForEach<TransformComponent>([&](EntityID, TransformComponent&) { ++count; });
    EXPECT_EQ(count, 2u);
}

TEST_F(ECSActorFixture, DestroyStaleHandleSilentNoOp)
{
    ActorHandle h = m_actors.Create();
    m_actors.Destroy(h);
    m_actors.Destroy(h);
    EXPECT_EQ(m_actors.Access(h), nullptr);
}

class CallbackActor : public Actor
{
public:
    bool CreateCalled  = false;
    bool DestroyCalled = false;
    void OnCreate() override
    {
        CreateCalled = true;
    }
    void OnDestroy() override
    {
        DestroyCalled = true;
    }
};

TEST_F(ECSActorFixture, SubclassLifecycleCallbacksFire)
{
    ActorHandle h = m_actors.Create<CallbackActor>();
    auto*       a = static_cast<CallbackActor*>(m_actors.Access(h));
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->CreateCalled);
    EXPECT_FALSE(a->DestroyCalled);

    m_actors.Destroy(h);
    EXPECT_TRUE(a->DestroyCalled);
}
