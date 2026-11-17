#pragma once
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <atomic>
#include <mutex>

namespace ZEngine::Managers
{
    /// Bounded asynchronous sharing of identical verified scene closures and
    /// immutable asset versions across different closures. One worker builds
    /// closures sequentially; only completed versions are shared across them.
    /// The supplied allocation route is used only by initialization, one worker
    /// at a time and quiescent shutdown. It and every package's allocator/schema
    /// registry outlive the cache, its workers, and every lease. Package root
    /// providers and allocators shared by concurrent callers must support that
    /// concurrency or be synchronized by their owner.
    class CookedCpuClosureCache final
    {
    public:
        static constexpr uint32_t MaxClosures      = 16;
        static constexpr uint32_t MaxAssetVersions = 65536;
        enum class State : uint8_t
        {
            Pending,
            Ready,
            Failed,
        };

        struct Result
        {
            State                    StateValue = State::Pending;
            Core::VFS::VFSError      Error      = Core::VFS::VFSError::OK;
            const CookedCpuAssetSet* Assets     = nullptr;
        };

        struct CancellationToken
        {
            const CookedCpuClosureCache* Owner  = nullptr;
            uint32_t                     Slot   = 0;
            uint64_t                     Serial = 0;
        };

        /// A lease is exclusive to its owner, but different leases may be
        /// polled/released on different threads. An Assets borrow lasts only
        /// while this lease remains live; never reset it concurrently with use.
        class Lease final
        {
        public:
            Lease() = default;
            ~Lease();
            Lease(const Lease&)            = delete;
            Lease& operator=(const Lease&) = delete;
            Lease(Lease&&) noexcept;
            Lease&                          operator=(Lease&&) noexcept;
            [[nodiscard]] bool              Poll(Result*) const;
            /// Retain the same closure without reopening its package. The
            /// output must be empty; bounded admission can reject the retain.
            /// As with Poll, synchronize against resetting this lease.
            [[nodiscard]] bool              Retain(Lease* output) const;
            /// Copy only while this lease is exclusively owned; the token can
            /// be published to another thread and does not retain the lease or
            /// the cache; both must still outlive token use.
            [[nodiscard]] CancellationToken CancellationIdentity() const noexcept
            {
                return {m_owner, m_slot, m_serial};
            }
            explicit operator bool() const noexcept
            {
                return m_owner != nullptr;
            }
            void Reset() noexcept;

        private:
            friend class CookedCpuClosureCache;
            CookedCpuClosureCache* m_owner  = nullptr;
            uint32_t               m_slot   = 0;
            uint64_t               m_serial = 0;
        };

        CookedCpuClosureCache() = default;
        ~CookedCpuClosureCache();
        CookedCpuClosureCache(const CookedCpuClosureCache&)            = delete;
        CookedCpuClosureCache& operator=(const CookedCpuClosureCache&) = delete;

        /// The route must provide a working-memory budget. Capacities and asset
        /// limits remain fixed until shutdown, so shared requests cannot weaken
        /// one another's admission policy.
        [[nodiscard]] bool     Initialize(Core::Memory::IAllocationRoute&, const ECS::SceneComponentSchemaRegistry&, const CookedCpuAssetLimits&, uint32_t capacity, uint32_t max_leases_per_closure, uint32_t max_asset_versions);
        /// Coalesces by captured manifest digest and selected scene UUID. On a
        /// new request the package is consumed; on a hit it remains with the
        /// caller. False means invalid input or bounded admission backpressure.
        [[nodiscard]] bool     Request(CookedPackageSnapshot&& package, Lease* output);
        /// Worker-only, nonblocking admission to the single cache worker. Runs
        /// one queued build or retirement; false means no work or another worker
        /// is active. Call again until idle, including during shutdown.
        [[nodiscard]] bool     PumpOne(const std::atomic<bool>* owner_cancelled = nullptr);
        /// Signal a building closure only if this exact lease is its sole
        /// owner. Shared consumers must not be cancelled on another's behalf.
        [[nodiscard]] bool     CancelUnsharedBuild(CancellationToken token) noexcept;
        struct Statistics
        {
            uint32_t Closures      = 0;
            uint32_t Leases        = 0;
            uint32_t Retiring      = 0;
            uint32_t AssetVersions = 0;
        };
        /// Nonblocking snapshot. False leaves output unchanged if the worker is busy.
        [[nodiscard]] bool TryGetStatistics(Statistics* output);
        /// Quiescent operation. Fails without modifying the cache while leases,
        /// queued work, retirement or a worker remain. Drain using PumpOne first.
        [[nodiscard]] bool Shutdown();

    private:
        enum class Phase : uint8_t
        {
            Free,
            Queued,
            Building,
            Ready,
            Failed,
            Retiring,
            Cleaning
        };
        struct Entry;
        [[nodiscard]] bool                       Poll(uint32_t slot, uint64_t serial, Result*) const;
        [[nodiscard]] bool                       Retain(uint32_t slot, uint64_t serial, Lease* output);
        void                                     Release(uint32_t slot, uint64_t serial) noexcept;
        mutable std::mutex                       m_mutex        = {};
        std::mutex                               m_worker_mutex = {};
        Core::Memory::IAllocationRoute*          m_route        = nullptr;
        const ECS::SceneComponentSchemaRegistry* m_schemas      = nullptr;
        CookedCpuAssetPool*                      m_pool         = nullptr;
        Entry*                                   m_entries      = nullptr;
        CookedCpuAssetLimits                     m_limits       = {};
        uint64_t                                 m_next_serial  = 0;
        uint32_t                                 m_capacity     = 0;
        uint32_t                                 m_max_leases   = 0;
        uint32_t                                 m_cursor       = 0;
    };
} // namespace ZEngine::Managers
