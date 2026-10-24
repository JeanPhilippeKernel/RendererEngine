#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>
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
    MemoryManager m_manager = {};
    Scene         m_scene   = {};

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

TEST_F(ECSFixture, ComponentLifetimeDistinguishesRemoveAddFromValueAndUnrelatedMembershipChanges)
{
    const auto id = m_scene.CreateEntity();
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
    m_scene.AddComponent<TransformComponent>(id, {});
    const auto original = m_scene.ComponentLifetime<TransformComponent>(id);
    ASSERT_NE(original, 0u);
    m_scene.GetComponent<TransformComponent>(id)->Position.x = 3.f;
    m_scene.AddComponent<VelocityComponent>(id, {});
    m_scene.RemoveComponent<VelocityComponent>(id);
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(id), original);
    const auto value = *m_scene.GetComponent<TransformComponent>(id);
    m_scene.RemoveComponent<TransformComponent>(id);
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
    m_scene.AddComponent<TransformComponent>(id, value);
    EXPECT_NE(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
    EXPECT_NE(m_scene.ComponentLifetime<TransformComponent>(id), original);
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(INVALID_ENTITY), 0u);
}

TEST_F(ECSFixture, ComponentLifetimesSurviveDenseGrowthAndSwapRemoval)
{
    constexpr uint32_t                            count     = 600;
    Core::Containers::FixedArray<EntityID, count> ids       = {};
    Core::Containers::FixedArray<uint64_t, count> lifetimes = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        ids[i] = m_scene.CreateEntity();
        m_scene.AddComponent<TransformComponent>(
            ids[i],
            {
            .Position = {float(i), 0.f, 0.f}
        });
        lifetimes[i] = m_scene.ComponentLifetime<TransformComponent>(ids[i]);
        ASSERT_NE(lifetimes[i], 0u);
    }
    for (uint32_t i = 0; i < count; ++i)
        EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(ids[i]), lifetimes[i]);
    for (uint32_t i = 0; i < count; i += 2)
        m_scene.RemoveComponent<TransformComponent>(ids[i]);
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(ids[i]), i % 2 ? lifetimes[i] : 0u);
        if (i % 2)
            EXPECT_FLOAT_EQ(m_scene.GetComponent<TransformComponent>(ids[i])->Position.x, float(i));
    }
    uint32_t remaining = 0;
    m_scene.ForEach<TransformComponent>([&](EntityID id, TransformComponent&) {
        EXPECT_NE(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
        ++remaining;
    });
    EXPECT_EQ(remaining, count / 2);
}

TEST_F(ECSFixture, EntityRetirementAndRecycleNeverResurrectAComponentLifetime)
{
    const auto id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    const auto lifetime = m_scene.ComponentLifetime<TransformComponent>(id);
    ASSERT_NE(lifetime, 0u);
    m_scene.DestroyEntity(id);
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
    const auto replacement = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(replacement, {});
    EXPECT_NE(m_scene.ComponentLifetime<TransformComponent>(replacement), 0u);
    EXPECT_NE(m_scene.ComponentLifetime<TransformComponent>(replacement), lifetime);
    EXPECT_EQ(m_scene.ComponentLifetime<TransformComponent>(id), 0u);
}

TEST_F(ECSFixture, ComponentLifetimeQueriesAreConstAndReuseArenaStorage)
{
    const auto id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    const Scene& scene    = m_scene;
    const auto   lifetime = scene.ComponentLifetime<TransformComponent>(id);
    const auto   bytes    = m_manager.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
    {
        EXPECT_EQ(scene.ComponentLifetime<TransformComponent>(id), lifetime);
        EXPECT_EQ(scene.ComponentLifetime<VelocityComponent>(id), 0u);
        EXPECT_EQ(scene.ComponentLifetime<TransformComponent>(INVALID_ENTITY), 0u);
    }
    EXPECT_EQ(m_manager.MainArena.m_current_offset, bytes);
}

TEST_F(ECSFixture, TypedAuthoredFieldLifetimeDispatchCoversEveryFieldAndRejectsUnknownFields)
{
    const auto id      = m_scene.CreateEntity();
    const auto install = [&]<typename T>(uint32_t count) {
        for (uint32_t i = 0; i < count; ++i)
        {
            m_scene.AddComponent<T>(id, {});
            if (i + 1 < count)
                m_scene.RemoveComponent<T>(id);
        }
        return m_scene.ComponentLifetime<T>(id);
    };
    const auto light  = install.template operator()<LightComponent>(2);
    const auto camera = install.template operator()<CameraComponent>(3);
    const auto body   = install.template operator()<RigidBodyComponent>(4);
    ASSERT_NE(light, camera);
    ASSERT_NE(camera, body);
    ASSERT_NE(light, body);
    for (auto field : {AuthoredFloatField::LightRange, AuthoredFloatField::LightSpotAngle, AuthoredFloatField::LightIntensity})
        EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, id, field), light);
    for (auto field : {AuthoredFloatField::CameraFar, AuthoredFloatField::CameraFovY, AuthoredFloatField::CameraNear, AuthoredFloatField::CameraAspectRatio})
        EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, id, field), camera);
    for (auto field : {AuthoredFloatField::RigidBodyMass, AuthoredFloatField::RigidBodyFriction, AuthoredFloatField::RigidBodyRestitution})
        EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, id, field), body);
    EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, id, AuthoredFloatField::Count), 0u);
    EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, id, static_cast<AuthoredFloatField>(255)), 0u);
    EXPECT_EQ(AuthoredFloatFieldLifetime(m_scene, INVALID_ENTITY, AuthoredFloatField::LightRange), 0u);
}

TEST_F(ECSFixture, ComponentOwnerMetadataIsIncludedInFreshAndIncrementalArenaBounds)
{
    struct alignas(64) AlignedComponent
    {
        uint64_t Value = 0;
    };
    const auto exercise = [&]<typename T>() {
        constexpr uint32_t entity_capacity = 1024;
        for (const uint32_t count : {1u, 256u, 257u, 512u, 513u, 1024u})
        {
            SCOPED_TRACE(count);
            ComponentStorage<T> storage        = {};
            const auto          initial_offset = m_manager.MainArena.m_current_offset;
            storage.Initialize(&m_manager.MainArena, entity_capacity);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto offset = m_manager.MainArena.m_current_offset;
                const auto bound  = storage.AdditionMemoryBound();
                storage.Add({i, 1}, {});
                EXPECT_LE(m_manager.MainArena.m_current_offset - offset, bound);
            }
            EXPECT_LE(m_manager.MainArena.m_current_offset - initial_offset, ComponentStorage<T>::AllocationUpperBound(count, entity_capacity));
        }
    };
    exercise.template operator()<VelocityComponent>();
    exercise.template operator()<AlignedComponent>();
}

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
