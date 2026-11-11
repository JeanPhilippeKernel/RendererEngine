#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/SceneInstance.h>
#include <ZEngine/ECS/ScenePublication.h>
#include <gtest/gtest.h>
#include <atomic>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

static_assert(!std::is_copy_constructible_v<SceneStorageLease>);
static_assert(std::is_nothrow_move_constructible_v<SceneStorageLease>);
static_assert(std::is_nothrow_move_assignable_v<SceneStorageLease>);
static_assert(!std::is_copy_constructible_v<SceneInstance>);

class SceneInstanceTest : public testing::Test
{
protected:
    MemoryManager                  Memory      = {};
    ArenaAllocator                 Arenas[2]   = {};
    PaddedAtomic<bool>             Occupied[2] = {};
    SceneStorageSlot               Slots[2]    = {};
    Scene                          Source      = {};
    SceneComponentSchemaRegistry   Schemas     = {};
    SceneDiagnostics               Diagnostics = {};
    SceneDocumentSettings          Settings    = {};
    std::unique_ptr<SceneInstance> Instance    = {};

    void                           SetUp() override
    {
        Memory.Initialize(ZMega(96), {});
        for (uint32_t i = 0; i < 2; ++i)
        {
            Memory.MainArena.CreateSubArena(ZMega(32), &Arenas[i], "SceneInstanceTest/Slot");
            Slots[i] = {&Arenas[i], &Occupied[i]};
        }
        Source.Initialize(&Memory.MainArena);
        Schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
        Settings.UUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        Settings.Name = "Engine-owned scene";
    }

    void TearDown() override
    {
        Instance.reset();
        Source.Shutdown();
        EXPECT_FALSE(Occupied[0].value.load());
        EXPECT_FALSE(Occupied[1].value.load());
    }

    bool Acquire(SceneStorageLease* lease)
    {
        return SceneStorageLease::Acquire({Slots, 2}, lease);
    }

    bool Create()
    {
        SceneStorageLease lease = {};
        return Acquire(&lease) && SceneInstance::Create(std::move(lease), &Instance, &Diagnostics);
    }

    void AddSourceEntity()
    {
        const auto entity = Source.CreateEntity();
        Source.AddComponent<UUIDComponent>(entity, {*uuids::uuid::from_string("22222222-2222-4222-8222-222222222222")});
        Source.AddComponent<TransformComponent>(
            entity,
            {
            .Position = {2.f, 3.f, 4.f}
        });
    }

    bool Capture(SceneDocument* document)
    {
        return SceneDocument::Capture(Source, Settings, Schemas, document, &Diagnostics);
    }
};

TEST_F(SceneInstanceTest, SourceCpuLeaseRetiresBeforeItsSceneStorageIsReused)
{
    TLSFSlab slab = {};
    slab.Init(&Memory.MainArena, ZMega(1));
    const auto baseline = slab.GetStatistics();
    using Payload       = ZEngine::Managers::AssetPayloadAllocation;
    auto* memory        = slab.TryRealloc(nullptr, sizeof(Payload) + 128);
    ASSERT_NE(memory, nullptr);
    auto* payload                      = new (memory) Payload{};
    payload->Slab                      = &slab;
    std::atomic<uint32_t> logical_pins = 0;
    payload->OwnerPins                 = &logical_pins;
    ASSERT_TRUE(payload->Arena.InitializeBorrowed(static_cast<char*>(memory) + sizeof(Payload), 128, "SceneInstanceTest/Payload"));
    ASSERT_TRUE(Create());
    ASSERT_TRUE(Instance->SourceAssets().Initialize(Instance->Arena()));
    ASSERT_TRUE(Instance->SourceAssets().Retain(0, payload));
    Payload::Destroy(payload); // The scene now owns the only payload reference.
    EXPECT_EQ(logical_pins.load(), 1u);
    Instance.reset();
    EXPECT_EQ(logical_pins.load(), 0u);
    EXPECT_EQ(slab.GetStatistics().LiveBytes, baseline.LiveBytes);
    EXPECT_EQ(slab.GetStatistics().LiveBlocks, baseline.LiveBlocks);
    EXPECT_FALSE(Occupied[0].value.load());
    ASSERT_TRUE(Create());
    Instance.reset();
    slab.Shutdown();
}

TEST_F(SceneInstanceTest, BuiltInCodecsExplicitlyAdmitPureWorkerExecution)
{
    ASSERT_TRUE(Schemas.FreezeForWorker(&Diagnostics));
    EXPECT_TRUE(Schemas.Frozen());
    EXPECT_EQ(Diagnostics.ErrorCount, 0u);
    EXPECT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics)); // Idempotent, no new registrations.
}

TEST_F(SceneInstanceTest, SourceDocumentParserAndInputShareLeasedStorageAndCannotEscapeTheirOwner)
{
    AddSourceEntity();
    SceneDocument source = {};
    ASSERT_TRUE(Capture(&source));
    ZEngine::Core::VFS::VFSMemoryBackend backend = {};
    backend.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::VFSContext vfs = {};
    vfs.Initialize(&Memory.MainArena);
    ASSERT_TRUE(vfs.Mount(&backend, ZEngine::Core::VFS::VFSPath::Root(), 0).Succeeded());
    const auto path = ZEngine::Core::VFS::VFSPath::Parse("/owned.zescene").Value();
    ASSERT_TRUE(source.Save(vfs, path, &Diagnostics).Succeeded());
    ASSERT_TRUE(Create());
    const auto before = Instance->Arena()->m_current_offset;
    ASSERT_TRUE(Instance->ReadSource(vfs, path, Schemas, ZMega(8), &Diagnostics));
    EXPECT_EQ(Instance->Arena()->m_current_offset - before, ZMega(8));
    auto memory = Instance->DocumentMemory();
    EXPECT_EQ(memory.Capacity, ZMega(8));
    EXPECT_GT(memory.Allocated, 0u);
    EXPECT_EQ(memory.Working, 0u);
    EXPECT_GT(memory.Peak, memory.Allocated);
    EXPECT_LE(memory.Peak, memory.Capacity);
    EXPECT_EQ(Instance->World().AliveCount(), 0u);
    const auto* original = source.Settings();
    EXPECT_FALSE(Instance->TryReleaseDocumentForDiscard(&source));
    EXPECT_EQ(source.Settings(), original);
    EXPECT_FALSE(Instance->Populate(std::move(source), Schemas, &Diagnostics));
    EXPECT_EQ(source.Settings(), original);
    ASSERT_TRUE(Instance->PopulateSource(Schemas, &Diagnostics));
    EXPECT_EQ(Instance->World().AliveCount(), 1u);
    EXPECT_EQ(Instance->World().Hierarchy().BuildCount(), 1u);
    EXPECT_EQ(Instance->World().Hierarchy().DerivationCount(), 1u);
    Instance->World().ForEach<TransformComponent>([](EntityID, const TransformComponent& transform) {
        EXPECT_FLOAT_EQ(transform.WorldTransform(0, 3), 2.f);
        EXPECT_FLOAT_EQ(transform.WorldTransform(1, 3), 3.f);
        EXPECT_FLOAT_EQ(transform.WorldTransform(2, 3), 4.f);
    });
    Instance->World().SynchronizeHierarchy();
    EXPECT_EQ(Instance->World().Hierarchy().DerivationCount(), 1u);
    EXPECT_FALSE(Instance->ReadSource(vfs, path, Schemas, ZMega(8), &Diagnostics));
    EXPECT_FALSE(Instance->PopulateSource(Schemas, &Diagnostics));
    std::string text = {};
    ASSERT_TRUE(Instance->Document().Emit(&text, &Diagnostics));
    EXPECT_NE(text.find("Engine-owned scene"), std::string::npos);
    Instance.reset();
    EXPECT_FALSE(Occupied[0].value.load());
    ASSERT_TRUE(Create()); // The same bounded backing can be reused after teardown.
    ASSERT_TRUE(Instance->ReadSource(vfs, path, Schemas, ZMega(8), &Diagnostics));
    ASSERT_TRUE(Instance->PopulateSource(Schemas, &Diagnostics));
    vfs.Shutdown();
}

TEST_F(SceneInstanceTest, RefusedDocumentWorkingStorageLeavesNoRetainedAllocationsAndReleasesTheLeaseOnDiscard)
{
    AddSourceEntity();
    SceneDocument source = {};
    ASSERT_TRUE(Capture(&source));
    ZEngine::Core::VFS::VFSMemoryBackend backend = {};
    backend.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::VFSContext vfs = {};
    vfs.Initialize(&Memory.MainArena);
    ASSERT_TRUE(vfs.Mount(&backend, ZEngine::Core::VFS::VFSPath::Root(), 0).Succeeded());
    const auto path = ZEngine::Core::VFS::VFSPath::Parse("/refused.zescene").Value();
    ASSERT_TRUE(source.Save(vfs, path, &Diagnostics).Succeeded());
    ASSERT_TRUE(Create());
    EXPECT_FALSE(Instance->ReadSource(vfs, path, Schemas, ZKilo(16), &Diagnostics));
    const auto memory = Instance->DocumentMemory();
    EXPECT_EQ(memory.Capacity, ZKilo(16));
    EXPECT_EQ(memory.Allocated, 0u);
    EXPECT_EQ(memory.Working, 0u);
    EXPECT_LE(memory.Peak, memory.Capacity);
    EXPECT_EQ(Instance->Document().Settings(), nullptr);
    EXPECT_EQ(Instance->World().AliveCount(), 0u);
    EXPECT_TRUE(Diagnostics.HasErrors());
    Instance.reset();
    EXPECT_FALSE(Occupied[0].value.load());
    vfs.Shutdown();
}

TEST_F(SceneInstanceTest, LeaseAcquisitionIsBoundedAndCannotReplaceALiveLease)
{
    SceneStorageLease first  = {};
    SceneStorageLease second = {};
    SceneStorageLease third  = {};
    ASSERT_TRUE(Acquire(&first));
    auto* original = first.Arena();
    EXPECT_FALSE(Acquire(&first));
    EXPECT_EQ(first.Arena(), original);
    ASSERT_TRUE(Acquire(&second));
    EXPECT_NE(first.Arena(), second.Arena());
    EXPECT_FALSE(Acquire(&third));
    EXPECT_EQ(third.Arena(), nullptr);
    first.Reset();
    ASSERT_TRUE(Acquire(&third));
    EXPECT_EQ(third.Arena(), original);
}

TEST_F(SceneInstanceTest, AdmissionDistinguishesBusyCapacityFromMissingConfigurationAndInvalidArguments)
{
    SceneStorageLease first = {}, second = {}, waiting = {};
    EXPECT_EQ(SceneStorageLease::Admit({Slots, 2}, nullptr), SceneStorageAdmission::InvalidArgument);
    EXPECT_EQ(SceneStorageLease::Admit({Slots, 2}, &waiting, 0), SceneStorageAdmission::InvalidArgument);
    EXPECT_EQ(SceneStorageLease::Admit({Slots, 2}, &waiting, ZMega(33)), SceneStorageAdmission::Unconfigured);
    EXPECT_EQ(SceneStorageLease::Admit({}, &waiting), SceneStorageAdmission::Unconfigured);
    ASSERT_EQ(SceneStorageLease::Admit({Slots, 2}, &first), SceneStorageAdmission::Acquired);
    ASSERT_EQ(SceneStorageLease::Admit({Slots, 2}, &second), SceneStorageAdmission::Acquired);
    EXPECT_EQ(SceneStorageLease::Admit({Slots, 2}, &waiting), SceneStorageAdmission::Busy);
    EXPECT_EQ(SceneStorageLease::Admit({Slots, 2}, &first), SceneStorageAdmission::InvalidArgument);
    EXPECT_EQ(waiting.Arena(), nullptr);
    first.Reset();
    ASSERT_EQ(SceneStorageLease::Admit({Slots, 2}, &waiting), SceneStorageAdmission::Acquired);
    EXPECT_EQ(waiting.Arena(), &Arenas[0]);
}

TEST_F(SceneInstanceTest, LeaseMoveAndResetReleaseExactlyTheirOwnedSlot)
{
    SceneStorageLease first = {};
    ASSERT_TRUE(Acquire(&first));
    ASSERT_NE(first.Arena()->Allocate(128), nullptr);
    auto*             arena = first.Arena();
    SceneStorageLease moved = std::move(first);
    EXPECT_EQ(first.Arena(), nullptr);
    EXPECT_EQ(moved.Arena(), arena);
    first.Reset();
    EXPECT_TRUE(Occupied[0].value.load());
    SceneStorageLease replacement = {};
    ASSERT_TRUE(Acquire(&replacement));
    replacement = std::move(moved);
    EXPECT_EQ(moved.Arena(), nullptr);
    EXPECT_FALSE(Occupied[1].value.load());
    replacement.Reset();
    replacement.Reset();
    EXPECT_EQ(arena->m_current_offset, arena->m_initial_current_offset);
}

TEST_F(SceneInstanceTest, InvalidAndUndersizedStorageCannotBeClaimed)
{
    SceneStorageLease lease = {};
    EXPECT_FALSE(SceneStorageLease::Acquire({Slots, 2}, nullptr));
    EXPECT_FALSE(SceneStorageLease::Acquire({Slots, 2}, &lease, 0));
    EXPECT_FALSE(SceneStorageLease::Acquire({Slots, 2}, &lease, ZMega(33)));
    const SceneStorageSlot invalid[] = {
        {   nullptr, &Occupied[0]},
        {&Arenas[1],      nullptr}
    };
    EXPECT_FALSE(SceneStorageLease::Acquire({invalid, 2}, &lease));
    EXPECT_EQ(lease.Arena(), nullptr);
}

TEST_F(SceneInstanceTest, ConcurrentLeasesNeverExposeTheSameOccupiedSlot)
{
    std::atomic<uint32_t>    users[2]     = {};
    std::atomic<uint32_t>    collisions   = 0;
    std::atomic<uint32_t>    acquisitions = 0;
    std::vector<std::thread> threads      = {};
    for (uint32_t worker = 0; worker < 8; ++worker)
    {
        threads.emplace_back([&] {
            for (uint32_t attempt = 0; attempt < 200; ++attempt)
            {
                SceneStorageLease lease = {};
                if (!Acquire(&lease))
                {
                    continue;
                }
                const auto index = lease.Arena() == &Arenas[0] ? 0u : 1u;
                if (users[index].fetch_add(1) != 0)
                {
                    collisions.fetch_add(1);
                }
                acquisitions.fetch_add(1);
                users[index].fetch_sub(1);
            }
        });
    }
    for (auto& thread : threads)
    {
        thread.join();
    }
    EXPECT_GT(acquisitions.load(), 0u);
    EXPECT_EQ(collisions.load(), 0u);
}

TEST_F(SceneInstanceTest, EmptyOwnerDoesNotDependOnAnEditorOrCreateDefaultActors)
{
    ASSERT_TRUE(Create());
    EXPECT_EQ(Instance->World().AliveCount(), 0u);
    EXPECT_EQ(Instance->Actors().Count(), 0u);
    EXPECT_EQ(Instance->Actors().BoundScene(), &Instance->World());
    EXPECT_EQ(Instance->World().GetArena(), Instance->Arena());
    EXPECT_EQ(Instance->Document().Settings(), nullptr);
    EXPECT_TRUE(Instance->Commands().IsEmpty());
    EXPECT_EQ(Instance->Tick().SystemCount(), 0u);
}

TEST_F(SceneInstanceTest, CommandsBelongOnlyToTheirSceneInstance)
{
    ASSERT_TRUE(Create());
    SceneStorageLease              lease = {};
    std::unique_ptr<SceneInstance> other = {};
    ASSERT_TRUE(Acquire(&lease));
    ASSERT_TRUE(SceneInstance::Create(std::move(lease), &other, &Diagnostics));
    Instance->Commands().SpawnEntity();
    EXPECT_TRUE(other->Commands().IsEmpty());
    Instance.reset();
    other->Commands().Flush(other->World());
    EXPECT_EQ(other->World().AliveCount(), 0u);
    other.reset();
}

TEST(ScenePublicationTest, RejectedAndAbandonedTransitionsPreserveGeneration)
{
    ScenePublication publication = {};
    const auto       initial     = publication.Generation();
    {
        ScenePublication::Transition transition = {};
        EXPECT_FALSE(publication.TryBegin(initial + 1, &transition));
        EXPECT_FALSE(publication.TryBegin(initial, nullptr));
        ASSERT_TRUE(publication.TryBegin(initial, &transition));
        EXPECT_FALSE(publication.Idle());
        EXPECT_FALSE(publication.TryBegin(initial, &transition));
    }
    EXPECT_TRUE(publication.Idle());
    EXPECT_EQ(publication.Generation(), initial);
}

TEST(ScenePublicationTest, PublicationAdvancesOnceAndSealsRetirementUntilScopeEnds)
{
    ScenePublication publication = {};
    const auto       initial     = publication.Generation();
    {
        ScenePublication::Transition transition = {};
        ASSERT_TRUE(publication.TryBegin(initial, &transition));
        transition.Publish();
        EXPECT_EQ(publication.Generation(), initial + 1);
        EXPECT_FALSE(publication.Idle());
        ScenePublication::Transition reentry = {};
        EXPECT_FALSE(publication.TryBegin(initial + 1, &reentry));
    }
    EXPECT_TRUE(publication.Idle());
    ScenePublication::Transition stale = {};
    EXPECT_FALSE(publication.TryBegin(initial, &stale));
}

TEST(ScenePublicationTest, ForeignThreadCannotEnterTheMainThreadAuthority)
{
    ScenePublication publication = {};
    bool             accepted    = true;
    const auto       generation  = publication.Generation();
    std::thread      worker([&] {
        ScenePublication::Transition transition = {};
        accepted                                = publication.TryBegin(generation, &transition);
    });
    worker.join();
    EXPECT_FALSE(accepted);
    EXPECT_TRUE(publication.Idle());
    EXPECT_EQ(publication.Generation(), generation);
}

TEST_F(SceneInstanceTest, PopulationRetainsDocumentIdentityAndPureECS)
{
    AddSourceEntity();
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Create());
    ASSERT_TRUE(Instance->Populate(std::move(document), Schemas, &Diagnostics));
    EXPECT_EQ(document.Settings(), nullptr);
    EXPECT_EQ(Instance->World().AliveCount(), 1u);
    EXPECT_EQ(Instance->Actors().Count(), 0u);
    ASSERT_NE(Instance->Document().Settings(), nullptr);
    EXPECT_EQ(Instance->Document().Settings()->Name, "Engine-owned scene");
    EXPECT_EQ(Instance->Document().Settings()->UUID, Settings.UUID);
    SceneDocument another = {};
    ASSERT_TRUE(Capture(&another));
    EXPECT_FALSE(Instance->Populate(std::move(another), Schemas, &Diagnostics));
    EXPECT_NE(another.Settings(), nullptr);
    EXPECT_EQ(Source.AliveCount(), 1u);
}

TEST_F(SceneInstanceTest, FailedCreationPreservesTheLeaseAndPreviousOutput)
{
    ASSERT_TRUE(Create());
    auto*             previous = Instance.get();
    SceneStorageLease lease    = {};
    ASSERT_TRUE(Acquire(&lease));
    auto* arena = lease.Arena();
    ASSERT_NE(arena->Allocate(ZMega(25)), nullptr);
    EXPECT_FALSE(SceneInstance::Create(std::move(lease), &Instance, &Diagnostics));
    EXPECT_EQ(Instance.get(), previous);
    EXPECT_EQ(lease.Arena(), arena);
}

TEST_F(SceneInstanceTest, FailedPopulationPreservesInputAndReleasesStorageOnDiscard)
{
    AddSourceEntity();
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Create());
    auto*      arena     = Instance->Arena();
    const auto remaining = arena->m_total_size - arena->m_current_offset;
    ASSERT_NE(arena->Allocate(remaining - 64), nullptr);
    EXPECT_FALSE(Instance->Populate(std::move(document), Schemas, &Diagnostics));
    EXPECT_NE(document.Settings(), nullptr);
    EXPECT_EQ(Instance->World().AliveCount(), 0u);
    Instance.reset();
    EXPECT_FALSE(Occupied[0].value.load());
    EXPECT_EQ(arena->m_current_offset, arena->m_initial_current_offset);
    ASSERT_TRUE(Create());
    EXPECT_TRUE(Instance->Populate(std::move(document), Schemas, &Diagnostics));
}

TEST_F(SceneInstanceTest, AdapterRollbackReturnsTheOwnedSnapshotBeforeDiscard)
{
    AddSourceEntity();
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Create());
    ASSERT_TRUE(Instance->Populate(std::move(document), Schemas, &Diagnostics));
    ASSERT_TRUE(Instance->TryReleaseDocumentForDiscard(&document));
    Instance.reset();
    ASSERT_NE(document.Settings(), nullptr);
    EXPECT_EQ(document.Settings()->Name, "Engine-owned scene");
    EXPECT_EQ(document.EntityCount(), 1u);
}

TEST_F(SceneInstanceTest, ActorShutdownObservesLiveWorldDocumentAndStorage)
{
    struct ProbeActor final : Actor
    {
        bool*                Destroyed = nullptr;
        const SceneDocument* Document  = nullptr;
        PaddedAtomic<bool>*  Occupied  = nullptr;

        void                 OnDestroy() override
        {
            EXPECT_TRUE(IsAlive());
            EXPECT_EQ(Document->Settings()->Name, "Engine-owned scene");
            EXPECT_TRUE(Occupied->value.load());
            *Destroyed = true;
        }
    };
    SceneDocument document = {};
    ASSERT_TRUE(Capture(&document));
    ASSERT_TRUE(Create());
    ASSERT_TRUE(Instance->Populate(std::move(document), Schemas, &Diagnostics));
    bool       destroyed = false;
    const auto handle    = Instance->Actors().Create<ProbeActor>();
    auto*      actor     = static_cast<ProbeActor*>(Instance->Actors().Access(handle));
    ASSERT_NE(actor, nullptr);
    actor->Destroyed = &destroyed;
    actor->Document  = &Instance->Document();
    actor->Occupied  = &Occupied[0];
    Instance.reset();
    EXPECT_TRUE(destroyed);
}
