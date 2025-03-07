#include <pch.h>
#include <Allocator.h>
#include <MemoryOperations.h>

namespace ZEngine::Core::Memory
{
    ArenaAllocator::~ArenaAllocator() {}

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

        if ((offset + size) <= total_size)
        {
            void* ptr       = &memory[offset];
            previous_offset = offset;
            current_offset  = (offset + size);

            Helpers::secure_memset(ptr, 0, size, size);
            return ptr;
        }
        return nullptr;
    }

    void* ArenaAllocator::Allocate(size_t size, size_t alignment, const char* file, int line)
    {
        return Allocate(size, alignment);
    }

    void  ArenaAllocator::Deallocate(void* pointer) {}

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

} // namespace ZEngine::Core::Memory