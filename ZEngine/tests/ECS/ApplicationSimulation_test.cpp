#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Engine/FixedTimestepAccumulator.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <memory>

namespace
{
    using namespace ZEngine;
    using namespace ZEngine::ECS;
    using namespace ZEngine::ECS::Components;

    struct SimulationApplication : Applications::GameApplication
    {
        bool Enabled = true;
        void OverrideWindowConfiguration() override {}
        void OnInitializing() override {}
        void OnInitialized() override {}
        void OnEvent(Core::CoreEvent&) override {}
        void OnUpdate(float) override {}
        void OnPreRender() override {}
        void OnPostRender() override {}
        void OnRenderUI() override {}
        void OnClosing() override {}
        void OnClosed() override {}
        bool ShouldSimulate() const override
        {
            return Enabled;
        }
    };

    struct SimulationMarker
    {
        uint32_t Steps  = 0;
        float    LastDt = 0.f;
    };

    struct SimulationActor : Actor
    {
        uint32_t Steps      = 0;
        float    LastDt     = 0.f;
        bool     SawCommand = false;
        void     OnCreate() override
        {
            AddComponent<TransformComponent>({});
        }
        void OnTick(float dt) override
        {
            ++Steps;
            LastDt                                          = dt;
            SawCommand                                      = HasComponent<SimulationMarker>();
            GetComponent<TransformComponent>()->Position.y += 2.f;
        }
    };

    struct StopSimulationActor : SimulationActor
    {
        SimulationApplication* App     = nullptr;
        EngineContext*         Context = nullptr;
        void                   OnTick(float dt) override
        {
            SimulationActor::OnTick(dt);
            if (App)
                App->Enabled = false;
            if (Context)
                Context->CloseRequested.value.store(true, std::memory_order_release);
        }
    };

    class ApplicationSimulationTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager      Memory   = {};
        Scene                            World    = {};
        WorldTick                        Tick     = {};
        WorldCommands                    Commands = {};
        ActorManager                     Actors   = {};
        SimulationApplication            App      = {};
        std::unique_ptr<EngineContext>   Context  = {};
        Timing::FixedTimestepAccumulator Accumulator{};
        EntityID                         Entity = {};

        void                             SetUp() override
        {
            Memory.Initialize(ZMega(64), {});
            World.Initialize(&Memory.MainArena);
            Tick.Initialize(&Memory.MainArena);
            Commands.Initialize(&Memory.MainArena);
            Actors.Initialize(&Memory.MainArena, World);
            Context                = std::make_unique<EngineContext>();
            Context->App           = &App;
            Context->Scene         = &World;
            Context->WorldTick     = &Tick;
            Context->WorldCommands = &Commands;
            Context->ActorManager  = &Actors;
            Entity                 = World.CreateEntity();
            World.AddComponent<TransformComponent>(
                Entity,
                {
                .Position = { 4.f, 0.f, 0.f},
                  .PreviousPosition = {-7.f, 0.f, 0.f}
            });
            World.AddComponent<SimulationMarker>(Entity, {});
        }
        void TearDown() override
        {
            Commands.Clear();
            Actors.Shutdown();
            World.Shutdown();
            Context.reset();
            Memory.Shutdown();
        }
        void RegisterSystem()
        {
            (void) Tick.RegisterSystem(
                [](Scene& world, float dt, WorldCommands&) {
                    world.ForEach<TransformComponent, SimulationMarker>([](EntityID, TransformComponent& transform, SimulationMarker& marker) {
                        transform.Position.x += 1.f;
                        ++marker.Steps;
                    });
                    world.ForEach<SimulationMarker>([&](EntityID, SimulationMarker& marker) { marker.LastDt = dt; });
                },
                {.WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()) | MaskBit(ComponentTypeOf<SimulationMarker>())});
            Tick.Commit();
        }
        float Advance(float dt)
        {
            return Engine::AdvanceSimulation(*Context, Accumulator, dt);
        }
    };
} // namespace

TEST_F(ApplicationSimulationTest, RuntimeApplicationsSimulateByDefault)
{
    EXPECT_TRUE(App.GameApplication::ShouldSimulate());
    RegisterSystem();
    const auto dt = Accumulator.FixedDt();
    EXPECT_NEAR(Advance(2.5f * dt), .5f, 1.e-5f);
    EXPECT_EQ(Accumulator.StepCount(), 2u);
    EXPECT_EQ(World.GetComponent<SimulationMarker>(Entity)->Steps, 2u);
    EXPECT_FLOAT_EQ(World.GetComponent<SimulationMarker>(Entity)->LastDt, dt);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 6.f);
}

TEST_F(ApplicationSimulationTest, SuspendedFramesCannotRunSystemsActorsCommandsOrTransformSnapshots)
{
    RegisterSystem();
    const auto handle = Actors.Create<SimulationActor>();
    auto*      actor  = static_cast<SimulationActor*>(Actors.Access(handle));
    Commands.DestroyEntity(Entity);
    uint32_t callbacks = 0;
    Commands.SpawnEntity({&callbacks, [](void* context, EntityID) { ++*static_cast<uint32_t*>(context); }});
    App.Enabled = false;
    Accumulator.Accumulate(Accumulator.FixedDt() * .75f);
    const auto bytes = Memory.MainArena.m_current_offset;
    for (uint32_t frame = 0; frame < 2000; ++frame)
        EXPECT_FLOAT_EQ(Advance(10.f), 1.f);
    EXPECT_EQ(Memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(Accumulator.StepCount(), 0u);
    EXPECT_FLOAT_EQ(Accumulator.Alpha(), 0.f);
    EXPECT_TRUE(World.IsAlive(Entity));
    EXPECT_EQ(World.GetComponent<SimulationMarker>(Entity)->Steps, 0u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->Position.x, 4.f);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->PreviousPosition.x, -7.f);
    EXPECT_EQ(actor->Steps, 0u);
    EXPECT_EQ(callbacks, 0u);
    EXPECT_FALSE(Commands.IsEmpty()); // lifecycle owner decides how to retire queued work
}

TEST_F(ApplicationSimulationTest, ResumeDoesNotAccumulatePausedTimeOrRetainAFractionalStep)
{
    RegisterSystem();
    const auto dt = Accumulator.FixedDt();
    EXPECT_NEAR(Advance(.75f * dt), .75f, 1.e-5f);
    App.Enabled = false;
    EXPECT_FLOAT_EQ(Advance(3600.f), 1.f);
    App.Enabled = true;
    EXPECT_NEAR(Advance(.5f * dt), .5f, 1.e-5f);
    EXPECT_EQ(Accumulator.StepCount(), 0u);
    EXPECT_NEAR(Advance(.75f * dt), .25f, 1.e-5f);
    EXPECT_EQ(Accumulator.StepCount(), 1u);
}

TEST_F(ApplicationSimulationTest, RuntimeStepFlushesCommandsBeforeActorsAndSnapshotsAfterActors)
{
    const auto handle = Actors.Create<SimulationActor>();
    auto*      actor  = static_cast<SimulationActor*>(Actors.Access(handle));
    const auto id     = actor->GetEntityID();
    (void) Tick.RegisterSystem(
        [](Scene& world, float, WorldCommands& commands) {
            world.ForEach<TransformComponent>([&](EntityID entity, TransformComponent& transform) {
                transform.Position.x += 3.f;
                if (!world.HasComponent<SimulationMarker>(entity))
                    commands.AddComponent<SimulationMarker>(entity, {});
            });
        },
        {.ReadMask = MaskBit(ComponentTypeOf<SimulationMarker>()), .WriteMask = MaskBit(ComponentTypeOf<TransformComponent>()), .UsesCommands = true});
    Tick.Commit();
    (void) Advance(Accumulator.FixedDt());
    EXPECT_EQ(actor->Steps, 1u);
    EXPECT_FLOAT_EQ(actor->LastDt, Accumulator.FixedDt());
    EXPECT_TRUE(actor->SawCommand);
    const auto* transform = World.GetComponent<TransformComponent>(id);
    EXPECT_FLOAT_EQ(transform->Position.x, 3.f);
    EXPECT_FLOAT_EQ(transform->Position.y, 2.f);
    EXPECT_FLOAT_EQ(transform->PreviousPosition.x, 3.f);
    EXPECT_FLOAT_EQ(transform->PreviousPosition.y, 2.f);
    EXPECT_TRUE(Commands.IsEmpty());
}

TEST_F(ApplicationSimulationTest, ActorOnlyApplicationsDoNotRequireAnyECSSystems)
{
    const auto handle = Actors.Create<SimulationActor>();
    auto*      actor  = static_cast<SimulationActor*>(Actors.Access(handle));
    EXPECT_EQ(Tick.SystemCount(), 0u);
    EXPECT_NEAR(Advance(2.5f * Accumulator.FixedDt()), .5f, 1.e-5f);
    EXPECT_EQ(actor->Steps, 2u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(actor->GetEntityID())->Position.y, 4.f);
}

TEST_F(ApplicationSimulationTest, CommandOnlyApplicationsFlushAtTheFixedStepBoundary)
{
    EntityID spawned = {};
    Commands.SpawnEntity({&spawned, [](void* context, EntityID entity) { *static_cast<EntityID*>(context) = entity; }});
    const auto dt = Accumulator.FixedDt();
    (void) Advance(.5f * dt);
    EXPECT_FALSE(spawned.IsValid());
    EXPECT_FALSE(Commands.IsEmpty());
    (void) Advance(.5f * dt);
    EXPECT_TRUE(World.IsAlive(spawned));
    EXPECT_TRUE(Commands.IsEmpty());
    EXPECT_EQ(Accumulator.StepCount(), 1u);
}

TEST_F(ApplicationSimulationTest, IdleRuntimeWorldSkipsTransformWalkAndElapsedTimeDebt)
{
    const auto bytes = Memory.MainArena.m_current_offset;
    for (uint32_t frame = 0; frame < 2000; ++frame)
        EXPECT_FLOAT_EQ(Advance(100.f), 1.f);
    EXPECT_EQ(Memory.MainArena.m_current_offset, bytes);
    EXPECT_EQ(Accumulator.StepCount(), 0u);
    EXPECT_FLOAT_EQ(World.GetComponent<TransformComponent>(Entity)->PreviousPosition.x, -7.f);
    RegisterSystem();
    (void) Advance(Accumulator.FixedDt() * .5f);
    EXPECT_EQ(Accumulator.StepCount(), 0u);
}

TEST_F(ApplicationSimulationTest, InvalidElapsedTimeResetsDebtWithoutPoisoningLaterFrames)
{
    RegisterSystem();
    const float invalid[] = {-1.f, std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()};
    for (const auto dt : invalid)
    {
        Accumulator.Accumulate(.75f * Accumulator.FixedDt());
        EXPECT_FLOAT_EQ(Advance(dt), 1.f);
        EXPECT_FLOAT_EQ(Accumulator.Alpha(), 0.f);
        EXPECT_EQ(Accumulator.StepCount(), 0u);
    }
    (void) Advance(Accumulator.FixedDt());
    EXPECT_EQ(Accumulator.StepCount(), 1u);
}

TEST_F(ApplicationSimulationTest, ForcedCloseAndIncompleteWorldServicesFailClosed)
{
    RegisterSystem();
    const auto rejected = [&] {
        Accumulator.Accumulate(.5f * Accumulator.FixedDt());
        EXPECT_FLOAT_EQ(Advance(10.f), 1.f);
        EXPECT_FLOAT_EQ(Accumulator.Alpha(), 0.f);
        EXPECT_EQ(Accumulator.StepCount(), 0u);
    };
    Context->CloseRequested.value.store(true);
    rejected();
    Context->CloseRequested.value.store(false);
    Context->App = nullptr;
    rejected();
    Context->App   = &App;
    Context->Scene = nullptr;
    rejected();
    Context->Scene     = &World;
    Context->WorldTick = nullptr;
    rejected();
    Context->WorldTick     = &Tick;
    Context->WorldCommands = nullptr;
    rejected();
    Context->WorldCommands = &Commands;
    Context->ActorManager  = nullptr;
    rejected();
    ActorManager unbound  = {};
    Context->ActorManager = &unbound;
    rejected();
    Context->ActorManager = &Actors;
    EXPECT_EQ(World.GetComponent<SimulationMarker>(Entity)->Steps, 0u);
}

TEST_F(ApplicationSimulationTest, ZeroElapsedTimePreservesRuntimeInterpolationButCannotStep)
{
    RegisterSystem();
    (void) Advance(.5f * Accumulator.FixedDt());
    EXPECT_FLOAT_EQ(Advance(0.f), .5f);
    EXPECT_EQ(Accumulator.StepCount(), 0u);
}

TEST_F(ApplicationSimulationTest, LongRuntimeFrameRetainsTheExistingFixedStepBudget)
{
    RegisterSystem();
    const auto alpha = Advance(100.f);
    EXPECT_EQ(Accumulator.StepCount(), 5u);
    EXPECT_EQ(World.GetComponent<SimulationMarker>(Entity)->Steps, 5u);
    EXPECT_TRUE(std::isfinite(alpha));
    EXPECT_GE(alpha, 0.f);
    EXPECT_LT(alpha, 1.f);
}

TEST_F(ApplicationSimulationTest, RepeatedLongFramesAndResetsKeepCatchupBoundedWithoutTimestampOverflow)
{
    RegisterSystem();
    for (uint32_t frame = 0; frame < 32; ++frame)
    {
        const auto alpha = Advance(100.f);
        EXPECT_EQ(Accumulator.StepCount(), (frame + 1) * 5u);
        EXPECT_EQ(World.GetComponent<SimulationMarker>(Entity)->Steps, (frame + 1) * 5u);
        EXPECT_TRUE(std::isfinite(alpha));
        EXPECT_GE(alpha, 0.f);
        EXPECT_LT(alpha, 1.f);
        Accumulator.Reset();
    }
}

TEST_F(ApplicationSimulationTest, PauseRequestedByAnActorStopsFurtherCatchupSteps)
{
    const auto handle = Actors.Create<StopSimulationActor>();
    auto*      actor  = static_cast<StopSimulationActor*>(Actors.Access(handle));
    actor->App        = &App;
    EXPECT_FLOAT_EQ(Advance(4.f * Accumulator.FixedDt()), 1.f);
    EXPECT_EQ(actor->Steps, 1u);
    EXPECT_EQ(Accumulator.StepCount(), 1u);
    EXPECT_FLOAT_EQ(Accumulator.Alpha(), 0.f);
}

TEST_F(ApplicationSimulationTest, ForcedCloseRequestedByAnActorStopsFurtherCatchupSteps)
{
    const auto handle = Actors.Create<StopSimulationActor>();
    auto*      actor  = static_cast<StopSimulationActor*>(Actors.Access(handle));
    actor->Context    = Context.get();
    EXPECT_FLOAT_EQ(Advance(4.f * Accumulator.FixedDt()), 1.f);
    EXPECT_EQ(actor->Steps, 1u);
    EXPECT_EQ(Accumulator.StepCount(), 1u);
    EXPECT_TRUE(Context->CloseRequested.value.load());
}
