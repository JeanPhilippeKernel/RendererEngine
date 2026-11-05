#include <ZEngine/Cook/CookPageAllocator.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    CookPageAllocator::~CookPageAllocator()
    {
        Release();
    }

    VFS::VFSResult<void> CookPageAllocator::Initialize(uint64_t maximum_bytes)
    {
        if (m_page_size)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::AlreadyExists);
        const auto page_size = Core::Memory::NativePageSize();
        if (!page_size)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        const auto initialized = m_ledger.Initialize(maximum_bytes);
        if (initialized.Failed())
            return initialized;
        const auto charged = m_ledger.TryCharge(sizeof(*this), CookMemoryCategory::DiagnosticsResult, &m_self_charge);
        if (charged.Failed())
            return charged;
        m_page_size   = page_size;
        m_allocations = m_frees = 0;
        return VFS::VFSResult<void>::Ok();
    }

    void CookPageAllocator::Release() noexcept
    {
        ZENGINE_VALIDATE_ASSERT(!OutstandingAllocations(), "Cook page allocator released before its consumers.");
        m_self_charge.Release();
        m_page_size = 0;
    }

    VFS::VFSResult<size_t> CookPageAllocator::AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept
    {
        if (!m_page_size || !bytes || !alignment || (alignment & (alignment - 1)) || category >= CookMemoryCategory::Count)
            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::InvalidPath);
        const auto     effective = std::max(alignment, alignof(Header));
        constexpr auto maximum   = std::numeric_limits<size_t>::max();
        if (effective - 1 > maximum - sizeof(Header) || bytes > maximum - sizeof(Header) - (effective - 1))
            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::SizeLimitExceeded);
        const size_t capacity = bytes + sizeof(Header) + effective - 1;
        const auto   rounded  = Core::Memory::NativePageFootprint(capacity, m_page_size);
        if (!rounded)
            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::SizeLimitExceeded);
        return VFS::VFSResult<size_t>::Ok(rounded);
    }

    VFS::VFSResult<void*> CookPageAllocator::Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept
    {
        const auto capacity = AllocationFootprint(bytes, alignment, category);
        if (capacity.Failed())
            return VFS::VFSResult<void*>::Fail(capacity.Error());
        CookMemoryCharge charge  = {};
        const auto       charged = m_ledger.TryCharge(capacity.Value(), category, &charge);
        if (charged.Failed())
            return VFS::VFSResult<void*>::Fail(charged.Error());
        void* mapping = Core::Memory::AllocateNativePages(capacity.Value());
        if (!mapping)
            return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
        auto*        base      = static_cast<uint8_t*>(mapping);
        const auto   effective = std::max(alignment, alignof(Header));
        const size_t padding   = (uintptr_t(0) - (reinterpret_cast<uintptr_t>(mapping) + sizeof(Header))) & (effective - 1);
        auto*        payload   = base + sizeof(Header) + padding;
        auto*        header    = new (payload - sizeof(Header)) Header{.Owner = this, .Mapping = mapping, .Bytes = capacity.Value()};
        header->Charge         = std::move(charge);
        ++m_allocations;
        return VFS::VFSResult<void*>::Ok(payload);
    }

    void CookPageAllocator::Free(void* pointer) noexcept
    {
        if (!pointer)
            return;
        auto* header = reinterpret_cast<Header*>(static_cast<uint8_t*>(pointer) - sizeof(Header));
        ZENGINE_VALIDATE_ASSERT(m_page_size && OutstandingAllocations() && header->Owner == this, "Foreign cook page allocation.");
        void*                         mapping = header->Mapping;
        [[maybe_unused]] const size_t bytes   = header->Bytes;
        CookMemoryCharge              charge(std::move(header->Charge));
        header->~Header();
        const bool freed = Core::Memory::FreeNativePages(mapping, bytes);
        ZENGINE_VALIDATE_ASSERT(freed, "Could not release an owned cook page mapping.");
        ++m_frees;
    }

    CookMemoryStatistics CookPageAllocator::Statistics() const noexcept
    {
        return m_ledger.Statistics();
    }

    VFS::VFSResult<CookAllocatorMemoryRequirements> CookPageAllocator::MemoryRequirements() const noexcept
    {
        if (!m_page_size)
            return VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(VFS::VFSError::InvalidPath);
        return VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({sizeof(*this), 0, false});
    }

    VFS::VFSResult<CookAllocatorMemoryRequirements> CookPageAllocator::UncoveredMemoryRequirements(const ICookMemoryCoverage& coverage) const noexcept
    {
        auto requirements = MemoryRequirements();
        if (requirements.Succeeded() && coverage.Contains(this, sizeof(*this)))
            requirements.Value().StateBytes = 0;
        return requirements;
    }
} // namespace ZEngine::Cook
