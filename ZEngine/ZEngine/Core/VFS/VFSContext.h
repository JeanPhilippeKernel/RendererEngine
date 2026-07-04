#pragma once
#include <IVFSContext.h>
#include <VFSMountTable.h>

namespace ZEngine::Core::VFS
{
    struct VFSContext : IVFSContext
    {
        void                                                    Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity = 16);

        [[nodiscard]] VFSResult<IVFSFile*>                      Open(const VFSPath& absolute_path, VFSOpenFlags flags) override;

        [[nodiscard]] VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena) override;

        [[nodiscard]] VFSResult<VFSFileStat>                    Stat(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<bool>                           Exists(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority) override;

        [[nodiscard]] VFSResult<void>                           Unmount(const VFSPath& logical_root) override;

        [[nodiscard]] VFSResult<void>                           CreateDir(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Remove(const VFSPath& absolute_path) override;

        [[nodiscard]] VFSResult<void>                           Rename(const VFSPath& src, const VFSPath& dst) override;

    private:
        [[nodiscard]] VFSResult<ResolveResult> ResolveWritable(const VFSPath& path) const;

        VFSMountTable                          m_mount_table;
        Memory::ArenaAllocator*                m_arena = nullptr;
    };

} // namespace ZEngine::Core::VFS
