#pragma once
#include <ZEngine/Cook/CookMemory.h>

namespace ZEngine::Cook
{
    // Measurable native backing: one page-rounded mapping per allocation,
    // including its inline header/alignment. No CRT heap, allocation map or
    // guessed heap overhead. Wrap in explicitly sized reusable scratch pools
    // for high-frequency parser allocations rather than mapping each tiny block.
    // Synchronous ownership; this allocator must outlive every returned block.
    class CookPageAllocator final : public ICookAllocator
    {
    public:
        CookPageAllocator() = default;
        ~CookPageAllocator() override;
        CookPageAllocator(const CookPageAllocator&)                                                             = delete;
        CookPageAllocator&                                                  operator=(const CookPageAllocator&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>                            Initialize(uint64_t maximum_bytes);
        void                                                                Release() noexcept;
        [[nodiscard]] Core::VFS::VFSResult<size_t>                          AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<void*>                           Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override;
        void                                                                Free(void* pointer) noexcept override;
        [[nodiscard]] CookMemoryStatistics                                  Statistics() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<void>                            TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
        {
            return m_page_size ? m_ledger.TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        [[nodiscard]] uint64_t AllocationCount() const noexcept
        {
            return m_allocations;
        }
        [[nodiscard]] uint64_t FreeCount() const noexcept
        {
            return m_frees;
        }
        [[nodiscard]] uint64_t OutstandingAllocations() const noexcept
        {
            return m_allocations - m_frees;
        }

    private:
        struct Header
        {
            CookPageAllocator* Owner   = nullptr;
            void*              Mapping = nullptr;
            size_t             Bytes   = 0;
            CookMemoryCharge   Charge  = {};
        };
        CookMemoryLedger m_ledger      = {};
        CookMemoryCharge m_self_charge = {};
        size_t           m_page_size   = 0;
        uint64_t         m_allocations = 0;
        uint64_t         m_frees       = 0;
    };
} // namespace ZEngine::Cook
