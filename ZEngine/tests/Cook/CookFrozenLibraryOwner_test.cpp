#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookFrozenLibraryOwner.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <barrier>
#include <thread>
#include <type_traits>
#include "../TemporaryDirectory.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook::Tests
{
    namespace VFS = Core::VFS;

    namespace
    {
        VFS::VFSPath FrozenPath(cstring path)
        {
            return VFS::VFSPath::Parse(path).Value();
        }
        VFS::VFSRelativePath FrozenRelative(cstring path)
        {
            return VFS::VFSRelativePath::Parse(path).Value();
        }
        constexpr uint8_t    FrozenBytes[] = {3, 1, 4, 1};

        VFS::VFSResult<void> CountFrozenEntry(void* context, const VFS::VFSRootEntry& entry)
        {
            ++*static_cast<uint32_t*>(context);
            EXPECT_TRUE(entry.Identity.IsValid());
            return VFS::VFSResult<void>::Ok();
        }

        static_assert(!std::is_copy_constructible_v<CookFrozenLibraryOwner>);
        static_assert(std::is_nothrow_default_constructible_v<VFS::VFSReadRootLease>);
        static_assert(!std::is_move_constructible_v<CookFrozenLibraryOwner>);
        static_assert(!std::is_copy_constructible_v<CookFrozenLibraryReadLease>);
        static_assert(std::is_nothrow_move_constructible_v<CookFrozenLibraryReadLease>);
        static_assert(std::is_nothrow_move_assignable_v<CookFrozenLibraryReadLease>);
    } // namespace

    class CookFrozenLibraryOwnerTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager  Memory    = {};
        VFS::VFSMemoryBackend        Backend   = {};
        VFS::VFSContext              Context   = {};
        FixedAllocator<ZKilo(16), 2> Allocator = {};
        CookFrozenLibraryOwner       Owner     = {};

        void                         SetUp() override
        {
            Memory.Initialize(ZMega(2), {});
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(1), 64).Succeeded());
            for (cstring path : {"/assets", "/scenes", "/scratch", "/output"})
            {
                ASSERT_TRUE(Backend.CreateDir(FrozenPath(path)).Succeeded());
            }
            ASSERT_TRUE(Backend.WriteFile(FrozenPath("/assets/image.png"), FrozenBytes).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
        }

        void TearDown() override
        {
            EXPECT_TRUE(Owner.Reset().Succeeded());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            Context.Shutdown();
        }

        VFS::VFSResult<void> InitializeOwner()
        {
            const CookFrozenRootRequest roots[] = {
                {.Key = "scenes", .Context = &Context, .Path = FrozenPath("/scenes")},
                {.Key = "assets", .Context = &Context, .Path = FrozenPath("/assets")},
            };
            return Owner.Initialize(Allocator, roots);
        }
    };

    TEST_F(CookFrozenLibraryOwnerTest, CanonicalRootOrderAndExactLogicalLookupUseOneAllocation)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        EXPECT_EQ(Allocator.Allocations, 1u);
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        EXPECT_EQ(read.RootCount(), 2u);
        EXPECT_TRUE(read.RootKey(0) == "assets");
        EXPECT_TRUE(read.RootKey(1) == "scenes");
        EXPECT_TRUE(read.RootKey(2).empty());
        EXPECT_TRUE(read.RootIdentity(0).Value().IsValid());
        VFS::VFSRelativePath relative = {};
        EXPECT_EQ(read.Resolve(FrozenRelative("assets/image.png"), &relative).Value(), 0u);
        EXPECT_EQ(relative, FrozenRelative("image.png"));
        EXPECT_EQ(read.Resolve(FrozenRelative("scenes/level.scene"), &relative).Value(), 1u);
        EXPECT_EQ(relative, FrozenRelative("level.scene"));
        const auto previous = relative;
        EXPECT_EQ(read.Resolve(FrozenRelative("assets-other/image.png"), &relative).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(relative, previous);
        EXPECT_EQ(read.Resolve(FrozenRelative("assets"), &relative).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.Resolve({}, &relative).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.Resolve(FrozenRelative("assets/image.png"), nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.RootIdentity(2).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.Allocations, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, RootSortReservationRefusesBeforeAllocationAndExactPeakRetryPreservesMovedPins)
    {
        ChargeTrackingAllocator allocator   = {};
        allocator.Parent                    = &Allocator;
        CookFrozenLibraryOwner      owner   = {};
        const CookFrozenRootRequest roots[] = {
            {"scenes", &Context, FrozenPath("/scenes")},
            {"assets", &Context, FrozenPath("/assets")}
        };
        for (const auto error : {VFS::VFSError::Unsupported, VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            allocator.Failure = error;
            const auto calls  = allocator.ChargeCalls;
            EXPECT_EQ(owner.Initialize(allocator, roots).Error(), error);
            EXPECT_EQ(allocator.ChargeCalls, calls + 1);
            EXPECT_EQ(allocator.AllocationCalls, 0u);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            CookFrozenLibraryReadLease read = {};
            EXPECT_EQ(owner.TryAcquireRead(&read).Error(), VFS::VFSError::Unsupported);
        }
        allocator.Failure = VFS::VFSError::OK;
        const auto fixed  = allocator.Bytes;
        ASSERT_GT(fixed, sizeof(CookMemoryCharge));
        ASSERT_TRUE(Allocator.Ledger.Initialize(fixed - 1).Succeeded());
        EXPECT_EQ(owner.Initialize(allocator, roots).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.AllocationCalls, 0u);
        ASSERT_TRUE(Allocator.Ledger.Initialize(ZKilo(16)).Succeeded());
        ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
        const auto payload = Allocator.Statistics().LiveBytes;
        EXPECT_EQ(Allocator.Statistics().PeakBytes, payload + fixed);
        ASSERT_TRUE(owner.Reset().Succeeded());
        for (const uint64_t available : {payload + fixed - 1, payload + fixed})
        {
            ASSERT_TRUE(Allocator.Ledger.Initialize(available).Succeeded());
            const auto result = owner.Initialize(allocator, roots);
            EXPECT_EQ(result.Succeeded(), available == payload + fixed);
            if (result.Failed())
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
            }
            else
            {
                EXPECT_EQ(Allocator.Statistics().LiveBytes, payload);
                CookFrozenLibraryReadLease read = {};
                ASSERT_TRUE(owner.TryAcquireRead(&read).Succeeded());
                EXPECT_EQ(read.RootKey(0), "assets");
                EXPECT_EQ(read.RootKey(1), "scenes");
                EXPECT_TRUE(read.RootIdentity(0).Value().IsValid());
                EXPECT_TRUE(read.RootIdentity(1).Value().IsValid());
                EXPECT_NE(read.RootIdentity(0).Value(), read.RootIdentity(1).Value());
            }
            ASSERT_TRUE(owner.Reset().Succeeded());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookFrozenLibraryOwnerTest, SingleRootDoesNotQueryAnUnusedSortingBudget)
    {
        ChargeTrackingAllocator allocator   = {};
        allocator.Parent                    = &Allocator;
        allocator.Failure                   = VFS::VFSError::Unsupported;
        CookFrozenLibraryOwner      owner   = {};
        const CookFrozenRootRequest roots[] = {
            {"assets", &Context, FrozenPath("/assets")}
        };
        ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
        EXPECT_EQ(allocator.ChargeCalls, 0u);
        EXPECT_EQ(allocator.AllocationCalls, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, PreownedNativeBindingChargeIncludesHeadersAndPageRoundingWithoutAllocating)
    {
        CookPageAllocator      native = {}, separate = {};
        CookBudgetAllocator    run   = {};
        CookFrozenLibraryOwner owner = {};
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(separate.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(run.Initialize(native, ZMega(4)).Succeeded());
        EXPECT_EQ(owner.FixedWorkingBytes(run).Error(), VFS::VFSError::InvalidPath);
        const CookFrozenRootRequest roots[] = {
            {"assets", &Context, FrozenPath("/assets")},
            {"scenes", &Context, FrozenPath("/scenes")}
        };
        ASSERT_TRUE(owner.Initialize(native, roots).Succeeded());
        const uint64_t mapping = native.Statistics().LiveBytes - sizeof(native);
        ASSERT_GT(mapping, 0u);
        const auto live = native.Statistics().LiveBytes;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            EXPECT_EQ(owner.FixedWorkingBytes(run).Value(), sizeof(owner) + mapping);
            EXPECT_EQ(owner.FixedWorkingBytes(native).Value(), sizeof(owner) + mapping);
            EXPECT_EQ(owner.FixedWorkingBytes(separate).Value(), sizeof(owner) + mapping + sizeof(native));
        }
        EXPECT_EQ(native.AllocationCount(), 1u);
        EXPECT_EQ(native.Statistics().LiveBytes, live);
        ASSERT_TRUE(owner.Reset().Succeeded());
        EXPECT_EQ(native.FreeCount(), 1u);
        EXPECT_EQ(owner.FixedWorkingBytes(run).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookFrozenLibraryOwnerTest, SharedAndSiblingScratchBackingChargeOnlyUncoveredRetainedStorage)
    {
        CookPageAllocator      native     = {};
        CookScratchAllocator   owner_pool = {}, sibling = {}, nested = {};
        CookBudgetAllocator    shared_run = {}, sibling_run = {}, nested_run = {};
        CookFrozenLibraryOwner owner = {};
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(owner_pool.Initialize(native, ZKilo(128), CookMemoryCategory::Captures).Succeeded());
        const uint64_t owner_mapping = native.Statistics().LiveBytes - sizeof(native);
        ASSERT_TRUE(sibling.Initialize(native, ZKilo(128), CookMemoryCategory::Captures).Succeeded());
        ASSERT_TRUE(nested.Initialize(owner_pool, ZKilo(64), CookMemoryCategory::Captures).Succeeded());
        ASSERT_TRUE(shared_run.Initialize(owner_pool, ZMega(4)).Succeeded());
        ASSERT_TRUE(sibling_run.Initialize(sibling, ZMega(4)).Succeeded());
        ASSERT_TRUE(nested_run.Initialize(nested, ZMega(4)).Succeeded());
        const CookFrozenRootRequest roots[] = {
            {"assets", &Context, FrozenPath("/assets")},
            {"scenes", &Context, FrozenPath("/scenes")}
        };
        ASSERT_TRUE(owner.Initialize(owner_pool, roots).Succeeded());
        EXPECT_EQ(owner.FixedWorkingBytes(shared_run).Value(), sizeof(owner));
        EXPECT_EQ(owner.FixedWorkingBytes(nested_run).Value(), sizeof(owner));
        EXPECT_EQ(owner.FixedWorkingBytes(sibling_run).Value(), sizeof(owner) + sizeof(owner_pool) + owner_mapping);
        EXPECT_EQ(native.AllocationCount(), 2u);
        const auto live = native.Statistics().LiveBytes;
        for (size_t repeat = 0; repeat < 128; ++repeat)
        {
            EXPECT_EQ(owner.FixedWorkingBytes(sibling_run).Value(), sizeof(owner) + sizeof(owner_pool) + owner_mapping);
        }
        EXPECT_EQ(native.AllocationCount(), 2u);
        EXPECT_EQ(native.Statistics().LiveBytes, live);
        ASSERT_TRUE(owner.Reset().Succeeded());
        EXPECT_EQ(owner_pool.OutstandingAllocations(), 1u); // Only the nested pool.
    }

    TEST_F(CookFrozenLibraryOwnerTest, UnknownOrUnderreportedPreownedBindingFootprintsPreserveExistingRootsAndStorage)
    {
        struct BindingAllocator final : ICookAllocator
        {
            CookPageAllocator*   Parent      = nullptr;
            bool                 KnownState  = true;
            bool                 KnownBlocks = true;
            bool                 Underreport = false;
            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                return Parent->TryCharge(bytes, category, output);
            }
            const ICookAllocator* ParentAllocator() const noexcept override
            {
                return Parent;
            }
            VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                if (!KnownState)
                {
                    return VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(VFS::VFSError::Unsupported);
                }
                return VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({sizeof(*Parent) + sizeof(*this), 0, false});
            }
            VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override
            {
                if (!KnownBlocks)
                {
                    return VFS::VFSResult<size_t>::Fail(VFS::VFSError::Unsupported);
                }
                return Underreport ? VFS::VFSResult<size_t>::Ok(bytes - 1) : Parent->AllocationFootprint(bytes, alignment, category);
            }
            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Parent->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent->Statistics();
            }
        } allocator              = {};
        CookPageAllocator native = {};
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        allocator.Parent                    = &native;
        CookFrozenLibraryOwner      owner   = {};
        const CookFrozenRootRequest roots[] = {
            {"assets", &Context, FrozenPath("/assets")},
            {"scenes", &Context, FrozenPath("/scenes")}
        };
        ASSERT_TRUE(owner.Initialize(allocator, roots).Succeeded());
        const auto                 bytes = native.Statistics().LiveBytes;
        CookFrozenLibraryReadLease read  = {};
        ASSERT_TRUE(owner.TryAcquireRead(&read).Succeeded());
        allocator.KnownState = false;
        EXPECT_EQ(owner.FixedWorkingBytes(native).Error(), VFS::VFSError::Unsupported);
        allocator.KnownState  = true;
        allocator.KnownBlocks = false;
        EXPECT_EQ(owner.FixedWorkingBytes(native).Error(), VFS::VFSError::Unsupported);
        allocator.KnownBlocks = true;
        allocator.Underreport = true;
        EXPECT_EQ(owner.FixedWorkingBytes(native).Error(), VFS::VFSError::Corrupted);
        allocator.Underreport = false;
        EXPECT_EQ(owner.FixedWorkingBytes(native).Value(), sizeof(owner) + sizeof(allocator) + bytes - sizeof(native));
        EXPECT_EQ(native.AllocationCount(), 1u);
        EXPECT_EQ(native.Statistics().LiveBytes, bytes);
        EXPECT_EQ(read.RootCount(), 2u);
        EXPECT_TRUE(read.Stat(FrozenRelative("assets/image.png")).Succeeded());
    }

    TEST_F(CookFrozenLibraryOwnerTest, MultipleReadersExcludeMutationsResetAndReinitializationWithoutAllocating)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease first = {}, second = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&first).Succeeded());
        ASSERT_TRUE(Owner.TryAcquireRead(&second).Succeeded());
        EXPECT_EQ(first.Generation(), second.Generation());
        CookLibraryMutationLease mutation = {};
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Owner.Reset().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(InitializeOwner().Error(), VFS::VFSError::AlreadyExists);
        const auto generation = first.Generation();
        first.Reset();
        EXPECT_EQ(Owner.TryAcquireMutation(&mutation).Error(), VFS::VFSError::AlreadyExists);
        second.Reset();
        ASSERT_TRUE(Owner.TryAcquireMutation(&mutation).Succeeded());
        EXPECT_EQ(Owner.TryAcquireRead(&first).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_FALSE(first.IsValid());
        mutation.Reset();
        ASSERT_TRUE(Owner.TryAcquireRead(&first).Succeeded());
        EXPECT_GT(first.Generation(), generation);
        EXPECT_EQ(Allocator.Allocations, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, ContextMappingPreservesSpellingAndChecksRootBoundariesWithoutAllocating)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        VFS::VFSRelativePath mapped = {};
        EXPECT_TRUE(read.MapContextPath(FrozenPath("/assets/image.png"), &mapped).Succeeded());
        EXPECT_EQ(mapped, FrozenRelative("assets/image.png"));
        EXPECT_TRUE(read.MapContextPath(FrozenPath("/scenes/Nested/Main.yaml"), &mapped).Succeeded());
        EXPECT_EQ(mapped, FrozenRelative("scenes/Nested/Main.yaml"));
        const auto previous = mapped;
        EXPECT_EQ(read.MapContextPath(FrozenPath("/assets-other/image.png"), &mapped).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(read.MapContextPath(FrozenPath("/assets"), &mapped).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.MapContextPath(VFS::VFSPath::Root(), &mapped).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.MapContextPath({}, &mapped).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.MapContextPath(FrozenPath("/assets/image.png"), nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(mapped, previous);
        Context.Shutdown();
        EXPECT_TRUE(read.MapContextPath(FrozenPath("/assets/image.png"), &mapped).Succeeded());
        EXPECT_EQ(mapped, FrozenRelative("assets/image.png"));
        EXPECT_EQ(Allocator.Allocations, 1u);
        read.Reset();
        const auto sealed = mapped;
        EXPECT_TRUE(read.MapContextPath(FrozenPath("/assets/image.png"), &mapped).Failed());
        EXPECT_EQ(mapped, sealed);
    }

    TEST_F(CookFrozenLibraryOwnerTest, ContextRootMappingDoesNotDropTheFirstRelativeCharacter)
    {
        const CookFrozenRootRequest roots[] = {
            {.Key = "project", .Context = &Context, .Path = VFS::VFSPath::Root()}
        };
        ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        VFS::VFSRelativePath mapped = {};
        EXPECT_TRUE(read.MapContextPath(FrozenPath("/assets/image.png"), &mapped).Succeeded());
        EXPECT_EQ(mapped, FrozenRelative("project/assets/image.png"));
    }

    TEST_F(CookFrozenLibraryOwnerTest, AmbiguousContextNamesAcrossPhysicallyDistinctRootsFailClosed)
    {
        VFS::VFSMemoryBackend other_backend = {};
        ASSERT_TRUE(other_backend.InitializeOwned(ZMega(1), 16).Succeeded());
        ASSERT_TRUE(other_backend.CreateDir(FrozenPath("/assets")).Succeeded());
        VFS::VFSContext other = {};
        other.Initialize(&Memory.MainArena);
        ASSERT_TRUE(other.Mount(&other_backend, VFS::VFSPath::Root(), 0).Succeeded());
        const CookFrozenRootRequest roots[] = {
            { .Key = "first", .Context = &Context, .Path = FrozenPath("/assets")},
            {.Key = "second",   .Context = &other, .Path = FrozenPath("/assets")},
        };
        ASSERT_TRUE(Owner.Initialize(Allocator, roots).Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        auto mapped = FrozenRelative("previous/output");
        EXPECT_EQ(read.MapContextPath(FrozenPath("/assets/image.png"), &mapped).Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(mapped, FrozenRelative("previous/output"));
        other.Shutdown();
    }

    TEST_F(CookFrozenLibraryOwnerTest, OpenStatIdentityAndEnumerationUsePinnedReadAuthority)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        const auto path = FrozenRelative("assets/image.png");
        EXPECT_EQ(read.Stat(path).Value().SizeBytes, sizeof(FrozenBytes));
        uint32_t   index = UINT32_MAX;
        const auto open  = read.Open(path, &index);
        ASSERT_TRUE(open.Succeeded());
        EXPECT_EQ(index, 0u);
        EXPECT_EQ(open.Value()->Identity().Value(), read.Identity(path).Value());
        EXPECT_TRUE(open.Value()->Write(FrozenBytes, 0).Failed());
        EXPECT_EQ(read.CloseChecked(1, open.Value()).Error(), VFS::VFSError::InvalidPath);
        EXPECT_TRUE(read.CloseChecked(index, open.Value()).Succeeded());
        uint8_t  buffer[ZKilo(4)] = {};
        uint32_t count            = 0;
        ASSERT_TRUE(read.Visit(0, nullptr, 1, buffer, CountFrozenEntry, &count).Succeeded());
        EXPECT_EQ(count, 1u);
        EXPECT_EQ(read.Visit(2, nullptr, 1, buffer, CountFrozenEntry, &count).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(read.Open(path, nullptr).Error(), VFS::VFSError::InvalidPath);
        index = UINT32_MAX;
        EXPECT_EQ(read.Open(FrozenRelative("assets/missing"), &index).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(index, UINT32_MAX);
        EXPECT_EQ(Allocator.Allocations, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, ScratchAndOutputMustBePhysicallyDisjoint)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        VFS::VFSScratchRootLease     scratch = {};
        VFS::VFSPublicationRootLease output = {}, overlapping = {};
        ASSERT_TRUE(Context.AcquireScratchRoot(FrozenPath("/scratch"), &scratch).Succeeded());
        ASSERT_TRUE(Context.AcquirePublicationRoot(FrozenPath("/output"), &output).Succeeded());
        ASSERT_TRUE(Context.AcquirePublicationRoot(FrozenPath("/assets"), &overlapping).Succeeded());
        EXPECT_TRUE(read.RequireDisjoint(scratch).Succeeded());
        EXPECT_TRUE(read.RequireDisjoint(output).Succeeded());
        EXPECT_EQ(read.RequireDisjoint(overlapping).Error(), VFS::VFSError::PermissionDenied);
        VFS::VFSScratchRootLease empty = {};
        EXPECT_EQ(read.RequireDisjoint(empty).Error(), VFS::VFSError::InvalidPath);
    }

    TEST_F(CookFrozenLibraryOwnerTest, FailureRollsBackEveryAllocationAndMountPin)
    {
        const CookFrozenRootRequest missing[] = {
            { .Key = "assets", .Context = &Context,  .Path = FrozenPath("/assets")},
            {.Key = "missing", .Context = &Context, .Path = FrozenPath("/missing")},
        };
        EXPECT_EQ(Owner.Initialize(Allocator, missing).Error(), VFS::VFSError::NotFound);
        EXPECT_EQ(Allocator.Frees, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        EXPECT_TRUE(Context.Unmount(VFS::VFSPath::Root()).Succeeded());
        ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
        ASSERT_TRUE(InitializeOwner().Succeeded());
        EXPECT_EQ(InitializeOwner().Error(), VFS::VFSError::AlreadyExists);
        EXPECT_EQ(Allocator.Allocations, 2u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, RejectsDuplicateKeysAndPhysicalAliasesIncludingNestedRoots)
    {
        const CookFrozenRootRequest duplicate[] = {
            {.Key = "assets", .Context = &Context, .Path = FrozenPath("/assets")},
            {.Key = "assets", .Context = &Context, .Path = FrozenPath("/scenes")},
        };
        EXPECT_EQ(Owner.Initialize(Allocator, duplicate).Error(), VFS::VFSError::AlreadyExists);
        const CookFrozenRootRequest alias[] = {
            {.Key = "assets", .Context = &Context, .Path = FrozenPath("/assets")},
            { .Key = "alias", .Context = &Context, .Path = FrozenPath("/assets")},
        };
        EXPECT_EQ(Owner.Initialize(Allocator, alias).Error(), VFS::VFSError::PermissionDenied);
        const CookFrozenRootRequest nested[] = {
            {.Key = "assets", .Context = &Context, .Path = FrozenPath("/assets")},
            { .Key = "whole", .Context = &Context,  .Path = VFS::VFSPath::Root()},
        };
        EXPECT_EQ(Owner.Initialize(Allocator, nested).Error(), VFS::VFSError::PermissionDenied);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        EXPECT_TRUE(Context.Unmount(VFS::VFSPath::Root()).Succeeded());
    }

    TEST_F(CookFrozenLibraryOwnerTest, InvalidRequestsAndAllocatorFailureDoNotPublishRoots)
    {
        EXPECT_EQ(Owner.Initialize(Allocator, {}).Error(), VFS::VFSError::InvalidPath);
        const CookFrozenRootRequest invalid[] = {
            {.Key = "Assets", .Context = &Context, .Path = FrozenPath("/assets")}
        };
        EXPECT_EQ(Owner.Initialize(Allocator, invalid).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Allocator.Allocations, 0u);
        Allocator.Failure = VFS::VFSError::OutOfMemory;
        EXPECT_EQ(InitializeOwner().Error(), VFS::VFSError::OutOfMemory);
        CookFrozenLibraryReadLease read = {};
        EXPECT_EQ(Owner.TryAcquireRead(&read).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(read.IsValid());
        Allocator.Failure = VFS::VFSError::OK;
        EXPECT_TRUE(InitializeOwner().Succeeded());
    }

    TEST_F(CookFrozenLibraryOwnerTest, MoveTransfersExclusionAndExistingOutputIsNeverReplaced)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease original = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&original).Succeeded());
        EXPECT_EQ(Owner.TryAcquireRead(&original).Error(), VFS::VFSError::AlreadyExists);
        const auto                 generation = original.Generation();
        CookFrozenLibraryReadLease moved(std::move(original));
        EXPECT_FALSE(original.IsValid());
        EXPECT_EQ(original.RootCount(), 0u);
        EXPECT_EQ(moved.Generation(), generation);
        EXPECT_EQ(Owner.Reset().Error(), VFS::VFSError::AlreadyExists);
        original = std::move(moved);
        EXPECT_FALSE(moved.IsValid());
        original.Reset();
        EXPECT_TRUE(Owner.Reset().Succeeded());
        EXPECT_EQ(Allocator.Frees, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, ReaderAndMutationRaceNeverGrantConflictingAuthority)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        for (uint32_t i = 0; i < 32; ++i)
        {
            std::barrier               start(2);
            CookFrozenLibraryReadLease read     = {};
            CookLibraryMutationLease   mutation = {};
            VFS::VFSResult<void>       acquired = VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
            std::thread                reader([&] {
                start.arrive_and_wait();
                acquired = Owner.TryAcquireRead(&read);
            });
            start.arrive_and_wait();
            const auto mutating = Owner.TryAcquireMutation(&mutation);
            reader.join();
            EXPECT_NE(acquired.Succeeded(), mutating.Succeeded());
            EXPECT_NE(read.IsValid(), mutation.IsValid());
        }
        EXPECT_EQ(Allocator.Allocations, 1u);
    }

    TEST_F(CookFrozenLibraryOwnerTest, OwnedRootsRemainUsableAfterContextShutdown)
    {
        ASSERT_TRUE(InitializeOwner().Succeeded());
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(Owner.TryAcquireRead(&read).Succeeded());
        Context.Shutdown();
        EXPECT_EQ(read.Stat(FrozenRelative("assets/image.png")).Value().SizeBytes, sizeof(FrozenBytes));
        uint32_t   index = 0;
        const auto open  = read.Open(FrozenRelative("assets/image.png"), &index);
        ASSERT_TRUE(open.Succeeded());
        EXPECT_TRUE(read.CloseChecked(index, open.Value()).Succeeded());
    }

#if defined(__APPLE__) || defined(__linux__) || defined(_WIN32)
    TEST_F(CookFrozenLibraryOwnerTest, NativeRootsOutliveFacadesAndRejectAliasesAcrossContexts)
    {
        ZEngine::Tests::TemporaryDirectory directory = {};
        std::error_code                    error     = {};
        ASSERT_TRUE(directory.Create("cook-frozen-owner", error));
        ASSERT_TRUE(std::filesystem::create_directory(directory.Path() / "assets", error));
        const auto             native       = directory.Path().generic_u8string();
        CookFrozenLibraryOwner native_owner = {};
        {
            Core::Memory::MemoryManager memory = {};
            memory.Initialize(ZMega(2), {});
            VFS::VFSDiskBackend backend = {}, alias = {};
            backend.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &memory.MainArena);
            alias.Initialize(reinterpret_cast<cstring>(native.c_str()), VFS::VFSBackendCaps::Read, &memory.MainArena);
            VFS::VFSContext context = {}, other = {};
            context.Initialize(&memory.MainArena);
            other.Initialize(&memory.MainArena);
            ASSERT_TRUE(context.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
            ASSERT_TRUE(other.Mount(&alias, VFS::VFSPath::Root(), 0).Succeeded());
            const CookFrozenRootRequest duplicates[] = {
                {.Key = "assets", .Context = &context, .Path = FrozenPath("/assets")},
                { .Key = "alias",   .Context = &other, .Path = FrozenPath("/assets")},
            };
            EXPECT_EQ(native_owner.Initialize(Allocator, duplicates).Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            const CookFrozenRootRequest roots[] = {
                {.Key = "assets", .Context = &context, .Path = FrozenPath("/assets")}
            };
            ASSERT_TRUE(native_owner.Initialize(Allocator, roots).Succeeded());
            context.Shutdown();
            other.Shutdown();
            memory.Shutdown();
        }
        CookFrozenLibraryReadLease read = {};
        ASSERT_TRUE(native_owner.TryAcquireRead(&read).Succeeded());
        uint8_t  buffer[ZKilo(4)] = {};
        uint32_t count            = 0;
        EXPECT_TRUE(read.Visit(0, nullptr, 0, buffer, CountFrozenEntry, &count).Succeeded());
        EXPECT_EQ(count, 0u);
    }
#endif
} // namespace ZEngine::Cook::Tests
