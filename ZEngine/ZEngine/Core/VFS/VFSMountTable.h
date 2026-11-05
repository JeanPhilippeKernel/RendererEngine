#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/ZEngineDef.h>
#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    struct MountPoint
    {
        IVFSBackend*                   Backend      = nullptr;
        VFSPath                        LogicalRoot  = {};
        int                            Priority     = 0;
        Helpers::Ref<IVFSRootProvider> RootProvider = {};
    };

    struct ResolveResult
    {
        IVFSBackend* Backend      = nullptr;
        VFSPath      RelativePath = {};
    };

    struct VFSMountTable
    {
        static constexpr uint32_t kMaxMountPoints = 256;

        VFSMountTable();
        ~VFSMountTable();
        VFSMountTable(const VFSMountTable&)                                    = delete;
        VFSMountTable&                         operator=(const VFSMountTable&) = delete;

        void                                   Initialize(Memory::ArenaAllocator* arena, size_t initial_capacity = 16);

        [[nodiscard]] VFSResult<void>          Mount(IVFSBackend* const backend, const VFSPath& logical_root, int priority);
        [[nodiscard]] VFSResult<void>          Unmount(const VFSPath& logical_root);
        [[nodiscard]] VFSResult<ResolveResult> Resolve(const VFSPath& path) const;
        [[nodiscard]] VFSResult<void>          ResolveAll(const VFSPath& dir_path, Containers::Array<ResolveResult>& out_results) const;
        [[nodiscard]] VFSResult<void>          AcquirePublicationRoot(const VFSPath&, VFSPublicationRootLease*, IVFSWorkingMemoryBudget* = nullptr);
        [[nodiscard]] VFSResult<void>          AcquireScratchRoot(const VFSPath&, VFSScratchRootLease*, IVFSWorkingMemoryBudget* = nullptr);
        [[nodiscard]] VFSResult<void>          AcquireReadRoot(const VFSPath&, VFSReadRootLease*, IVFSWorkingMemoryBudget* = nullptr);

        void                                   Clear();

        size_t                                 Count() const;

    private:
        static bool                             IsPrefixOf(const VFSPath& prefix, const VFSPath& path);
        [[nodiscard]] static VFSResult<VFSPath> StripPrefix(const VFSPath& prefix, const VFSPath& path);

        [[nodiscard]] VFSResult<void>           AcquireRoot(const VFSPath&, VFSRootLease*, VFSRootAccess access, IVFSWorkingMemoryBudget*);
        [[nodiscard]] size_t                    FindMount(const VFSPath&) const;

        Helpers::Ref<VFSMountState>             m_state = {};
        mutable std::shared_mutex               m_mutex = {};
    };

} // namespace ZEngine::Core::VFS
