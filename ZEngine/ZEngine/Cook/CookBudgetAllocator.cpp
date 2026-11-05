#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <algorithm>
#include <limits>
#include <new>
#include <utility>

namespace ZEngine::Cook
{
    CookBudgetAllocator::~CookBudgetAllocator()
    {
        Release();
    }

    Core::VFS::VFSResult<void> CookBudgetAllocator::Initialize(ICookAllocator& parent, uint64_t max_working_bytes, const ICookMemoryCoverage* coverage)
    {
        if (m_parent || &parent == this)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto initialized = m_ledger.Initialize(max_working_bytes);
        if (initialized.Failed())
        {
            return initialized;
        }
        const auto topology = ValidateCookAllocatorChain(parent);
        if (topology.Failed())
        {
            return topology;
        }
        const auto requirements = parent.MemoryRequirements();
        if (requirements.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(requirements.Error());
        }
        if (requirements.Value().IncludesAllocations && !requirements.Value().PoolBytes)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        if (requirements.Value().PoolBytes > UINT64_MAX - requirements.Value().StateBytes || requirements.Value().StateBytes > UINT64_MAX - sizeof(*this))
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        const auto profile = parent.AllocationFootprint(1, DEFAULT_ALIGNMENT, CookMemoryCategory::Codecs);
        if (profile.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(profile.Error());
        }
        if (profile.Value() < 1)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        const auto uncovered = coverage ? parent.UncoveredMemoryRequirements(*coverage) : requirements;
        if (uncovered.Failed())
        {
            return Core::VFS::VFSResult<void>::Fail(uncovered.Error());
        }
        if (uncovered.Value().StateBytes > requirements.Value().StateBytes || uncovered.Value().PoolBytes != requirements.Value().PoolBytes || uncovered.Value().IncludesAllocations != requirements.Value().IncludesAllocations)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        }
        const uint64_t own_bytes   = coverage && coverage->Contains(this, sizeof(*this)) ? 0 : sizeof(*this);
        const uint64_t state_bytes = own_bytes + uncovered.Value().StateBytes;
        if (state_bytes > max_working_bytes || requirements.Value().PoolBytes > max_working_bytes - state_bytes)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        const auto charged = m_ledger.TryCharge(state_bytes, CookMemoryCategory::DiagnosticsResult, &m_self_charge);
        if (charged.Failed())
        {
            return charged;
        }
        const auto pool = m_ledger.TryCharge(requirements.Value().PoolBytes, CookMemoryCategory::Codecs, &m_pool_charge);
        if (pool.Failed())
        {
            m_self_charge.Release();
            return pool;
        }
        m_parent               = &parent;
        m_state_bytes          = sizeof(*this) + requirements.Value().StateBytes;
        m_allocations_included = requirements.Value().IncludesAllocations;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookBudgetAllocator::Release() noexcept
    {
        ZENGINE_VALIDATE_ASSERT(!m_outstanding, "Cook budget allocator released before its allocations.");
        ZENGINE_VALIDATE_ASSERT(m_ledger.Statistics().LiveBytes == m_self_charge.Bytes() + m_pool_charge.Bytes(), "Cook budget allocator released before its charges.");
        m_pool_charge.Release();
        m_self_charge.Release();
        m_parent               = nullptr;
        m_state_bytes          = 0;
        m_allocations_included = false;
    }

    Core::VFS::VFSResult<CookAllocatorMemoryRequirements> CookBudgetAllocator::MemoryRequirements() const noexcept
    {
        if (!m_parent)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({m_state_bytes, m_pool_charge.Bytes(), m_allocations_included});
    }

    Core::VFS::VFSResult<CookAllocatorMemoryRequirements> CookBudgetAllocator::UncoveredMemoryRequirements(const ICookMemoryCoverage& coverage) const noexcept
    {
        if (!m_parent)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto parent = m_parent->UncoveredMemoryRequirements(coverage);
        if (parent.Failed())
        {
            return parent;
        }
        if (parent.Value().StateBytes > m_state_bytes - sizeof(*this) || parent.Value().PoolBytes != m_pool_charge.Bytes() || parent.Value().IncludesAllocations != m_allocations_included)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::Corrupted);
        }
        const uint64_t own_bytes = coverage.Contains(this, sizeof(*this)) ? 0 : sizeof(*this);
        if (parent.Value().StateBytes > UINT64_MAX - own_bytes)
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Ok({parent.Value().StateBytes + own_bytes, m_pool_charge.Bytes(), m_allocations_included});
    }

    Core::VFS::VFSResult<size_t> CookBudgetAllocator::BlockCapacity(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept
    {
        if (!bytes || !alignment || (alignment & (alignment - 1)) || category >= CookMemoryCategory::Count)
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto     effective_alignment = std::max(alignment, alignof(Header));
        constexpr auto maximum             = std::numeric_limits<size_t>::max();
        if (effective_alignment - 1 > maximum - sizeof(Header) || bytes > maximum - sizeof(Header) - (effective_alignment - 1))
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        }
        return Core::VFS::VFSResult<size_t>::Ok(bytes + sizeof(Header) + effective_alignment - 1);
    }

    Core::VFS::VFSResult<size_t> CookBudgetAllocator::AllocationFootprint(size_t bytes, size_t alignment, CookMemoryCategory category) const noexcept
    {
        if (!m_parent)
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        const auto capacity = BlockCapacity(bytes, alignment, category);
        if (capacity.Failed())
        {
            return capacity;
        }
        const auto footprint = m_parent->AllocationFootprint(capacity.Value(), DEFAULT_ALIGNMENT, category);
        if (footprint.Succeeded() && footprint.Value() < capacity.Value())
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::Corrupted);
        }
        return footprint;
    }

    Core::VFS::VFSResult<void*> CookBudgetAllocator::Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept
    {
        const auto footprint = AllocationFootprint(bytes, alignment, category);
        if (footprint.Failed())
        {
            return Core::VFS::VFSResult<void*>::Fail(footprint.Error());
        }
        const size_t     capacity            = BlockCapacity(bytes, alignment, category).Value();
        const auto       effective_alignment = std::max(alignment, alignof(Header));
        CookMemoryCharge charge              = {};
        const auto       charged             = m_ledger.TryCharge(m_allocations_included ? 0 : footprint.Value(), category, &charge);
        if (charged.Failed())
        {
            return Core::VFS::VFSResult<void*>::Fail(charged.Error());
        }
        const auto allocated = m_parent->Allocate(capacity, DEFAULT_ALIGNMENT, category);
        if (allocated.Failed())
        {
            return allocated;
        }
        if (!allocated.Value())
        {
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::OutOfMemory);
        }
        if (reinterpret_cast<uintptr_t>(allocated.Value()) % DEFAULT_ALIGNMENT)
        {
            m_parent->Free(allocated.Value());
            return Core::VFS::VFSResult<void*>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        auto*        base    = static_cast<uint8_t*>(allocated.Value());
        // Unsigned modular arithmetic computes padding without rounding a size
        // past SIZE_MAX. Capacity above includes this maximum alignment slack.
        const size_t padding = (uintptr_t(0) - (reinterpret_cast<uintptr_t>(base) + sizeof(Header))) & (effective_alignment - 1);
        auto*        payload = base + sizeof(Header) + padding;
        auto*        header  = new (payload - sizeof(Header)) Header{.Owner = this, .Allocation = base};
        header->Charge       = std::move(charge);
        ++m_outstanding;
        return Core::VFS::VFSResult<void*>::Ok(payload);
    }

    void CookBudgetAllocator::Free(void* pointer) noexcept
    {
        if (!pointer)
        {
            return;
        }
        auto* header = reinterpret_cast<Header*>(static_cast<uint8_t*>(pointer) - sizeof(Header));
        ZENGINE_VALIDATE_ASSERT(m_parent && m_outstanding && header->Owner == this, "Foreign cook budget allocation.");
        auto*            allocation = header->Allocation;
        // Move the charge out before destroying/freeing its own backing storage;
        // it remains live until the upstream Free has completed.
        CookMemoryCharge charge(std::move(header->Charge));
        header->~Header();
        m_parent->Free(allocation);
        --m_outstanding;
    }

    CookMemoryStatistics CookBudgetAllocator::Statistics() const noexcept
    {
        return m_ledger.Statistics();
    }

    Core::VFS::VFSResult<void> CookBudgetAllocator::TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output)
    {
        return m_parent ? m_ledger.TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
    }
} // namespace ZEngine::Cook
