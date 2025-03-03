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

    void ArenaAllocator::Deallocate(void* pointer)
    {
        // no-op
    }

    void* ArenaAllocator::Resize(void* old_memory, size_t old_size, size_t new_size)
    {
        // Todo : implement resize
        return nullptr;
    }

    void ArenaAllocator::Clear()
    {
        current_offset = 0;
    }

} // namespace ZEngine::Core::Memory