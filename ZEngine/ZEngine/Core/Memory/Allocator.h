#pragma once
#include <ZEngine/ZEngineDef.h>
#include <stddef.h>
#include <cstdint>

namespace ZEngine::Core::Memory
{
    struct ArenaAllocator;
    struct ArenaTemp;
    struct ArenaCommitTracker;
    struct ArenaGrowthChunk;
    struct ArenaGrowth;
    class IAllocationRoute;

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

    // Allocation-free snapshot of a native root reservation's retained backing.
    // Root/sub-arena views share Identity, the actual live root Owner, and
    // complete backing totals. Moves update Owner without changing Identity. Reserved
    // address space is separate from writable pages; borrowed/routed storage
    // cannot infer its external owner's footprint and does not expose a snapshot.
    struct ArenaRetainedStorage
    {
        const void*           Identity                                      = nullptr;
        size_t                CommittedBytes                                = 0;
        size_t                TrackingBytes                                 = 0;
        size_t                ReservedAddressBytes                          = 0;
        const ArenaAllocator* Owner                                         = nullptr;
        [[nodiscard]] bool    operator==(const ArenaRetainedStorage&) const = default;
    };

    // ArenaAllocator — linear bump-pointer allocator, normally backed by a virtual memory reservation.
    // InitializeBorrowed instead views caller storage; InitializeGrowingBorrowed
    // adds routed, non-moving blocks. Only the first two modes are contiguous.
    //
    // Windows reserves with PAGE_NOACCESS; POSIX reserves with PROT_NONE. Both backends
    // promote only pages touched by an allocation. A root-owned page bitmap keeps those
    // promotions discontiguous, so a root allocation after a child arena never makes the
    // child's unused pages writable. Individual allocations cannot be freed — the entire
    // arena is reclaimed at once via Clear() or Shutdown(). This makes it suitable for
    // per-frame, per-task, or lifetime-scoped data.
    //
    // LIFETIME CONTRACT — all users must observe:
    //   Distinct sibling arenas may allocate concurrently under exclusive owners;
    //   shared root page tracking is synchronized. One arena's cursor is NOT thread-safe.
    //   Reserve child ranges before transfer; do not move/reset/destroy an arena while
    //   another thread uses it, or destroy a root before all child owners have joined.
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
        /// Bounded view over already-writable caller-owned storage. No OS commit,
        /// release or ownership transfer. The backing owner must outlive this view
        /// and every allocation/sub-arena carved from it. Failure preserves this arena.
        [[nodiscard]] bool                          InitializeBorrowed(void* memory, size_t bytes, cstring owner_name = "BorrowedArena");
        /// Stable, segmented growth from caller-owned initial storage. Further
        /// writable blocks (including headers/slack) use only the supplied route.
        /// The cap bounds retained payload capacity; marks/Clear reuse blocks
        /// without reallocating. Shutdown frees growth blocks, never initial
        /// storage. No contiguous sub-arenas may be carved from this mode.
        [[nodiscard]] bool                          InitializeGrowingBorrowed(void* memory, size_t initial_bytes, size_t max_bytes, IAllocationRoute& route, cstring owner_name = "GrowingBorrowedArena");
        void                                        Shutdown();

        void*                                       Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
        void*                                       Allocate(size_t size, size_t alignment, const char* file, int line);

        // AllocateNoZero — same as Allocate but skips the secure_memset zeroing step.
        // Use only when the caller will fully initialize the returned memory before reading it
        // (e.g. large decode buffers, staging allocations). Saves up to 0.4 ms for 16 MB
        // allocations. Do NOT use for structs whose fields rely on zero-initialization.
        void*                                       AllocateNoZero(size_t size, size_t alignment = DEFAULT_ALIGNMENT);

        void*                                       Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        /// Checked resize: invalid ranges/capacity failures return nullptr and
        /// preserve the previous allocation and arena cursors.
        [[nodiscard]] void*                         TryResize(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        void                                        Clear();

        void                                        CreateSubArena(size_t size, ArenaAllocator* out_arena, cstring owner_name = "UnnamedSubArena");
        /// Failure preserves both storage views and their cursors.
        [[nodiscard]] bool                          TryCreateSubArena(size_t size, ArenaAllocator* out_arena, cstring owner_name = "UnnamedSubArena");

        [[nodiscard]] const ArenaAllocationFailure& LastFailure() const
        {
            return m_last_failure;
        }

        // Owned, page-rounded tracker storage only; sub-arenas share their root's
        // tracker and borrowed arenas own none. Does not include arena pages.
        [[nodiscard]] size_t   AllocationTrackingBytes() const noexcept;
        // Caller keeps the complete arena family stable while reading/using the
        // snapshot. Failure preserves output and never commits/allocates pages.
        [[nodiscard]] bool     RetainedStorage(ArenaRetainedStorage* output) const noexcept;
        // Pure native-family containment check. Every page in the nonempty
        // range must already be writable. Includes sibling views, but does not
        // infer object lifetime or ownership of pointers stored in that object.
        [[nodiscard]] bool     RetainsStorage(const void* pointer, size_t bytes) const noexcept;

        uint8_t*               m_memory                  = nullptr;
        bool                   m_is_sub_arena            = false;
        bool                   m_is_borrowed             = false;
        size_t                 m_total_size              = 0;
        // Physical address-space range reserved for a contiguous arena; total
        // retained payload capacity for routed growth. Sub-arenas reserve a
        // page-rounded range while preserving m_total_size as usable capacity.
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
        friend struct ArenaGrowth;
        ArenaCommitTracker* m_commit_tracker      = nullptr;
        size_t              m_root_offset         = 0;
        bool                m_owns_commit_tracker = false;
        IAllocationRoute*   m_growth_route        = nullptr;
        ArenaGrowthChunk*   m_growth_first        = nullptr;
        ArenaGrowthChunk*   m_growth_current      = nullptr;
        size_t              m_growth_initial_size = 0;

        void                MoveFrom(ArenaAllocator& other) noexcept;
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
        PoolAllocator(const PoolAllocator&)                = delete;
        PoolAllocator&     operator=(const PoolAllocator&) = delete;

        void               Initialize(Arena* arena, size_t size, size_t chunk_size, size_t alignment = DEFAULT_ALIGNMENT);
        [[nodiscard]] bool TryInitialize(Arena* arena, size_t size, size_t chunk_size, size_t alignment = DEFAULT_ALIGNMENT);

        void*              Allocate();
        void*              Allocate(const char* file, int line);

        void               Free(void* ptr);
        void               Clear();

        uint8_t*           memory     = nullptr;
        PoolFreeNode*      head       = nullptr;
        size_t             total_size = 0;
        size_t             chunk_size = 0;
    };

    ArenaTemp BeginTempArena(ArenaAllocator* arena);
    void      EndTempArena(ArenaTemp arena);

    // Scratch owns a temporary arena lifetime. The parent arena must not be
    // allocated from until this object releases its ArenaTemp.
    class ArenaScratch
    {
    public:
        explicit ArenaScratch(ArenaAllocator* arena) : m_temp(BeginTempArena(arena)) {}
        ~ArenaScratch()
        {
            EndTempArena(m_temp);
        }
        ArenaScratch(const ArenaScratch&)              = delete;
        ArenaScratch&   operator=(const ArenaScratch&) = delete;
        ArenaAllocator* Arena() const noexcept
        {
            return m_temp.Arena;
        }

    private:
        ArenaTemp m_temp = {};
    };

    // A checkpoint makes tentative allocations in its owning arena. Unlike a
    // scratch arena, it does not suspend the arena and can retain allocations.
    class ArenaCheckpoint
    {
    public:
        explicit ArenaCheckpoint(ArenaAllocator* arena) : m_arena(arena), m_current_offset(arena->m_current_offset), m_previous_offset(arena->m_previous_offset) {}
        ~ArenaCheckpoint()
        {
            if (!m_committed)
            {
                m_arena->m_current_offset  = m_current_offset;
                m_arena->m_previous_offset = m_previous_offset;
            }
        }
        ArenaCheckpoint(const ArenaCheckpoint&)            = delete;
        ArenaCheckpoint& operator=(const ArenaCheckpoint&) = delete;
        void             Commit() noexcept
        {
            m_committed = true;
        }

    private:
        ArenaAllocator* m_arena           = nullptr;
        size_t          m_current_offset  = 0;
        size_t          m_previous_offset = 0;
        bool            m_committed       = false;
    };
} // namespace ZEngine::Core::Memory

#define ZGetScratch(arena)       ZEngine::Core::Memory::BeginTempArena(arena)
#define ZReleaseScratch(scratch) ZEngine::Core::Memory::EndTempArena(scratch)
