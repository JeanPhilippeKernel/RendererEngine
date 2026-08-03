#pragma once
#include <ZEngine/ZEngineDef.h>
#include <stddef.h>
#include <cstdint>

namespace ZEngine::Core::Memory
{
    struct ArenaAllocator;
    struct ArenaTemp;

    struct ArenaTemp
    {
        ArenaAllocator* Arena          = nullptr;
        size_t          CurrentOffset  = 0;
        size_t          PreviousOffset = 0;
    };

    // ArenaAllocator — linear bump-pointer allocator backed by a virtual memory reservation.
    //
    // Memory is reserved upfront via mmap/VirtualAlloc (PROT_NONE / PAGE_NOACCESS) and
    // committed on demand in page-aligned increments as allocations are made. Individual
    // allocations cannot be freed — the entire arena is reclaimed at once via Clear() or
    // Shutdown(). This makes it suitable for per-frame, per-task, or lifetime-scoped data.
    //
    // LIFETIME CONTRACT — all users must observe:
    //   1. Any PoolAllocator carved from this arena via PoolAllocator::Initialize() must
    //      not be accessed after this arena's Clear() or Shutdown() is called. Clear()
    //      rewinds the bump pointer but does NOT notify carved pools — the pool's memory
    //      pointer becomes aliased to future allocations without warning.
    //   2. Sub-arenas created via CreateSubArena() must be Shutdown() before their parent.
    //      Sub-arenas do not own their backing memory; the parent's Shutdown() unmaps it.
    //   3. This arena must outlive all objects allocated from it. Pointers into the arena
    //      become dangling after Shutdown() — there is no destructor notification.
    //   4. Clear() does not call destructors on objects allocated from the arena. Callers
    //      are responsible for manually destroying any non-trivial objects before calling
    //      Clear().
    //   5. Scratch scopes (ZGetScratch / ZReleaseScratch) are not re-entrant on the same
    //      arena. Do not open a scratch scope on an arena that is already inside an outer
    //      scratch scope — the inner release will rewind the arena beneath the outer's
    //      allocations, corrupting them.
    struct ArenaAllocator
    {
        ~ArenaAllocator()
        {
            Shutdown();
        };

        void          Initialize(uint64_t size, unsigned long page_size);
        void          Shutdown();

        void*         Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
        void*         Allocate(size_t size, size_t alignment, const char* file, int line);

        void*         Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        void          Clear();

        void          CreateSubArena(size_t size, ArenaAllocator* out_arena);

        uint8_t*      m_memory                  = nullptr;
        bool          m_is_sub_arena            = false;
        size_t        m_total_size              = 0;
        size_t        m_initial_current_offset  = 0;
        size_t        m_initial_previous_offset = 0;
        size_t        m_current_offset          = 0;
        size_t        m_previous_offset         = 0;
        size_t        m_committed_size          = 0;
        unsigned long m_mem_page_size           = 0;

    }; // struct ArenaAllocator

    struct PoolFreeNode
    {
        PoolFreeNode* Next = nullptr;
    };

    // PoolAllocator — fixed-size chunk allocator with O(1) alloc and free.
    //
    // Backing memory is carved from an ArenaAllocator at Initialize() time. The pool does
    // NOT own its memory — it manages a free list over a region owned by the parent arena.
    //
    // LIFETIME CONTRACT — all users must observe:
    //   1. The parent ArenaAllocator must outlive this pool. After the arena's Shutdown()
    //      or Clear(), pool.memory is a dangling pointer. Any subsequent Allocate(), Free(),
    //      or Clear() call on this pool is undefined behavior.
    //   2. Do not call the parent arena's Clear() while this pool has live allocations.
    //      Clear() rewinds the arena's bump pointer without notifying the pool — the pool's
    //      free list and live chunks will be silently aliased by future arena allocations.
    //   3. Declaration order in structs matters: if an ArenaAllocator and a PoolAllocator
    //      carved from it are members of the same struct, the arena must be declared AFTER
    //      the pool so it is destroyed first (C++ destroys members in reverse declaration
    //      order). If the arena is destroyed first, pool.memory becomes dangling before the
    //      pool's own destructor runs.
    //   4. Clear() zeros all chunk memory before rebuilding the free list. Pointers to
    //      previously allocated chunks become invalid after Clear() — do not retain them
    //      across a Clear() call.
    //   5. Free() asserts that the pointer is owned by this pool and is chunk-aligned.
    //      Passing a pointer from a different pool or an interior pointer will halt the
    //      program in both debug and release builds.
    struct PoolAllocator
    {
        using Arena = ArenaAllocator;

        ~PoolAllocator() {};

        void          Initialize(Arena* arena, size_t size, size_t chunk_size, size_t alignment = DEFAULT_ALIGNMENT);

        void*         Allocate();
        void*         Allocate(const char* file, int line);

        void          Free(void* ptr);
        void          Clear();

        uint8_t*      memory     = nullptr;
        PoolFreeNode* head       = nullptr;
        size_t        total_size = 0;
        size_t        chunk_size = 0;
    };

    ArenaTemp BeginTempArena(ArenaAllocator* arena);
    void      EndTempArena(ArenaTemp arena);
} // namespace ZEngine::Core::Memory

#define ZGetScratch(arena)       ZEngine::Core::Memory::BeginTempArena(arena)
#define ZReleaseScratch(scratch) ZEngine::Core::Memory::EndTempArena(scratch)
