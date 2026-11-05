#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Cook/CookServiceMemoryInternal.h>
#include <algorithm>
#include <limits>
#include <utility>

namespace ZEngine::Cook
{
    namespace
    {
        Core::VFS::VFSResult<size_t> ChainDepth(const ICookAllocator& allocator) noexcept
        {
            const ICookAllocator* slow  = &allocator;
            const ICookAllocator* fast  = &allocator;
            size_t                depth = 0;
            while (slow)
            {
                if (depth == SIZE_MAX)
                    return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
                ++depth;
                slow = slow->ParentAllocator();
                fast = fast ? fast->ParentAllocator() : nullptr;
                fast = fast ? fast->ParentAllocator() : nullptr;
                if (slow && slow == fast)
                    return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::Corrupted);
            }
            return Core::VFS::VFSResult<size_t>::Ok(depth);
        }

        bool ValidRequirements(const CookAllocatorMemoryRequirements& requirements) noexcept
        {
            return (!requirements.IncludesAllocations || requirements.PoolBytes) && requirements.PoolBytes <= UINT64_MAX - requirements.StateBytes;
        }

        struct AllocatorProfiles
        {
            CookAllocatorMemoryRequirements Physical  = {};
            CookAllocatorMemoryRequirements Uncovered = {};
        };

        bool ContainsProfile(const CookAllocatorMemoryRequirements& aggregate, const CookAllocatorMemoryRequirements& shared) noexcept
        {
            return shared.StateBytes <= aggregate.StateBytes && shared.PoolBytes <= aggregate.PoolBytes;
        }

        Core::VFS::VFSResult<AllocatorProfiles> Requirements(const ICookAllocator& allocator, const ICookMemoryCoverage* coverage) noexcept
        {
            const auto physical = allocator.MemoryRequirements();
            if (physical.Failed())
                return Core::VFS::VFSResult<AllocatorProfiles>::Fail(physical.Error());
            if (!ValidRequirements(physical.Value()))
                return Core::VFS::VFSResult<AllocatorProfiles>::Fail(Core::VFS::VFSError::Corrupted);
            if (!coverage)
                return Core::VFS::VFSResult<AllocatorProfiles>::Ok({physical.Value(), physical.Value()});
            const auto uncovered = allocator.UncoveredMemoryRequirements(*coverage);
            if (uncovered.Failed())
                return Core::VFS::VFSResult<AllocatorProfiles>::Fail(uncovered.Error());
            if (!ValidRequirements(uncovered.Value()) || uncovered.Value().StateBytes > physical.Value().StateBytes || uncovered.Value().PoolBytes != physical.Value().PoolBytes || uncovered.Value().IncludesAllocations != physical.Value().IncludesAllocations)
                return Core::VFS::VFSResult<AllocatorProfiles>::Fail(Core::VFS::VFSError::Corrupted);
            return Core::VFS::VFSResult<AllocatorProfiles>::Ok({physical.Value(), uncovered.Value()});
        }
    } // namespace

    size_t Detail::CookAllocatorQueryWorkingBytes() noexcept
    {
        // CookUncoveredAllocatorBytes retains owner/existing/shared profiles
        // and both depth results. Requirements adds two native profile results,
        // its aggregate Ok argument and returned result. ChainDepth instead
        // needs only two borrowed cursors and a scalar/result, so that smaller
        // helper does not increase the bound. No depth-sized table is present.
        return 3 * sizeof(Core::VFS::VFSResult<AllocatorProfiles>) + 2 * sizeof(Core::VFS::VFSResult<size_t>) + 2 * sizeof(const ICookAllocator*) + 2 * sizeof(uint64_t) + sizeof(size_t) + sizeof(Core::VFS::VFSResult<uint64_t>) + 2 * sizeof(Core::VFS::VFSResult<CookAllocatorMemoryRequirements>) + sizeof(AllocatorProfiles) + sizeof(Core::VFS::VFSResult<AllocatorProfiles>);
    }

    Core::VFS::VFSResult<void> ValidateCookAllocatorChain(const ICookAllocator& allocator) noexcept
    {
        const auto depth = ChainDepth(allocator);
        return depth.Succeeded() ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(depth.Error());
    }

    Core::VFS::VFSResult<uint64_t> CookUncoveredAllocatorBytes(const ICookAllocator& allocator, const ICookAllocator& covered, const ICookMemoryCoverage* coverage) noexcept
    {
        // Validate topology before querying recursive aggregate profiles. Floyd's
        // cycle check needs neither a visited array nor an arbitrary depth cap.
        const auto owner_depth = ChainDepth(allocator);
        if (owner_depth.Failed())
            return Core::VFS::VFSResult<uint64_t>::Fail(owner_depth.Error());
        const auto covered_depth = ChainDepth(covered);
        if (covered_depth.Failed())
            return Core::VFS::VFSResult<uint64_t>::Fail(covered_depth.Error());
        const auto owner = Requirements(allocator, coverage);
        if (owner.Failed())
            return Core::VFS::VFSResult<uint64_t>::Fail(owner.Error());
        const auto existing = Requirements(covered, coverage);
        if (existing.Failed())
            return Core::VFS::VFSResult<uint64_t>::Fail(existing.Error());
        const ICookAllocator* left  = &allocator;
        const ICookAllocator* right = &covered;
        for (size_t depth = covered_depth.Value(); depth < owner_depth.Value(); ++depth)
            left = left->ParentAllocator();
        for (size_t depth = owner_depth.Value(); depth < covered_depth.Value(); ++depth)
            right = right->ParentAllocator();
        while (left != right)
        {
            left  = left->ParentAllocator();
            right = right->ParentAllocator();
        }
        uint64_t state = owner.Value().Uncovered.StateBytes;
        uint64_t pools = owner.Value().Uncovered.PoolBytes;
        if (left)
        {
            const auto shared = Requirements(*left, coverage);
            if (shared.Failed())
                return Core::VFS::VFSResult<uint64_t>::Fail(shared.Error());
            // Service discounts must not mask an inconsistent physical parent
            // aggregate. Validate both views before subtracting the shared state.
            if (!ContainsProfile(owner.Value().Physical, shared.Value().Physical) || !ContainsProfile(existing.Value().Physical, shared.Value().Physical) || !ContainsProfile(owner.Value().Uncovered, shared.Value().Uncovered) || !ContainsProfile(existing.Value().Uncovered, shared.Value().Uncovered))
                return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::Corrupted);
            state -= shared.Value().Uncovered.StateBytes;
            pools -= shared.Value().Uncovered.PoolBytes;
        }
        // The complete owner sum was checked above; subtracting covered parts
        // cannot overflow. No charges/allocations are acquired by this query.
        return Core::VFS::VFSResult<uint64_t>::Ok(state + pools);
    }

    uint64_t CookAllocationWindow(const ICookAllocator& allocator, uint64_t maximum_working_bytes) noexcept
    {
        const auto requirements = allocator.MemoryRequirements();
        if (requirements.Succeeded() && requirements.Value().IncludesAllocations)
            return std::min(maximum_working_bytes, requirements.Value().PoolBytes);
        const uint64_t live = allocator.Statistics().LiveBytes;
        return live < maximum_working_bytes ? maximum_working_bytes - live : 0;
    }

    CookWorkspace::~CookWorkspace()
    {
        Release();
    }

    Core::VFS::VFSResult<void> CookWorkspace::Initialize(ICookAllocator& allocator, size_t bytes, CookMemoryCategory category)
    {
        if (m_allocator || !bytes || category >= CookMemoryCategory::Count)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        const auto allocation = allocator.Allocate(bytes, DEFAULT_ALIGNMENT, category);
        if (allocation.Failed())
            return Core::VFS::VFSResult<void>::Fail(allocation.Error());
        auto* pointer = allocation.Value();
        if (!pointer)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::OutOfMemory);
        if (reinterpret_cast<uintptr_t>(pointer) % DEFAULT_ALIGNMENT != 0 || !m_arena.InitializeBorrowed(pointer, bytes, "CookWorkspace"))
        {
            allocator.Free(pointer);
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        }
        m_allocator = &allocator;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookWorkspace::Release() noexcept
    {
        if (m_allocator)
        {
            auto* pointer = m_arena.m_memory;
            m_arena.Shutdown();
            m_allocator->Free(pointer);
            m_allocator = nullptr;
        }
    }

    CookMemoryCharge::~CookMemoryCharge()
    {
        Release();
    }

    CookMemoryCharge::CookMemoryCharge(CookMemoryCharge&& other) noexcept : m_ledger(std::exchange(other.m_ledger, nullptr)), m_bytes(std::exchange(other.m_bytes, 0)), m_category(other.m_category) {}

    CookMemoryCharge& CookMemoryCharge::operator=(CookMemoryCharge&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_ledger   = std::exchange(other.m_ledger, nullptr);
            m_bytes    = std::exchange(other.m_bytes, 0);
            m_category = other.m_category;
        }
        return *this;
    }

    void CookMemoryCharge::Release() noexcept
    {
        if (m_ledger)
        {
            m_ledger->Release(m_bytes, m_category);
            m_ledger = nullptr;
            m_bytes  = 0;
        }
    }

    Core::VFS::VFSResult<void> CookMemoryCharge::TryResize(uint64_t bytes) noexcept
    {
        return m_ledger ? m_ledger->Resize(*this, bytes) : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
    }

    CookMemoryLedger::~CookMemoryLedger()
    {
        ZENGINE_VALIDATE_ASSERT(m_statistics.LiveBytes == 0, "Cook memory ledger destroyed before its charges.");
    }

    Core::VFS::VFSResult<void> CookMemoryLedger::Initialize(uint64_t max_working_bytes)
    {
        if (!max_working_bytes || max_working_bytes > std::numeric_limits<size_t>::max() || m_statistics.LiveBytes)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        m_limit      = max_working_bytes;
        m_statistics = {};
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookMemoryLedger::TryCharge(uint64_t retained_bytes, CookMemoryCategory category, CookMemoryCharge* out_charge)
    {
        const auto index = static_cast<size_t>(category);
        if (!IsInitialized() || !out_charge || out_charge->IsActive() || index >= m_statistics.LiveByCategory.size())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        // Subtraction, not unchecked addition: UINT64_MAX inputs fail safely.
        if (retained_bytes > RemainingBytes())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        if (retained_bytes == 0)
            return Core::VFS::VFSResult<void>::Ok();
        m_statistics.LiveBytes             += retained_bytes;
        m_statistics.PeakBytes              = std::max(m_statistics.PeakBytes, m_statistics.LiveBytes);
        m_statistics.LiveByCategory[index] += retained_bytes;
        m_statistics.PeakByCategory[index]  = std::max(m_statistics.PeakByCategory[index], m_statistics.LiveByCategory[index]);
        out_charge->m_ledger                = this;
        out_charge->m_bytes                 = retained_bytes;
        out_charge->m_category              = category;
        return Core::VFS::VFSResult<void>::Ok();
    }

    Core::VFS::VFSResult<void> CookMemoryLedger::Resize(CookMemoryCharge& charge, uint64_t bytes) noexcept
    {
        if (bytes > charge.m_bytes && bytes - charge.m_bytes > RemainingBytes())
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
        const auto index                    = static_cast<size_t>(charge.m_category);
        m_statistics.LiveBytes             -= charge.m_bytes;
        m_statistics.LiveByCategory[index] -= charge.m_bytes;
        m_statistics.LiveBytes             += bytes;
        m_statistics.LiveByCategory[index] += bytes;
        m_statistics.PeakBytes              = std::max(m_statistics.PeakBytes, m_statistics.LiveBytes);
        m_statistics.PeakByCategory[index]  = std::max(m_statistics.PeakByCategory[index], m_statistics.LiveByCategory[index]);
        charge.m_bytes                      = bytes;
        if (!bytes)
            charge.m_ledger = nullptr;
        return Core::VFS::VFSResult<void>::Ok();
    }

    void CookMemoryLedger::Release(uint64_t bytes, CookMemoryCategory category) noexcept
    {
        const auto index = static_cast<size_t>(category);
        ZENGINE_VALIDATE_ASSERT(index < m_statistics.LiveByCategory.size() && bytes <= m_statistics.LiveByCategory[index], "Invalid cook memory charge release.");
        m_statistics.LiveByCategory[index] -= bytes;
        m_statistics.LiveBytes             -= bytes;
    }
} // namespace ZEngine::Cook
