#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    struct VFSDirEntry
    {
        VFSFileStat Stat        = {};
        VFSPath     Path        = {};
        bool        IsDirectory = false;
    };

    enum class VFSBackendCaps : uint32_t
    {
        None      = 0,
        Read      = BIT(0),
        Write     = BIT(1),
        List      = BIT(2),
        MemoryMap = BIT(3),
        Watch     = BIT(4),
    };

    inline VFSBackendCaps operator|(VFSBackendCaps a, VFSBackendCaps b)
    {
        return static_cast<VFSBackendCaps>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool HasCap(VFSBackendCaps caps, VFSBackendCaps test)
    {
        return (static_cast<uint32_t>(caps) & static_cast<uint32_t>(test)) != 0;
    }

    struct IVFSBackend
    {
        virtual ~IVFSBackend()                                                                                                                    = default;

        [[nodiscard]] virtual VFSResult<IVFSFile*>                            Open(const VFSPath& path, VFSOpenFlags flags)                       = 0;
        virtual void                                                          Close(IVFSFile* file)                                               = 0;
        [[nodiscard]] virtual VFSResult<VFSFileStat>                          Stat(const VFSPath& path) const                                     = 0;
        virtual bool                                                          Exists(const VFSPath& path) const                                   = 0;

        [[nodiscard]] virtual VFSResult<Core::Containers::Array<VFSDirEntry>> List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const = 0;

        [[nodiscard]] virtual VFSResult<void>                                 CreateDir(const VFSPath& path)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> Remove(const VFSPath& path)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        [[nodiscard]] virtual VFSResult<void> Rename(const VFSPath& from, const VFSPath& to)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        virtual cstring        BackendType() const  = 0;
        virtual VFSBackendCaps Capabilities() const = 0;
    };

} // namespace ZEngine::Core::VFS
