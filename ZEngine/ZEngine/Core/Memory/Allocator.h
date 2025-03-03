#pragma once
#include <stddef.h>
#include <cstdint>

#ifndef DEFAULT_ALIGNMENT
#define DEFAULT_ALIGNMENT (2 * sizeof(void*))
#endif // !DEFAULT_ALIGNMENT

namespace ZEngine::Core::Memory
{
    struct Allocator
    {
        virtual ~Allocator() {}
        virtual void* Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT)         = 0;
        virtual void* Allocate(size_t size, size_t alignment, const char* file, int line) = 0;
        virtual void  Deallocate(void* pointer)                                           = 0;
    }; // struct Allocator

    struct ArenaAllocator : public Allocator
    {
        virtual ~ArenaAllocator();

        void     Initialize(size_t size);
        void     Shutdown();

        void*    Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT) override;
        void*    Allocate(size_t size, size_t alignment, const char* file, int line) override;
        void     Deallocate(void* pointer) override;

        void*    Resize(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        void     Clear();

        uint8_t* memory          = nullptr;
        size_t   total_size      = 0;
        size_t   current_offset  = 0;
        size_t   previous_offset = 0;
    }; // struct ArenaAllocator
} // namespace ZEngine::Core::Memory
