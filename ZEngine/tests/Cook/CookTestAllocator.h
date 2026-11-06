#pragma once
#include <ZEngine/Cook/CookMemory.h>
#include <gtest/gtest.h>

namespace ZEngine::Cook::Tests
{
    // Direct owner/helper tests can inject a fixed-state refusal independently
    // of their retained payload allocations. Not a run/service ownership model.
    struct ChargeTrackingAllocator final : ICookAllocator
    {
        ICookAllocator*            Parent          = nullptr;
        Core::VFS::VFSError        Failure         = Core::VFS::VFSError::OK;
        uint64_t                   Bytes           = 0;
        uint32_t                   ChargeCalls     = 0;
        uint32_t                   AllocationCalls = 0;
        CookMemoryCategory         Category        = CookMemoryCategory::Count;

        Core::VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
        {
            ++ChargeCalls;
            Bytes    = bytes;
            Category = category;
            return Failure == Core::VFS::VFSError::OK ? Parent->TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Failure);
        }
        Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
        {
            ++AllocationCalls;
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
    };

    // Fixed-state gates must never allocate. Records the exact reservation and
    // can fail it independently of an already allocated graph/capture fixture.
    struct FixedStateAllocator final : ICookAllocator
    {
        CookMemoryLedger    Ledger      = {};
        Core::VFS::VFSError Failure     = Core::VFS::VFSError::OK;
        CookMemoryCategory  Category    = CookMemoryCategory::Count;
        uint64_t            Bytes       = 0;
        uint32_t            ChargeCalls = 0;
        uint32_t            Allocations = 0;

        FixedStateAllocator()
        {
            EXPECT_TRUE(Ledger.Initialize(ZKilo(4)).Succeeded());
        }

        Core::VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
        {
            ++ChargeCalls;
            Bytes    = bytes;
            Category = category;
            return Failure == Core::VFS::VFSError::OK ? Ledger.TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Failure);
        }

        Core::VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
        {
            ++Allocations;
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::Unsupported);
        }

        void Free(void*) noexcept override
        {
            ADD_FAILURE() << "Fixed-state helper must not free storage.";
        }

        CookMemoryStatistics Statistics() const noexcept override
        {
            return Ledger.Statistics();
        }
    };

    // Deterministic bounded backing and failure injection shared by cook tests.
    // No heap allocations; the default budget matches one slot, not all capacity.
    template <size_t BlockBytes, size_t SlotCount>
    struct FixedAllocator final : ICookAllocator
    {
        alignas(DEFAULT_ALIGNMENT) uint8_t Bytes[SlotCount][BlockBytes]       = {};
        CookMemoryLedger                                          Ledger      = {};
        Core::Containers::FixedArray<CookMemoryCharge, SlotCount> Charges     = {};
        Core::VFS::VFSError                                       Failure     = Core::VFS::VFSError::OK;
        uint32_t                                                  Allocations = 0;
        uint32_t                                                  Frees       = 0;

        FixedAllocator()
        {
            EXPECT_TRUE(Ledger.Initialize(BlockBytes).Succeeded());
        }

        Core::VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
        {
            return Ledger.TryCharge(bytes, category, output);
        }

        Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
        {
            ++Allocations;
            if (Failure != Core::VFS::VFSError::OK)
            {
                return Core::VFS::VFSResult<void*>::Fail(Failure);
            }
            if (!bytes || !alignment || (alignment & (alignment - 1)) || alignment > DEFAULT_ALIGNMENT || category >= CookMemoryCategory::Count)
            {
                return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::InvalidPath);
            }
            if (bytes > BlockBytes)
            {
                return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
            }
            for (size_t i = 0; i < Charges.size(); ++i)
            {
                if (Charges[i].IsActive())
                {
                    continue;
                }
                const auto charged = Ledger.TryCharge(bytes, category, &Charges[i]);
                return charged.Failed() ? Core::VFS::VFSResult<void*>::Fail(charged.Error()) : Core::VFS::VFSResult<void*>::Ok(Bytes[i]);
            }
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::OutOfMemory);
        }

        void Free(void* pointer) noexcept override
        {
            if (!pointer)
            {
                return;
            }
            for (size_t i = 0; i < Charges.size(); ++i)
            {
                if (pointer == Bytes[i])
                {
                    EXPECT_TRUE(Charges[i].IsActive()) << "Duplicate cook allocation release.";
                    ++Frees;
                    Charges[i].Release();
                    return;
                }
            }
            ADD_FAILURE() << "Foreign cook allocation.";
        }

        CookMemoryStatistics Statistics() const noexcept override
        {
            return Ledger.Statistics();
        }

        // Synthetic requested-byte model for deterministic unit tests, not a
        // claim about the physical inline fixture/slab's complete RSS footprint.
        Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({});
        }

        Core::VFS::VFSResult<size_t> AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override
        {
            if (!bytes || !alignment || (alignment & (alignment - 1)) || alignment > DEFAULT_ALIGNMENT || category >= CookMemoryCategory::Count)
            {
                return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::InvalidPath);
            }
            return Core::VFS::VFSResult<size_t>::Ok(bytes);
        }
    };
} // namespace ZEngine::Cook::Tests
