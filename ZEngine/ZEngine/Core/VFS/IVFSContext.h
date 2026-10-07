#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{

    struct VFSImportPublication
    {
        uint64_t           Id = 0;

        [[nodiscard]] bool IsValid() const
        {
            return Id != 0;
        }
    };

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

        // Recursively remove a directory and all its contents.
        [[nodiscard]] virtual VFSResult<void>                           RemoveAll(const VFSPath& absolute_path)                                = 0;

        [[nodiscard]] virtual VFSResult<void>                           Rename(const VFSPath& src, const VFSPath& dst)                         = 0;

        virtual void                                                    Close(IVFSFile* const file)                                            = 0;

        // Editor importers use this scope to publish a group of generated files without
        // letting watcher callbacks index the group while it is incomplete. Default
        // no-ops preserve the lightweight VFS contexts used by tools and tests.
        [[nodiscard]] virtual VFSImportPublication                      BeginImportPublication()
        {
            return {};
        }
        virtual void RecordImportPublicationArtifact(VFSImportPublication, const VFSPath&) {}
        virtual void EndImportPublication(VFSImportPublication) {}

        virtual void Shutdown() = 0;
    };

} // namespace ZEngine::Core::VFS
