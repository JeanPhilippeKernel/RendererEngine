#pragma once
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdlib>

namespace ZEngine::Tests
{
    // Synthetic fixed-state budget; heap allocations are counted separately so
    // refusal can prove that libyaml has not allocated its first parser block.
    struct SceneTestAllocationRoute final : ZEngine::Core::Memory::IAllocationRoute, ZEngine::Core::Memory::IWorkingMemoryBudget
    {
        size_t                                       Limit         = 0;
        size_t                                       Required      = 0;
        size_t                                       Live          = 0;
        size_t                                       Peak          = 0;
        size_t                                       Attempts      = 0;
        size_t                                       Blocks        = 0;
        size_t                                       RefuseBytes   = 0;
        size_t                                       Refusals      = 0;
        size_t                                       WatchBytes    = 0;
        size_t                                       WatchCalls    = 0;
        size_t                                       WatchReleases = 0;

        ZEngine::Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
        {
            return this;
        }
        bool TryReserveWorkingBytes(size_t bytes) noexcept override
        {
            Required = bytes;
            if (WatchBytes && bytes == WatchBytes)
            {
                ++WatchCalls;
            }
            if (RefuseBytes && bytes == RefuseBytes)
            {
                ++Refusals;
                return false;
            }
            if (Live > Limit || bytes > Limit - Live)
            {
                return false;
            }
            Live += bytes;
            Peak  = std::max(Peak, Live);
            return true;
        }
        void ReleaseWorkingBytes(size_t bytes) noexcept override
        {
            if (WatchBytes && bytes == WatchBytes)
            {
                ++WatchReleases;
            }
            EXPECT_LE(bytes, Live);
            Live -= bytes;
        }
        void* Allocate(size_t bytes) noexcept override
        {
            ++Attempts;
            void* block = std::malloc(bytes);
            if (block)
            {
                ++Blocks;
            }
            return block;
        }
        void* Reallocate(void* pointer, size_t, size_t bytes) noexcept override
        {
            if (!pointer)
            {
                return Allocate(bytes);
            }
            ++Attempts;
            return std::realloc(pointer, bytes);
        }
        void Free(void* pointer) noexcept override
        {
            if (pointer)
            {
                EXPECT_GT(Blocks, 0u);
                --Blocks;
                std::free(pointer);
            }
        }
    };
} // namespace ZEngine::Tests
