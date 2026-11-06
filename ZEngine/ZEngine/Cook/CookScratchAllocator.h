#pragma once
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>

namespace ZEngine::Cook
{
    // One budgeted block for an asset's parser/decoder/inspection lifetime.
    // Individual allocations reuse TLSF storage, never touch another heap, and
    // can be freed in arbitrary order. Charge the complete retained workspace
    // once (including TLSF metadata and alignment gaps), not each child twice.
    // The parent allocator and every scratch consumer must obey that lifetime.
    // Synchronous per-asset use: independent workers need independent instances.
    class CookScratchAllocator final : public ICookAllocator
    {
    public:
        CookScratchAllocator() = default;
        ~CookScratchAllocator() override;
        CookScratchAllocator(const CookScratchAllocator&)                                = delete;
        CookScratchAllocator&                     operator=(const CookScratchAllocator&) = delete;

        // Capacity/category are explicit; there is no automatic reservation.
        // Failure leaves this allocator empty and releases acquired backing.
        [[nodiscard]] Core::VFS::VFSResult<void>  Initialize(ICookAllocator& parent, size_t bytes, CookMemoryCategory backing_category);
        void                                      Release() noexcept;

        [[nodiscard]] Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override;
        void                                      Free(void* pointer) noexcept override;
        [[nodiscard]] CookMemoryStatistics        Statistics() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<void>  TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
        {
            return m_parent ? m_parent->TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        [[nodiscard]] Core::VFS::VFSResult<size_t>                          AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept override;
        [[nodiscard]] const ICookAllocator*                                 ParentAllocator() const noexcept override
        {
            return m_parent;
        }
        [[nodiscard]] size_t OutstandingAllocations() const noexcept
        {
            return m_outstanding;
        }

    private:
        [[nodiscard]] Core::VFS::VFSResult<CookAllocatorMemoryRequirements> Requirements(const ICookMemoryCoverage*) const noexcept;
        ICookAllocator*                                                     m_parent           = nullptr;
        CookWorkspace                                                       m_workspace        = {};
        Core::Memory::TLSFSlab                                              m_slab             = {};
        size_t                                                              m_outstanding      = 0;
        size_t                                                              m_backing_bytes    = 0;
        CookMemoryCategory                                                  m_backing_category = CookMemoryCategory::Codecs;
    };
} // namespace ZEngine::Cook
