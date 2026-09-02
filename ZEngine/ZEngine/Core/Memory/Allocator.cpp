#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::Core::Memory
{
    void ArenaAllocator::Initialize(uint64_t size, size_t page_size)
    {
#ifdef _WIN32
        // Reserve the full range. Pages are committed lazily in ArenaAllocateRaw via
        // VirtualAlloc(MEM_COMMIT) — this avoids consuming pagefile quota for budget
        // regions that may never be fully used (e.g. ImportPipeline at 3.5 GB).
        m_memory = (uint8_t*) VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
        // macOS/Linux use overcommit: mmap with full permissions allocates virtual address
        // space; physical pages are only backed on first write by the OS zero page.
        // No mprotect calls are ever needed — m_committed_size = size signals ArenaAllocateRaw
        // to skip the commit block entirely on every allocation.
        m_memory = (uint8_t*) mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m_memory == MAP_FAILED)
            m_memory = nullptr;
#endif
        if (!m_memory)
            return;

        m_total_size              = size;
        m_mem_page_size           = page_size;
        m_initial_current_offset  = 0;
        m_initial_previous_offset = 0;
#ifndef _WIN32
        m_committed_size = size; // all pages accessible via OS overcommit — no mprotect needed
#endif
    }

    void ArenaAllocator::Shutdown()
    {
        // Not thread-safe: external synchronization is required if the arena is shared across threads.
        const size_t total = m_total_size; // save before zero — munmap requires the original size
        m_total_size       = 0;
        m_committed_size   = 0;
        m_current_offset   = m_initial_current_offset;
        m_previous_offset  = m_initial_previous_offset;

        if (m_is_sub_arena)
        {
            // Sub-arenas do not own the memory, so we don't free it
            m_memory = nullptr;
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
    }

    // Internal bump-pointer allocator — shared by Allocate and AllocateNoZero.
    // Preconditions (callers assert before invoking): alignment is power-of-two, size > 0.
    static void* ArenaAllocateRaw(ArenaAllocator* a, size_t size, size_t alignment)
    {
        if (!a->m_memory)
            return nullptr;

        uintptr_t current_ptr  = (uintptr_t) a->m_memory + (uintptr_t) a->m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) a->m_memory;

        if ((offset + size) > a->m_total_size)
            return nullptr;

#ifdef _WIN32
        // Windows only: commit pages on demand — macOS/Linux never enter this block because
        // Initialize sets m_committed_size = total_size (overcommit; no mprotect needed).
        if ((offset + size) > a->m_committed_size)
        {
            size_t commit_size = (offset + size + a->m_mem_page_size - 1) & ~(a->m_mem_page_size - 1);
            // Commit the whole [base, commit_size) range rather than just the delta —
            // VirtualAlloc(MEM_COMMIT) on an already-committed page is a documented no-op,
            // so this is safe. Avoids relying on commit_size - a->m_committed_size staying
            // correct across the page-align mask above.
            void*  r           = VirtualAlloc(a->m_memory, commit_size, MEM_COMMIT, PAGE_READWRITE);
            if (!r)
                return nullptr;
            a->m_committed_size = commit_size;
        }
#endif

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
                        size_t new_end = m_previous_offset + new_size;
#ifdef _WIN32
                        if (new_end > m_committed_size)
                        {
                            size_t commit_size   = (new_end + m_mem_page_size - 1) & ~(m_mem_page_size - 1);
                            void*  commit_result = VirtualAlloc(m_memory, commit_size, MEM_COMMIT, PAGE_READWRITE);
                            ZENGINE_VALIDATE_ASSERT(commit_result != nullptr, "ArenaAllocator::Resize: failed to commit new pages")
                            if (!commit_result)
                                return nullptr;
                            m_committed_size = commit_size;
                        }
#endif

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

    void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena)
    {
        ZENGINE_VALIDATE_ASSERT(out_arena != nullptr, "ArenaAllocator::CreateSubArena: out_arena must not be null")
        ZENGINE_VALIDATE_ASSERT(size > 0, "ArenaAllocator::CreateSubArena: size must be > 0")
        ZENGINE_VALIDATE_ASSERT(m_memory != nullptr, "ArenaAllocator::CreateSubArena: parent arena not initialized")

        // Page-align the sub-arena start on every platform. On Windows this keeps
        // VirtualAlloc(MEM_COMMIT) boundaries clean — a non-page-aligned m_memory would
        // round the commit address down, silently committing bytes from the preceding
        // sub-arena. macOS/Linux don't need this for correctness (no mprotect on the
        // commit path), but the wasted padding (<= one page per sub-arena) is negligible
        // against multi-GB budgets, so we keep boundaries uniform across platforms.
        uintptr_t current_ptr  = (uintptr_t) m_memory + (uintptr_t) m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, m_mem_page_size);
        offset                -= (uintptr_t) m_memory;

        ZENGINE_VALIDATE_ASSERT((offset + size) <= m_total_size, "ArenaAllocator::CreateSubArena: not enough space in parent arena")

        out_arena->m_memory                  = m_memory + offset;
        out_arena->m_is_sub_arena            = true;
        out_arena->m_initial_previous_offset = 0;
        out_arena->m_initial_current_offset  = 0;
        out_arena->m_previous_offset         = 0;
        out_arena->m_current_offset          = 0;
        out_arena->m_total_size              = size;
        out_arena->m_mem_page_size           = m_mem_page_size;

        // Windows: m_committed_size = 0 — sub-arena commits its own pages lazily via
        //   VirtualAlloc(MEM_COMMIT) in ArenaAllocateRaw. Avoids consuming pagefile quota
        //   for large budgets (ImportPipeline 3.5 GB, AssetManager 1 GB, …) that may
        //   never be fully used, which was causing UIContext commit to fail.
        // macOS/Linux: m_committed_size = size — the parent's mmap(PROT_READ|PROT_WRITE)
        //   already covers this range; no mprotect call is needed, ever.
#ifdef _WIN32
        out_arena->m_committed_size = 0;
#else
        out_arena->m_committed_size = size;
#endif

        // Advance the parent's cursor to reserve the sub-arena's address range.
        // No commit is done here — each platform handles commit in its own way above.
        m_previous_offset = offset;
        m_current_offset  = offset + size;

#ifdef _WIN32
        // Advance the parent's m_committed_size to match so that a subsequent direct
        // Allocate on the parent (e.g. Device->Arena == MainArena in Engine.cpp) does
        // not attempt to commit the entire sub-arena region in one VirtualAlloc call.
        // The sub-arena pages remain PAGE_NOACCESS; each sub-arena commits lazily.
        if (m_committed_size < m_current_offset)
            m_committed_size = m_current_offset;
#endif
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
