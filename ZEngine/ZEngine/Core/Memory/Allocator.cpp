#include <pch.h>
#include <Allocator.h>
#include <MemoryOperations.h>

namespace ZEngine::Core::Memory
{
    void ArenaAllocator::Initialize(size_t size)
    {
        memory          = (uint8_t*) malloc(size);
        total_size      = size;
        current_offset  = 0;
        previous_offset = 0;
    }

    void ArenaAllocator::Shutdown()
    {
        Clear();
        free(memory);
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment)
    {
        uintptr_t current_ptr  = (uintptr_t) memory + (uintptr_t) current_offset;
        uintptr_t offset       = Helpers::memory_align(current_ptr, alignment);
        offset                -= (uintptr_t) memory;

        assert((offset + size) <= total_size);

        void* ptr       = &memory[offset];
        previous_offset = offset;
        current_offset  = (offset + size);

        Helpers::secure_memset(ptr, 0, size, size);
        return ptr;
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment, const char* file, int line)
    {
        return Allocate(size, alignment);
    }

    void* ArenaAllocator::Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment)
    {
        assert(Helpers::is_power_of_two(alignment) && "Alignment should be power of 2");

        uint8_t* old_mem = reinterpret_cast<uint8_t*>(old_memory);
        if (old_mem == nullptr || old_size == 0)
        {
            return Allocate(new_size, alignment);
        }
        else if ((memory <= old_mem) && old_mem < (memory + total_size))
        {
            if ((memory + previous_offset) == old_mem)
            {
                current_offset = previous_offset + new_size;
                if (current_offset <= total_size)
                {
                    if (new_size > old_size)
                    {
                        void*  dst  = &memory[previous_offset + old_size];
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
        previous_offset = 0;
        current_offset  = 0;
    }

    void ArenaAllocator::CreateSubArena(size_t size, ArenaAllocator* out_arena)
    {
        out_arena->memory          = reinterpret_cast<uint8_t*>(Allocate(size));
        out_arena->total_size      = size;
        out_arena->previous_offset = out_arena->current_offset = previous_offset;
    }

    ArenaTemp BeginTempArena(ArenaAllocator* arena)
    {
        ArenaTemp temp      = {};
        temp.Arena          = arena;
        temp.PreviousOffset = arena->previous_offset;
        temp.CurrentOffset  = arena->current_offset;
        return temp;
    }

    void EndTempArena(ArenaTemp tmp)
    {
        auto arena             = tmp.Arena;
        arena->previous_offset = tmp.PreviousOffset;
        arena->current_offset  = tmp.CurrentOffset;
    }

    void PoolAllocator::Initialize(Arena* arena, size_t size, size_t chk_size, size_t alignment)
    {
        uintptr_t initial_start  = (uintptr_t) &arena->memory[arena->current_offset];
        uintptr_t start          = Helpers::memory_align(initial_start, (uintptr_t) alignment);
        size                    -= (size_t) (start - initial_start);

        chk_size                 = Helpers::memory_align_size_t(chk_size, alignment);

        assert(chk_size >= sizeof(PoolFreeNode) && "Chunk size is too small");
        assert(size >= chk_size && "Backing buffer length is smaller than the chunk size");

        memory = (uint8_t*) arena->Allocate(size, alignment);

        assert(memory && "Failed to allocate memory");

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