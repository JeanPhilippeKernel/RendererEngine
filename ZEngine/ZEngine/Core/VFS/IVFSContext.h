#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace ZEngine::Core::VFS
{

    struct IVFSContext
    {
        virtual ~IVFSContext()                                                                                                                 = default;

        [[nodiscard]] virtual VFSResult<IVFSFile*>                      Open(const VFSPath& absolute_path, VFSOpenFlags flags)                 = 0;
        [[nodiscard]] virtual VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena)   = 0;

        [[nodiscard]] virtual VFSResult<VFSFileStat>                    Stat(const VFSPath& absolute_path)                                     = 0;

        [[nodiscard]] virtual VFSResult<bool>                           Exists(const VFSPath& absolute_path)                                   = 0;

        [[nodiscard]] virtual VFSResult<void>                           Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority) = 0;

        [[nodiscard]] virtual VFSResult<void>                           Unmount(const VFSPath& logical_root)                                   = 0;

        [[nodiscard]] virtual VFSResult<void>                           CreateDir(const VFSPath& absolute_path)                                = 0;

        [[nodiscard]] virtual VFSResult<void>                           Remove(const VFSPath& absolute_path)                                   = 0;

        [[nodiscard]] virtual VFSResult<void>                           Rename(const VFSPath& src, const VFSPath& dst)                         = 0;

        virtual void                                                    Close(IVFSFile* const file)                                            = 0;
    };

} // namespace ZEngine::Core::VFS
