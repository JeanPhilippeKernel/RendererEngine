#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <tlsf.h>
#include <algorithm>

namespace ZEngine::Core::Memory
{
    bool TLSFSlab::TryInit(ArenaAllocator* arena, size_t bytes)
    {
        const size_t overhead = tlsf_size() + tlsf_pool_overhead();
        if (Pool || !arena || bytes < overhead || bytes - overhead < tlsf_block_size_min() || bytes - overhead >= tlsf_block_size_max())
        {
            return false;
        }
        const auto temporary = BeginTempArena(arena);
        void*      backing   = arena->AllocateNoZero(bytes);
        if (!backing)
        {
            return false;
        }
        auto pool = tlsf_create_with_pool(backing, bytes);
        if (!pool)
        {
            EndTempArena(temporary);
            return false;
        }
        Backing      = backing;
        Pool         = pool;
        m_statistics = {.BackingBytes = bytes, .ControlBytes = overhead};
        return true;
    }

    void TLSFSlab::Init(ArenaAllocator* arena, size_t bytes)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "TLSFSlab::Init: arena must not be null")
        ZENGINE_VALIDATE_ASSERT(bytes > tlsf_size(), "TLSFSlab::Init: bytes too small for TLSF metadata")

        Backing = arena->AllocateNoZero(bytes);
        ZENGINE_VALIDATE_ASSERT(Backing != nullptr, "TLSFSlab::Init: arena out of memory")

        Pool = tlsf_create_with_pool(Backing, bytes);
        ZENGINE_VALIDATE_ASSERT(Pool != nullptr, "TLSFSlab::Init: tlsf_create_with_pool failed")
        m_statistics = {.BackingBytes = bytes, .ControlBytes = tlsf_size() + tlsf_pool_overhead()};
    }

    void* TLSFSlab::Alloc(size_t n)
    {
        ZENGINE_VALIDATE_ASSERT(n > 0, "TLSFSlab::Alloc: size must be > 0")

        AcquireLock();
        void* p = tlsf_malloc(Pool, n);
        if (p)
        {
            RecordAllocationChange(0, tlsf_block_size(p) + tlsf_alloc_overhead());
        }
        ReleaseLock();

        ZENGINE_VALIDATE_ASSERT(p != nullptr, "TLSFSlab::Alloc: slab exhausted — increase slab capacity at Init")
        return p;
    }

    void* TLSFSlab::Realloc(void* ptr, size_t n)
    {
        ZENGINE_VALIDATE_ASSERT(n > 0, "TLSFSlab::Realloc: size must be > 0")

        if (ptr == nullptr)
        {
            return Alloc(n);
        }

        AcquireLock();
        const auto old_bytes = tlsf_block_size(ptr) + tlsf_alloc_overhead();
        void*      p         = tlsf_realloc(Pool, ptr, n);
        if (p)
        {
            RecordAllocationChange(old_bytes, tlsf_block_size(p) + tlsf_alloc_overhead());
        }
        ReleaseLock();

        ZENGINE_VALIDATE_ASSERT(p != nullptr, "TLSFSlab::Realloc: slab exhausted — increase slab capacity at Init")
        return p;
    }

    void* TLSFSlab::TryRealloc(void* ptr, size_t n)
    {
        // Leave headroom for TLSF alignment/metadata arithmetic as well as its
        // maximum representable block size. Do not pass overflow-sized requests.
        if (!Pool || n == 0 || n >= tlsf_block_size_max())
        {
            return nullptr;
        }
        AcquireLock();
        const auto old_bytes = ptr ? tlsf_block_size(ptr) + tlsf_alloc_overhead() : 0;
        void*      result    = ptr ? tlsf_realloc(Pool, ptr, n) : tlsf_malloc(Pool, n);
        if (result)
        {
            RecordAllocationChange(old_bytes, tlsf_block_size(result) + tlsf_alloc_overhead());
        }
        ReleaseLock();
        return result;
    }

    void* TLSFSlab::TryReallocAligned(void* ptr, size_t old_size, size_t new_size, size_t alignment)
    {
        if (!Pool || !AllocationFootprintBound(new_size, alignment) || (!ptr && old_size))
        {
            return nullptr;
        }
        AcquireLock();
        const bool   had_block     = ptr != nullptr;
        const size_t previous_size = had_block ? tlsf_block_size(ptr) : 0;
        if (old_size > previous_size)
        {
            ReleaseLock();
            return nullptr;
        }
        if (alignment <= tlsf_align_size())
        {
            void* result = had_block ? tlsf_realloc(Pool, ptr, new_size) : tlsf_malloc(Pool, new_size);
            if (result)
            {
                RecordAllocationChange(had_block ? previous_size + tlsf_alloc_overhead() : 0, tlsf_block_size(result) + tlsf_alloc_overhead());
            }
            ReleaseLock();
            return result;
        }
        if (ptr && reinterpret_cast<uintptr_t>(ptr) % alignment == 0 && new_size <= previous_size)
        {
            ReleaseLock();
            return ptr;
        }
        void* replacement = tlsf_memalign(Pool, alignment, new_size);
        if (!replacement)
        {
            ReleaseLock();
            return nullptr;
        }
        if (ptr && old_size && Helpers::secure_memcpy(replacement, new_size, ptr, std::min(old_size, new_size)) != Helpers::MEMORY_OP_SUCCESS)
        {
            tlsf_free(Pool, replacement);
            ReleaseLock();
            return nullptr;
        }
        const size_t replacement_size = tlsf_block_size(replacement) + tlsf_alloc_overhead();
        if (ptr)
        {
            tlsf_free(Pool, ptr);
        }
        RecordAllocationChange(had_block ? previous_size + tlsf_alloc_overhead() : 0, replacement_size);
        ReleaseLock();
        return replacement;
    }

    size_t TLSFSlab::AllocationFootprintBound(size_t n, size_t alignment) noexcept
    {
        // tlsf_memalign needs room for an aligned split plus its leading block.
        // Reject requests that could overflow its internal round-up arithmetic.
        const size_t maximum  = tlsf_block_size_max();
        const size_t overhead = tlsf_block_size_min() + 2 * tlsf_alloc_overhead();
        if (!n || !alignment || (alignment & (alignment - 1)) || alignment >= (maximum - overhead) / 2 || n >= maximum - 2 * alignment - overhead)
        {
            return 0;
        }
        const size_t base_alignment = tlsf_align_size();
        const size_t payload        = std::max((n + base_alignment - 1) & ~(base_alignment - 1), tlsf_block_size_min());
        // block_can_split compares against sizeof(block_header_t), exposed by
        // the pinned API as block_size_min + alloc_overhead. A smaller tail
        // stays in the allocated block; include it and the used-block header.
        return payload + tlsf_block_size_min() + 2 * tlsf_alloc_overhead();
    }

    void* TLSFSlab::TryAllocate(size_t n, size_t alignment)
    {
        if (!Pool || !AllocationFootprintBound(n, alignment))
        {
            return nullptr;
        }
        AcquireLock();
        void* result = tlsf_memalign(Pool, alignment, n);
        if (result)
        {
            RecordAllocationChange(0, tlsf_block_size(result) + tlsf_alloc_overhead());
        }
        ReleaseLock();
        return result;
    }

    void TLSFSlab::Free(void* ptr)
    {
        if (!ptr)
        {
            return;
        }
        AcquireLock();
        const auto bytes = tlsf_block_size(ptr) + tlsf_alloc_overhead();
        tlsf_free(Pool, ptr);
        RecordAllocationChange(bytes, 0);
        ReleaseLock();
    }

    void TLSFSlab::Shutdown()
    {
        if (Pool)
        {
            tlsf_destroy(Pool);
            Pool = nullptr;
        }
        // Backing is owned by the parent arena — do not free it here.
        Backing      = nullptr;
        m_statistics = {};
    }

    TLSFSlab::Statistics TLSFSlab::GetStatistics() const
    {
        AcquireLock();
        const auto result = m_statistics;
        ReleaseLock();
        return result;
    }

    void TLSFSlab::RecordAllocationChange(size_t old_bytes, size_t new_bytes)
    {
        ZENGINE_VALIDATE_ASSERT(old_bytes <= m_statistics.LiveBytes, "Invalid TLSF live allocation accounting")
        m_statistics.LiveBytes -= old_bytes;
        m_statistics.LiveBytes += new_bytes;
        if (old_bytes)
        {
            --m_statistics.LiveBlocks;
        }
        if (new_bytes)
        {
            ++m_statistics.LiveBlocks;
        }
        m_statistics.HighWaterLiveBytes = std::max(m_statistics.HighWaterLiveBytes, m_statistics.LiveBytes);
    }

    size_t TLSFSlab::Overhead() const
    {
        return Pool ? tlsf_size() : 0;
    }

    size_t TLSFSlab::TotalOverhead() const
    {
        return Pool ? tlsf_size() + tlsf_pool_overhead() : 0;
    }

} // namespace ZEngine::Core::Memory
