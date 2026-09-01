#include <ZEngine/Core/Memory/TLSFSlab.h>
#include <ZEngine/ZEngineDef.h>
#include <tlsf.h>

namespace ZEngine::Core::Memory
{
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
            return Alloc(n);

        AcquireLock();
        void* p = tlsf_realloc(Pool, ptr, n);
        ReleaseLock();

        ZENGINE_VALIDATE_ASSERT(p != nullptr, "TLSFSlab::Realloc: slab exhausted — increase slab capacity at Init")
        return p;
    }

    void TLSFSlab::Free(void* ptr)
    {
        if (!ptr)
            return;
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
