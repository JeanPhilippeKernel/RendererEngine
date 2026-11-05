#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <gtest/gtest.h>
#include <stb/stb_image.h>
#include <type_traits>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace
    {
        using ScratchBackingAllocator = Tests::FixedAllocator<ZKilo(64), 1>;
    } // namespace

    TEST(CookScratchAllocatorTest, DefaultsAndInvalidRequestsAreInert)
    {
        static_assert(!std::is_copy_constructible_v<CookScratchAllocator>);
        static_assert(!std::is_move_constructible_v<CookScratchAllocator>);
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        EXPECT_EQ(scratch.Statistics().LiveBytes, 0u);
        EXPECT_EQ(scratch.Allocate(16, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Initialize(scratch, ZKilo(32), CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Initialize(parent, 0, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Count).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(parent.Allocations, 0u);
        scratch.Free(nullptr);
        scratch.Release();
        scratch.Release();
        EXPECT_EQ(parent.Frees, 0u);
    }

    TEST(CookScratchAllocatorTest, OneChargedBlockServesArbitraryAlignedAllocationsAndFrees)
    {
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        ASSERT_TRUE(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        for (uint32_t round = 0; round < 128; ++round)
        {
            Core::Containers::FixedArray<void*, 64> allocations = {};
            for (size_t i = 0; i < allocations.size(); ++i)
            {
                const size_t alignment  = size_t(1) << (i % 9);
                const auto   allocation = scratch.Allocate(17 + i, alignment, CookMemoryCategory::Codecs);
                ASSERT_TRUE(allocation.Succeeded()) << "round " << round << ", allocation " << i;
                allocations[i] = allocation.Value();
                EXPECT_EQ(reinterpret_cast<uintptr_t>(allocation.Value()) % alignment, 0u);
            }
            EXPECT_EQ(scratch.OutstandingAllocations(), allocations.size());
            for (size_t i = 0; i < allocations.size(); i += 2)
            {
                scratch.Free(allocations[i]);
            }
            for (size_t i = 1; i < allocations.size(); i += 2)
            {
                scratch.Free(allocations[i]);
            }
            EXPECT_EQ(scratch.OutstandingAllocations(), 0u);
        }
        EXPECT_EQ(parent.Allocations, 1u);
        EXPECT_EQ(parent.Frees, 0u);
        EXPECT_EQ(scratch.Statistics().LiveBytes, ZKilo(32));
        EXPECT_EQ(scratch.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::Codecs)], ZKilo(32));
        scratch.Release();
        EXPECT_EQ(parent.Frees, 1u);
        EXPECT_EQ(parent.Statistics().LiveBytes, 0u);
        EXPECT_EQ(parent.Statistics().PeakBytes, ZKilo(32));
    }

    TEST(CookScratchAllocatorTest, ExhaustionAndOverflowFailWithoutFallbackAndPreserveExistingBytes)
    {
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        ASSERT_TRUE(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Captures).Succeeded());
        const auto first = scratch.Allocate(128, 16, CookMemoryCategory::Codecs);
        ASSERT_TRUE(first.Succeeded());
        ASSERT_EQ(Helpers::secure_memset(first.Value(), 42, 128, 128), Helpers::MEMORY_OP_SUCCESS);
        EXPECT_EQ(scratch.Allocate(ZMega(1), 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(scratch.Allocate(SIZE_MAX, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(scratch.Allocate(1, size_t(1) << 31, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(scratch.Allocate(1, size_t(1) << (sizeof(size_t) * 8 - 1), CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(scratch.Allocate(0, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Allocate(1, 0, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Allocate(1, 3, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.Allocate(1, 16, CookMemoryCategory::Count).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.OutstandingAllocations(), 1u);
        for (size_t i = 0; i < 128; ++i)
        {
            EXPECT_EQ(static_cast<const uint8_t*>(first.Value())[i], 42);
        }
        scratch.Free(first.Value());
        const auto recovered = scratch.Allocate(ZKilo(16), 64, CookMemoryCategory::Codecs);
        ASSERT_TRUE(recovered.Succeeded());
        scratch.Free(recovered.Value());
        EXPECT_EQ(parent.Allocations, 1u);
    }

    TEST(CookScratchAllocatorTest, InitializationFailureReleasesBackingAndRetainsUnderlyingError)
    {
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        parent.Failure                  = Core::VFS::VFSError::OutOfMemory;
        EXPECT_EQ(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::OutOfMemory);
        EXPECT_EQ(parent.Frees, 0u);
        parent.Failure = Core::VFS::VFSError::OK;
        EXPECT_EQ(scratch.Initialize(parent, 1, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(parent.Frees, 1u);
        EXPECT_EQ(parent.Statistics().LiveBytes, 0u);
        ASSERT_TRUE(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        const auto attempts = parent.Allocations;
        EXPECT_EQ(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(parent.Allocations, attempts);
    }

    TEST(CookScratchAllocatorTest, NestedWorkspacesAreNotDoubleCharged)
    {
        ScratchBackingAllocator parent = {};
        CookScratchAllocator    outer = {}, inner = {};
        ASSERT_TRUE(outer.Initialize(parent, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        ASSERT_TRUE(inner.Initialize(outer, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        const auto allocation = inner.Allocate(128, 256, CookMemoryCategory::Codecs);
        ASSERT_TRUE(allocation.Succeeded());
        EXPECT_EQ(parent.Allocations, 1u);
        EXPECT_EQ(inner.Statistics().LiveBytes, ZKilo(64));
        inner.Free(allocation.Value());
        inner.Release();
        EXPECT_EQ(outer.OutstandingAllocations(), 0u);
        EXPECT_EQ(parent.Statistics().LiveBytes, ZKilo(64));
        outer.Release();
        EXPECT_EQ(parent.Statistics().LiveBytes, 0u);
    }

    TEST(CookScratchAllocatorTest, RealImageDecodesReuseOneWorkspaceWithoutAdditionalParentAllocations)
    {
        // One-pixel 24-bit BMP. The decoder's working buffers all use this slab.
        constexpr uint8_t bmp[] = {
            0x42, 0x4d, 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 21, 43, 65, 0,
        };
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        ASSERT_TRUE(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        CookCodecAllocationRoute               route(scratch);
        Importers::ImageCodec::AllocationScope scope(route);
        for (uint32_t i = 0; i < 128; ++i)
        {
            int   width = 0, height = 0, channels = 0;
            auto* pixels = stbi_load_from_memory(bmp, sizeof(bmp), &width, &height, &channels, STBI_rgb_alpha);
            ASSERT_NE(pixels, nullptr);
            EXPECT_EQ(width, 1);
            EXPECT_EQ(height, 1);
            EXPECT_EQ(pixels[0], 65);
            EXPECT_EQ(pixels[1], 43);
            EXPECT_EQ(pixels[2], 21);
            EXPECT_EQ(pixels[3], 255);
            stbi_image_free(pixels);
            EXPECT_EQ(scratch.OutstandingAllocations(), 0u);
        }
        EXPECT_EQ(route.Error(), Core::VFS::VFSError::OK);
        EXPECT_EQ(parent.Allocations, 1u);
        EXPECT_EQ(parent.Frees, 0u);
    }
    TEST(CookScratchAllocatorTest, FootprintQueriesIncludeBlockOverheadWithoutAllocatingFromThePool)
    {
        ScratchBackingAllocator parent  = {};
        CookScratchAllocator    scratch = {};
        EXPECT_EQ(scratch.AllocationFootprint(1, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(scratch.Initialize(parent, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        const auto calls = parent.Allocations;
        for (size_t bytes : {size_t(1), size_t(127), size_t(4097)})
        {
            const auto footprint = scratch.AllocationFootprint(bytes, 16, CookMemoryCategory::Codecs);
            ASSERT_TRUE(footprint.Succeeded());
            EXPECT_GT(footprint.Value(), bytes);
        }
        EXPECT_EQ(parent.Allocations, calls);
        EXPECT_EQ(scratch.OutstandingAllocations(), 0u);
        EXPECT_EQ(scratch.AllocationFootprint(1, 3, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(scratch.AllocationFootprint(SIZE_MAX, 16, CookMemoryCategory::Codecs).Error(), Core::VFS::VFSError::SizeLimitExceeded);
    }

    TEST(CookScratchAllocatorTest, NestedRetainedPoolsChargeIdleBackingOnceWithoutChargingChildBlocksAgain)
    {
        CookPageAllocator    native   = {};
        CookBudgetAllocator  upstream = {};
        CookScratchAllocator outer = {}, inner = {};
        CookBudgetAllocator  run = {}, child = {};
        EXPECT_EQ(outer.MemoryRequirements().Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(native.Initialize(ZMega(4)).Succeeded());
        ASSERT_TRUE(upstream.Initialize(native, ZMega(4)).Succeeded());
        ASSERT_TRUE(outer.Initialize(upstream, ZKilo(64), CookMemoryCategory::Codecs).Succeeded());
        ASSERT_TRUE(inner.Initialize(outer, ZKilo(32), CookMemoryCategory::Codecs).Succeeded());
        const auto requirements = inner.MemoryRequirements();
        ASSERT_TRUE(requirements.Succeeded());
        EXPECT_EQ(requirements.Value().StateBytes, sizeof(native) + sizeof(upstream) + sizeof(outer) + sizeof(inner));
        EXPECT_EQ(requirements.Value().PoolBytes, native.Statistics().LiveBytes - sizeof(native));
        EXPECT_TRUE(requirements.Value().IncludesAllocations);
        const uint64_t exact = sizeof(run) + requirements.Value().StateBytes + requirements.Value().PoolBytes;
        EXPECT_EQ(run.Initialize(inner, exact - 1).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(run.Statistics().LiveBytes, 0u);
        EXPECT_EQ(native.AllocationCount(), 1u);
        ASSERT_TRUE(run.Initialize(inner, exact).Succeeded());
        EXPECT_EQ(run.Statistics().LiveBytes, exact);
        EXPECT_EQ(CookAllocationWindow(run, exact), requirements.Value().PoolBytes);
        ASSERT_TRUE(child.Initialize(run, exact + sizeof(child)).Succeeded());
        const auto calls = native.AllocationCount();
        for (size_t repeat = 0; repeat < 256; ++repeat)
        {
            const auto allocation = child.Allocate(123, 128, CookMemoryCategory::Captures);
            ASSERT_TRUE(allocation.Succeeded());
            EXPECT_EQ(Helpers::secure_memset(allocation.Value(), 31, 123, 123), Helpers::MEMORY_OP_SUCCESS);
            EXPECT_EQ(run.Statistics().LiveBytes, exact);
            EXPECT_EQ(child.Statistics().LiveBytes, exact + sizeof(child));
            EXPECT_EQ(native.AllocationCount(), calls);
            child.Free(allocation.Value());
        }
        EXPECT_EQ(child.OutstandingAllocations(), 0u);
        EXPECT_EQ(run.OutstandingAllocations(), 0u);
        EXPECT_EQ(inner.OutstandingAllocations(), 0u);
        EXPECT_EQ(outer.OutstandingAllocations(), 1u);
        child.Release();
        run.Release();
        inner.Release();
        outer.Release();
        EXPECT_EQ(native.AllocationCount(), 1u);
        EXPECT_EQ(native.FreeCount(), 1u);
        EXPECT_EQ(native.Statistics().LiveBytes, sizeof(native));
        EXPECT_EQ(CookAllocationWindow(upstream, ZMega(4)), ZMega(4) - upstream.Statistics().LiveBytes);
    }
} // namespace ZEngine::Cook
