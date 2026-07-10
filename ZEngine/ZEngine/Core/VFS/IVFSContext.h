#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace ZEngine::Core::VFS
{
    struct VFSDirEntry
    {
        VFSPath     Path        = {};
        VFSFileStat Stat        = {};
        bool        IsDirectory = false;
    };

    struct IVFSContext
    {
        virtual ~IVFSContext()                                                                                                      = default;

        virtual VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags)                       = 0;
        virtual void                                            Close(IVFSFile* file)                                               = 0;
        virtual VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const                                     = 0;
        virtual bool                                            Exists(const VFSPath& path) const                                   = 0;
        virtual VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const = 0;
    };

    struct VFSDiskContext final : public IVFSContext
    {
        explicit VFSDiskContext(cstring native_root);
        ~VFSDiskContext() override;

        VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags) override;
        void                                            Close(IVFSFile* file) override;
        VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const override;
        bool                                            Exists(const VFSPath& path) const override;
        VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const override;

    private:
        bool   ToNativePath(const VFSPath& vfs_path, char* out_buffer, size_t out_size) const;

        char   m_native_root[VFS_MAX_PATH] = {};
        size_t m_native_root_len           = 0;
    };

} // namespace ZEngine::Core::VFS
