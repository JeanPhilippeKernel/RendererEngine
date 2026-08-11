#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif

#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>

namespace ZEngine::Core::Memory
{
    void ArenaAllocator::Initialize(uint64_t size, unsigned long page_size)
    {
#ifdef _WIN32
        m_memory = (uint8_t*) VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
        m_memory = (uint8_t*) mmap(nullptr, size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (m_memory == MAP_FAILED)
        {
            m_memory = nullptr;
        }
#endif
        if (!m_memory)
        {
            // Failed to allocate memory
            return;
        }
        m_total_size              = size;
        m_mem_page_size           = page_size;
        m_initial_current_offset  = 0;
        m_initial_previous_offset = 0;
    }

    void ArenaAllocator::Shutdown()
    {
        // Not thread-safe: external synchronization is required if the arena is shared across threads.
        m_total_size      = 0;
        m_committed_size  = 0;
        m_current_offset  = m_initial_current_offset;
        m_previous_offset = m_initial_previous_offset;

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
            munmap(m_memory, m_total_size);
#endif
            m_memory = nullptr;
        }
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        if (!m_memory)
        {
            // Memory not initialized or failed to allocate memory or already shutdown
            return nullptr;
        }

        uintptr_t current_ptr  = (uintptr_t) m_memory + (uintptr_t) m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) m_memory;

        if ((offset + size) > m_total_size)
        {
            // Out of memory
            return nullptr;
        }

        if ((offset + size) > m_committed_size)
        {
            // this use the general formula to round up to the nearest multiple of m_mem_page_size
            // formula: (x + y - 1) & ~(y - 1)
            size_t commit_size = (offset + size + m_mem_page_size - 1) & ~(m_mem_page_size - 1);
#ifdef _WIN32
            void* commit_result = VirtualAlloc(m_memory + m_committed_size, commit_size - m_committed_size, MEM_COMMIT, PAGE_READWRITE);
            if (!commit_result)
#else
            int commit_result = mprotect(m_memory + m_committed_size, commit_size - m_committed_size, PROT_READ | PROT_WRITE);
            if (commit_result != 0)
#endif
            {
                // Failed to commit memory
                return nullptr;
            }
            m_committed_size = commit_size;
        }

        void* ptr         = &m_memory[offset];
        m_previous_offset = offset;
        m_current_offset  = (offset + size);

        Helpers::secure_memset(ptr, 0, size, size);
        return ptr;
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment, const char* file, int line)
    {
        return Allocate(size, alignment);
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
                        if (new_end > m_committed_size)
                        {
                            size_t commit_size = (new_end + m_mem_page_size - 1) & ~(m_mem_page_size - 1);
#ifdef _WIN32
                            void* commit_result = VirtualAlloc(m_memory + m_committed_size, commit_size - m_committed_size, MEM_COMMIT, PAGE_READWRITE);
                            if (!commit_result)
#else
                            int commit_result = mprotect(m_memory + m_committed_size, commit_size - m_committed_size, PROT_READ | PROT_WRITE);
                            if (commit_result != 0)
#endif
                            {
                                ZENGINE_VALIDATE_ASSERT(false, "ArenaAllocator::Resize: failed to commit new pages")
                                return nullptr;
                            }
                            m_committed_size = commit_size;
                        }

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
        ZENGINE_VALIDATE_ASSERT((m_current_offset + size) <= m_total_size, "ArenaAllocator::CreateSubArena: not enough space in parent arena")

        out_arena->m_memory = reinterpret_cast<uint8_t*>(Allocate(size));
        ZENGINE_VALIDATE_ASSERT(out_arena->m_memory != nullptr, "ArenaAllocator::CreateSubArena: failed to allocate sub-arena memory")
        out_arena->m_is_sub_arena            = true;
        out_arena->m_initial_previous_offset = 0;
        out_arena->m_initial_current_offset  = 0;

        out_arena->m_previous_offset         = 0;
        out_arena->m_current_offset          = 0;
        out_arena->m_total_size              = size;
        out_arena->m_committed_size          = size; // memory already committed by parent arena
        out_arena->m_mem_page_size           = m_mem_page_size;
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
        PoolFreeNode* node = head;

        if (node == nullptr)
        {
            return nullptr;
        }

        head = head->Next;
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

        PoolFreeNode* node = (PoolFreeNode*) ptr;
        node->Next         = head;
        head               = node;
    }

    void PoolAllocator::Clear()
    {
        auto chunk_count = total_size / chunk_size;

        for (size_t i = 0; i < chunk_count; i++)
        {
            void* ptr = &memory[i * chunk_size];
            Helpers::secure_memset(ptr, 0, chunk_size, chunk_size);

            PoolFreeNode* node = (PoolFreeNode*) ptr;
            node->Next         = head;
            head               = node;
        }
    }
} // namespace ZEngine::Core::Memory
