#pragma once
#include <ZEngine/Cook/CookMemory.h>

namespace ZEngine::Cook
{
    struct SceneCookContext;

    // One caller-owned immutable context graph for all registered codec,
    // reference, migration and adapter callbacks. Contains recognizes every
    // nonnull root; MemoryRequirements reports the COMPLETE graph once, including
    // this owner, nested services, idle backing and allocation overhead, less
    // storage already covered by the allocator/registries. Both queries are pure,
    // bounded and allocation-free. Unknown ownership fails closed. No callback
    // may reach another heap/service or grow its pre-owned context during a cook.
    // Custom owners may compose external/shared storage using the same contract;
    // never infer an object's full graph from sizeof(void*) or allocator totals.
    class ICookCallbackStorage
    {
    public:
        virtual ~ICookCallbackStorage()                                                                        = default;
        [[nodiscard]] virtual bool                                Contains(const void* context) const noexcept = 0;
        // Optional native family included by MemoryRequirements unless already
        // registry-covered. Stable through the call; never just a borrowed range.
        [[nodiscard]] virtual const Core::Memory::ArenaAllocator* StorageArena() const noexcept
        {
            return nullptr;
        }
        [[nodiscard]] virtual Core::VFS::VFSResult<CookServiceMemoryRequirements> MemoryRequirements(const SceneCookContext&, const ICookAllocator&) const noexcept
        {
            return Core::VFS::VFSResult<CookServiceMemoryRequirements>::Fail(Core::VFS::VFSError::Unsupported);
        }
    };

    // Borrow one native arena family containing the complete immutable callback
    // graph. Root/sub/sibling views share backing; contexts may not own external
    // writable storage. Includes idle pages/tracker/native owner state, counting
    // registry/VFS-facade shared backing once. No allocations, copied roots or
    // hash index. Native allocator fixed objects may also reside in this family;
    // their coverage-aware state profile avoids a duplicate charge. Allocator
    // pools/per-block backing and VFS-operational domains remain independent;
    // other aliases need a custom complete owner contract. All service profiles
    // must succeed and be charged before input work.
    class CookArenaCallbackStorage final : public ICookCallbackStorage
    {
    public:
        CookArenaCallbackStorage() = default;
        explicit CookArenaCallbackStorage(const Core::Memory::ArenaAllocator& arena) noexcept : m_arena(&arena) {}
        [[nodiscard]] bool                                Contains(const void* context) const noexcept override;
        [[nodiscard]] const Core::Memory::ArenaAllocator* StorageArena() const noexcept override
        {
            return m_arena;
        }
        [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> MemoryRequirements(const SceneCookContext&, const ICookAllocator& covered) const noexcept override;

    private:
        const Core::Memory::ArenaAllocator* m_arena = nullptr;
    };

    // Checks every registered context root before querying the owner once.
    // All-null built-ins need no owner and return an empty footprint. A nonnull
    // context without declared ownership fails before input acquisition or work.
    [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> QueryCookCallbackMemory(const SceneCookContext&, const ICookAllocator& covered) noexcept;
} // namespace ZEngine::Cook
