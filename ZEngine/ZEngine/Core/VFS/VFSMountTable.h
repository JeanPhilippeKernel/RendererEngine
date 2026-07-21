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
        IVFSBackend* Backend     = nullptr;
        VFSPath      LogicalRoot = {};
        int          Priority    = 0;
    };

    struct ResolveResult
    {
        IVFSBackend* Backend      = nullptr;
        VFSPath      RelativePath = {};
    };

    struct VFSMountTable
    {
        static constexpr uint32_t              kMaxMountPoints = 256;

        void                                   Initialize(Memory::ArenaAllocator* arena, size_t initial_capacity = 16);

        [[nodiscard]] VFSResult<void>          Mount(IVFSBackend* const backend, const VFSPath& logical_root, int priority);
        [[nodiscard]] VFSResult<void>          Unmount(const VFSPath& logical_root);
        [[nodiscard]] VFSResult<ResolveResult> Resolve(const VFSPath& path) const;
        [[nodiscard]] VFSResult<void>          ResolveAll(const VFSPath& dir_path, Containers::Array<ResolveResult>& out_results) const;

        size_t                                 Count() const;

    private:
        static bool                             IsPrefixOf(const VFSPath& prefix, const VFSPath& path);
        [[nodiscard]] static VFSResult<VFSPath> StripPrefix(const VFSPath& prefix, const VFSPath& path);

        Memory::ArenaAllocator*                 m_arena  = nullptr;
        Containers::Array<MountPoint>           m_mounts = {};
        mutable std::shared_mutex               m_mutex;
    };

} // namespace ZEngine::Core::VFS