#pragma once
#include <ZEngineDef.h>
#include <stddef.h>
#include <cstdint>

namespace ZEngine::Core::Memory
{
    struct ArenaAllocator;
    struct ArenaTemp;

    struct ArenaTemp
    {
        ArenaAllocator* Arena          = nullptr;
        size_t          CurrentOffset  = 0;
        size_t          PreviousOffset = 0;
    };

    struct ArenaAllocator
    {
        ~ArenaAllocator() {};

        void     Initialize(size_t size);
        void     Shutdown();

        void*    Allocate(size_t size, size_t alignment = DEFAULT_ALIGNMENT);
        void*    Allocate(size_t size, size_t alignment, const char* file, int line);

        void*    Reallocate(void* old_memory, size_t old_size, size_t new_size, size_t alignment = DEFAULT_ALIGNMENT);
        void     Clear();

        uint8_t* m_memory          = nullptr;
        size_t   m_total_size      = 0;
        size_t   m_current_offset  = 0;
        size_t   m_previous_offset = 0;
    }; // struct ArenaAllocator

    struct PoolFreeNode
    {
        PoolFreeNode* Next = nullptr;
    };

    struct PoolAllocator
    {
        using Arena = ArenaAllocator;

        ~PoolAllocator() {};

        void          Initialize(Arena* arena, size_t size, size_t chunk_size, size_t alignment = DEFAULT_ALIGNMENT);

        void*         Allocate();
        void*         Allocate(const char* file, int line);

        void          Free(void* ptr);
        void          Clear();

        uint8_t*      memory     = nullptr;
        PoolFreeNode* head       = nullptr;
        size_t        total_size = 0;
        size_t        chunk_size = 0;
    };

    ArenaTemp BeginTempArena(ArenaAllocator* arena);
    void      EndTempArena(ArenaTemp arena);
} // namespace ZEngine::Core::Memory

#define ZGetScratch(arena)       ZEngine::Core::Memory::BeginTempArena(arena)
#define ZReleaseScratch(scratch) ZEngine::Core::Memory::EndTempArena(scratch)
