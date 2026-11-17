#include <ZEngine/Managers/CookedCpuAssetPool.h>
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <limits>
#include <new>
#include <utility>

namespace ZEngine::Managers
{
    namespace VFS = Core::VFS;

    struct CookedCpuClosureCache::Entry
    {
        Phase                 PhaseValue = Phase::Free;
        uint64_t              Serial     = 0;
        uint32_t              Leases     = 0;
        Cook::Digest256       Manifest   = {};
        uuids::uuid           Scene      = {};
        std::atomic<bool>     Cancelled  = false;
        VFS::VFSError         Error      = VFS::VFSError::OK;
        CookedPackageSnapshot Package    = {};
        CookedCpuAssetSet     Assets     = {};
    };

    CookedCpuClosureCache::Lease::~Lease()
    {
        Reset();
    }

    CookedCpuClosureCache::Lease::Lease(Lease&& other) noexcept : m_owner(std::exchange(other.m_owner, nullptr)), m_slot(std::exchange(other.m_slot, 0)), m_serial(std::exchange(other.m_serial, 0)) {}

    CookedCpuClosureCache::Lease& CookedCpuClosureCache::Lease::operator=(Lease&& other) noexcept
    {
        if (this != &other)
        {
            Reset();
            m_owner  = std::exchange(other.m_owner, nullptr);
            m_slot   = std::exchange(other.m_slot, 0);
            m_serial = std::exchange(other.m_serial, 0);
        }
        return *this;
    }

    bool CookedCpuClosureCache::Lease::Poll(Result* output) const
    {
        return m_owner && m_owner->Poll(m_slot, m_serial, output);
    }

    bool CookedCpuClosureCache::Lease::Retain(Lease* output) const
    {
        return m_owner && m_owner->Retain(m_slot, m_serial, output);
    }

    void CookedCpuClosureCache::Lease::Reset() noexcept
    {
        if (m_owner)
        {
            auto* owner = std::exchange(m_owner, nullptr);
            owner->Release(m_slot, m_serial);
        }
        m_slot   = 0;
        m_serial = 0;
    }

    CookedCpuClosureCache::~CookedCpuClosureCache()
    {
        const bool closed = Shutdown();
        ZENGINE_VALIDATE_ASSERT(closed, "Cooked CPU closure cache destroyed before its leases and worker were drained.")
        (void) closed;
    }

    bool CookedCpuClosureCache::Initialize(Core::Memory::IAllocationRoute& route, const ECS::SceneComponentSchemaRegistry& schemas, const CookedCpuAssetLimits& limits, uint32_t capacity, uint32_t max_leases_per_closure, uint32_t max_asset_versions)
    {
        if (!capacity || capacity > MaxClosures || max_asset_versions > MaxAssetVersions || !max_leases_per_closure || !limits.MaxObjectBytes || !route.WorkingMemoryBudget() || !schemas.Frozen() || size_t(capacity) > std::numeric_limits<size_t>::max() / sizeof(Entry))
        {
            return false;
        }
        std::lock_guard worker_lock(m_worker_mutex);
        std::lock_guard lock(m_mutex);
        if (m_entries)
        {
            return false;
        }
        static_assert(alignof(Entry) <= alignof(std::max_align_t));
        auto* entries = static_cast<Entry*>(route.Allocate(size_t(capacity) * sizeof(Entry)));
        if (!entries)
        {
            return false;
        }
        for (uint32_t i = 0; i < capacity; ++i)
        {
            new (entries + i) Entry{};
        }
        const auto discard_entries = [&] {
            for (uint32_t i = 0; i < capacity; ++i)
            {
                entries[i].~Entry();
            }
            route.Free(entries);
        };
        static_assert(alignof(CookedCpuAssetPool) <= alignof(std::max_align_t));
        void* pool_storage = route.Allocate(sizeof(CookedCpuAssetPool));
        if (!pool_storage)
        {
            discard_entries();
            return false;
        }
        auto* pool = new (pool_storage) CookedCpuAssetPool{};
        if (!pool->Initialize(route, max_asset_versions))
        {
            pool->~CookedCpuAssetPool();
            route.Free(pool_storage);
            discard_entries();
            return false;
        }
        m_route       = &route;
        m_schemas     = &schemas;
        m_pool        = pool;
        m_entries     = entries;
        m_limits      = limits;
        m_capacity    = capacity;
        m_max_leases  = max_leases_per_closure;
        m_cursor      = 0;
        m_next_serial = 0;
        return true;
    }

    bool CookedCpuClosureCache::Request(CookedPackageSnapshot&& package, Lease* output)
    {
        if (!output || *output || !package.IsValid() || !package.Scene())
        {
            return false;
        }
        std::lock_guard lock(m_mutex);
        if (!m_entries || package.Schemas() != m_schemas)
        {
            return false;
        }
        const auto digest    = package.ManifestDigest();
        const auto scene     = package.Scene()->UUID;
        uint32_t   free_slot = m_capacity;
        for (uint32_t i = 0; i < m_capacity; ++i)
        {
            auto& entry = m_entries[i];
            if (entry.PhaseValue == Phase::Free)
            {
                if (free_slot == m_capacity)
                {
                    free_slot = i;
                }
                continue;
            }
            const bool shareable = entry.PhaseValue == Phase::Queued || entry.PhaseValue == Phase::Building || entry.PhaseValue == Phase::Ready;
            if (shareable && !entry.Cancelled.load(std::memory_order_acquire) && entry.Manifest == digest && entry.Scene == scene)
            {
                if (entry.Leases == m_max_leases)
                {
                    return false;
                }
                ++entry.Leases;
                output->m_owner  = this;
                output->m_slot   = i;
                output->m_serial = entry.Serial;
                return true;
            }
        }
        if (free_slot == m_capacity || m_next_serial == std::numeric_limits<uint64_t>::max())
        {
            return false;
        }
        auto& entry    = m_entries[free_slot];
        entry.Serial   = ++m_next_serial;
        entry.Leases   = 1;
        entry.Manifest = digest;
        entry.Scene    = scene;
        entry.Cancelled.store(false, std::memory_order_relaxed);
        entry.Error      = VFS::VFSError::OK;
        entry.Package    = std::move(package);
        entry.PhaseValue = Phase::Queued;
        output->m_owner  = this;
        output->m_slot   = free_slot;
        output->m_serial = entry.Serial;
        return true;
    }

    bool CookedCpuClosureCache::Retain(uint32_t slot, uint64_t serial, Lease* output)
    {
        if (!output || *output)
        {
            return false;
        }
        std::lock_guard lock(m_mutex);
        if (!m_entries || slot >= m_capacity)
        {
            return false;
        }
        auto&      entry     = m_entries[slot];
        const bool shareable = entry.PhaseValue == Phase::Queued || entry.PhaseValue == Phase::Building || entry.PhaseValue == Phase::Ready || entry.PhaseValue == Phase::Failed;
        if (entry.Serial != serial || !entry.Leases || entry.Leases >= m_max_leases || !shareable || entry.Cancelled.load(std::memory_order_acquire))
        {
            return false;
        }
        ++entry.Leases;
        output->m_owner  = this;
        output->m_slot   = slot;
        output->m_serial = serial;
        return true;
    }

    bool CookedCpuClosureCache::Poll(uint32_t slot, uint64_t serial, Result* output) const
    {
        if (!output)
        {
            return false;
        }
        std::lock_guard lock(m_mutex);
        if (!m_entries || slot >= m_capacity)
        {
            return false;
        }
        const auto& entry = m_entries[slot];
        if (entry.Serial != serial || !entry.Leases)
        {
            return false;
        }
        Result result = {};
        switch (entry.PhaseValue)
        {
            case Phase::Queued:
            case Phase::Building:
                break;
            case Phase::Ready:
                result.StateValue = State::Ready;
                result.Assets     = &entry.Assets;
                break;
            case Phase::Failed:
                result.StateValue = State::Failed;
                result.Error      = entry.Error;
                break;
            default:
                return false;
        }
        *output = result;
        return true;
    }

    void CookedCpuClosureCache::Release(uint32_t slot, uint64_t serial) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (!m_entries || slot >= m_capacity)
        {
            return;
        }
        auto& entry = m_entries[slot];
        if (entry.Serial != serial || !entry.Leases)
        {
            return;
        }
        if (--entry.Leases)
        {
            return;
        }
        if (entry.PhaseValue == Phase::Queued || entry.PhaseValue == Phase::Building)
        {
            entry.Cancelled.store(true, std::memory_order_release);
        }
        else if (entry.PhaseValue == Phase::Ready || entry.PhaseValue == Phase::Failed)
        {
            entry.PhaseValue = Phase::Retiring;
        }
    }

    bool CookedCpuClosureCache::CancelUnsharedBuild(CancellationToken token) noexcept
    {
        std::lock_guard lock(m_mutex);
        if (!m_entries || token.Owner != this || !token.Serial || token.Slot >= m_capacity)
            return false;
        auto& entry = m_entries[token.Slot];
        if (entry.Serial != token.Serial || entry.Leases != 1 || entry.PhaseValue != Phase::Building)
            return false;
        entry.Cancelled.store(true, std::memory_order_release);
        return true;
    }

    bool CookedCpuClosureCache::TryGetStatistics(Statistics* output)
    {
        if (!output)
            return false;
        std::unique_lock worker_lock(m_worker_mutex, std::try_to_lock);
        if (!worker_lock.owns_lock())
            return false;
        std::lock_guard lock(m_mutex);
        if (!m_entries || !m_pool)
            return false;
        Statistics result    = {};
        result.AssetVersions = m_pool->m_live;
        for (uint32_t i = 0; i < m_capacity; ++i)
        {
            const auto& entry = m_entries[i];
            if (entry.PhaseValue != Phase::Free)
                ++result.Closures;
            result.Leases += entry.Leases;
            if (entry.PhaseValue == Phase::Retiring || entry.PhaseValue == Phase::Cleaning)
                ++result.Retiring;
        }
        *output = result;
        return true;
    }

    bool CookedCpuClosureCache::PumpOne(const std::atomic<bool>* owner_cancelled)
    {
        std::unique_lock worker_lock(m_worker_mutex, std::try_to_lock);
        if (!worker_lock.owns_lock())
        {
            return false;
        }
        uint32_t selected = 0;
        bool     build    = false;
        {
            std::lock_guard lock(m_mutex);
            if (!m_entries)
            {
                return false;
            }
            selected = m_capacity;
            for (uint32_t offset = 0; offset < m_capacity; ++offset)
            {
                const uint32_t index = static_cast<uint32_t>((uint64_t(m_cursor) + offset) % m_capacity);
                auto&          entry = m_entries[index];
                if (entry.PhaseValue == Phase::Retiring || entry.PhaseValue == Phase::Queued)
                {
                    selected         = index;
                    m_cursor         = (index + 1) % m_capacity;
                    build            = entry.PhaseValue == Phase::Queued && entry.Leases && !entry.Cancelled.load(std::memory_order_acquire);
                    entry.PhaseValue = build ? Phase::Building : Phase::Cleaning;
                    if (build && entry.Leases == 1 && owner_cancelled && owner_cancelled->load(std::memory_order_acquire))
                        entry.Cancelled.store(true, std::memory_order_release);
                    break;
                }
            }
            if (selected == m_capacity)
            {
                return false;
            }
        }
        auto& entry = m_entries[selected];
        if (build)
        {
            const auto result = CookedCpuAssetSet::CaptureShared(entry.Package, *m_route, *m_pool, m_limits, &entry.Assets, &entry.Cancelled);
            {
                std::lock_guard lock(m_mutex);
                if (!entry.Leases)
                {
                    entry.PhaseValue = Phase::Cleaning;
                }
                else if (entry.Cancelled.load(std::memory_order_acquire))
                {
                    entry.Error      = VFS::VFSError::Cancelled;
                    entry.PhaseValue = Phase::Failed;
                }
                else if (result.Failed())
                {
                    entry.Error      = result.Error();
                    entry.PhaseValue = Phase::Failed;
                }
                else
                {
                    entry.PhaseValue = Phase::Ready;
                }
            }
        }
        // These objects are touched only by this worker. Never free a closure or
        // its rooted package while holding the producer-facing state lock.
        entry.Package.Reset();
        bool cleaning = false;
        {
            std::lock_guard lock(m_mutex);
            cleaning = entry.PhaseValue == Phase::Cleaning;
        }
        if (cleaning)
        {
            entry.Assets.Reset();
            std::lock_guard lock(m_mutex);
            entry.Manifest   = {};
            entry.Scene      = {};
            entry.Error      = VFS::VFSError::OK;
            entry.PhaseValue = Phase::Free;
        }
        return true;
    }

    bool CookedCpuClosureCache::Shutdown()
    {
        std::unique_lock worker_lock(m_worker_mutex, std::try_to_lock);
        if (!worker_lock.owns_lock())
        {
            return false;
        }
        std::lock_guard lock(m_mutex);
        if (!m_entries)
        {
            return true;
        }
        for (uint32_t i = 0; i < m_capacity; ++i)
        {
            if (m_entries[i].PhaseValue != Phase::Free || m_entries[i].Leases)
            {
                return false;
            }
        }
        if (!m_pool || m_pool->m_live)
        {
            return false;
        }
        for (uint32_t i = 0; i < m_capacity; ++i)
        {
            m_entries[i].~Entry();
        }
        (void) m_pool->Shutdown();
        m_pool->~CookedCpuAssetPool();
        m_route->Free(m_pool);
        m_route->Free(m_entries);
        m_entries    = nullptr;
        m_route      = nullptr;
        m_schemas    = nullptr;
        m_pool       = nullptr;
        m_capacity   = 0;
        m_max_leases = 0;
        m_cursor     = 0;
        return true;
    }
} // namespace ZEngine::Managers
