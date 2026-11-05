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

    // Pure native caller-facade storage description, not an operational root or
    // pin. The complete fixed object and native arena family contain all retained
    // facade state; independently retained root/provider/mapping domains are
    // reported by their existing working-memory contracts. No external mutable
    // service graph is implied by an arena pointer. Stable until the cook ends.
    struct VFSContextStorage
    {
        const void*                   Object      = nullptr;
        size_t                        ObjectBytes = 0;
        const Memory::ArenaAllocator* Arena       = nullptr;
    };

    struct IVFSContext
    {
        virtual ~IVFSContext() = default;

        // Unknown custom facades fail closed. No allocation, root acquisition,
        // watcher changes or provider callbacks are permitted in this query.
        [[nodiscard]] virtual VFSResult<VFSContextStorage> NativeStorage() const noexcept
        {
            return VFSResult<VFSContextStorage>::Fail(VFSError::Unsupported);
        }

        [[nodiscard]] virtual VFSResult<void> AcquirePublicationRoot(const VFSPath&, VFSPublicationRootLease*, IVFSWorkingMemoryBudget* = nullptr)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> AcquireScratchRoot(const VFSPath&, VFSScratchRootLease*, IVFSWorkingMemoryBudget* = nullptr)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> AcquireReadRoot(const VFSPath&, VFSReadRootLease*, IVFSWorkingMemoryBudget* = nullptr)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        [[nodiscard]] virtual VFSResult<IVFSFile*>                      Open(const VFSPath& absolute_path, VFSOpenFlags flags)                 = 0;
        [[nodiscard]] virtual VFSResult<Containers::Array<VFSDirEntry>> List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena)   = 0;

        [[nodiscard]] virtual VFSResult<VFSFileStat>                    Stat(const VFSPath& absolute_path)                                     = 0;

        [[nodiscard]] virtual VFSResult<bool>                           Exists(const VFSPath& absolute_path)                                   = 0;

        [[nodiscard]] virtual VFSResult<void>                           Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority) = 0;

        [[nodiscard]] virtual VFSResult<void>                           Unmount(const VFSPath& logical_root)                                   = 0;

        [[nodiscard]] virtual VFSResult<void>                           CreateDir(const VFSPath& absolute_path)                                = 0;

        // Unlike recursive CreateDir, fail if the directory name already exists.
        [[nodiscard]] virtual VFSResult<void>                           CreateDirExclusive(const VFSPath&)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        [[nodiscard]] virtual VFSResult<void> Remove(const VFSPath& absolute_path)           = 0;

        // Recursively remove a directory and all its contents.
        [[nodiscard]] virtual VFSResult<void> RemoveAll(const VFSPath& absolute_path)        = 0;

        [[nodiscard]] virtual VFSResult<void> Rename(const VFSPath& src, const VFSPath& dst) = 0;

        /// Atomically rename without replacing a destination. Unsupported backends fail closed.
        [[nodiscard]] virtual VFSResult<void> RenameNoReplace(const VFSPath&, const VFSPath&)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        virtual void                          Close(IVFSFile* const file) = 0;

        // Always release the wrapper, but preserve the checked close outcome. Native
        // Close must invalidate once, including on failure; teardown cannot retry it.
        [[nodiscard]] virtual VFSResult<void> CloseChecked(IVFSFile* file)
        {
            if (!file)
                return VFSResult<void>::Ok();
            const auto result = file->Close();
            Close(file);
            return result;
        }

        // Editor importers use this scope to publish a group of generated files without
        // letting watcher callbacks index the group while it is incomplete. Default
        // no-ops preserve the lightweight VFS contexts used by tools and tests.
        [[nodiscard]] virtual VFSImportPublication BeginImportPublication()
        {
            return {};
        }
        virtual void RecordImportPublicationArtifact(VFSImportPublication, const VFSPath&) {}
        virtual void EndImportPublication(VFSImportPublication) {}

        virtual void Shutdown() = 0;
    };

} // namespace ZEngine::Core::VFS
