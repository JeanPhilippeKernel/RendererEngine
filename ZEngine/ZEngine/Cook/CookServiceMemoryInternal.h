#pragma once
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/VFS/IVFSContext.h>

namespace ZEngine::Cook
{
    struct SceneCookContext;
    struct SceneCookRequest;
    struct SceneCookResult;
    struct CookDiagnostics;
} // namespace ZEngine::Cook

namespace ZEngine::Cook::Detail
{
    // Principal value/result storage for the constant-space allocator-chain
    // query, including its nested native profile helper; no custom stack bound.
    [[nodiscard]] size_t CookAllocatorQueryWorkingBytes() noexcept;

    // Five borrowed native views: schema, migration, callback, output, scratch. Common
    // backing-family and inline-object containment, without another persistent
    // registry, allocator or dynamically sized scratch. The run reuses one
    // initialized snapshot for registry/VFS/caller projections; standalone query
    // wrappers may own a temporary snapshot. Native callbacks reuse this same
    // snapshot; custom owners keep their complete memory-contract query.
    class CookNativeServiceMemory final : public ICookMemoryCoverage
    {
    public:
        // Principal local state of native projections, excluding the snapshot
        // and caller-owned returned result. Reserve before invoking a query;
        // not a compiler/library/custom-service call-stack bound.
        [[nodiscard]] static size_t                                       CallerQueryWorkingBytes() noexcept;
        [[nodiscard]] static size_t                                       ServiceQueryWorkingBytes() noexcept;
        [[nodiscard]] static size_t                                       ProviderQueryWorkingBytes() noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void>                          Initialize(const SceneCookContext&, const Core::Memory::ArenaAllocator* callbacks) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> RegistryRequirements(const SceneCookContext&) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> CallbackRequirements(const void* owner, size_t owner_bytes) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> CallbackRequirements(const SceneCookContext&, const ICookAllocator& covered) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<CookServiceMemoryRequirements> VFSRequirements() const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<uint64_t>                      CallerRequirements(const SceneCookRequest&, const SceneCookContext&, const SceneCookResult*, const CookDiagnostics*) const noexcept;
        [[nodiscard]] bool                                                Contains(const void*, size_t) const noexcept override;

    private:
        Core::Containers::FixedArray<const Core::Memory::ArenaAllocator*, 5> m_arenas  = {};
        Core::Containers::FixedArray<Core::Memory::ArenaRetainedStorage, 5>  m_backing = {};
        Core::Containers::FixedArray<Core::VFS::VFSContextStorage, 2>        m_facades = {};
        [[nodiscard]] bool                                                   ContainsBacking(const void*, size_t) const noexcept;
        [[nodiscard]] bool                                                   EarlierFamily(size_t index) const noexcept;
        [[nodiscard]] bool                                                   EarlierView(size_t index) const noexcept;
        [[nodiscard]] uint64_t                                               OwnerBytes(size_t index) const noexcept;
    };
} // namespace ZEngine::Cook::Detail
