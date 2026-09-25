#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <cerrno>
#endif

#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <limits>

namespace ZEngine::Core::Memory
{
    struct ArenaCommitTracker
    {
        uint8_t* CommittedPages  = nullptr;
        size_t   PageCount       = 0;
        size_t   AllocationBytes = 0;
    };

    static bool RoundUpToPage(size_t value, size_t page_size, size_t* result)
    {
        if (value > std::numeric_limits<size_t>::max() - (page_size - 1))
            return false;
        *result = (value + page_size - 1) & ~(page_size - 1);
        return true;
    }

    static void SetAllocationFailure(ArenaAllocator* arena, ArenaAllocationFailureKind kind, size_t requested_size, int platform_error = 0)
    {
        arena->m_last_failure = {
            kind,
            arena->m_owner_name,
            requested_size,
            arena->m_current_offset,
            arena->m_total_size,
            platform_error,
        };
    }

    static void ClearAllocationFailure(ArenaAllocator* arena)
    {
        arena->m_last_failure = {};
    }

    static ArenaCommitTracker* CreateCommitTracker(size_t reserved_size, size_t page_size, int* platform_error)
    {
        const size_t page_count = reserved_size / page_size;
        if (page_count > std::numeric_limits<size_t>::max() - sizeof(ArenaCommitTracker))
        {
            *platform_error = 0;
            return nullptr;
        }

        const size_t tracker_size = sizeof(ArenaCommitTracker) + page_count;
#ifdef _WIN32
        auto* tracker = static_cast<ArenaCommitTracker*>(VirtualAlloc(nullptr, tracker_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        if (!tracker)
        {
            *platform_error = static_cast<int>(GetLastError());
            return nullptr;
        }
#else
        auto* tracker = static_cast<ArenaCommitTracker*>(mmap(nullptr, tracker_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        if (tracker == MAP_FAILED)
        {
            *platform_error = errno;
            return nullptr;
        }
#endif
        tracker->CommittedPages  = reinterpret_cast<uint8_t*>(tracker) + sizeof(ArenaCommitTracker);
        tracker->PageCount       = page_count;
        tracker->AllocationBytes = tracker_size;
        return tracker;
    }

    static void DestroyCommitTracker(ArenaCommitTracker* tracker)
    {
        if (!tracker)
            return;
#ifdef _WIN32
        VirtualFree(tracker, 0, MEM_RELEASE);
#else
        munmap(tracker, tracker->AllocationBytes);
#endif
    }

    bool CommitAllocationPages(ArenaAllocator* arena, size_t allocation_offset, size_t allocation_size)
    {
        if (!arena->m_commit_tracker)
        {
            SetAllocationFailure(arena, ArenaAllocationFailureKind::ArenaNotInitialized, allocation_size);
            return false;
        }

        const size_t page_size  = arena->m_mem_page_size;
        const size_t page_start = allocation_offset & ~(page_size - 1);
        size_t       page_end   = 0;
        if (allocation_offset > std::numeric_limits<size_t>::max() - allocation_size || !RoundUpToPage(allocation_offset + allocation_size, page_size, &page_end) || page_end > arena->m_reserved_size)
        {
            SetAllocationFailure(arena, ArenaAllocationFailureKind::CapacityExceeded, allocation_size);
            return false;
        }

        const size_t first_page = (arena->m_root_offset + page_start) / page_size;
        const size_t page_count = (page_end - page_start) / page_size;
        if (first_page > arena->m_commit_tracker->PageCount || page_count > arena->m_commit_tracker->PageCount - first_page)
        {
            SetAllocationFailure(arena, ArenaAllocationFailureKind::CapacityExceeded, allocation_size);
            return false;
        }

#ifdef _WIN32
        void* result = VirtualAlloc(arena->m_memory + page_start, page_end - page_start, MEM_COMMIT, PAGE_READWRITE);
        if (!result)
        {
            SetAllocationFailure(arena, ArenaAllocationFailureKind::CommitFailed, allocation_size, static_cast<int>(GetLastError()));
            return false;
        }
#else
        if (mprotect(arena->m_memory + page_start, page_end - page_start, PROT_READ | PROT_WRITE) != 0)
        {
            SetAllocationFailure(arena, ArenaAllocationFailureKind::CommitFailed, allocation_size, errno);
            return false;
        }
#endif

        size_t newly_committed_pages = 0;
        for (size_t page = first_page; page < first_page + page_count; ++page)
        {
            if (arena->m_commit_tracker->CommittedPages[page] == 0)
            {
                arena->m_commit_tracker->CommittedPages[page] = 1;
                ++newly_committed_pages;
            }
        }
        arena->m_committed_size += newly_committed_pages * page_size;
        ClearAllocationFailure(arena);
        return true;
    }

    void ArenaAllocator::Initialize(uint64_t size, size_t page_size, cstring owner_name)
    {
        m_owner_name         = owner_name ? owner_name : "UnnamedArena";
        m_last_failure       = {};
        m_mem_page_size      = page_size ? page_size : 4096;

        size_t reserved_size = 0;
        if (size == 0 || size > std::numeric_limits<size_t>::max() || !RoundUpToPage(static_cast<size_t>(size), m_mem_page_size, &reserved_size))
        {
            SetAllocationFailure(this, ArenaAllocationFailureKind::ReservationFailed, static_cast<size_t>(size));
            return;
        }

#ifdef _WIN32
        // Reserve address space only. CommitAllocationPages promotes exact allocation
        // ranges with MEM_COMMIT as the cursor advances.
        m_memory                    = static_cast<uint8_t*>(VirtualAlloc(nullptr, reserved_size, MEM_RESERVE, PAGE_NOACCESS));
        const int reservation_error = m_memory ? 0 : static_cast<int>(GetLastError());
#else
        // Reserve address space without making the complete range writable. Do not use
        // MAP_NORESERVE: it only defers admission failure to a later, uncontrolled write.
        m_memory                    = static_cast<uint8_t*>(mmap(nullptr, reserved_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        const int reservation_error = m_memory == MAP_FAILED ? errno : 0;
        if (m_memory == MAP_FAILED)
            m_memory = nullptr;
#endif
        if (!m_memory)
        {
            SetAllocationFailure(this, ArenaAllocationFailureKind::ReservationFailed, static_cast<size_t>(size), reservation_error);
            return;
        }

        int tracker_error = 0;
        m_commit_tracker  = CreateCommitTracker(reserved_size, m_mem_page_size, &tracker_error);
        if (!m_commit_tracker)
        {
#ifdef _WIN32
            VirtualFree(m_memory, 0, MEM_RELEASE);
#else
            munmap(m_memory, reserved_size);
#endif
            m_memory = nullptr;
            SetAllocationFailure(this, ArenaAllocationFailureKind::ReservationFailed, static_cast<size_t>(size), tracker_error);
            return;
        }

        m_total_size              = static_cast<size_t>(size);
        m_reserved_size           = reserved_size;
        m_initial_current_offset  = 0;
        m_initial_previous_offset = 0;
        m_current_offset          = 0;
        m_previous_offset         = 0;
        m_committed_size          = 0;
        m_root_offset             = 0;
        m_owns_commit_tracker     = true;
    }

    void ArenaAllocator::Shutdown()
    {
        // Not thread-safe: external synchronization is required if the arena is shared across threads.
        const size_t total        = m_reserved_size; // save before zero — munmap requires the original size
        auto*        tracker      = m_commit_tracker;
        const bool   owns_tracker = m_owns_commit_tracker;
        m_total_size              = 0;
        m_reserved_size           = 0;
        m_committed_size          = 0;
        m_current_offset          = m_initial_current_offset;
        m_previous_offset         = m_initial_previous_offset;
        m_commit_tracker          = nullptr;
        m_root_offset             = 0;
        m_owns_commit_tracker     = false;

        if (m_is_sub_arena)
        {
            // Sub-arenas do not own the memory, so we don't free it
            m_memory       = nullptr;
            m_is_sub_arena = false;
            return;
        }

        if (m_memory)
        {
#ifdef _WIN32
            VirtualFree(m_memory, 0, MEM_RELEASE);
#else
            munmap(m_memory, total);
#endif
            m_memory = nullptr;
        }
        if (owns_tracker)
            DestroyCommitTracker(tracker);
    }

    // Internal bump-pointer allocator — shared by Allocate and AllocateNoZero.
    // Preconditions (callers assert before invoking): alignment is power-of-two, size > 0.
    static void* ArenaAllocateRaw(ArenaAllocator* a, size_t size, size_t alignment)
    {
        if (!a->m_memory)
        {
            SetAllocationFailure(a, ArenaAllocationFailureKind::ArenaNotInitialized, size);
            return nullptr;
        }

        uintptr_t current_ptr  = (uintptr_t) a->m_memory + (uintptr_t) a->m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) a->m_memory;

        if (offset > a->m_total_size || size > a->m_total_size - offset)
        {
            SetAllocationFailure(a, ArenaAllocationFailureKind::CapacityExceeded, size);
            return nullptr;
        }

        if (!CommitAllocationPages(a, static_cast<size_t>(offset), size))
            return nullptr;

        void* ptr            = &a->m_memory[offset];
        a->m_previous_offset = offset;
        a->m_current_offset  = offset + size;
        return ptr;
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        ZENGINE_VALIDATE_ASSERT(Helpers::is_power_of_two(alignment), "ArenaAllocator::Allocate: alignment must be a power of two")
        ZENGINE_VALIDATE_ASSERT(size > 0, "ArenaAllocator::Allocate: size must be > 0")

        void* ptr = ArenaAllocateRaw(this, size, alignment);
        if (ptr)
            Helpers::secure_memset(ptr, 0, size, size);
        return ptr;
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment, const char* file, int line)
    {
        return Allocate(size, alignment);
    }

    void* ArenaAllocator::AllocateNoZero(size_t size, size_t alignment)
    {
        ZENGINE_VALIDATE_ASSERT(Helpers::is_power_of_two(alignment), "ArenaAllocator::AllocateNoZero: alignment must be a power of two")
        ZENGINE_VALIDATE_ASSERT(size > 0, "ArenaAllocator::AllocateNoZero: size must be > 0")
        return ArenaAllocateRaw(this, size, alignment);
    }

    void* ArenaAllocator::Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment)
    {
        ZENGINE_VALIDATE_ASSERT(Helpers::is_power_of_two(alignment), "Alignment should be power of 2")
        ZENGINE_VALIDATE_ASSERT(new_size > 0, "ArenaAllocator::Resize: new_size must be > 0")

        uint8_t* old_mem = reinterpret_cast<uint8_t*>(old_memory);
        if (old_mem == nullptr || old_size == 0)
        {
            return Allocate(new_size, alignment);
        }
        else if ((m_memory <= old_mem) && old_mem < (m_memory + m_total_size))
        {
            if ((m_memory + m_previous_offset) == old_mem)
            {
                if ((m_previous_offset + new_size) <= m_total_size)
                {
                    if (new_size > old_size)
                    {
                        if (!CommitAllocationPages(this, m_previous_offset, new_size))
                            return nullptr;

                        void*  dst       = &m_memory[m_previous_offset + old_size];
                        size_t zero_size = new_size - old_size;
                        Helpers::secure_memset(dst, 0, zero_size, zero_size);
                    }
                    else
                    {
                        void*  dst       = &m_memory[m_previous_offset + new_size];
                        size_t zero_size = old_size - new_size;
                        Helpers::secure_memset(dst, 0, zero_size, zero_size);
                    }

                    m_current_offset = m_previous_offset + new_size;
                    return old_memory;
                }
                // fast path capacity exceeded — fall through to slow path
            }

            auto new_mem = Allocate(new_size, alignment);
            ZENGINE_VALIDATE_ASSERT(new_mem != nullptr, "ArenaAllocator::Resize: arena out of memory")
            size_t copy_size = old_size < new_size ? old_size : new_size;
            Helpers::secure_memcpy(new_mem, new_size, old_memory, copy_size);
            return new_mem;
        }

        ZENGINE_VALIDATE_ASSERT(false, "ArenaAllocator::Resize: pointer not owned by this arena")
        return nullptr;
    }

    void ArenaAllocator::Clear()
    {
        m_previous_offset = m_initial_previous_offset;
        m_current_offset  = m_initial_current_offset;
    }

    void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena, cstring owner_name)
    {
        ZENGINE_VALIDATE_ASSERT(out_arena != nullptr, "ArenaAllocator::CreateSubArena: out_arena must not be null")
        ZENGINE_VALIDATE_ASSERT(size > 0, "ArenaAllocator::CreateSubArena: size must be > 0")
        ZENGINE_VALIDATE_ASSERT(m_memory != nullptr, "ArenaAllocator::CreateSubArena: parent arena not initialized")

        // Give every child a page-rounded physical range. It preserves the requested
        // logical capacity while ensuring a later parent allocation cannot mprotect the
        // child's final, otherwise unused partial page.
        uintptr_t current_ptr  = (uintptr_t) m_memory + (uintptr_t) m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, m_mem_page_size);
        offset                -= (uintptr_t) m_memory;
        size_t reserved_size   = 0;
        ZENGINE_VALIDATE_ASSERT(RoundUpToPage(size, m_mem_page_size, &reserved_size), "ArenaAllocator::CreateSubArena: size cannot be page-aligned")

        if (offset > m_total_size || reserved_size > m_total_size - offset)
        {
            SetAllocationFailure(this, ArenaAllocationFailureKind::CapacityExceeded, size);
            ZENGINE_VALIDATE_ASSERT(false, "ArenaAllocator::CreateSubArena: not enough space in parent arena")
            return;
        }

        out_arena->m_memory                  = m_memory + offset;
        out_arena->m_is_sub_arena            = true;
        out_arena->m_owns_commit_tracker     = false;
        out_arena->m_commit_tracker          = m_commit_tracker;
        out_arena->m_root_offset             = m_root_offset + static_cast<size_t>(offset);
        out_arena->m_owner_name              = owner_name ? owner_name : "UnnamedSubArena";
        out_arena->m_last_failure            = {};
        out_arena->m_initial_previous_offset = 0;
        out_arena->m_initial_current_offset  = 0;
        out_arena->m_previous_offset         = 0;
        out_arena->m_current_offset          = 0;
        out_arena->m_total_size              = size;
        out_arena->m_reserved_size           = reserved_size;
        out_arena->m_mem_page_size           = m_mem_page_size;
        out_arena->m_committed_size          = 0;

        // Advance the parent's cursor to reserve the sub-arena's address range.
        // No page is committed until the owning arena allocates from its range.
        m_previous_offset                    = offset;
        m_current_offset                     = offset + reserved_size;
    }

    ArenaTemp BeginTempArena(ArenaAllocator* arena)
    {
        ArenaTemp temp      = {};
        temp.Arena          = arena;
        temp.PreviousOffset = arena->m_previous_offset;
        temp.CurrentOffset  = arena->m_current_offset;
        return temp;
    }

    void EndTempArena(ArenaTemp tmp)
    {
        auto arena               = tmp.Arena;
        arena->m_previous_offset = tmp.PreviousOffset;
        arena->m_current_offset  = tmp.CurrentOffset;
    }

    void PoolAllocator::Initialize(Arena* arena, size_t size, size_t chk_size, size_t alignment)
    {
        // Let Allocate manage alignment internally — do not pre-subtract padding.
        // Pre-subtracting caused a double-reduction: size was reduced by the padding
        // computed here, then Allocate re-aligned internally, potentially adding no
        // padding (if the arena was already aligned) while size was already shrunk.
        chk_size = Helpers::memory_align_size_t(chk_size, alignment);

        ZENGINE_VALIDATE_ASSERT(chk_size >= sizeof(PoolFreeNode), "Chunk size is too small");
        ZENGINE_VALIDATE_ASSERT(size >= chk_size, "Backing buffer length is smaller than the chunk size");

        memory = (uint8_t*) arena->Allocate(size, alignment);

        ZENGINE_VALIDATE_ASSERT(memory != nullptr, "PoolAllocator::Initialize: allocation failed");

        total_size = size;
        chunk_size = chk_size;
        head       = nullptr;

        Clear();
    }

    void* PoolAllocator::Allocate()
    {
        ZENGINE_VALIDATE_ASSERT(head != nullptr, "PoolAllocator::Allocate: pool exhausted — increase pool capacity at initialization")

        PoolFreeNode* node = head;
        head               = head->Next;
        Helpers::secure_memset(node, 0, chunk_size, chunk_size);
        return node;
    }

    void* PoolAllocator::Allocate(const char* file, int line)
    {
        return Allocate();
    }

    void PoolAllocator::Free(void* ptr)
    {
        ZENGINE_VALIDATE_ASSERT(ptr != nullptr, "PoolAllocator::Free: null pointer")

        auto p     = (uintptr_t) ptr;
        auto start = (uintptr_t) memory;
        auto end   = (uintptr_t) memory + total_size;

        ZENGINE_VALIDATE_ASSERT(p >= start && p < end, "PoolAllocator::Free: pointer not owned by this pool")
        ZENGINE_VALIDATE_ASSERT((p - start) % chunk_size == 0, "PoolAllocator::Free: pointer is not chunk-aligned — possible corruption or wrong pointer")

#ifndef NDEBUG
        // O(free-list length) double-free check — debug builds only.
        for (const PoolFreeNode* n = head; n != nullptr; n = n->Next)
            ZENGINE_VALIDATE_ASSERT(n != ptr, "PoolAllocator::Free: double-free detected")
#endif

        // Placement new: begins the PoolFreeNode object lifetime at the chunk's address
        // without allocating memory. Required by the C++ object model — a C-style cast
        // (PoolFreeNode*)ptr reinterprets bits without starting a new object lifetime,
        // which is UB for non-trivially-reachable access patterns. Since PoolFreeNode is
        // trivially constructible, this generates identical machine code to the cast.
        PoolFreeNode* node = ::new (ptr) PoolFreeNode{};
        node->Next         = head;
        head               = node;
    }

    void PoolAllocator::Clear()
    {
        auto chunk_count = total_size / chunk_size;
        head             = nullptr;

        for (size_t i = 0; i < chunk_count; i++)
        {
            void* ptr = &memory[i * chunk_size];
            Helpers::secure_memset(ptr, 0, chunk_size, chunk_size);

            // Placement new — see PoolAllocator::Free for rationale.
            PoolFreeNode* node = ::new (ptr) PoolFreeNode{};
            node->Next         = head;
            head               = node;
        }
    }
} // namespace ZEngine::Core::Memory
