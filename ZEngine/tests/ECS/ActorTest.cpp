#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <gtest/gtest.h>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;

class ActorFixture : public ::testing::Test
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

// 1. Create returns a valid, live handle.
TEST_F(ActorFixture, CreateReturnsValidHandle)
{
    ActorHandle h = m_actors.Create();
    EXPECT_TRUE(h.Valid());
    EXPECT_TRUE(m_actors.IsLive(h));
    ASSERT_NE(m_actors.Access(h), nullptr);
    EXPECT_TRUE(m_actors.Access(h)->IsAlive());
}

class LifecycleActor : public Actor
{
public:
    bool  CreateFired  = false;
    bool  DestroyFired = false;
    float Accumulated  = 0.f;

    void  OnCreate() override
    {
        CreateFired = true;
    }
    void OnDestroy() override
    {
        DestroyFired = true;
    }
    void OnTick(float dt) override
    {
        Accumulated += dt;
    }
};

// 2. OnCreate fires immediately after Create().
TEST_F(ActorFixture, OnCreateFiresOnCreate)
{
    ActorHandle     h = m_actors.Create<LifecycleActor>();
    LifecycleActor* a = static_cast<LifecycleActor*>(m_actors.Access(h));
    ASSERT_NE(a, nullptr);
    EXPECT_TRUE(a->CreateFired);
}

// 3. Component added through Actor is retrievable with correct values.
TEST_F(ActorFixture, ComponentAddGetRoundtrip)
{
    ActorHandle h     = m_actors.Create();
    Actor*      actor = m_actors.Access(h);
    ASSERT_NE(actor, nullptr);

    actor->AddComponent<TransformComponent>({
        {7.f, 8.f, 9.f}
    });

    TransformComponent* t = actor->GetComponent<TransformComponent>();
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->Position.x, 7.f);
    EXPECT_FLOAT_EQ(t->Position.y, 8.f);
    EXPECT_FLOAT_EQ(t->Position.z, 9.f);
    EXPECT_TRUE(actor->HasComponent<TransformComponent>());
}

// 4. Destroy makes the handle stale and Access returns nullptr.
TEST_F(ActorFixture, DestroyMakesHandleStale)
{
    ActorHandle h  = m_actors.Create();
    EntityID    id = m_actors.Access(h)->GetEntityID();

    m_actors.Destroy(h);

    EXPECT_FALSE(m_actors.IsLive(h));
    EXPECT_EQ(m_actors.Access(h), nullptr);
    EXPECT_FALSE(m_scene.IsAlive(id));
}

// 5. OnDestroy fires when Destroy() is called.
TEST_F(ActorFixture, OnDestroyFiresOnDestroy)
{
    ActorHandle     h = m_actors.Create<LifecycleActor>();
    LifecycleActor* a = static_cast<LifecycleActor*>(m_actors.Access(h));
    ASSERT_NE(a, nullptr);
    EXPECT_FALSE(a->DestroyFired);

    m_actors.Destroy(h);
    EXPECT_TRUE(a->DestroyFired);
}

// 6. Scene::ForEach visits components attached to Actor-backed entities.
TEST_F(ActorFixture, ForEachVisitsActorEntity)
{
    ActorHandle h = m_actors.Create();
    m_actors.Access(h)->AddComponent<TransformComponent>({
        {1.f, 2.f, 3.f}
    });

    uint32_t count = 0;
    m_scene.ForEach<TransformComponent>([&](EntityID, TransformComponent& t) {
        EXPECT_FLOAT_EQ(t.Position.x, 1.f);
        ++count;
    });
    EXPECT_EQ(count, 1u);
}

// 7. Tick dispatches OnTick to all live Actors with the correct dt.
TEST_F(ActorFixture, TickDispatchesOnTick)
{
    ActorHandle     h = m_actors.Create<LifecycleActor>();
    LifecycleActor* a = static_cast<LifecycleActor*>(m_actors.Access(h));
    ASSERT_NE(a, nullptr);

    m_actors.Tick(1.f);
    m_actors.Tick(1.f);

    EXPECT_FLOAT_EQ(a->Accumulated, 2.f);
}

// 8. Duplicate EntityID wrapping should trigger a debug assert.
// DISABLED: ActorManager::Create<T>() has no duplicate-EntityID guard yet.
// When the guard is added, re-enable this test and supply a path that
// injects the same EntityID into two Actors.
TEST_F(ActorFixture, DISABLED_DuplicateEntityIDAsserts) {}
