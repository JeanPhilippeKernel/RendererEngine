#pragma once
#include <Allocator.h>

namespace ZEngine::Core::Memory
{
    struct MemoryConfiguration
    {
        uint64_t DefaultSize = ZGiga(2ull);
    };

    struct MemoryManager
    {
        void Initialize(const MemoryConfiguration& config)
        {
            this->ArenaAllocator.Initialize(config.DefaultSize);
        }

        void Shutdowm()
        {
            ArenaAllocator.Shutdown();
        }

        ArenaAllocator ArenaAllocator = {};
    };
} // namespace ZEngine::Core::Memory