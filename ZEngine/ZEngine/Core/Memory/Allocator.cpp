#include <pch.h>
#include <Allocator.h>
#include <MemoryOperations.h>

namespace ZEngine::Core::Memory
{
    void ArenaAllocator::Initialize(size_t size)
    {
        m_memory          = (uint8_t*) malloc(size);
        m_total_size      = size;
        m_current_offset  = 0;
        m_previous_offset = 0;
    }

    void ArenaAllocator::Shutdown()
    {
        Clear();
        free(m_memory);
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        uintptr_t current_ptr  = (uintptr_t) m_memory + (uintptr_t) m_current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) m_memory;

        assert((offset + size) <= m_total_size);

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

        uint8_t* old_mem = reinterpret_cast<uint8_t*>(old_memory);
        if (old_mem == nullptr || old_size == 0)
        {
            return Allocate(new_size, alignment);
        }
        else if ((m_memory <= old_mem) && old_mem < (m_memory + m_total_size))
        {
            if ((m_memory + m_previous_offset) == old_mem)
            {
                m_current_offset = m_previous_offset + new_size;
                if (m_current_offset <= m_total_size)
                {
                    if (new_size > old_size)
                    {
                        void*  dst  = &m_memory[m_previous_offset + old_size];
                        size_t size = new_size - old_size;
                        Helpers::secure_memset(dst, 0, size, size);
                    }
                    return old_memory;
                }
            }
            else
            {
                auto   new_mem = Allocate(new_size, alignment);
                size_t size    = old_size < new_size ? old_size : new_size;
                Helpers::secure_memmove(new_mem, size, old_memory, size);
                return new_mem;
            }
        }

        return nullptr;
    }

    void ArenaAllocator::Clear()
    {
        m_previous_offset = m_initial_previous_offset;
        m_current_offset  = m_initial_current_offset;
    }

    void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena)
    {
        out_arena->m_memory                  = reinterpret_cast<uint8_t*>(Allocate(size));
        out_arena->m_initial_previous_offset = m_previous_offset;
        out_arena->m_initial_current_offset  = m_previous_offset;

        out_arena->m_previous_offset         = out_arena->m_initial_previous_offset;
        out_arena->m_current_offset          = out_arena->m_initial_current_offset;
        out_arena->m_total_size              = m_previous_offset + size;
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
        uintptr_t initial_start  = (uintptr_t) &arena->m_memory[arena->m_current_offset];
        uintptr_t start          = Helpers::memory_align(initial_start, (uintptr_t) alignment);
        size                    -= (size_t) (start - initial_start);

        chk_size                 = Helpers::memory_align_size_t(chk_size, alignment);

        ZENGINE_VALIDATE_ASSERT(chk_size >= sizeof(PoolFreeNode), "Chunk size is too small");
        ZENGINE_VALIDATE_ASSERT(size >= chk_size, "Backing buffer length is smaller than the chunk size");

        memory = (uint8_t*) arena->Allocate(size, alignment);

        ZENGINE_VALIDATE_ASSERT(memory, "Failed to allocate memory");

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
        if (!ptr)
        {
            return;
        }

        auto start = memory;
        auto end   = &memory[total_size];

        if (!(start <= ptr && ptr < end))
        {
            return;
        }

        PoolFreeNode* node = (PoolFreeNode*) (ptr);
        node->Next         = head;
        head               = node;
    }

    void PoolAllocator::Clear()
    {
        auto   chunk_count = total_size / chunk_size;
        size_t i           = 0;

        for (i = 0; i < chunk_count; i++)
        {
            void*         ptr  = &memory[i * chunk_size];
            PoolFreeNode* node = (PoolFreeNode*) ptr;
            // Push free node onto thte free list
            node->Next         = head;
            head               = node;
        }
    }
} // namespace ZEngine::Core::Memory