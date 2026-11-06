#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    TEST(CookPageAllocatorTest, InitializationAndQueriesHaveNoMappingSideEffects)
    {
        CookPageAllocator allocator = {};
        EXPECT_EQ(allocator.Allocate(1, 16, CookMemoryCategory::Codecs).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.AllocationFootprint(1, 16, CookMemoryCategory::Codecs).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.MemoryRequirements().Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Initialize(0).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Initialize(sizeof(allocator) - 1).Error(), VFS::VFSError::SizeLimitExceeded);
        ASSERT_TRUE(allocator.Initialize(ZMega(4)).Succeeded());
        const auto requirements = allocator.MemoryRequirements();
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(allocator));
        EXPECT_EQ(requirements.Value().PoolBytes, 0u);
        EXPECT_FALSE(requirements.Value().IncludesAllocations);
        EXPECT_EQ(allocator.Initialize(ZMega(4)).Error(), VFS::VFSError::AlreadyExists);
        const auto before = allocator.Statistics();
        for (size_t repeat = 0; repeat < 32; ++repeat)
        {
            const auto footprint = allocator.AllocationFootprint(7, 64, CookMemoryCategory::Codecs);
            ASSERT_TRUE(footprint.Succeeded());
            EXPECT_GE(footprint.Value(), 7u + 63u);
        }
        EXPECT_EQ(allocator.Statistics().LiveBytes, before.LiveBytes);
        EXPECT_EQ(allocator.Statistics().PeakBytes, before.PeakBytes);
        EXPECT_EQ(allocator.AllocationCount(), 0u);
        EXPECT_EQ(allocator.FreeCount(), 0u);
        allocator.Free(nullptr);
        allocator.Release();
        allocator.Release();
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        ASSERT_TRUE(allocator.Initialize(ZMega(4)).Succeeded());
    }

    TEST(CookPageAllocatorTest, PageRoundedHeadersAndOverAlignmentAreIncludedBeforeAllocation)
    {
        CookPageAllocator allocator = {};
        ASSERT_TRUE(allocator.Initialize(ZMega(4)).Succeeded());
        for (size_t alignment : {size_t(1), size_t(16), size_t(256), size_t(ZKilo(64))})
        {
            SCOPED_TRACE(alignment);
            const auto footprint = allocator.AllocationFootprint(123, alignment, CookMemoryCategory::Captures);
            ASSERT_TRUE(footprint.Succeeded());
            const auto before = allocator.Statistics();
            const auto block  = allocator.Allocate(123, alignment, CookMemoryCategory::Captures);
            ASSERT_TRUE(block.Succeeded());
            EXPECT_EQ(reinterpret_cast<uintptr_t>(block.Value()) % alignment, 0u);
            EXPECT_EQ(allocator.Statistics().LiveBytes, before.LiveBytes + footprint.Value());
            EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::Captures)], footprint.Value());
            EXPECT_EQ(Helpers::secure_memset(block.Value(), 17, 123, 123), Helpers::MEMORY_OP_SUCCESS);
            EXPECT_EQ(static_cast<const uint8_t*>(block.Value())[122], 17);
            allocator.Free(block.Value());
            EXPECT_EQ(allocator.Statistics().LiveBytes, before.LiveBytes);
        }
        EXPECT_EQ(allocator.AllocationCount(), 4u);
        EXPECT_EQ(allocator.FreeCount(), 4u);
    }

    TEST(CookPageAllocatorTest, ExactBudgetRefusesBeforeMappingAndCanRetryAfterFree)
    {
        CookPageAllocator allocator = {};
        ASSERT_TRUE(allocator.Initialize(ZMega(4)).Succeeded());
        const auto footprint = allocator.AllocationFootprint(100, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(footprint.Succeeded());
        const auto required = sizeof(allocator) + footprint.Value();
        allocator.Release();
        ASSERT_TRUE(allocator.Initialize(required - 1).Succeeded());
        EXPECT_EQ(allocator.Allocate(100, 16, CookMemoryCategory::Codecs).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.AllocationCount(), 0u);
        allocator.Release();
        ASSERT_TRUE(allocator.Initialize(required).Succeeded());
        auto block = allocator.Allocate(100, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(block.Succeeded());
        EXPECT_EQ(allocator.Statistics().LiveBytes, required);
        EXPECT_EQ(allocator.Allocate(1, 1, CookMemoryCategory::Captures).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.AllocationCount(), 1u);
        allocator.Free(block.Value());
        block = allocator.Allocate(100, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(block.Succeeded());
        allocator.Free(block.Value());
        EXPECT_EQ(allocator.OutstandingAllocations(), 0u);
    }

    TEST(CookPageAllocatorTest, InvalidAndOverflowRequestsPreserveLiveMappingsAndCharges)
    {
        CookPageAllocator allocator = {};
        ASSERT_TRUE(allocator.Initialize(ZMega(4)).Succeeded());
        const auto block = allocator.Allocate(32, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(block.Succeeded());
        ASSERT_EQ(Helpers::secure_memset(block.Value(), 41, 32, 32), Helpers::MEMORY_OP_SUCCESS);
        const auto bytes = allocator.Statistics().LiveBytes;
        EXPECT_EQ(allocator.Allocate(0, 16, CookMemoryCategory::Codecs).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 0, CookMemoryCategory::Codecs).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 3, CookMemoryCategory::Codecs).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(1, 16, CookMemoryCategory::Count).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocate(SIZE_MAX, 16, CookMemoryCategory::Codecs).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Allocate(SIZE_MAX / 2, size_t(1) << (sizeof(size_t) * 8 - 1), CookMemoryCategory::Codecs).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(allocator.Statistics().LiveBytes, bytes);
        EXPECT_EQ(allocator.AllocationCount(), 1u);
        EXPECT_EQ(static_cast<const uint8_t*>(block.Value())[31], 41);
        allocator.Free(block.Value());
    }

    TEST(CookPageAllocatorTest, BudgetIncludesNativeMappingSlackAndNestedBudgetHeaders)
    {
        CookPageAllocator   native = {};
        CookBudgetAllocator outer = {}, inner = {};
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(outer.Initialize(native, ZMega(4)).Succeeded());
        ASSERT_TRUE(inner.Initialize(outer, ZMega(4)).Succeeded());
        const auto footprint = inner.AllocationFootprint(123, 128, CookMemoryCategory::Codecs);
        ASSERT_TRUE(footprint.Succeeded());
        const auto block = inner.Allocate(123, 128, CookMemoryCategory::Codecs);
        ASSERT_TRUE(block.Succeeded());
        EXPECT_EQ(native.Statistics().LiveBytes - sizeof(native), footprint.Value());
        EXPECT_EQ(outer.Statistics().LiveBytes - sizeof(outer) - sizeof(native), footprint.Value());
        EXPECT_EQ(inner.Statistics().LiveBytes - sizeof(inner) - sizeof(outer) - sizeof(native), footprint.Value());
        EXPECT_EQ(native.AllocationCount(), 1u);
        inner.Free(block.Value());
        inner.Release();
        ASSERT_TRUE(inner.Initialize(outer, sizeof(inner) + sizeof(outer) + sizeof(native) + footprint.Value() - 1).Succeeded());
        EXPECT_EQ(inner.Allocate(123, 128, CookMemoryCategory::Codecs).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(native.AllocationCount(), 1u);
    }
} // namespace ZEngine::Cook
