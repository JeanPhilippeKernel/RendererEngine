#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <type_traits>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    TEST(CookBudgetAllocatorTest, DefaultAndFailedInitializationAllocateNothing)
    {
        static_assert(!std::is_copy_constructible_v<CookBudgetAllocator>);
        static_assert(!std::is_move_constructible_v<CookBudgetAllocator>);
        Tests::FixedAllocator<4096, 4> parent    = {};
        CookBudgetAllocator            allocator = {};
        CookMemoryCharge               charge    = {};
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.Allocate(1, 1, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.TryCharge(1, CookMemoryCategory::Codecs, &charge).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Initialize(allocator, 4096).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Initialize(parent, 0).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Initialize(parent, sizeof(allocator) - 1).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(parent.Allocations, 0u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        ASSERT_TRUE(allocator.Initialize(parent, 4096).Succeeded());
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(allocator));
        EXPECT_EQ(allocator.Initialize(parent, 4096).Error(), Core::VFS::VFSError::InvalidPath);
        allocator.Free(nullptr);
        allocator.Release();
        allocator.Release();
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookBudgetAllocatorTest, HeadersCapacityAlignmentAndFixedStateShareTheBudget)
    {
        Tests::FixedAllocator<4096, 4> parent    = {};
        CookBudgetAllocator            allocator = {};
        ASSERT_TRUE(allocator.Initialize(parent, 4096).Succeeded());
        CookMemoryCharge fixed = {};
        ASSERT_TRUE(allocator.TryCharge(128, CookMemoryCategory::DiagnosticsResult, &fixed).Succeeded());
        const auto allocation = allocator.Allocate(256, 256, CookMemoryCategory::CatalogGraph);
        ASSERT_TRUE(allocation.Succeeded());
        EXPECT_EQ(reinterpret_cast<uintptr_t>(allocation.Value()) % 256, 0u);
        const auto retained = parent.Statistics().LiveBytes;
        EXPECT_GT(retained, 256u);
        EXPECT_EQ(parent.Allocations, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(allocator) + 128 + retained);
        EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::CatalogGraph)], retained);
        EXPECT_EQ(allocator.OutstandingAllocations(), 1u);
        EXPECT_EQ(Helpers::secure_memset(allocation.Value(), 42, 256, 256), Helpers::MEMORY_OP_SUCCESS);
        allocator.Free(allocation.Value());
        EXPECT_EQ(parent.Frees, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(allocator) + 128);
        fixed.Release();
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(allocator));
        EXPECT_EQ(allocator.OutstandingAllocations(), 0u);
    }

    TEST(CookBudgetAllocatorTest, ExactCapacityBudgetIncludesMetadataAndRefusesBeforeUpstreamAllocation)
    {
        Tests::FixedAllocator<4096, 4> parent    = {};
        CookBudgetAllocator            allocator = {};
        ASSERT_TRUE(allocator.Initialize(parent, 4096).Succeeded());
        auto allocation = allocator.Allocate(100, 64, CookMemoryCategory::Captures);
        ASSERT_TRUE(allocation.Succeeded());
        const auto exact = allocator.Statistics().LiveBytes;
        allocator.Free(allocation.Value());
        allocator.Release();
        ASSERT_TRUE(allocator.Initialize(parent, exact - 1).Succeeded());
        const auto calls = parent.Allocations;
        EXPECT_EQ(allocator.Allocate(100, 64, CookMemoryCategory::Captures).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(parent.Allocations, calls);
        allocator.Release();
        ASSERT_TRUE(allocator.Initialize(parent, exact).Succeeded());
        allocation = allocator.Allocate(100, 64, CookMemoryCategory::Captures);
        ASSERT_TRUE(allocation.Succeeded());
        EXPECT_EQ(allocator.Allocate(1, 1, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Statistics().LiveBytes, exact);
        allocator.Free(allocation.Value());
    }

    TEST(CookBudgetAllocatorTest, InvalidOverflowAndBackingFailuresPreserveExistingBytesAndCharges)
    {
        Tests::FixedAllocator<4096, 4> parent    = {};
        CookBudgetAllocator            allocator = {};
        ASSERT_TRUE(allocator.Initialize(parent, 4096).Succeeded());
        const auto original = allocator.Allocate(100, 16, CookMemoryCategory::Captures);
        ASSERT_TRUE(original.Succeeded());
        ASSERT_EQ(Helpers::secure_memset(original.Value(), 42, 100, 100), Helpers::MEMORY_OP_SUCCESS);
        const auto before = allocator.Statistics();
        const auto calls  = parent.Allocations;
        EXPECT_EQ(allocator.Allocate(0, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 0, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 3, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 16, CookMemoryCategory::Count).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(SIZE_MAX, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Allocate(SIZE_MAX / 2, size_t(1) << (sizeof(size_t) * 8 - 1), CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(parent.Allocations, calls);
        for (auto error : {Core::VFS::VFSError::OutOfMemory, Core::VFS::VFSError::SizeLimitExceeded})
        {
            parent.Failure = error;
            EXPECT_EQ(allocator.Allocate(100, 16, CookMemoryCategory::Codecs).Error(), error);
            EXPECT_EQ(allocator.Statistics().LiveBytes, before.LiveBytes);
            EXPECT_EQ(allocator.Statistics().LiveByCategory, before.LiveByCategory);
            EXPECT_EQ(allocator.OutstandingAllocations(), 1u);
            for (size_t i = 0; i < 100; ++i)
            {
                EXPECT_EQ(static_cast<const uint8_t*>(original.Value())[i], 42);
            }
        }
        allocator.Free(original.Value());
    }

    TEST(CookBudgetAllocatorTest, RepeatedCodecScratchAllocationsUseOneUpstreamBlockWithoutDoubleCharging)
    {
        Tests::FixedAllocator<ZKilo(64), 1> parent    = {};
        CookBudgetAllocator                 allocator = {};
        ASSERT_TRUE(allocator.Initialize(parent, ZKilo(64)).Succeeded());
        {
            CookScratchAllocator scratch = {};
            ASSERT_TRUE(scratch.Initialize(allocator, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
            const auto capacity = allocator.Statistics().LiveBytes;
            for (size_t i = 0; i < 4096; ++i)
            {
                const auto block = scratch.Allocate(128, 64, CookMemoryCategory::Codecs);
                ASSERT_TRUE(block.Succeeded());
                scratch.Free(block.Value());
                EXPECT_EQ(allocator.Statistics().LiveBytes, capacity);
            }
            EXPECT_EQ(parent.Allocations, 1u);
            EXPECT_EQ(allocator.OutstandingAllocations(), 1u);
        }
        EXPECT_EQ(parent.Frees, 1u);
        EXPECT_EQ(allocator.OutstandingAllocations(), 0u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, sizeof(allocator));
    }
    TEST(CookBudgetAllocatorTest, UnknownFootprintsFailBeforeAllocatingOrChargingState)
    {
        struct UnknownAllocator final : ICookAllocator
        {
            size_t                                                Calls = 0;
            Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({});
            }
            Core::VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Calls;
                return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::OutOfMemory);
            }
            void                 Free(void*) noexcept override {}
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
        } parent                      = {};
        CookBudgetAllocator allocator = {};
        EXPECT_EQ(allocator.Initialize(parent, 4096).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(parent.Calls, 0u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.OutstandingAllocations(), 0u);
    }

    TEST(CookBudgetAllocatorTest, FootprintErrorsAndUnderreportedCapacityCannotReachTheBackingAllocator)
    {
        struct FootprintAllocator final : ICookAllocator
        {
            Tests::FixedAllocator<4096, 4>                        Parent      = {};
            Core::VFS::VFSError                                   Error       = Core::VFS::VFSError::OK;
            bool                                                  Underreport = false;
            Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return Parent.MemoryRequirements();
            }
            Core::VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t, CookMemoryCategory) const noexcept override
            {
                if (Error != Core::VFS::VFSError::OK)
                {
                    return Core::VFS::VFSResult<size_t>::Fail(Error);
                }
                return Core::VFS::VFSResult<size_t>::Ok(Underreport ? bytes - 1 : bytes);
            }
            Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Parent.Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent.Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent.Statistics();
            }
        } parent                      = {};
        CookBudgetAllocator allocator = {};
        ASSERT_TRUE(allocator.Initialize(parent, 4096).Succeeded());
        const auto original = allocator.Allocate(128, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(original.Succeeded());
        const auto bytes = allocator.Statistics().LiveBytes;
        const auto calls = parent.Parent.Allocations;
        for (auto error : {Core::VFS::VFSError::Unsupported, Core::VFS::VFSError::SizeLimitExceeded, Core::VFS::VFSError::OutOfMemory})
        {
            parent.Error = error;
            EXPECT_EQ(allocator.Allocate(128, 16, CookMemoryCategory::Codecs).Error(), error);
            EXPECT_EQ(allocator.Statistics().LiveBytes, bytes);
            EXPECT_EQ(parent.Parent.Allocations, calls);
        }
        parent.Error       = Core::VFS::VFSError::OK;
        parent.Underreport = true;
        EXPECT_EQ(allocator.Allocate(128, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(allocator.Statistics().LiveBytes, bytes);
        EXPECT_EQ(parent.Parent.Allocations, calls);
        allocator.Free(original.Value());
    }

    TEST(CookBudgetAllocatorTest, UnknownRetainedStateFailsEvenWithAKnownBlockFootprint)
    {
        struct UnknownState final : ICookAllocator
        {
            size_t                       Calls = 0;
            Core::VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t, CookMemoryCategory) const noexcept override
            {
                return Core::VFS::VFSResult<size_t>::Ok(bytes);
            }
            Core::VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ++Calls;
                return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::OutOfMemory);
            }
            void                 Free(void*) noexcept override {}
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
        } parent                      = {};
        CookBudgetAllocator allocator = {};
        EXPECT_EQ(allocator.Initialize(parent, 4096).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(parent.Calls, 0u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookBudgetAllocatorTest, CompleteRetainedStateIsCheckedBeforeAllocationIncludingOverflow)
    {
        struct RetainedAllocator final : ICookAllocator
        {
            Tests::FixedAllocator<4096, 4>                        Parent       = {};
            CookAllocatorMemoryRequirements                       Requirements = {128, 512, false};
            Core::VFS::VFSError                                   Error        = Core::VFS::VFSError::OK;
            Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
            {
                return Error == Core::VFS::VFSError::OK ? Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok(Requirements) : Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Error);
            }
            Core::VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override
            {
                return Parent.AllocationFootprint(bytes, alignment, category);
            }
            Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Parent.Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Parent.Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Parent.Statistics();
            }
        } parent                      = {};
        CookBudgetAllocator allocator = {};
        const uint64_t      exact     = sizeof(allocator) + 128 + 512;
        EXPECT_EQ(allocator.Initialize(parent, exact - 1).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        ASSERT_TRUE(allocator.Initialize(parent, exact).Succeeded());
        EXPECT_EQ(allocator.Statistics().LiveBytes, exact);
        EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::DiagnosticsResult)], sizeof(allocator) + 128);
        EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::Codecs)], 512u);
        EXPECT_EQ(allocator.Allocate(1, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(parent.Parent.Allocations, 0u);
        allocator.Release();
        for (const auto requirements : {
             CookAllocatorMemoryRequirements{UINT64_MAX,          0, false},
             CookAllocatorMemoryRequirements{         1, UINT64_MAX, false}
        })
        {
            parent.Requirements = requirements;
            EXPECT_EQ(allocator.Initialize(parent, UINT64_MAX).Error(), Core::VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        }
        parent.Requirements = {0, 0, true};
        EXPECT_EQ(allocator.Initialize(parent, 4096).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        parent.Error = Core::VFS::VFSError::Unsupported;
        EXPECT_EQ(allocator.Initialize(parent, 4096).Error(), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(parent.Parent.Allocations, 0u);
    }
} // namespace ZEngine::Cook
