#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/WorldCommands.h>
#include <ZEngine/ECS/WorldTick.h>
#include <gtest/gtest.h>
#include <atomic>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;

// Shared execution order recorder — written by systems, verified by tests
static std::atomic<uint32_t> g_exec_order{0};
static uint32_t              g_exec_seq[16] = {};

struct VelocityComponent
{
    float X = 0.f;
};
struct AudioComponent
{
    float Volume = 0.f;
};

static_assert(sizeof(VelocityComponent) <= 64);
static_assert(sizeof(AudioComponent) <= 64);

class SchedulerFixture : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    Scene         m_scene;
    WorldCommands m_commands;
    WorldTick     m_tick;

    void          SetUp() override
    {
        m_manager.Initialize(ZMega(64), {});
        m_scene.Initialize(&m_manager.MainArena);
        m_commands.Initialize(&m_manager.MainArena);
        m_tick.Initialize(&m_manager.MainArena);
        g_exec_order.store(0, std::memory_order_relaxed);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }
};

// Test 1 — Two independent systems run in the same wave
TEST_F(SchedulerFixture, IndependentSystemsInSameWave)
{
    // A writes Transform, B writes Velocity — no overlap → same wave
    auto SystemA = [](Scene&, float, WorldCommands&) {};
    auto SystemB = [](Scene&, float, WorldCommands&) {};

    m_tick.RegisterSystem(SystemA, {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    m_tick.RegisterSystem(SystemB, {.WriteMask = MaskBit(ComponentTypeOf<VelocityComponent>())});
    m_tick.Commit();

    EXPECT_EQ(m_tick.WaveCount(), 1u);
    m_tick.Tick(m_scene, 0.016f, m_commands);
}

// Test 2 — Conflicting systems with OrderBefore run in separate waves
TEST_F(SchedulerFixture, ConflictingSystemsInSeparateWaves)
{
    // A writes Transform, B reads Transform → conflict → must be in different waves
    auto     SystemA = [](Scene&, float, WorldCommands&) {};
    auto     SystemB = [](Scene&, float, WorldCommands&) {};

    SystemID a       = m_tick.RegisterSystem(SystemA, {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    SystemID b       = m_tick.RegisterSystem(SystemB, {.ReadMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    m_tick.OrderBefore(a, b);
    m_tick.Commit();

    EXPECT_EQ(m_tick.WaveCount(), 2u);
    m_tick.Tick(m_scene, 0.016f, m_commands);
}

// Test 3 — Systems execute in declared order (write before read, verified via component state)
TEST_F(SchedulerFixture, WriteBeforeReadOrderVerifiedViaState)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(
        id,
        {
        {0.f, 0.f, 0.f}
    });

    // Writer sets Position.x = 99
    auto         Writer       = [](Scene& s, float, WorldCommands&) { s.ForEach<TransformComponent>([](EntityID, TransformComponent& t) { t.Position.x = 99.f; }); };

    // Reader captures the value — must see 99, not 0
    static float s_read_value = 0.f;
    auto         Reader       = [](Scene& s, float, WorldCommands&) { s.ForEach<TransformComponent>([](EntityID, TransformComponent& t) { s_read_value = t.Position.x; }); };

    SystemID     w            = m_tick.RegisterSystem(Writer, {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    SystemID     r            = m_tick.RegisterSystem(Reader, {.ReadMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    m_tick.OrderBefore(w, r);
    m_tick.Commit();

    s_read_value = 0.f;
    m_tick.Tick(m_scene, 0.016f, m_commands);

    EXPECT_FLOAT_EQ(s_read_value, 99.f);
}

// Test 4 — WorldCommands deferred spawn is applied after Tick
TEST_F(SchedulerFixture, WorldCommandsSpawnAppliedAfterTick)
{
    static EntityID s_spawned;
    s_spawned    = INVALID_ENTITY;

    auto Spawner = [](Scene&, float, WorldCommands& cmds) { cmds.SpawnEntity({&s_spawned, [](void* ctx, EntityID id) { *static_cast<EntityID*>(ctx) = id; }}); };

    m_tick.RegisterSystem(Spawner, {});
    m_tick.Commit();

    m_tick.Tick(m_scene, 0.016f, m_commands);

    // Entity not yet alive — Flush hasn't run
    EXPECT_FALSE(s_spawned.IsValid());

    m_commands.Flush(m_scene);

    EXPECT_TRUE(s_spawned.IsValid());
    EXPECT_TRUE(m_scene.IsAlive(s_spawned));
}

// Test 5 — Three-system chain produces three sequential waves
TEST_F(SchedulerFixture, ThreeSystemChainProducesThreeWaves)
{
    auto     A = [](Scene&, float, WorldCommands&) {};
    auto     B = [](Scene&, float, WorldCommands&) {};
    auto     C = [](Scene&, float, WorldCommands&) {};

    // A writes Transform, B reads Transform + writes Velocity, C reads Velocity
    SystemID a = m_tick.RegisterSystem(A, {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>())});
    SystemID b = m_tick.RegisterSystem(
        B,
        {
        .ReadMask  = MaskBit(ComponentTypeOf<TransformComponent>()),
        .WriteMask = MaskBit(ComponentTypeOf<VelocityComponent>()),
        });
    SystemID c = m_tick.RegisterSystem(C, {.ReadMask = MaskBit(ComponentTypeOf<VelocityComponent>())});

    m_tick.OrderBefore(a, b);
    m_tick.OrderBefore(b, c);
    m_tick.Commit();

    EXPECT_EQ(m_tick.WaveCount(), 3u);
    m_tick.Tick(m_scene, 0.016f, m_commands);
}

// Test 6 — Two parallel systems both calling SpawnEntity produce two valid
// entities with no corruption. This is the core regression test for the
// staging-buffer fix: without it, concurrent push() calls on the shared
// WorldCommands would race and lose one or both spawns.
TEST_F(SchedulerFixture, ParallelSystemsBothSpawn_BothEntitiesCreated)
{
    static std::atomic<int> s_spawn_count{0};

    auto                    SpawnerA = [](Scene&, float, WorldCommands& cmds) { cmds.SpawnEntity({nullptr, [](void*, EntityID) { s_spawn_count.fetch_add(1, std::memory_order_relaxed); }}); };
    auto                    SpawnerB = [](Scene&, float, WorldCommands& cmds) { cmds.SpawnEntity({nullptr, [](void*, EntityID) { s_spawn_count.fetch_add(1, std::memory_order_relaxed); }}); };

    // Disjoint masks → same wave (no conflict, no OrderBefore required).
    m_tick.RegisterSystem(SpawnerA, {.UsesCommands = true});
    m_tick.RegisterSystem(SpawnerB, {.UsesCommands = true});
    m_tick.Commit();

    EXPECT_EQ(m_tick.WaveCount(), 1u);

    s_spawn_count.store(0);
    m_tick.Tick(m_scene, 0.016f, m_commands);
    m_commands.Flush(m_scene);

    EXPECT_EQ(s_spawn_count.load(), 2) << "both spawn callbacks must fire";
    EXPECT_EQ(m_scene.AliveCount(), 2u) << "both entities must be created";
}

// Test 7 — SpawnCallbackIndex is correctly remapped when staging buffers are
// merged. Each spawner attaches a distinct callback; verify the correct callback
// fires for the correct entity (index fixup correctness).
TEST_F(SchedulerFixture, ParallelSpawnCallbacks_IndicesRemappedCorrectly)
{
    static EntityID s_from_a = INVALID_ENTITY;
    static EntityID s_from_b = INVALID_ENTITY;

    auto            SpawnerA = [](Scene&, float, WorldCommands& cmds) { cmds.SpawnEntity({&s_from_a, [](void* ctx, EntityID id) { *static_cast<EntityID*>(ctx) = id; }}); };
    auto            SpawnerB = [](Scene&, float, WorldCommands& cmds) { cmds.SpawnEntity({&s_from_b, [](void* ctx, EntityID id) { *static_cast<EntityID*>(ctx) = id; }}); };

    m_tick.RegisterSystem(SpawnerA, {.UsesCommands = true});
    m_tick.RegisterSystem(SpawnerB, {.UsesCommands = true});
    m_tick.Commit();

    s_from_a = INVALID_ENTITY;
    s_from_b = INVALID_ENTITY;

    m_tick.Tick(m_scene, 0.016f, m_commands);
    m_commands.Flush(m_scene);

    EXPECT_TRUE(s_from_a.IsValid()) << "SpawnerA callback must have fired";
    EXPECT_TRUE(s_from_b.IsValid()) << "SpawnerB callback must have fired";
    EXPECT_NE(s_from_a, s_from_b) << "each spawn must produce a distinct entity";
    EXPECT_TRUE(m_scene.IsAlive(s_from_a));
    EXPECT_TRUE(m_scene.IsAlive(s_from_b));
}
