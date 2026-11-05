#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <gtest/gtest.h>
#include <limits>
#include <type_traits>
#include <utility>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace
    {
        using FixedCookAllocator = Tests::FixedAllocator<4096, 4>;

        struct MemoryProfileNode final : ICookAllocator
        {
            const ICookAllocator*           Parent         = nullptr;
            CookAllocatorMemoryRequirements Requirements   = {};
            Core::VFS::VFSError             Error          = Core::VFS::VFSError::OK;
            CookAllocatorMemoryRequirements Uncovered      = {};
            bool                            HasCoverage    = false;
            mutable size_t                  EdgeQueries    = 0;
            mutable size_t                  ProfileQueries = 0;
            const ICookAllocator*           ParentAllocator() const noexcept override
            {
                ++EdgeQueries;
                return Parent;
            }
            Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                ++ProfileQueries;
                return Error == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok(Requirements) : Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Error);
            }
            Core::VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept override
            {
                return HasCoverage ? Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok(Uncovered) : MemoryRequirements();
            }
            Core::VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ADD_FAILURE() << "Retained-chain queries must not allocate.";
                return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::OutOfMemory);
            }
            void Free(void*) noexcept override
            {
                ADD_FAILURE() << "Retained-chain queries must not free.";
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
        };
    } // namespace

    TEST(CookMemoryTest, UncoveredAllocatorChainsUseTheNearestCommonAncestorInLinearTimeAndConstantStorage)
    {
        Core::Containers::FixedArray<MemoryProfileNode, 128> chain = {};
        for (size_t index = 0; index < chain.size(); ++index)
        {
            chain[index].Parent       = index ? &chain[index - 1] : nullptr;
            chain[index].Requirements = {index + 1, 4096, true};
        }
        EXPECT_EQ(CookUncoveredAllocatorBytes(chain[127], chain[63]).Value(), 64u);
        size_t edges    = 0;
        size_t profiles = 0;
        for (const auto& node : chain)
        {
            edges    += node.EdgeQueries;
            profiles += node.ProfileQueries;
        }
        EXPECT_LT(edges, 16 * chain.size());
        EXPECT_EQ(profiles, 3u);
        EXPECT_EQ(CookUncoveredAllocatorBytes(chain[63], chain[127]).Value(), 0u);
        MemoryProfileNode sibling = {};
        sibling.Parent            = &chain[31];
        sibling.Requirements      = {33, 8192, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(sibling, chain[127]).Value(), 1u + 4096u);
        EXPECT_EQ(CookUncoveredAllocatorBytes(chain[127], sibling).Value(), 96u);
        MemoryProfileNode independent = {};
        independent.Requirements      = {8, 1024, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(independent, chain[127]).Value(), 1032u);
        EXPECT_EQ(CookUncoveredAllocatorBytes(chain[127], independent).Value(), 128u + 4096u);
        EXPECT_EQ(CookUncoveredAllocatorBytes(sibling, sibling).Value(), 0u);
    }

    TEST(CookMemoryTest, CyclicAllocatorChainsFailBeforeAnyRecursiveMemoryProfileIsQueried)
    {
        MemoryProfileNode leaf = {}, first = {}, second = {}, prefix = {};
        first.Parent = &first;
        EXPECT_EQ(CookUncoveredAllocatorBytes(first, leaf).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(CookUncoveredAllocatorBytes(leaf, first).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(CookUncoveredAllocatorBytes(first, first).Error(), Core::VFS::VFSError::Corrupted);
        first.Parent  = &second;
        second.Parent = &first;
        prefix.Parent = &first;
        EXPECT_EQ(CookUncoveredAllocatorBytes(prefix, leaf).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(CookUncoveredAllocatorBytes(leaf, prefix).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(first.ProfileQueries, 0u);
        EXPECT_EQ(second.ProfileQueries, 0u);
        EXPECT_EQ(prefix.ProfileQueries, 0u);
        EXPECT_EQ(leaf.ProfileQueries, 0u);
    }

    TEST(CookMemoryTest, UnknownOverflowAndInconsistentSharedAllocatorProfilesFailClosed)
    {
        MemoryProfileNode leaf = {}, owner = {}, covered = {};
        leaf.Requirements = {32, 1024, true};
        owner.Parent = covered.Parent = &leaf;
        owner.Requirements = covered.Requirements = {64, 2048, true};
        owner.Error                               = Core::VFS::VFSError::Unsupported;
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered).Error(), Core::VFS::VFSError::Unsupported);
        owner.Error   = Core::VFS::VFSError::OK;
        covered.Error = Core::VFS::VFSError::Unsupported;
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered).Error(), Core::VFS::VFSError::Unsupported);
        covered.Error = Core::VFS::VFSError::OK;
        for (const auto requirements : {
             CookAllocatorMemoryRequirements{UINT64_MAX,    1, false},
             CookAllocatorMemoryRequirements{        64,    0,  true},
             CookAllocatorMemoryRequirements{        31, 2048,  true},
             CookAllocatorMemoryRequirements{        64, 1023,  true}
        })
        {
            owner.Requirements = requirements;
            EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered).Error(), Core::VFS::VFSError::Corrupted);
        }
        owner.Requirements   = {64, 2048, true};
        covered.Requirements = {31, 2048, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered).Error(), Core::VFS::VFSError::Corrupted);
    }

    TEST(CookMemoryTest, ServiceCoverageCannotMaskInconsistentPhysicalAncestorProfiles)
    {
        struct Coverage final : ICookMemoryCoverage
        {
            bool Contains(const void*, size_t) const noexcept override
            {
                return false;
            }
        } coverage               = {};
        MemoryProfileNode shared = {}, owner = {}, covered = {};
        owner.Parent = covered.Parent = &shared;
        shared.Requirements           = {32, 1024, true};
        owner.Requirements = covered.Requirements = {64, 2048, true};
        shared.HasCoverage = owner.HasCoverage = covered.HasCoverage = true;
        shared.Uncovered                                             = {0, 1024, true};
        owner.Uncovered = covered.Uncovered = {0, 2048, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Value(), 1024u);
        owner.Requirements.StateBytes = 31;
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Error(), Core::VFS::VFSError::Corrupted);
        owner.Requirements.StateBytes   = 64;
        covered.Requirements.StateBytes = 31;
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Error(), Core::VFS::VFSError::Corrupted);
        covered.Requirements.StateBytes = 64;
        shared.Uncovered                = {1, 1024, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Error(), Core::VFS::VFSError::Corrupted);
        shared.Uncovered = {0, 1023, true};
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Error(), Core::VFS::VFSError::Corrupted);
        shared.Error = Core::VFS::VFSError::Unsupported;
        EXPECT_EQ(CookUncoveredAllocatorBytes(owner, covered, &coverage).Error(), Core::VFS::VFSError::Unsupported);
    }

    TEST(CookMemoryTest, DefaultsAreInertAndInvalidInitializationPreservesTheLimit)
    {
        static_assert(!std::is_copy_constructible_v<CookMemoryCharge>);
        static_assert(std::is_nothrow_move_constructible_v<CookMemoryCharge>);
        static_assert(!std::is_move_constructible_v<CookMemoryLedger>);
        CookMemoryLedger ledger = {};
        CookMemoryCharge charge = {};
        EXPECT_FALSE(ledger.IsInitialized());
        EXPECT_EQ(ledger.RemainingBytes(), 0u);
        EXPECT_FALSE(charge.IsActive());
        EXPECT_EQ(ledger.Initialize(0).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.TryCharge(1, CookMemoryCategory::Codecs, &charge).Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(ledger.Initialize(100).Succeeded());
        EXPECT_EQ(ledger.Initialize(0).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.RemainingBytes(), 100u);
        EXPECT_EQ(ledger.Statistics().PeakBytes, 0u);
    }

    TEST(CookMemoryTest, ExistingChargeResizeIsTransactionalAndPreservesCategoryAndPeak)
    {
        CookMemoryLedger ledger = {};
        ASSERT_TRUE(ledger.Initialize(100).Succeeded());
        CookMemoryCharge charge = {}, other = {};
        EXPECT_EQ(charge.TryResize(1).Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(ledger.TryCharge(25, CookMemoryCategory::Captures, &charge).Succeeded());
        ASSERT_TRUE(ledger.TryCharge(20, CookMemoryCategory::Codecs, &other).Succeeded());
        ASSERT_TRUE(charge.TryResize(80).Succeeded());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 100u);
        EXPECT_EQ(ledger.Statistics().LiveByCategory[1], 80u);
        EXPECT_EQ(ledger.Statistics().LiveByCategory[2], 20u);
        EXPECT_EQ(charge.TryResize(UINT64_MAX).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(charge.Bytes(), 80u);
        EXPECT_EQ(ledger.Statistics().PeakBytes, 100u);
        ASSERT_TRUE(charge.TryResize(5).Succeeded());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 25u);
        EXPECT_EQ(ledger.Statistics().PeakByCategory[1], 80u);
        ASSERT_TRUE(charge.TryResize(0).Succeeded());
        EXPECT_FALSE(charge.IsActive());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 20u);
        ASSERT_TRUE(ledger.TryCharge(80, CookMemoryCategory::CatalogGraph, &charge).Succeeded());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 100u);
    }

    TEST(CookMemoryTest, ExactAggregateBudgetIncludesSimultaneousCategories)
    {
        CookMemoryLedger ledger = {};
        ASSERT_TRUE(ledger.Initialize(100).Succeeded());
        CookMemoryCharge graph = {}, capture = {}, extra = {};
        ASSERT_TRUE(ledger.TryCharge(60, CookMemoryCategory::CatalogGraph, &graph).Succeeded());
        ASSERT_TRUE(ledger.TryCharge(40, CookMemoryCategory::Captures, &capture).Succeeded());
        EXPECT_EQ(ledger.RemainingBytes(), 0u);
        const auto before = ledger.Statistics();
        EXPECT_EQ(ledger.TryCharge(1, CookMemoryCategory::Codecs, &extra).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_FALSE(extra.IsActive());
        EXPECT_EQ(ledger.Statistics().LiveByCategory, before.LiveByCategory);
        EXPECT_EQ(ledger.Statistics().PeakBytes, 100u);
        graph.Release();
        EXPECT_EQ(ledger.RemainingBytes(), 60u);
        EXPECT_EQ(ledger.Statistics().LiveBytes, 40u);
        EXPECT_EQ(ledger.Statistics().LiveByCategory[0], 0u);
        EXPECT_EQ(ledger.Statistics().PeakByCategory[0], 60u);
        ASSERT_TRUE(ledger.TryCharge(60, CookMemoryCategory::Codecs, &extra).Succeeded());
        EXPECT_EQ(ledger.Statistics().PeakBytes, 100u);
    }

    TEST(CookMemoryTest, OverflowAndInvalidArgumentsCannotMutateAccountingOrExistingCharges)
    {
        CookMemoryLedger ledger = {};
        ASSERT_TRUE(ledger.Initialize(100).Succeeded());
        CookMemoryCharge existing = {}, empty = {};
        ASSERT_TRUE(ledger.TryCharge(1, CookMemoryCategory::Captures, &existing).Succeeded());
        EXPECT_EQ(ledger.TryCharge(std::numeric_limits<uint64_t>::max(), CookMemoryCategory::Codecs, &empty).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(ledger.TryCharge(1, CookMemoryCategory::Codecs, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.TryCharge(1, CookMemoryCategory::Count, &empty).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.TryCharge(1, static_cast<CookMemoryCategory>(255), &empty).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.TryCharge(1, CookMemoryCategory::Codecs, &existing).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ledger.Initialize(200).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(existing.Bytes(), 1u);
        EXPECT_EQ(ledger.Statistics().LiveBytes, 1u);
        EXPECT_EQ(ledger.RemainingBytes(), 99u);
    }

    TEST(CookMemoryTest, MovesTransferExactlyOneReleaseAndSelfMoveIsSafe)
    {
        CookMemoryLedger ledger = {};
        ASSERT_TRUE(ledger.Initialize(100).Succeeded());
        CookMemoryCharge first = {}, second = {};
        ASSERT_TRUE(ledger.TryCharge(60, CookMemoryCategory::Codecs, &first).Succeeded());
        ASSERT_TRUE(ledger.TryCharge(40, CookMemoryCategory::Captures, &second).Succeeded());
        CookMemoryCharge moved(std::move(first));
        EXPECT_FALSE(first.IsActive());
        EXPECT_EQ(first.Bytes(), 0u);
        second = std::move(moved);
        EXPECT_FALSE(moved.IsActive());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 60u);
        auto* same = &second;
        second     = std::move(*same);
        EXPECT_EQ(ledger.Statistics().LiveBytes, 60u);
        second.Release();
        second.Release();
        EXPECT_EQ(ledger.Statistics().LiveBytes, 0u);
        EXPECT_EQ(ledger.Statistics().PeakBytes, 100u);
        ASSERT_TRUE(ledger.Initialize(10).Succeeded());
        EXPECT_EQ(ledger.Statistics().PeakBytes, 0u);
    }

    TEST(CookMemoryTest, EmptyChargeConsumesNoStorageAndNeedsNoRelease)
    {
        CookMemoryLedger ledger = {};
        ASSERT_TRUE(ledger.Initialize(1).Succeeded());
        CookMemoryCharge empty = {};
        ASSERT_TRUE(ledger.TryCharge(0, CookMemoryCategory::Codecs, &empty).Succeeded());
        EXPECT_FALSE(empty.IsActive());
        EXPECT_EQ(ledger.Statistics().LiveBytes, 0u);
        EXPECT_EQ(ledger.Statistics().PeakBytes, 0u);
    }

    TEST(CookMemoryTest, WorkspacesUseOneChargedBlockAndFallibleEngineContainers)
    {
        FixedCookAllocator allocator = {};
        {
            CookWorkspace workspace = {};
            ASSERT_TRUE(workspace.Initialize(allocator, 512, CookMemoryCategory::Codecs).Succeeded());
            auto* arena = workspace.Arena();
            ASSERT_NE(arena, nullptr);
            EXPECT_TRUE(arena->m_is_borrowed);
            EXPECT_EQ(allocator.Statistics().LiveBytes, 512u);
            EXPECT_EQ(workspace.Initialize(allocator, 16, CookMemoryCategory::Captures).Error(), Core::VFS::VFSError::InvalidPath);
            {
                Core::Memory::ArenaScratch        scratch(arena);
                Core::Containers::Array<uint64_t> values = {};
                values.m_allocator                       = arena;
                ASSERT_TRUE(values.try_reserve(32));
                EXPECT_FALSE(values.try_reserve(1024));
                EXPECT_EQ(values.capacity(), 32u);
                EXPECT_EQ(allocator.Statistics().LiveBytes, 512u); // no double charge
            }
            EXPECT_EQ(arena->m_current_offset, 0u);
            EXPECT_EQ(allocator.Allocations, 1u);
            EXPECT_EQ(allocator.Frees, 0u);
        }
        EXPECT_EQ(allocator.Frees, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookMemoryTest, WorkspaceFailuresLeaveItEmptyAndReleasesAreIdempotent)
    {
        FixedCookAllocator allocator = {};
        CookWorkspace      workspace = {};
        EXPECT_EQ(workspace.Initialize(allocator, 0, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(workspace.Initialize(allocator, 1, CookMemoryCategory::Count).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocations, 0u);
        EXPECT_EQ(workspace.Initialize(allocator, 4097, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(workspace.Arena(), nullptr);
        allocator.Failure = Core::VFS::VFSError::OutOfMemory;
        EXPECT_EQ(workspace.Initialize(allocator, 512, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::OutOfMemory);
        EXPECT_EQ(workspace.Arena(), nullptr);
        allocator.Failure = Core::VFS::VFSError::OK;
        ASSERT_TRUE(workspace.Initialize(allocator, 512, CookMemoryCategory::Codecs).Succeeded());
        workspace.Release();
        workspace.Release();
        EXPECT_EQ(workspace.Arena(), nullptr);
        EXPECT_EQ(allocator.Frees, 1u);
    }

    TEST(CookMemoryTest, NestedCodecFixedWorkingReservationsShareTheLedgerWithoutAllocating)
    {
        FixedCookAllocator                allocator = {};
        CookCodecAllocationRoute          route(allocator);
        Core::Memory::WorkingMemoryCharge first = {}, second = {}, refused = {};
        auto*                             budget = route.WorkingMemoryBudget();
        ASSERT_NE(budget, nullptr);
        EXPECT_TRUE(budget->TryReserveWorkingBytes(0));
        budget->ReleaseWorkingBytes(0);
        ASSERT_TRUE(first.TryAcquire(budget, 1024));
        ASSERT_TRUE(second.TryAcquire(budget, 2048));
        EXPECT_EQ(allocator.Statistics().LiveBytes, 3072u);
        EXPECT_FALSE(first.TryAcquire(budget, 1));
        EXPECT_EQ(allocator.Statistics().LiveBytes, 3072u);
        first.Release(); // Independent owners need not drain in LIFO order.
        EXPECT_EQ(allocator.Statistics().LiveBytes, 2048u);
        ASSERT_TRUE(first.TryAcquire(budget, 2048));
        EXPECT_EQ(allocator.Statistics().LiveBytes, 4096u);
        EXPECT_FALSE(refused.TryAcquire(budget, 1));
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Allocations, 0u);
        EXPECT_EQ(route.Allocate(1), nullptr); // Refusal remains sticky.
        first.Release();
        second.Release();
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.Allocations, 0u);
        EXPECT_EQ(allocator.Frees, 0u);
    }

    TEST(CookMemoryTest, CodecFixedWorkingReservationOverflowPreservesAndReleasesEarlierCharges)
    {
        FixedCookAllocator                allocator = {};
        CookCodecAllocationRoute          route(allocator);
        Core::Memory::WorkingMemoryCharge first = {}, refused = {};
        ASSERT_TRUE(first.TryAcquire(route.WorkingMemoryBudget(), 1));
        EXPECT_FALSE(refused.TryAcquire(route.WorkingMemoryBudget(), SIZE_MAX));
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 1u);
        first.Release();
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.Allocations, 0u);
    }

    TEST(CookMemoryTest, UnknownFixedWorkingContractsRefuseWithoutHeapFallbackButOrdinaryBudgetlessScopesRemainValid)
    {
        MemoryProfileNode                 allocator = {};
        CookCodecAllocationRoute          route(allocator);
        Core::Memory::WorkingMemoryCharge fixed = {};
        EXPECT_FALSE(fixed.TryAcquire(route.WorkingMemoryBudget(), 64));
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(route.Allocate(1), nullptr);
        EXPECT_TRUE(fixed.TryAcquire(nullptr, 64));
        fixed.Release();
    }

    TEST(CookMemoryTest, ImageDecoderAllocationsUseTheCodecsCategoryAndTheSameBudget)
    {
        FixedCookAllocator                     allocator = {};
        CookCodecAllocationRoute               route(allocator);
        Importers::ImageCodec::AllocationScope scope(route);
        void*                                  block = Importers::ImageCodec::Allocate(32);
        ASSERT_NE(block, nullptr);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 32u);
        EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::Codecs)], 32u);
        Importers::ImageCodec::Free(block);
        Importers::ImageCodec::Free(nullptr);
        EXPECT_EQ(allocator.Frees, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::OK);
    }

    TEST(CookMemoryTest, ImageDecoderRefusalIsStickyAndRetainsTheRealFailureCategory)
    {
        FixedCookAllocator allocator = {};
        allocator.Failure            = Core::VFS::VFSError::OutOfMemory;
        CookCodecAllocationRoute route(allocator);
        EXPECT_EQ(route.Allocate(4), nullptr);
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::OutOfMemory);
        allocator.Failure = Core::VFS::VFSError::OK;
        EXPECT_EQ(route.Allocate(4), nullptr);
        EXPECT_EQ(allocator.Allocations, 1u);
        CookCodecAllocationRoute limited(allocator);
        EXPECT_EQ(limited.Allocate(4097), nullptr);
        EXPECT_EQ(limited.Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookMemoryTest, ImageReallocationCountsBothLiveBlocksAndPreservesOldBytesOnBudgetFailure)
    {
        FixedCookAllocator       allocator = {};
        CookCodecAllocationRoute route(allocator);
        auto*                    original = static_cast<uint8_t*>(route.Allocate(3000));
        ASSERT_NE(original, nullptr);
        original[0] = 42;
        EXPECT_EQ(route.Reallocate(original, 3000, 2000), nullptr);
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(original[0], 42u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 3000u);
        EXPECT_EQ(allocator.Statistics().PeakBytes, 3000u);
        route.Free(original); // cleanup must still work after a sticky refusal
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookMemoryTest, ImageReallocationCopiesOnlyTheRetainedPrefixAndReleasesReplacedStorage)
    {
        FixedCookAllocator       allocator = {};
        CookCodecAllocationRoute route(allocator);
        auto*                    original = static_cast<uint8_t*>(route.Reallocate(nullptr, 0, 4));
        ASSERT_NE(original, nullptr);
        original[0]  = 42;
        original[3]  = 17;
        auto* larger = static_cast<uint8_t*>(route.Reallocate(original, 4, 8));
        ASSERT_NE(larger, nullptr);
        EXPECT_EQ(larger[0], 42u);
        EXPECT_EQ(larger[3], 17u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 8u);
        EXPECT_EQ(allocator.Statistics().PeakBytes, 12u);
        EXPECT_EQ(allocator.Frees, 1u);
        auto* smaller = static_cast<uint8_t*>(route.Reallocate(larger, 8, 2));
        ASSERT_NE(smaller, nullptr);
        EXPECT_EQ(smaller[0], 42u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 2u);
        EXPECT_EQ(route.Reallocate(smaller, 2, 0), nullptr);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.Frees, 3u);
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::OK);
    }
} // namespace ZEngine::Cook
