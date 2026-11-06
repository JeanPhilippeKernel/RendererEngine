#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

// Compiled into the yaml target, not zEngineLib, to keep libyaml independently
// linkable. This bridge deliberately depends only on the lean route interface
// and the standard library, never on engine initialization or logging.
namespace ZEngine::Core::Serialization
{
    namespace
    {
        thread_local Memory::IAllocationRoute* CurrentRoute = nullptr;

        struct alignas(std::max_align_t) AllocationHeader
        {
            Memory::IAllocationRoute* Owner = nullptr;
            size_t                    Bytes = 0;
        };

        AllocationHeader* Header(void* pointer)
        {
            return static_cast<AllocationHeader*>(pointer) - 1;
        }

        bool Capacity(size_t bytes, size_t& capacity)
        {
            bytes = std::max(bytes, size_t(1));
            if (bytes > std::numeric_limits<size_t>::max() - sizeof(AllocationHeader))
            {
                return false;
            }
            capacity = sizeof(AllocationHeader) + bytes;
            return true;
        }
    } // namespace

    YAMLAllocationScope::YAMLAllocationScope(Memory::IAllocationRoute& route) noexcept : m_previous(CurrentRoute), m_current(&route)
    {
        CurrentRoute = &route;
    }

    Memory::IWorkingMemoryBudget* YAMLWorkingMemoryBudget() noexcept
    {
        return CurrentRoute ? CurrentRoute->WorkingMemoryBudget() : nullptr;
    }

    YAMLAllocationScope::~YAMLAllocationScope()
    {
        assert(CurrentRoute == m_current && "YAML allocation scopes must unwind in reverse order on their creating thread.");
        CurrentRoute = m_previous;
    }

    void* AllocateYAML(size_t bytes)
    {
        size_t capacity = 0;
        if (!Capacity(bytes, capacity))
        {
            return nullptr;
        }
        void* memory = CurrentRoute ? CurrentRoute->Allocate(capacity) : std::malloc(capacity);
        if (!memory)
        {
            return nullptr;
        }
        auto* header = new (memory) AllocationHeader{.Owner = CurrentRoute, .Bytes = capacity};
        return header + 1;
    }

    void* ReallocateYAML(void* pointer, size_t bytes)
    {
        if (!pointer)
        {
            return AllocateYAML(bytes);
        }
        size_t capacity = 0;
        if (!Capacity(bytes, capacity))
        {
            return nullptr;
        }
        auto* header = Header(pointer);
        auto* owner  = header->Owner;
        void* memory = owner ? owner->Reallocate(header, header->Bytes, capacity) : std::realloc(header, capacity);
        if (!memory)
        {
            return nullptr;
        }
        header = new (memory) AllocationHeader{.Owner = owner, .Bytes = capacity};
        return header + 1;
    }

    void FreeYAML(void* pointer)
    {
        if (!pointer)
        {
            return;
        }
        auto* header = Header(pointer);
        auto* owner  = header->Owner;
        header->~AllocationHeader();
        if (owner)
        {
            owner->Free(header);
        }
        else
        {
            std::free(header);
        }
    }
} // namespace ZEngine::Core::Serialization

extern "C" void* ZEngineYAMLAllocate(size_t bytes)
{
    return ZEngine::Core::Serialization::AllocateYAML(bytes);
}

extern "C" void* ZEngineYAMLReallocate(void* pointer, size_t bytes)
{
    return ZEngine::Core::Serialization::ReallocateYAML(pointer, bytes);
}

extern "C" void ZEngineYAMLFree(void* pointer)
{
    ZEngine::Core::Serialization::FreeYAML(pointer);
}

extern "C" char* ZEngineYAMLDuplicate(const char* text)
{
    if (!text)
    {
        return nullptr;
    }
    const size_t bytes = std::strlen(text) + 1;
    auto*        copy  = static_cast<char*>(ZEngineYAMLAllocate(bytes));
    if (copy)
    {
        std::copy_n(text, bytes, copy);
    }
    return copy;
}
