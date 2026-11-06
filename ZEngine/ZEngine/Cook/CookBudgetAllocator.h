#pragma once
#include <ZEngine/Cook/CookMemory.h>

namespace ZEngine::Cook
{
    // Per-call policy around the caller's backing allocator. One upstream block
    // includes its inline charge/header and alignment slack: no allocation map,
    // separate control block, eager reservation, or uncharged fallback. Use
    // CookWorkspace/CookScratchAllocator above this for amortized container and
    // codec allocation. The supplied parent must outlive this synchronous run.
    class CookBudgetAllocator final : public ICookAllocator
    {
    public:
        CookBudgetAllocator() = default;
        ~CookBudgetAllocator() override;
        CookBudgetAllocator(const CookBudgetAllocator&)                                                           = delete;
        CookBudgetAllocator&                                                operator=(const CookBudgetAllocator&) = delete;

        // Includes this fixed state and the complete retained parent chain,
        // even when those values live on the caller stack. Optional coverage
        // discounts only proven fixed state; its owner must charge that storage
        // separately before any work. No coverage pointer is retained. No allocation.
        [[nodiscard]] Core::VFS::VFSResult<void>                            Initialize(ICookAllocator& parent, uint64_t max_working_bytes, const ICookMemoryCoverage* coverage = nullptr);
        void                                                                Release() noexcept;

        [[nodiscard]] Core::VFS::VFSResult<void*>                           Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override;
        void                                                                Free(void* pointer) noexcept override;
        [[nodiscard]] CookMemoryStatistics                                  Statistics() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<size_t>                          AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept override;
        [[nodiscard]] const ICookAllocator*                                 ParentAllocator() const noexcept override
        {
            return m_parent;
        }

        // Charges fixed caller-owned state such as diagnostics/pending results.
        // Charges must be released before this allocator, just like allocations.
        [[nodiscard]] Core::VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override;
        [[nodiscard]] size_t                     OutstandingAllocations() const noexcept
        {
            return m_outstanding;
        }

    private:
        struct Header
        {
            CookBudgetAllocator* Owner      = nullptr;
            void*                Allocation = nullptr;
            CookMemoryCharge     Charge     = {};
        };

        ICookAllocator*                                   m_parent               = nullptr;
        CookMemoryLedger                                  m_ledger               = {};
        CookMemoryCharge                                  m_self_charge          = {};
        CookMemoryCharge                                  m_pool_charge          = {};
        size_t                                            m_outstanding          = 0;
        uint64_t                                          m_state_bytes          = 0;
        bool                                              m_allocations_included = false;
        [[nodiscard]] static Core::VFS::VFSResult<size_t> BlockCapacity(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept;
    };
} // namespace ZEngine::Cook
