#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdint>

namespace ZEngine::Core::VFS
{
    static constexpr size_t VFS_MAX_PATH       = MAX_FILE_PATH_COUNT;
    static constexpr size_t VFS_MAX_COMPONENTS = 32;

    struct VFSPathComponent
    {
        cstring Data   = nullptr;
        size_t  Length = 0;

        bool    Empty() const
        {
            return Length == 0 || Data == nullptr;
        }

        bool Equals(cstring other) const;
    };

    struct VFSPath
    {
        static VFSResult<VFSPath> Parse(cstring raw);
        static VFSResult<VFSPath> Parse(cstring raw, size_t length);

        static VFSResult<VFSPath> FromNative(cstring native_path);

        static VFSPath            Root();

        cstring                   CStr() const
        {
            return m_buffer;
        }
        size_t Length() const
        {
            return m_length;
        }
        bool IsValid() const
        {
            return m_length > 0;
        }
        bool IsRoot() const
        {
            return m_length == 1 && m_buffer[0] == '/';
        }

        VFSPathComponent Filename() const;

        VFSPathComponent Stem() const;

        VFSPathComponent Extension() const;

        VFSPath          Parent() const;

        uint32_t         ComponentCount() const
        {
            return m_component_count;
        }

        VFSPathComponent   ComponentAt(uint32_t index) const;

        VFSResult<VFSPath> Append(cstring segment) const;
        VFSResult<VFSPath> Append(const VFSPath& other) const;

        VFSPath            operator/(cstring segment) const;

        bool               operator==(const VFSPath& other) const;
        bool               operator!=(const VFSPath& other) const;
        bool               operator<(const VFSPath& other) const;
        bool               IsPrefixOf(const VFSPath& other) const;

        void               ToNative(char* out_buffer, size_t out_size) const;

        uint64_t           Hash() const
        {
            return m_hash;
        }

    private:
        char     m_buffer[VFS_MAX_PATH]                  = {};
        size_t   m_length                                = 0;

        uint16_t m_component_offsets[VFS_MAX_COMPONENTS] = {};
        uint16_t m_component_lengths[VFS_MAX_COMPONENTS] = {};
        uint32_t m_component_count                       = 0;

        uint64_t m_hash                                  = 0;

        bool     BuildComponents();
        uint64_t ComputeHash() const;
    };

    struct VFSPathHasher
    {
        uint64_t operator()(const VFSPath& path) const
        {
            return path.Hash();
        }
    };

} // namespace ZEngine::Core::VFS