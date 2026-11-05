#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <cstdint>

namespace ZEngine::Cook
{
    // Retained caller-service state/backing, not live child allocation payloads.
    // Address reservation is telemetry only, separate from writable storage.
    struct CookServiceMemoryRequirements
    {
        uint64_t StateBytes           = 0;
        uint64_t StorageBytes         = 0;
        uint64_t ReservedAddressBytes = 0;
    };

    enum class CookMemoryCategory : uint8_t
    {
        CatalogGraph      = 0,
        Captures          = 1,
        Codecs            = 2,
        StagingManifest   = 3,
        DiagnosticsResult = 4,
        Count             = 5,
    };

    struct CookMemoryStatistics
    {
        uint64_t                                                                               LiveBytes      = 0;
        uint64_t                                                                               PeakBytes      = 0;
        Core::Containers::FixedArray<uint64_t, static_cast<size_t>(CookMemoryCategory::Count)> LiveByCategory = {};
        Core::Containers::FixedArray<uint64_t, static_cast<size_t>(CookMemoryCategory::Count)> PeakByCategory = {};
    };

    // Stable, allocation-free description of an allocator's upstream chain.
    // StateBytes includes fixed allocator/control state; PoolBytes includes
    // complete retained pools, idle capacity and upstream backing overhead. Ordinary
    // outstanding caller allocations are accounted by their owners separately.
    // IncludesAllocations means every returned block lives inside this retained
    // storage: charge the pool once, not its occupied child blocks again.
    // The chain/pool capacities must remain unchanged until consumers drain.
    struct CookAllocatorMemoryRequirements
    {
        uint64_t StateBytes          = 0;
        uint64_t PoolBytes           = 0;
        bool     IncludesAllocations = false;
    };

    // Borrowed proof that a complete fixed object range belongs to storage
    // charged separately for this call. Pure/constant-space; not an allocation
    // registry, recursive graph inspection or permission to discount pools.
    // The covered storage must remain charged/stable until all consumers drain.
    class ICookMemoryCoverage
    {
    public:
        virtual ~ICookMemoryCoverage()                                                       = default;
        [[nodiscard]] virtual bool Contains(const void* object, size_t bytes) const noexcept = 0;
    };

    class CookMemoryCharge;

    class ICookAllocator
    {
    public:
        virtual ~ICookAllocator()                                                                                                        = default;
        [[nodiscard]] virtual Core::VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept = 0;
        virtual void                                      Free(void* pointer) noexcept                                                   = 0;
        [[nodiscard]] virtual CookMemoryStatistics        Statistics() const noexcept                                                    = 0;
        // Charge fixed working storage that does not come from Allocate, such
        // as bounded parser objects on the call stack. This is not a child block
        // inside a retained pool. Charges outlive that storage and drain before
        // the allocator. Unknown implementations cannot silently waive the cost.
        [[nodiscard]] virtual Core::VFS::VFSResult<void>  TryCharge(uint64_t, CookMemoryCategory, CookMemoryCharge*)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
        }
        // Actual upstream ownership edge, stable until consumers drain. Leaf
        // allocators return null. Transparent wrappers expose their parent so
        // separately owned inputs can deduplicate a shared retained ancestor.
        [[nodiscard]] virtual const ICookAllocator* ParentAllocator() const noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual Core::VFS::VFSResult<CookAllocatorMemoryRequirements> MemoryRequirements() const noexcept
        {
            return Core::VFS::VFSResult<CookAllocatorMemoryRequirements>::Fail(Core::VFS::VFSError::Unsupported);
        }
        // Same physical pools/allocation-containment contract as above, excluding
        // only complete fixed allocator objects proven covered. Native wrappers
        // compose this through their real parent chain. Opaque/custom allocators
        // keep their full charge unless they explicitly implement that proof.
        // The ordinary profile always remains the complete physical footprint,
        // independent of which other caller happened to charge the same state.
        [[nodiscard]] virtual Core::VFS::VFSResult<CookAllocatorMemoryRequirements> UncoveredMemoryRequirements(const ICookMemoryCoverage&) const noexcept
        {
            return MemoryRequirements();
        }
        // Allocation-free upper bound for one returned block, including payload,
        // upstream headers, alignment/size-class slack and retained writable pages.
        // Query before Allocate; refusal must not allocate or mutate the parent.
        // Shared idle pools/fixed allocator state are reported separately by
        // MemoryRequirements, not silently hidden in this per-block footprint.
        // Unknown implementations fail closed, never assume zero overhead.
        [[nodiscard]] virtual Core::VFS::VFSResult<size_t> AllocationFootprint(size_t, size_t, CookMemoryCategory) const noexcept
        {
            return Core::VFS::VFSResult<size_t>::Fail(Core::VFS::VFSError::Unsupported);
        }
    };

    // Stream-sizing hint, not permission to allocate or a fragmentation promise.
    // A retained pool remains reusable even when its whole capacity is already
    // charged. Dynamic backing instead uses uncharged ledger headroom. Allocate
    // remains the authority for the real block's footprint and available space.
    [[nodiscard]] uint64_t                       CookAllocationWindow(const ICookAllocator& allocator, uint64_t maximum_working_bytes) noexcept;

    // Additional retained allocator/control/pool storage not already included
    // in covered. Single-parent chains: O(depth), constant storage, no registry,
    // allocation, parent mutation or guessed identity. Cycles/unknown/overflow
    // fail closed. Optional service coverage removes proven fixed state from
    // both chains and their common ancestor, never pools or block footprints.
    // Ordinary outstanding allocations are still owner-accounted.
    [[nodiscard]] Core::VFS::VFSResult<uint64_t> CookUncoveredAllocatorBytes(const ICookAllocator& allocator, const ICookAllocator& covered, const ICookMemoryCoverage* coverage = nullptr) noexcept;
    // Validate the stable single-parent topology before recursive profile queries.
    [[nodiscard]] Core::VFS::VFSResult<void>     ValidateCookAllocatorChain(const ICookAllocator&) noexcept;

    // One fallible, allocator-owned block exposed as an engine arena. Reuse it
    // for bounded per-asset parser/codec/container scratch; no secondary heap or
    // OS reservation is introduced. Capacity is already charged by the allocator.
    // All arena users must finish before Release/destruction. Not movable because
    // engine containers borrow the address of the embedded arena.
    class CookWorkspace
    {
    public:
        CookWorkspace() = default;
        ~CookWorkspace();
        CookWorkspace(const CookWorkspace&)                                         = delete;
        CookWorkspace&                              operator=(const CookWorkspace&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>    Initialize(ICookAllocator& allocator, size_t bytes, CookMemoryCategory category);
        void                                        Release() noexcept;
        [[nodiscard]] Core::Memory::ArenaAllocator* Arena() noexcept
        {
            return m_allocator ? &m_arena : nullptr;
        }

    private:
        ICookAllocator*              m_allocator = nullptr;
        Core::Memory::ArenaAllocator m_arena     = {};
    };

    class CookMemoryLedger;

    // A charge, not a memory allocation. Acquire before committing writable
    // storage, and release only after it is freed/rewound. An arena/codec owner
    // charges its retained capacity once, not every child allocation again.
    class CookMemoryCharge
    {
    public:
        CookMemoryCharge() = default;
        ~CookMemoryCharge();
        CookMemoryCharge(const CookMemoryCharge&)            = delete;
        CookMemoryCharge& operator=(const CookMemoryCharge&) = delete;
        CookMemoryCharge(CookMemoryCharge&& other) noexcept;
        CookMemoryCharge&                        operator=(CookMemoryCharge&& other) noexcept;

        void                                     Release() noexcept;
        // Transactional adjustment of an existing charge. Growth is checked
        // before mutation; shrink/free cannot fail. No allocation or new owner.
        [[nodiscard]] Core::VFS::VFSResult<void> TryResize(uint64_t retained_bytes) noexcept;
        [[nodiscard]] bool                       IsActive() const noexcept
        {
            return m_ledger != nullptr;
        }
        [[nodiscard]] uint64_t Bytes() const noexcept
        {
            return m_bytes;
        }

    private:
        friend class CookMemoryLedger;
        CookMemoryLedger*  m_ledger   = nullptr;
        uint64_t           m_bytes    = 0;
        CookMemoryCategory m_category = CookMemoryCategory::CatalogGraph;
    };

    // Per-call, synchronous, allocation-free accounting. Deliberately not atomic:
    // independent cooks own independent ledgers. This object must outlive charges.
    class CookMemoryLedger
    {
    public:
        CookMemoryLedger() = default;
        ~CookMemoryLedger();
        CookMemoryLedger(const CookMemoryLedger&)                                   = delete;
        CookMemoryLedger&                        operator=(const CookMemoryLedger&) = delete;

        // No storage reservation. Invalid/re-entrant initialization preserves state.
        [[nodiscard]] Core::VFS::VFSResult<void> Initialize(uint64_t max_working_bytes);
        [[nodiscard]] Core::VFS::VFSResult<void> TryCharge(uint64_t retained_bytes, CookMemoryCategory category, CookMemoryCharge* out_charge);
        [[nodiscard]] CookMemoryStatistics       Statistics() const noexcept
        {
            return m_statistics;
        }
        [[nodiscard]] uint64_t RemainingBytes() const noexcept
        {
            return m_limit - m_statistics.LiveBytes;
        }
        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return m_limit != 0;
        }

    private:
        friend class CookMemoryCharge;
        void                                     Release(uint64_t bytes, CookMemoryCategory category) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> Resize(CookMemoryCharge&, uint64_t bytes) noexcept;
        uint64_t                                 m_limit      = 0;
        CookMemoryStatistics                     m_statistics = {};
    };
} // namespace ZEngine::Cook
