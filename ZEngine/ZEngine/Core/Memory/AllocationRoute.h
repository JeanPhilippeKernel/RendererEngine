#pragma once
#include <ZEngine/Core/Memory/WorkingMemoryBudget.h>
#include <cstddef>

namespace ZEngine::Core::Memory
{
    // Borrowed, fallible allocation route for shared CPU codecs/parsers. No
    // callback may throw or fall back to an unaccounted allocator on refusal.
    // Successful allocations are aligned for std::max_align_t. Reallocate
    // preserves the old allocation on failure; Free accepts null.
    class IAllocationRoute
    {
    public:
        virtual ~IAllocationRoute()                                                                                        = default;
        [[nodiscard]] virtual void*                 Allocate(size_t bytes) noexcept                                        = 0;
        [[nodiscard]] virtual void*                 Reallocate(void* pointer, size_t old_bytes, size_t new_bytes) noexcept = 0;
        virtual void                                Free(void* pointer) noexcept                                           = 0;
        // Ordinary editor routes may be budgetless. A bounded cook route must
        // provide its real fixed-storage budget, never a permissive fallback.
        [[nodiscard]] virtual IWorkingMemoryBudget* WorkingMemoryBudget() noexcept
        {
            return nullptr;
        }
    };
} // namespace ZEngine::Core::Memory
