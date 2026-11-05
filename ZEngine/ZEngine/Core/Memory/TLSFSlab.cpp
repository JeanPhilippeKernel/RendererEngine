#include <ZEngine/Core/Memory/TLSFSlab.h>
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
        Backing = backing;
        Pool    = pool;
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
    }

    void* TLSFSlab::Alloc(size_t n)
    {
        ZENGINE_VALIDATE_ASSERT(n > 0, "TLSFSlab::Alloc: size must be > 0")

        AcquireLock();
        void* p = tlsf_malloc(Pool, n);
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
        void* p = tlsf_realloc(Pool, ptr, n);
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
        void* result = ptr ? tlsf_realloc(Pool, ptr, n) : tlsf_malloc(Pool, n);
        ReleaseLock();
        return result;
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
        tlsf_free(Pool, ptr);
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
        Backing = nullptr;
    }

    size_t TLSFSlab::Overhead() const
    {
        return Pool ? tlsf_size() : 0;
    }

} // namespace ZEngine::Core::Memory
