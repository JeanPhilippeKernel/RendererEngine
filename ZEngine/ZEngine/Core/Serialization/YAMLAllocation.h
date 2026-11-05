#pragma once
#include <stddef.h>

#ifdef __cplusplus
#include <ZEngine/Core/Memory/AllocationRoute.h>

namespace ZEngine::Core::Serialization
{
    // The creating thread's current route, borrowed only for a parser scope.
    // No budget for ordinary, unrouted editor parsing. Cook routes provide one.
    [[nodiscard]] Memory::IWorkingMemoryBudget* YAMLWorkingMemoryBudget() noexcept;

    // Installs a checked route for the shared libyaml implementation on this
    // thread. Nestable and allocation-free. Destroy all parser/emitter/events
    // allocated in this scope before leaving it, on the creating thread. The
    // borrowed route must outlive those allocations. Blocks remember their
    // originating route, so a nested scope cannot redirect their free/realloc.
    // Outside a scope, existing editor callers retain system-heap allocation.
    class YAMLAllocationScope
    {
    public:
        explicit YAMLAllocationScope(Memory::IAllocationRoute& route) noexcept;
        ~YAMLAllocationScope();
        YAMLAllocationScope(const YAMLAllocationScope&)            = delete;
        YAMLAllocationScope& operator=(const YAMLAllocationScope&) = delete;

    private:
        Memory::IAllocationRoute* m_previous = nullptr;
        Memory::IAllocationRoute* m_current  = nullptr;
    };
} // namespace ZEngine::Core::Serialization

extern "C"
{
#endif
    // Private bridge used only by the libyaml API translation unit. All sizes
    // passed to the route include the aligned owner/size header. No engine,
    // renderer, cook, or allocator initialization is needed to link libyaml.
    void* ZEngineYAMLAllocate(size_t bytes);
    void* ZEngineYAMLReallocate(void* pointer, size_t bytes);
    void  ZEngineYAMLFree(void* pointer);
    char* ZEngineYAMLDuplicate(const char* text);
#ifdef __cplusplus
}
#endif
