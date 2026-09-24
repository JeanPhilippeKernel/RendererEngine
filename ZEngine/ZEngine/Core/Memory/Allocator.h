#pragma once
#include <ZEngine/ZEngineDef.h>
#include <stddef.h>
#include <cstdint>

namespace ZEngine::Core::Memory
{
    struct ArenaAllocator;
    struct ArenaTemp;
    struct ArenaCommitTracker;

    enum class ArenaAllocationFailureKind : uint8_t
    {
        None,
        ArenaNotInitialized,
        CapacityExceeded,
        ReservationFailed,
        CommitFailed,
    };

    // Allocation APIs return nullptr on capacity or platform-commit failure. The
    // caller can use this record to include the bounded owner and OS error in its
    // own recovery path instead of collapsing every failure into a generic OOM.
    struct ArenaAllocationFailure
    {
        ArenaAllocationFailureKind Kind          = ArenaAllocationFailureKind::None;
        cstring                    OwnerName     = nullptr;
        size_t                     RequestedSize = 0;
        size_t                     CurrentUsage  = 0;
        size_t                     Capacity      = 0;
        int                        PlatformError = 0;
    };

    struct ArenaTemp
    {
        ArenaAllocator* Arena          = nullptr;
        size_t          CurrentOffset  = 0;
        size_t          PreviousOffset = 0;
    };

    // ArenaAllocator — linear bump-pointer allocator backed by a virtual memory reservation.
    //
    // Windows reserves with PAGE_NOACCESS; POSIX reserves with PROT_NONE. Both backends
    // promote only pages touched by an allocation. A root-owned page bitmap keeps those
    // promotions discontiguous, so a root allocation after a child arena never makes the
    // child's unused pages writable. Individual allocations cannot be freed — the entire
    // arena is reclaimed at once via Clear() or Shutdown(). This makes it suitable for
    // per-frame, per-task, or lifetime-scoped data.
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
        friend bool CommitAllocationPages(ArenaAllocator* arena, size_t allocation_offset, size_t allocation_size);

        ArenaAllocator() = default;
        ~ArenaAllocator()
        {
            Shutdown();
        }

        // Copying would shallow-copy m_memory — both instances would then call
        // VirtualFree/munmap on the same pointer at destruction (double-free).
        ArenaAllocator(const ArenaAllocator&)            = delete;
        ArenaAllocator& operator=(const ArenaAllocator&) = delete;

        ArenaAllocator(ArenaAllocator&& other) noexcept
        {
            MoveFrom(other);
        }

        ArenaAllocator& operator=(ArenaAllocator&& other) noexcept
        {
            if (this != &other)
            {
                Shutdown();
                MoveFrom(other);
            }
            return *this;
        }

        void                                        Initialize(uint64_t size, size_t page_size, cstring owner_name = "UnnamedArena");
        void                                        Shutdown();

        void*                                       Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
        void*                                       Allocate(size_t size, size_t alignment, const char* file, int line);

        // AllocateNoZero — same as Allocate but skips the secure_memset zeroing step.
        // Use only when the caller will fully initialize the returned memory before reading it
        // (e.g. large decode buffers, staging allocations). Saves up to 0.4 ms for 16 MB
        // allocations. Do NOT use for structs whose fields rely on zero-initialization.
        void*                                       AllocateNoZero(size_t size, size_t alignment = DEFAULT_ALIGNMENT);

        void*                                       Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        void                                        Clear();

        void                                        CreateSubArena(size_t size, ArenaAllocator* out_arena, cstring owner_name = "UnnamedSubArena");

        [[nodiscard]] const ArenaAllocationFailure& LastFailure() const
        {
            return m_last_failure;
        }

        uint8_t*               m_memory                  = nullptr;
        bool                   m_is_sub_arena            = false;
        size_t                 m_total_size              = 0;
        // Physical address-space range reserved for this arena. Sub-arenas reserve a
        // page-rounded range while preserving m_total_size as their usable capacity.
        size_t                 m_reserved_size           = 0;
        size_t                 m_initial_current_offset  = 0;
        size_t                 m_initial_previous_offset = 0;
        size_t                 m_current_offset          = 0;
        size_t                 m_previous_offset         = 0;
        // Bytes in pages promoted writable for this arena only. This is intentionally
        // not a contiguous-prefix cursor: parent and child commitments may interleave.
        size_t                 m_committed_size          = 0;
        // size_t, not unsigned long: unsigned long is 32-bit on Windows LLP64, which
        // truncates the page-align mask past 4 GB (see ArenaAllocateRaw / Resize).
        size_t                 m_mem_page_size           = 0;
        cstring                m_owner_name              = "UnnamedArena";
        ArenaAllocationFailure m_last_failure            = {};

    private:
        ArenaCommitTracker* m_commit_tracker      = nullptr;
        size_t              m_root_offset         = 0;
        bool                m_owns_commit_tracker = false;

        void                MoveFrom(ArenaAllocator& other) noexcept
        {
            m_memory                    = other.m_memory;
            m_is_sub_arena              = other.m_is_sub_arena;
            m_total_size                = other.m_total_size;
            m_reserved_size             = other.m_reserved_size;
            m_initial_current_offset    = other.m_initial_current_offset;
            m_initial_previous_offset   = other.m_initial_previous_offset;
            m_current_offset            = other.m_current_offset;
            m_previous_offset           = other.m_previous_offset;
            m_committed_size            = other.m_committed_size;
            m_mem_page_size             = other.m_mem_page_size;
            m_owner_name                = other.m_owner_name;
            m_last_failure              = other.m_last_failure;
            m_commit_tracker            = other.m_commit_tracker;
            m_root_offset               = other.m_root_offset;
            m_owns_commit_tracker       = other.m_owns_commit_tracker;

            other.m_memory              = nullptr;
            other.m_total_size          = 0;
            other.m_reserved_size       = 0;
            other.m_committed_size      = 0;
            other.m_is_sub_arena        = false;
            other.m_commit_tracker      = nullptr;
            other.m_root_offset         = 0;
            other.m_owns_commit_tracker = false;
        }
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
        using Arena     = ArenaAllocator;

        PoolAllocator() = default;
        ~PoolAllocator() {};

        // PoolAllocator does not own its backing memory (the parent arena does), so a
        // shallow copy is not immediately unsafe — but it silently duplicates the free
        // list, letting two independent PoolAllocator instances hand out the same chunk.
        // Deleted until there's a real use case for copying a pool.
        PoolAllocator(const PoolAllocator&)            = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;

        void           Initialize(Arena* arena, size_t size, size_t chunk_size, size_t alignment = DEFAULT_ALIGNMENT);

        void*          Allocate();
        void*          Allocate(const char* file, int line);

        void           Free(void* ptr);
        void           Clear();

        uint8_t*       memory     = nullptr;
        PoolFreeNode*  head       = nullptr;
        size_t         total_size = 0;
        size_t         chunk_size = 0;
    };

    ArenaTemp BeginTempArena(ArenaAllocator* arena);
    void      EndTempArena(ArenaTemp arena);
} // namespace ZEngine::Core::Memory

#define ZGetScratch(arena)       ZEngine::Core::Memory::BeginTempArena(arena)
#define ZReleaseScratch(scratch) ZEngine::Core::Memory::EndTempArena(scratch)
