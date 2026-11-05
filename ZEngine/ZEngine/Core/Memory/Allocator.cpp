#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#include <cerrno>
#endif

#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <algorithm>
#include <limits>
#include <new>

namespace ZEngine::Core::Memory
{
    struct ArenaCommitTracker
    {
        uint8_t*              CommittedPages  = nullptr;
        size_t                PageCount       = 0;
        size_t                AllocationBytes = 0;
        size_t                CommittedBytes  = 0;
        const ArenaAllocator* Owner           = nullptr;
    };

    void ArenaAllocator::MoveFrom(ArenaAllocator& other) noexcept
    {
        m_memory                  = other.m_memory;
        m_is_sub_arena            = other.m_is_sub_arena;
        m_is_borrowed             = other.m_is_borrowed;
        m_total_size              = other.m_total_size;
        m_reserved_size           = other.m_reserved_size;
        m_initial_current_offset  = other.m_initial_current_offset;
        m_initial_previous_offset = other.m_initial_previous_offset;
        m_current_offset          = other.m_current_offset;
        m_previous_offset         = other.m_previous_offset;
        m_committed_size          = other.m_committed_size;
        m_mem_page_size           = other.m_mem_page_size;
        m_owner_name              = other.m_owner_name;
        m_last_failure            = other.m_last_failure;
        m_commit_tracker          = other.m_commit_tracker;
        m_root_offset             = other.m_root_offset;
        m_owns_commit_tracker     = other.m_owns_commit_tracker;
        m_growth_route            = other.m_growth_route;
        m_growth_first            = other.m_growth_first;
        m_growth_current          = other.m_growth_current;
        m_growth_initial_size     = other.m_growth_initial_size;
        if (m_owns_commit_tracker && m_commit_tracker)
            m_commit_tracker->Owner = this;

        other.m_memory              = nullptr;
        other.m_total_size          = 0;
        other.m_reserved_size       = 0;
        other.m_committed_size      = 0;
        other.m_is_sub_arena        = false;
        other.m_is_borrowed         = false;
        other.m_commit_tracker      = nullptr;
        other.m_root_offset         = 0;
        other.m_owns_commit_tracker = false;
        other.m_growth_route        = nullptr;
        other.m_growth_first        = nullptr;
        other.m_growth_current      = nullptr;
        other.m_growth_initial_size = 0;
    }

    static bool RoundUpToPage(size_t value, size_t page_size, size_t* result)
    {
        if (value > std::numeric_limits<size_t>::max() - (page_size - 1))
        {
            return false;
        }
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

    struct alignas(std::max_align_t) ArenaGrowthChunk
    {
        ArenaGrowthChunk* Next     = nullptr;
        size_t            Offset   = 0;
        size_t            Capacity = 0;
    };

    // Logical offsets span block capacities, including skipped tails. They stay
    // stable across growth and allow ordinary arena marks to rewind in O(1).
    // The cached block makes sequential allocation/last-allocation resize O(1);
    // only a rewind or an older-pointer resize walks the geometric block list.
    struct ArenaGrowth
    {
        struct Region
        {
            uint8_t*          Memory   = nullptr;
            size_t            Offset   = 0;
            size_t            Capacity = 0;
            ArenaGrowthChunk* Chunk    = nullptr;
        };

        static Region At(ArenaAllocator& arena, size_t offset)
        {
            if (offset < arena.m_growth_initial_size || !arena.m_growth_first)
                return {arena.m_memory, 0, arena.m_growth_initial_size};
            auto* chunk = arena.m_growth_current;
            if (!chunk || offset < chunk->Offset)
                chunk = arena.m_growth_first;
            while (chunk->Next && offset >= chunk->Next->Offset)
                chunk = chunk->Next;
            return {reinterpret_cast<uint8_t*>(chunk + 1), chunk->Offset, chunk->Capacity, chunk};
        }

        static void Shutdown(ArenaAllocator& arena)
        {
            auto* chunk = arena.m_growth_first;
            while (chunk)
            {
                auto* next = chunk->Next;
                chunk->~ArenaGrowthChunk();
                arena.m_growth_route->Free(chunk);
                chunk = next;
            }
            arena.m_growth_route        = nullptr;
            arena.m_growth_first        = nullptr;
            arena.m_growth_current      = nullptr;
            arena.m_growth_initial_size = 0;
        }

        static void* Allocate(ArenaAllocator& arena, size_t size, size_t alignment)
        {
            auto   region = At(arena, arena.m_current_offset);
            size_t used   = arena.m_current_offset - region.Offset;
            for (;;)
            {
                const auto base    = reinterpret_cast<uintptr_t>(region.Memory);
                const auto address = base + used;
                const auto padding = (alignment - address % alignment) % alignment;
                if (used <= region.Capacity && padding <= region.Capacity - used && size <= region.Capacity - used - padding)
                {
                    const size_t offset     = used + padding;
                    arena.m_previous_offset = region.Offset + offset;
                    arena.m_current_offset  = arena.m_previous_offset + size;
                    arena.m_growth_current  = region.Chunk;
                    ClearAllocationFailure(&arena);
                    return region.Memory + offset;
                }
                auto* next = region.Chunk ? region.Chunk->Next : arena.m_growth_first;
                if (!next)
                {
                    const size_t remaining = arena.m_total_size - arena.m_reserved_size;
                    // Route alignment covers the block header/data. Larger
                    // requested alignments need at most alignment-1 slack.
                    const size_t slack     = alignment > alignof(ArenaGrowthChunk) ? alignment - 1 : 0;
                    if (size > remaining || slack > remaining - size)
                    {
                        SetAllocationFailure(&arena, ArenaAllocationFailureKind::CapacityExceeded, size);
                        return nullptr;
                    }
                    const size_t doubled  = region.Capacity > remaining / 2 ? remaining : region.Capacity * 2;
                    const size_t capacity = std::max(size + slack, doubled);
                    if (capacity > std::numeric_limits<size_t>::max() - sizeof(ArenaGrowthChunk))
                    {
                        SetAllocationFailure(&arena, ArenaAllocationFailureKind::CapacityExceeded, size);
                        return nullptr;
                    }
                    void* memory = arena.m_growth_route->Allocate(sizeof(ArenaGrowthChunk) + capacity);
                    if (!memory)
                    {
                        SetAllocationFailure(&arena, ArenaAllocationFailureKind::CommitFailed, size);
                        return nullptr;
                    }
                    next = new (memory) ArenaGrowthChunk{.Offset = arena.m_reserved_size, .Capacity = capacity};
                    if (region.Chunk)
                        region.Chunk->Next = next;
                    else
                        arena.m_growth_first = next;
                    arena.m_reserved_size  += capacity;
                    arena.m_committed_size += capacity;
                }
                region = {reinterpret_cast<uint8_t*>(next + 1), next->Offset, next->Capacity, next};
                used   = 0;
            }
        }

        static void* Resize(ArenaAllocator& arena, void* old_memory, size_t old_size, size_t new_size, size_t alignment)
        {
            const auto address  = reinterpret_cast<uintptr_t>(old_memory);
            auto       region   = At(arena, arena.m_previous_offset);
            const auto contains = [address](const Region& candidate) {
                const auto base = reinterpret_cast<uintptr_t>(candidate.Memory);
                return address >= base && address - base < candidate.Capacity;
            };
            if (!contains(region))
            {
                region = {arena.m_memory, 0, arena.m_growth_initial_size};
                for (auto* chunk = arena.m_growth_first; !contains(region) && chunk; chunk = chunk->Next)
                    region = {reinterpret_cast<uint8_t*>(chunk + 1), chunk->Offset, chunk->Capacity, chunk};
            }
            if (!contains(region) || address % alignment != 0)
                return nullptr;
            const size_t offset = address - reinterpret_cast<uintptr_t>(region.Memory);
            if (old_size > region.Capacity - offset || region.Offset + offset > arena.m_current_offset || old_size > arena.m_current_offset - region.Offset - offset)
                return nullptr;
            if (region.Offset + offset == arena.m_previous_offset && new_size <= region.Capacity - offset)
            {
                const auto start = std::min(old_size, new_size);
                const auto bytes = std::max(old_size, new_size) - start;
                if (bytes)
                    Helpers::secure_memset(region.Memory + offset + start, 0, bytes, bytes);
                arena.m_current_offset = region.Offset + offset + new_size;
                ClearAllocationFailure(&arena);
                return old_memory;
            }
            void* memory = arena.Allocate(new_size, alignment);
            if (memory)
                Helpers::secure_memcpy(memory, new_size, old_memory, std::min(old_size, new_size));
            return memory;
        }

        static bool Enabled(const ArenaAllocator& arena)
        {
            return arena.m_growth_route != nullptr;
        }
    };

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
        tracker->CommittedBytes  = 0;
        tracker->Owner           = nullptr;
        return tracker;
    }

    static void DestroyCommitTracker(ArenaCommitTracker* tracker)
    {
        if (!tracker)
        {
            return;
        }
#ifdef _WIN32
        VirtualFree(tracker, 0, MEM_RELEASE);
#else
        munmap(tracker, tracker->AllocationBytes);
#endif
    }

    size_t ArenaAllocator::AllocationTrackingBytes() const noexcept
    {
        if (!m_owns_commit_tracker || !m_commit_tracker)
            return 0;
        size_t rounded = 0;
        return RoundUpToPage(m_commit_tracker->AllocationBytes, m_mem_page_size, &rounded) ? rounded : std::numeric_limits<size_t>::max();
    }

    bool ArenaAllocator::RetainedStorage(ArenaRetainedStorage* output) const noexcept
    {
        if (!output || !m_memory || m_is_borrowed || !m_commit_tracker || !m_commit_tracker->Owner || !m_mem_page_size)
            return false;
        size_t tracking = 0;
        if (!RoundUpToPage(m_commit_tracker->AllocationBytes, m_mem_page_size, &tracking) || m_commit_tracker->PageCount > SIZE_MAX / m_mem_page_size)
            return false;
        *output = {m_commit_tracker, m_commit_tracker->CommittedBytes, tracking, m_commit_tracker->PageCount * m_mem_page_size, m_commit_tracker->Owner};
        return true;
    }

    bool ArenaAllocator::RetainsStorage(const void* pointer, size_t bytes) const noexcept
    {
        if (!pointer || !bytes || !m_memory || m_is_borrowed || !m_commit_tracker || !m_mem_page_size || m_commit_tracker->PageCount > SIZE_MAX / m_mem_page_size)
            return false;
        const auto root    = reinterpret_cast<uintptr_t>(m_memory) - m_root_offset;
        const auto address = reinterpret_cast<uintptr_t>(pointer);
        if (address < root)
            return false;
        const auto offset   = address - root;
        const auto capacity = m_commit_tracker->PageCount * m_mem_page_size;
        if (offset >= capacity || bytes > capacity - offset)
            return false;
        const auto last = (offset + bytes - 1) / m_mem_page_size;
        for (size_t page = offset / m_mem_page_size; page <= last; ++page)
            if (!m_commit_tracker->CommittedPages[page])
                return false;
        return true;
    }

    bool CommitAllocationPages(ArenaAllocator* arena, size_t allocation_offset, size_t allocation_size)
    {
        if (arena->m_is_borrowed)
        {
            if (allocation_offset > arena->m_total_size || allocation_size > arena->m_total_size - allocation_offset)
            {
                SetAllocationFailure(arena, ArenaAllocationFailureKind::CapacityExceeded, allocation_size);
                return false;
            }
            ClearAllocationFailure(arena);
            return true;
        }
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
        const size_t bytes                       = newly_committed_pages * page_size;
        arena->m_committed_size                 += bytes;
        arena->m_commit_tracker->CommittedBytes += bytes;
        ClearAllocationFailure(arena);
        return true;
    }

    void ArenaAllocator::Initialize(uint64_t size, size_t page_size, cstring owner_name)
    {
        m_owner_name   = owner_name ? owner_name : "UnnamedArena";
        m_last_failure = {};
        // Standalone bounded owners may not have a MemoryManager to pass the
        // native page size. In particular, macOS arm64 uses 16 KiB pages.
        if (!page_size)
        {
#ifdef _WIN32
            SYSTEM_INFO system_info = {};
            GetSystemInfo(&system_info);
            page_size = system_info.dwPageSize;
#else
            const long native_page_size = sysconf(_SC_PAGESIZE);
            if (native_page_size > 0)
            {
                page_size = static_cast<size_t>(native_page_size);
            }
#endif
        }
        if (!page_size || (page_size & (page_size - 1)))
        {
            SetAllocationFailure(this, ArenaAllocationFailureKind::ReservationFailed, static_cast<size_t>(size));
            return;
        }
        m_mem_page_size      = page_size;

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
        {
            m_memory = nullptr;
        }
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
        m_commit_tracker->Owner   = this;
    }

    bool ArenaAllocator::InitializeBorrowed(void* memory, size_t bytes, cstring owner_name)
    {
        if (m_memory || !memory || !bytes || bytes > UINTPTR_MAX - reinterpret_cast<uintptr_t>(memory))
        {
            return false;
        }
        m_memory     = static_cast<uint8_t*>(memory);
        m_total_size = m_reserved_size = m_committed_size = bytes;
        m_current_offset = m_previous_offset = m_initial_current_offset = m_initial_previous_offset = 0;
        m_mem_page_size                                                                             = 1; // Already committed; sub-arena boundaries need no OS-page rounding.
        m_is_borrowed                                                                               = true;
        m_owner_name                                                                                = owner_name;
        m_last_failure                                                                              = {};
        return true;
    }

    bool ArenaAllocator::InitializeGrowingBorrowed(void* memory, size_t initial_bytes, size_t max_bytes, IAllocationRoute& route, cstring owner_name)
    {
        if (max_bytes < initial_bytes || !InitializeBorrowed(memory, initial_bytes, owner_name))
            return false;
        m_total_size          = max_bytes;
        m_growth_initial_size = initial_bytes;
        m_growth_route        = &route;
        return true;
    }

    void ArenaAllocator::Shutdown()
    {
        ArenaGrowth::Shutdown(*this);
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

        if (m_is_sub_arena || m_is_borrowed)
        {
            // Sub-arenas do not own the memory, so we don't free it
            m_memory       = nullptr;
            m_is_sub_arena = false;
            m_is_borrowed  = false;
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
        {
            DestroyCommitTracker(tracker);
        }
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
        if (ArenaGrowth::Enabled(*a))
            return ArenaGrowth::Allocate(*a, size, alignment);

        uintptr_t current_ptr  = (uintptr_t) a->m_memory + (uintptr_t) a->m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) a->m_memory;

        if (offset > a->m_total_size || size > a->m_total_size - offset)
        {
            SetAllocationFailure(a, ArenaAllocationFailureKind::CapacityExceeded, size);
            return nullptr;
        }

        if (!CommitAllocationPages(a, static_cast<size_t>(offset), size))
        {
            return nullptr;
        }

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
        {
            Helpers::secure_memset(ptr, 0, size, size);
        }
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
        return TryResize(old_memory, old_size, new_size, alignment);
    }

    void* ArenaAllocator::TryResize(void* old_memory, size_t old_size, size_t new_size, size_t alignment)
    {
        if (!Helpers::is_power_of_two(alignment) || new_size == 0)
        {
            return nullptr;
        }
        if (!old_memory || old_size == 0)
        {
            return Allocate(new_size, alignment);
        }
        if (m_growth_route)
            return ArenaGrowth::Resize(*this, old_memory, old_size, new_size, alignment);
        const uintptr_t base    = reinterpret_cast<uintptr_t>(m_memory);
        const uintptr_t address = reinterpret_cast<uintptr_t>(old_memory);
        if (!m_memory || address < base || address - base > m_current_offset || old_size > m_current_offset - (address - base) || address % alignment != 0)
        {
            return nullptr;
        }
        const size_t offset = address - base;
        if (offset == m_previous_offset)
        {
            if (new_size <= m_total_size - offset)
            {
                if (new_size > old_size)
                {
                    if (!CommitAllocationPages(this, offset, new_size))
                    {
                        return nullptr;
                    }
                    const size_t zero_size = new_size - old_size;
                    Helpers::secure_memset(m_memory + offset + old_size, 0, zero_size, zero_size);
                }
                else
                {
                    const size_t zero_size = old_size - new_size;
                    Helpers::secure_memset(m_memory + offset + new_size, 0, zero_size, zero_size);
                }
                m_current_offset = offset + new_size;
                return old_memory;
            }
        }
        void* new_memory = Allocate(new_size, alignment);
        if (!new_memory)
        {
            return nullptr;
        }
        Helpers::secure_memcpy(new_memory, new_size, old_memory, std::min(old_size, new_size));
        return new_memory;
    }

    void ArenaAllocator::Clear()
    {
        m_previous_offset = m_initial_previous_offset;
        m_current_offset  = m_initial_current_offset;
    }

    void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena, cstring owner_name)
    {
        if (m_growth_route)
        {
            SetAllocationFailure(this, ArenaAllocationFailureKind::CapacityExceeded, size);
            return; // A segmented owner cannot expose a contiguous child range.
        }
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
        out_arena->m_is_borrowed             = m_is_borrowed;
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
        out_arena->m_committed_size          = m_is_borrowed ? size : 0;

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
        const bool initialized = TryInitialize(arena, size, chk_size, alignment);
        ZENGINE_VALIDATE_ASSERT(initialized, "PoolAllocator::Initialize: invalid layout or allocation failure");
    }

    bool PoolAllocator::TryInitialize(Arena* arena, size_t size, size_t chk_size, size_t alignment)
    {
        if (!arena || !alignment || (alignment & (alignment - 1)))
            return false;
        // Every free chunk contains a pointer, even when the caller's objects
        // themselves only require byte alignment.
        alignment = std::max(alignment, alignof(PoolFreeNode));
        if (chk_size > SIZE_MAX - (alignment - 1))
            return false;
        chk_size = Helpers::memory_align_size_t(chk_size, alignment);
        if (chk_size < sizeof(PoolFreeNode) || size < chk_size)
            return false;
        auto* storage = static_cast<uint8_t*>(arena->Allocate(size, alignment));
        if (!storage)
            return false;
        memory     = storage;
        total_size = size;
        chunk_size = chk_size;
        head       = nullptr;
        Clear();
        return true;
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
