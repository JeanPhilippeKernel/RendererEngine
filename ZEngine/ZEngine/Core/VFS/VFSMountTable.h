#pragma once
#include <Core/Containers/Array.h>
#include <Core/Memory/Allocator.h>
#include <Core/VFS/IVFSBackend.h>
#include <ZEngineDef.h>
#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    struct MountPoint
    {
        VFSPath      LogicalRoot;
        IVFSBackend* Backend  = nullptr;
        int          Priority = 0;
    };

    struct ResolveResult
    {
        IVFSBackend* Backend = nullptr;
        VFSPath      RelativePath;
    };

    struct VFSMountTable
    {
        static constexpr uint32_t              kMaxMountPoints = 256;

        void                                   Initialize(Memory::ArenaAllocator* arena, size_t initial_capacity = 16);

        [[nodiscard]] VFSResult<void>          Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority);
        [[nodiscard]] VFSResult<void>          Unmount(const VFSPath& logical_root);
        [[nodiscard]] VFSResult<ResolveResult> Resolve(const VFSPath& path) const;
        [[nodiscard]] VFSResult<void>          ResolveAll(const VFSPath& dir_path, Containers::Array<ResolveResult>& out_results) const;

        size_t                                 Count() const;

    private:
        static bool                             IsPrefixOf(const VFSPath& prefix, const VFSPath& path);
        [[nodiscard]] static VFSResult<VFSPath> StripPrefix(const VFSPath& prefix, const VFSPath& path);

        Memory::ArenaAllocator*                 m_arena = nullptr;
        Containers::Array<MountPoint>           m_mounts;
        mutable std::shared_mutex               m_mutex;
    };

} // namespace ZEngine::Core::VFS