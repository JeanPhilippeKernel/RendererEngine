#pragma once
#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <limits>
#include <new>

namespace ZEngine::Core::Memory
{
    /// Exclusive-owner route over one bounded arena-backed slab. No heap/OS
    /// fallback. Transfer ownership only after all previous readers have joined.
    /// Parent storage outlives the route and every allocation/working charge.
    class SlabAllocationRoute final : public IAllocationRoute, private IWorkingMemoryBudget
    {
    public:
        SlabAllocationRoute() = default;
        ~SlabAllocationRoute() override
        {
            ZENGINE_VALIDATE_ASSERT(!m_allocated && !m_working, "Slab route destroyed before its allocations/working charges.")
            m_slab.Shutdown();
        }
        SlabAllocationRoute(const SlabAllocationRoute&)            = delete;
        SlabAllocationRoute& operator=(const SlabAllocationRoute&) = delete;

        [[nodiscard]] bool   Initialize(ArenaAllocator* arena, size_t bytes)
        {
            if (m_slab.Pool || !m_slab.TryInit(arena, bytes))
            {
                return false;
            }
            m_capacity = bytes;
            m_overhead = m_slab.TotalOverhead();
            m_peak     = m_overhead;
            return true;
        }
        [[nodiscard]] void* Allocate(size_t bytes) noexcept override
        {
            if (!bytes || bytes > std::numeric_limits<size_t>::max() - sizeof(Header))
            {
                return nullptr;
            }
            const size_t total  = bytes + sizeof(Header);
            const size_t charge = TLSFSlab::AllocationFootprintBound(total, alignof(Header));
            if (!charge || !Fits(charge))
            {
                return nullptr;
            }
            void* allocation = m_slab.TryAllocate(total, alignof(Header));
            if (!allocation)
            {
                return nullptr;
            }
            auto* header  = new (allocation) Header{this, bytes, charge};
            m_allocated  += charge;
            UpdatePeak();
            return header + 1;
        }
        [[nodiscard]] void* Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept override
        {
            if (!pointer)
            {
                return Allocate(new_bytes);
            }
            const auto* header = static_cast<Header*>(pointer) - 1;
            if (header->Owner != this || old_bytes > header->Bytes)
            {
                return nullptr;
            }
            if (!new_bytes)
            {
                Free(pointer);
                return nullptr;
            }
            void* replacement = Allocate(new_bytes);
            if (!replacement)
            {
                return nullptr;
            }
            const size_t count = std::min(old_bytes, new_bytes);
            if (count && Helpers::secure_memcpy(replacement, new_bytes, pointer, count) != Helpers::MEMORY_OP_SUCCESS)
            {
                Free(replacement);
                return nullptr;
            }
            Free(pointer);
            return replacement;
        }
        void Free(void* pointer) noexcept override
        {
            if (!pointer)
            {
                return;
            }
            auto* header = static_cast<Header*>(pointer) - 1;
            ZENGINE_VALIDATE_ASSERT(header->Owner == this && header->Charge <= m_allocated, "Allocation returned through the wrong slab route.")
            m_allocated -= header->Charge;
            header->~Header();
            m_slab.Free(header);
        }
        [[nodiscard]] IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
        {
            return this;
        }
        [[nodiscard]] size_t Capacity() const noexcept
        {
            return m_capacity;
        }
        [[nodiscard]] size_t AllocatedBytes() const noexcept
        {
            return m_allocated;
        }
        [[nodiscard]] size_t WorkingBytes() const noexcept
        {
            return m_working;
        }
        [[nodiscard]] size_t PeakBytes() const noexcept
        {
            return m_peak;
        }

    private:
        struct alignas(std::max_align_t) Header
        {
            SlabAllocationRoute* Owner  = nullptr;
            size_t               Bytes  = 0;
            size_t               Charge = 0;
        };
        bool Fits(size_t bytes) const noexcept
        {
            return m_slab.Pool && bytes <= m_capacity - m_overhead - m_allocated - m_working;
        }
        void UpdatePeak() noexcept
        {
            m_peak = std::max(m_peak, m_overhead + m_allocated + m_working);
        }
        bool TryReserveWorkingBytes(size_t bytes) noexcept override
        {
            if (!Fits(bytes))
            {
                return false;
            }
            m_working += bytes;
            UpdatePeak();
            return true;
        }
        void ReleaseWorkingBytes(size_t bytes) noexcept override
        {
            ZENGINE_VALIDATE_ASSERT(bytes <= m_working, "Slab working reservation released twice.")
            m_working -= bytes;
        }
        TLSFSlab m_slab      = {};
        size_t   m_capacity  = 0;
        size_t   m_overhead  = 0;
        size_t   m_allocated = 0;
        size_t   m_working   = 0;
        size_t   m_peak      = 0;
    };
} // namespace ZEngine::Core::Memory
