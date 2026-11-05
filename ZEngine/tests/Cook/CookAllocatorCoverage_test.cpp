#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCallbackStorage.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/SceneComponentSchemaRegistry.h>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    class CookAllocatorCoverageTest : public testing::Test
    {
    protected:
        Core::Memory::ArenaAllocator      SchemaArena = {}, CallbackArena = {}, VFSArena = {};
        ECS::SceneComponentSchemaRegistry Schemas  = {};
        CookAdapterRegistry               Adapters = {};
        VFS::VFSMemoryBackend             Backend  = {};
        VFS::VFSContext                   Files    = {};
        CookArenaCallbackStorage          Callbacks{CallbackArena};
        SceneCookContext                  Context = {};

        void                              SetUp() override
        {
            SchemaArena.Initialize(ZMega(2), 0);
            CallbackArena.Initialize(ZMega(2), 0);
            VFSArena.Initialize(ZMega(2), 0);
            Schemas.Initialize(&SchemaArena);
            Files.Initialize(&VFSArena);
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(1), 32).Succeeded());
            ASSERT_TRUE(Files.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(Files.CreateDir(VFS::VFSPath::Parse("/assets").Value()).Succeeded());
            ASSERT_TRUE(Files.CreateDir(VFS::VFSPath::Parse("/scenes").Value()).Succeeded());
            const CookAdapterFns entries[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(Adapters.Initialize(entries).Succeeded());
            Context = {.Output = &Files, .Scratch = &Files, .Schemas = &Schemas, .Adapters = &Adapters, .Callbacks = &Callbacks};
        }

        void TearDown() override
        {
            Files.Shutdown();
        }

        template <typename T, typename... Args>
        std::unique_ptr<T, void (*)(T*)> Store(Core::Memory::ArenaAllocator& arena, Args&&... args)
        {
            void* storage = arena.Allocate(sizeof(T), alignof(T));
            EXPECT_NE(storage, nullptr);
            return {storage ? new (storage) T{std::forward<Args>(args)...} : nullptr, [](T* object) { std::destroy_at(object); }};
        }

        struct ProbeAllocator : ICookAllocator
        {
            const ICookAllocator*                           Parent      = nullptr;
            mutable size_t                                  Profiles    = 0;
            size_t                                          Allocations = 0;

            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                ++Profiles;
                return VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({128, 512, false});
            }
            VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t, CookMemoryCategory) const noexcept override
            {
                return VFS::VFSResult<size_t>::Ok(bytes);
            }
            VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Allocations;
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
            }
            void                 Free(void*) noexcept override {}
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
            const ICookAllocator* ParentAllocator() const noexcept override
            {
                return Parent;
            }
        };
    };

    TEST_F(CookAllocatorCoverageTest, NativePageStateInEachServiceFamilyIsCoveredWithoutChangingPhysicalProfile)
    {
        for (auto* arena : {&SchemaArena, &CallbackArena, &VFSArena})
        {
            auto page = Store<CookPageAllocator>(*arena);
            ASSERT_NE(page, nullptr);
            ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
            Detail::CookNativeServiceMemory services = {};
            ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
            const auto offset    = arena->m_current_offset;
            const auto committed = arena->m_committed_size;
            for (size_t repeat = 0; repeat < 128; ++repeat)
            {
                const auto physical  = page->MemoryRequirements();
                const auto uncovered = page->UncoveredMemoryRequirements(services);
                ASSERT_TRUE(physical.Succeeded());
                ASSERT_TRUE(uncovered.Succeeded());
                EXPECT_EQ(physical.Value().StateBytes, sizeof(*page));
                EXPECT_EQ(uncovered.Value().StateBytes, 0u);
                EXPECT_EQ(uncovered.Value().PoolBytes, physical.Value().PoolBytes);
                EXPECT_EQ(uncovered.Value().IncludesAllocations, physical.Value().IncludesAllocations);
            }
            EXPECT_EQ(page->AllocationCount(), 0u);
            EXPECT_EQ(arena->m_current_offset, offset);
            EXPECT_EQ(arena->m_committed_size, committed);
        }
    }

    TEST_F(CookAllocatorCoverageTest, FrozenSourceAndOwnerStateAcrossNativeFamiliesNeverDiscountsBindingMappings)
    {
        for (auto* arena : {&SchemaArena, &CallbackArena, &VFSArena})
        {
            auto page  = Store<CookPageAllocator>(*arena);
            auto owner = Store<CookFrozenLibraryOwner>(*arena);
            ASSERT_NE(page, nullptr);
            ASSERT_NE(owner, nullptr);
            ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
            const CookFrozenRootRequest roots[] = {
                {"assets", &Files, VFS::VFSPath::Parse("/assets").Value()},
                {"scenes", &Files, VFS::VFSPath::Parse("/scenes").Value()}
            };
            ASSERT_TRUE(owner->Initialize(*page, roots).Succeeded());
            auto source = Store<CookFrozenInputSource>(*arena, *owner);
            ASSERT_NE(source, nullptr);
            Detail::CookNativeServiceMemory services = {};
            ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
            CookBudgetAllocator run = {};
            ASSERT_TRUE(run.Initialize(*page, ZMega(4), &services).Succeeded());
            const auto mapping   = page->Statistics().LiveBytes - sizeof(*page);
            const auto offset    = arena->m_current_offset;
            const auto committed = arena->m_committed_size;
            ASSERT_GT(mapping, 0u);
            for (size_t repeat = 0; repeat < 128; ++repeat)
            {
                EXPECT_EQ(source->FixedWorkingBytes(run).Value(), sizeof(*source) + sizeof(*owner) + mapping);
                EXPECT_EQ(source->UncoveredFixedWorkingBytes(run, services).Value(), mapping);
                EXPECT_EQ(owner->FixedWorkingBytes(run, &services).Value(), mapping);
                EXPECT_EQ(source->FixedWorkingBytes(*page).Value(), sizeof(*source) + sizeof(*owner) + mapping);
            }
            EXPECT_EQ(page->AllocationCount(), 1u);
            EXPECT_EQ(arena->m_current_offset, offset);
            EXPECT_EQ(arena->m_committed_size, committed);
            CookFrozenLibraryReadLease read = {};
            ASSERT_TRUE(owner->TryAcquireRead(&read).Succeeded());
            EXPECT_EQ(read.RootCount(), 2u);
            EXPECT_EQ(owner->Reset().Error(), VFS::VFSError::AlreadyExists);
        }
    }

    TEST_F(CookAllocatorCoverageTest, FrozenOwnerSiblingPoolRetainsItsFullBackingWhileCoveredFixedStateIsDeduplicated)
    {
        auto page  = Store<CookPageAllocator>(SchemaArena);
        auto pool  = Store<CookScratchAllocator>(CallbackArena);
        auto owner = Store<CookFrozenLibraryOwner>(VFSArena);
        ASSERT_NE(page, nullptr);
        ASSERT_NE(pool, nullptr);
        ASSERT_NE(owner, nullptr);
        ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(pool->Initialize(*page, ZKilo(64), CookMemoryCategory::Captures).Succeeded());
        CookScratchAllocator sibling = {};
        ASSERT_TRUE(sibling.Initialize(*page, ZKilo(64), CookMemoryCategory::Captures).Succeeded());
        const CookFrozenRootRequest roots[] = {
            {"assets", &Files, VFS::VFSPath::Parse("/assets").Value()}
        };
        ASSERT_TRUE(owner->Initialize(*pool, roots).Succeeded());
        auto source = Store<CookFrozenInputSource>(SchemaArena, *owner);
        ASSERT_NE(source, nullptr);
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        CookBudgetAllocator run = {};
        ASSERT_TRUE(run.Initialize(sibling, ZMega(4), &services).Succeeded());
        const auto backing = pool->MemoryRequirements().Value().PoolBytes;
        const auto calls   = page->AllocationCount();
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            EXPECT_EQ(source->UncoveredFixedWorkingBytes(run, services).Value(), backing);
            EXPECT_EQ(CookUncoveredAllocatorBytes(*pool, run, &services).Value(), backing);
            EXPECT_EQ(CookUncoveredAllocatorBytes(*pool, *pool, &services).Value(), 0u);
            EXPECT_EQ(CookUncoveredAllocatorBytes(*page, run, &services).Value(), 0u);
            EXPECT_EQ(CookUncoveredAllocatorBytes(*pool, run).Value(), sizeof(*pool) + backing);
        }
        EXPECT_EQ(page->AllocationCount(), calls);
        EXPECT_EQ(pool->OutstandingAllocations(), 1u);
        ASSERT_TRUE(owner->Reset().Succeeded());
    }

    TEST_F(CookAllocatorCoverageTest, CompleteVFSFacadeCoversFrozenProviderStateButNotItsSeparateBindingBlock)
    {
        struct CompleteContext final : VFS::VFSContext
        {
            CookPageAllocator                      Page  = {};
            CookFrozenLibraryOwner                 Owner = {};
            CookFrozenInputSource                  Source{Owner};
            const Core::Memory::ArenaAllocator*    Arena = nullptr;

            VFS::VFSResult<VFS::VFSContextStorage> NativeStorage() const noexcept override
            {
                return VFS::VFSResult<VFS::VFSContextStorage>::Ok({this, sizeof(*this), Arena});
            }
        } facade     = {};
        facade.Arena = &VFSArena;
        facade.Initialize(&VFSArena);
        ASSERT_TRUE(facade.Page.Initialize(ZMega(4)).Succeeded());
        const CookFrozenRootRequest roots[] = {
            {"assets", &Files, VFS::VFSPath::Parse("/assets").Value()}
        };
        ASSERT_TRUE(facade.Owner.Initialize(facade.Page, roots).Succeeded());
        Context.Output = Context.Scratch         = &facade;
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        const auto        mapping     = facade.Page.Statistics().LiveBytes - sizeof(facade.Page);
        CookPageAllocator independent = {};
        ASSERT_TRUE(independent.Initialize(ZMega(4)).Succeeded());
        EXPECT_EQ(facade.Source.UncoveredFixedWorkingBytes(independent, services).Value(), mapping);
        EXPECT_EQ(facade.Source.FixedWorkingBytes(independent).Value(), sizeof(facade.Source) + sizeof(facade.Owner) + sizeof(facade.Page) + mapping);
        EXPECT_EQ(facade.Page.AllocationCount(), 1u);
        ASSERT_TRUE(facade.Owner.Reset().Succeeded());
        facade.Shutdown();
    }

    TEST_F(CookAllocatorCoverageTest, OpaqueProvidersKeepTheirCompleteProfileEvenWhenTheirInterfaceIsCovered)
    {
        struct OpaqueProvider final : ICookInputSource
        {
            VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator&) const noexcept override
            {
                return VFS::VFSResult<uint64_t>::Ok(12345);
            }
            VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease*, CookDiagnostics*) override
            {
                ADD_FAILURE() << "A memory query must not acquire inputs.";
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
            }
        };
        auto source = Store<OpaqueProvider>(SchemaArena);
        ASSERT_NE(source, nullptr);
        CookPageAllocator page = {};
        ASSERT_TRUE(page.Initialize(ZMega(4)).Succeeded());
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        EXPECT_TRUE(services.Contains(source.get(), sizeof(*source)));
        EXPECT_EQ(source->UncoveredFixedWorkingBytes(page, services).Value(), 12345u);
        EXPECT_EQ(page.AllocationCount(), 0u);
    }

    TEST_F(CookAllocatorCoverageTest, NativeWrapperChainSharesFixedStateButKeepsEveryRetainedPoolAtExactBudget)
    {
        auto page    = Store<CookPageAllocator>(SchemaArena);
        auto scratch = Store<CookScratchAllocator>(VFSArena);
        auto inner   = Store<CookBudgetAllocator>(CallbackArena);
        ASSERT_NE(page, nullptr);
        ASSERT_NE(scratch, nullptr);
        ASSERT_NE(inner, nullptr);
        ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(scratch->Initialize(*page, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        ASSERT_TRUE(inner->Initialize(*scratch, ZMega(1), &services).Succeeded());
        const auto physical  = inner->MemoryRequirements();
        const auto uncovered = inner->UncoveredMemoryRequirements(services);
        ASSERT_TRUE(physical.Succeeded());
        ASSERT_TRUE(uncovered.Succeeded());
        EXPECT_EQ(physical.Value().StateBytes, sizeof(*page) + sizeof(*scratch) + sizeof(*inner));
        EXPECT_EQ(uncovered.Value().StateBytes, 0u);
        EXPECT_EQ(uncovered.Value().PoolBytes, physical.Value().PoolBytes);
        EXPECT_TRUE(uncovered.Value().IncludesAllocations);
        EXPECT_EQ(inner->Statistics().LiveBytes, physical.Value().PoolBytes);
        const auto          calls = page->AllocationCount();
        CookBudgetAllocator outer = {};
        const uint64_t      exact = physical.Value().PoolBytes + sizeof(outer);
        EXPECT_EQ(outer.Initialize(*inner, exact - 1, &services).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(outer.Statistics().LiveBytes, 0u);
        EXPECT_EQ(page->AllocationCount(), calls);
        ASSERT_TRUE(outer.Initialize(*inner, exact, &services).Succeeded());
        EXPECT_EQ(outer.Statistics().LiveBytes, exact);
        EXPECT_EQ(outer.MemoryRequirements().Value().StateBytes, physical.Value().StateBytes + sizeof(outer));
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            const auto block = outer.Allocate(128, 16, CookMemoryCategory::Codecs);
            ASSERT_TRUE(block.Succeeded());
            outer.Free(block.Value());
            EXPECT_EQ(outer.Statistics().LiveBytes, exact);
        }
        EXPECT_EQ(page->AllocationCount(), calls);
        EXPECT_EQ(page->OutstandingAllocations(), 1u);
        EXPECT_EQ(scratch->OutstandingAllocations(), 0u);
    }

    TEST_F(CookAllocatorCoverageTest, IndependentAllocatorStateAndOpaqueCustomOwnersDoNotReceiveInferredDiscounts)
    {
        CookPageAllocator external = {};
        ASSERT_TRUE(external.Initialize(ZMega(4)).Succeeded());
        auto opaque = Store<ProbeAllocator>(SchemaArena);
        ASSERT_NE(opaque, nullptr);
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        EXPECT_EQ(external.UncoveredMemoryRequirements(services).Value().StateBytes, sizeof(external));
        EXPECT_TRUE(services.Contains(opaque.get(), sizeof(*opaque)));
        EXPECT_EQ(opaque->UncoveredMemoryRequirements(services).Value().StateBytes, 128u);
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(budget.Initialize(*opaque, sizeof(budget) + 128 + 512, &services).Succeeded());
        EXPECT_EQ(budget.Statistics().LiveBytes, sizeof(budget) + 128 + 512);
        EXPECT_EQ(opaque->Allocations, 0u);
    }

    TEST_F(CookAllocatorCoverageTest, CompleteVFSFacadeCoversItsEmbeddedAllocatorWithoutDiscountingOtherState)
    {
        struct CompleteContext final : VFS::VFSContext
        {
            CookPageAllocator                      Page  = {};
            const Core::Memory::ArenaAllocator*    Arena = nullptr;

            VFS::VFSResult<VFS::VFSContextStorage> NativeStorage() const noexcept override
            {
                return VFS::VFSResult<VFS::VFSContextStorage>::Ok({this, sizeof(*this), Arena});
            }
        } facade     = {};
        facade.Arena = &VFSArena;
        facade.Initialize(&VFSArena);
        ASSERT_TRUE(facade.Page.Initialize(ZMega(4)).Succeeded());
        Context.Output = Context.Scratch         = &facade;
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        EXPECT_FALSE(SchemaArena.RetainsStorage(&facade.Page, sizeof(facade.Page)));
        EXPECT_FALSE(VFSArena.RetainsStorage(&facade.Page, sizeof(facade.Page)));
        EXPECT_TRUE(services.Contains(&facade.Page, sizeof(facade.Page)));
        EXPECT_EQ(facade.Page.UncoveredMemoryRequirements(services).Value().StateBytes, 0u);
        CookBudgetAllocator budget = {};
        ASSERT_TRUE(budget.Initialize(facade.Page, sizeof(budget), &services).Succeeded());
        EXPECT_EQ(budget.Statistics().LiveBytes, sizeof(budget));
        facade.Shutdown();
    }

    TEST_F(CookAllocatorCoverageTest, CoveredBudgetObjectCanHaveZeroLedgerChargeButRetainsItsPhysicalProfile)
    {
        auto page   = Store<CookPageAllocator>(CallbackArena);
        auto budget = Store<CookBudgetAllocator>(SchemaArena);
        ASSERT_NE(budget, nullptr);
        ASSERT_NE(page, nullptr);
        ASSERT_TRUE(page->Initialize(ZMega(4)).Succeeded());
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        ASSERT_TRUE(budget->Initialize(*page, ZMega(1), &services).Succeeded());
        EXPECT_EQ(budget->Statistics().LiveBytes, 0u);
        EXPECT_EQ(budget->MemoryRequirements().Value().StateBytes, sizeof(*budget) + sizeof(*page));
        budget->Release();
        EXPECT_EQ(budget->MemoryRequirements().Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(budget->UncoveredMemoryRequirements(services).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(budget->Initialize(*page, ZMega(1)).Succeeded());
        EXPECT_EQ(budget->Statistics().LiveBytes, sizeof(*budget) + sizeof(*page));
    }

    TEST_F(CookAllocatorCoverageTest, CoverageCannotDiscountPoolsChangeContainmentOrIncreaseState)
    {
        struct MalformedAllocator final : ProbeAllocator
        {
            CookAllocatorMemoryRequirements                 Claim = {};
            VFS::VFSError                                   Error = VFS::VFSError::OK;
            VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept override
            {
                return Error == VFS::VFSError::OK ? VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok(Claim) : VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Error);
            }
        } parent                                 = {};
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        CookBudgetAllocator budget   = {};
        CookPageAllocator   separate = {};
        ASSERT_TRUE(separate.Initialize(ZMega(1)).Succeeded());
        for (auto claim : {
             CookAllocatorMemoryRequirements{129, 512, false},
             CookAllocatorMemoryRequirements{  0, 511, false},
             CookAllocatorMemoryRequirements{  0, 512,  true}
        })
        {
            parent.Claim = claim;
            EXPECT_EQ(budget.Initialize(parent, ZMega(1), &services).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(CookUncoveredAllocatorBytes(parent, separate, &services).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(CookUncoveredAllocatorBytes(separate, parent, &services).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            EXPECT_EQ(parent.Allocations, 0u);
        }
        parent.Error = VFS::VFSError::Unsupported;
        EXPECT_EQ(budget.Initialize(parent, ZMega(1), &services).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(CookUncoveredAllocatorBytes(parent, separate, &services).Error(), VFS::VFSError::Unsupported);
        EXPECT_EQ(parent.Allocations, 0u);
    }

    TEST_F(CookAllocatorCoverageTest, ExternalFixedStateUsesTheLedgerRatherThanPretendingToFitInsideAnIdleRetainedPool)
    {
        CookPageAllocator    page    = {};
        CookScratchAllocator scratch = {};
        CookBudgetAllocator  budget  = {};
        ASSERT_EQ(page.TryCharge(1, CookMemoryCategory::Codecs, nullptr).Error(), VFS::VFSError::InvalidPath);
        ASSERT_EQ(scratch.TryCharge(1, CookMemoryCategory::Codecs, nullptr).Error(), VFS::VFSError::InvalidPath);
        ASSERT_TRUE(page.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(scratch.Initialize(page, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        const auto requirements = scratch.MemoryRequirements().Value();
        const auto fixed_bytes  = sizeof(budget) + requirements.StateBytes + requirements.PoolBytes;
        ASSERT_TRUE(budget.Initialize(scratch, fixed_bytes + 128).Succeeded());
        ICookAllocator&  generic = budget;
        CookMemoryCharge charge = {}, refused = {};
        const auto       allocations = page.AllocationCount();
        EXPECT_EQ(generic.TryCharge(129, CookMemoryCategory::StagingManifest, &refused).Error(), VFS::VFSError::SizeLimitExceeded);
        ASSERT_TRUE(generic.TryCharge(128, CookMemoryCategory::StagingManifest, &charge).Succeeded());
        EXPECT_EQ(generic.Statistics().LiveBytes, fixed_bytes + 128);
        // The fully charged pool is still reusable; unrelated fixed stack
        // storage cannot consume its idle capacity as a discount, however.
        const auto block = generic.Allocate(32, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(block.Succeeded());
        generic.Free(block.Value());
        EXPECT_EQ(page.AllocationCount(), allocations);
        charge.Release();
        EXPECT_EQ(generic.Statistics().LiveBytes, fixed_bytes);
        const auto parent_live = page.Statistics().LiveBytes;
        ASSERT_TRUE(scratch.TryCharge(64, CookMemoryCategory::Codecs, &charge).Succeeded());
        EXPECT_EQ(page.Statistics().LiveBytes, parent_live + 64);
        EXPECT_EQ(page.AllocationCount(), allocations);
        charge.Release();
        EXPECT_EQ(page.Statistics().LiveBytes, parent_live);
    }

    TEST_F(CookAllocatorCoverageTest, CyclicAllocatorTopologyFailsBeforeAnyRecursiveProfileOrAllocation)
    {
        ProbeAllocator      first = {}, second = {};
        CookBudgetAllocator budget = {};
        first.Parent               = &first;
        EXPECT_EQ(budget.Initialize(first, ZMega(1)).Error(), VFS::VFSError::Corrupted);
        first.Parent  = &second;
        second.Parent = &first;
        EXPECT_EQ(budget.Initialize(first, ZMega(1)).Error(), VFS::VFSError::Corrupted);
        Detail::CookNativeServiceMemory services = {};
        ASSERT_TRUE(services.Initialize(Context, &CallbackArena).Succeeded());
        EXPECT_EQ(CookUncoveredAllocatorBytes(first, second, &services).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(CookUncoveredAllocatorBytes(second, first, &services).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(first.Profiles, 0u);
        EXPECT_EQ(second.Profiles, 0u);
        EXPECT_EQ(first.Allocations, 0u);
        EXPECT_EQ(second.Allocations, 0u);
        EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
    }
} // namespace ZEngine::Cook
