#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <atomic>
#include <utility>

namespace ZEngine::Managers
{
    /// One immutable CPU version. The cache owns the initial reference; source
    /// owners retain additional references independently of GPU version pins.
    struct AssetPayloadAllocation
    {
        Core::Memory::ArenaAllocator Arena      = {};
        Core::Memory::TLSFSlab*      Slab       = nullptr;
        std::atomic<uint32_t>        References = 1;
        // Stable cache-slot counter, shared across immutable reimport versions.
        // A source owner names an asset UUID, not just yesterday's payload.
        std::atomic<uint32_t>*       OwnerPins  = nullptr;
        uint64_t                     LastUse    = 0; // Protected by the ingest lock.

        static void                  Destroy(AssetPayloadAllocation* allocation) noexcept
        {
            if (allocation && allocation->References.fetch_sub(1, std::memory_order_acq_rel) == 1)
            {
                auto* slab = allocation->Slab;
                allocation->~AssetPayloadAllocation();
                slab->Free(allocation);
            }
        }

        static void Pin(AssetPayloadAllocation* allocation) noexcept
        {
            if (allocation)
            {
                allocation->References.fetch_add(1, std::memory_order_relaxed);
                if (allocation->OwnerPins)
                {
                    allocation->OwnerPins->fetch_add(1, std::memory_order_relaxed);
                }
            }
        }

        static void Unpin(AssetPayloadAllocation* allocation) noexcept
        {
            if (allocation && allocation->OwnerPins)
            {
                allocation->OwnerPins->fetch_sub(1, std::memory_order_acq_rel);
            }
            Destroy(allocation);
        }
    };

    /// Small operation pin for an offscreen preview or upload consumer. It
    /// protects the logical UUID across reimport without a scene-sized pin set.
    class AssetPayloadPin final
    {
    public:
        AssetPayloadPin() = default;
        /// Caller resolves the allocation under the cache lock.
        explicit AssetPayloadPin(AssetPayloadAllocation* allocation) noexcept : m_allocation(allocation)
        {
            AssetPayloadAllocation::Pin(m_allocation);
        }
        ~AssetPayloadPin()
        {
            Reset();
        }
        AssetPayloadPin(const AssetPayloadPin&)            = delete;
        AssetPayloadPin& operator=(const AssetPayloadPin&) = delete;
        AssetPayloadPin(AssetPayloadPin&& other) noexcept : m_allocation(std::exchange(other.m_allocation, nullptr)) {}
        AssetPayloadPin& operator=(AssetPayloadPin&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                m_allocation = std::exchange(other.m_allocation, nullptr);
            }
            return *this;
        }
        explicit operator bool() const noexcept
        {
            return m_allocation != nullptr;
        }
        void Reset() noexcept
        {
            AssetPayloadAllocation::Unpin(std::exchange(m_allocation, nullptr));
        }

    private:
        AssetPayloadAllocation* m_allocation = nullptr;
    };

    /// Exclusive-owner, bounded CPU dependency set. Metadata belongs to the scene
    /// arena, never the process arena. Retiring a version requires no cache lock
    /// or callback into an asset service. The asset slab must outlive all leases.
    class AssetCacheLease final
    {
    public:
        static constexpr uint32_t SlotsPerType = 8192;
        static constexpr uint32_t SlotCount    = SlotsPerType * 3;

        AssetCacheLease()                      = default;
        ~AssetCacheLease()
        {
            Reset();
        }
        AssetCacheLease(const AssetCacheLease&)              = delete;
        AssetCacheLease&   operator=(const AssetCacheLease&) = delete;

        [[nodiscard]] bool Initialize(Core::Memory::ArenaAllocator* arena)
        {
            if (m_slots)
            {
                return true;
            }
            if (!arena)
            {
                return false;
            }
            m_slots = static_cast<AssetPayloadAllocation**>(arena->Allocate(sizeof(*m_slots) * SlotCount, alignof(AssetPayloadAllocation*)));
            return m_slots != nullptr;
        }

        /// Caller retains the cache lock while resolving and retaining a version.
        /// A stable slot is deduplicated in O(1); reimport exchanges its version.
        [[nodiscard]] bool Retain(uint32_t slot, AssetPayloadAllocation* allocation) noexcept
        {
            if (!m_slots || slot >= SlotCount || !allocation)
            {
                return false;
            }
            if (m_slots[slot] != allocation)
            {
                AssetPayloadAllocation::Pin(allocation);
                AssetPayloadAllocation::Unpin(m_slots[slot]);
                m_slots[slot] = allocation;
            }
            return true;
        }

        [[nodiscard]] bool Initialized() const noexcept
        {
            return m_slots != nullptr;
        }

        /// Before an adapter explicitly releases its borrowed arena in a
        /// destructor body. Ordinary member destruction uses Reset directly.
        void Release() noexcept
        {
            Reset();
            m_slots = nullptr;
        }

        void Reset() noexcept
        {
            if (m_slots)
            {
                for (uint32_t index = 0; index < SlotCount; ++index)
                {
                    AssetPayloadAllocation::Unpin(m_slots[index]);
                    m_slots[index] = nullptr;
                }
            }
        }

    private:
        AssetPayloadAllocation** m_slots = nullptr;
    };
} // namespace ZEngine::Managers
