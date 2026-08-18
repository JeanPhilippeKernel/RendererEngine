#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <cstddef>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    struct IVFSBackend;

    enum class VFSOpenFlags : uint32_t
    {
        None     = 0,
        Read     = BIT(0),
        Write    = BIT(1),
        Append   = BIT(2),
        Create   = BIT(3), // create file if it does not exist (requires Write)
        Truncate = BIT(4), // truncate to zero length on open  (requires Write)
    };

    inline VFSOpenFlags operator|(VFSOpenFlags a, VFSOpenFlags b)
    {
        return static_cast<VFSOpenFlags>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool HasFlag(VFSOpenFlags flags, VFSOpenFlags test)
    {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
    }

    struct VFSFileStat
    {
        uint64_t SizeBytes   = 0;
        int64_t  MTimeNs     = 0;
        bool     IsDirectory = false;
        bool     IsReadOnly  = false;
    };

    struct IVFSFile
    {
        virtual ~IVFSFile()                                                                                      = default;
        IVFSBackend*                   Owner                                                                     = nullptr;

        virtual VFSResult<size_t>      Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset)        = 0;
        virtual VFSResult<size_t>      Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) = 0;

        virtual VFSResult<uint64_t>    Size() const                                                              = 0;
        virtual VFSResult<VFSFileStat> Stat() const                                                              = 0;
        virtual VFSResult<void>        Flush()                                                                   = 0;
        virtual const VFSPath&         Path() const                                                              = 0;
        virtual VFSResult<void>        Close()                                                                   = 0;

        // Read the whole file into out_buffer
        VFSResult<size_t>              ReadAll(Core::Containers::ArrayView<uint8_t> out_buffer);
    };

} // namespace ZEngine::Core::VFS
