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
            this->Allocator.Initialize(config.DefaultSize);
        }

        void Shutdowm()
        {
            Allocator.Shutdown();
        }

        ArenaAllocator Allocator = {};
    };
} // namespace ZEngine::Core::Memory