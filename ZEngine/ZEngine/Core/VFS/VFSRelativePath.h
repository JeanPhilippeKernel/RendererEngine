#pragma once
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/VFSPath.h>

namespace ZEngine::Core::VFS
{
    // Strict spelling within an already-acquired root, not a global VFS path.
    // No allocation, implicit VFSPath conversion, normalization, or root sentinel.
    class VFSRelativePath
    {
    public:
        // Same strict spelling rules as Parse, without constructing/copying the
        // owned path. Bounded C-string scan and length-delimited UTF-8 views;
        // validation never normalizes or follows a path.
        [[nodiscard]] static VFSResult<void>            Validate(cstring raw);
        [[nodiscard]] static VFSResult<void>            Validate(cstring raw, size_t length);
        [[nodiscard]] static VFSResult<void>            Validate(Containers::StringView raw);

        [[nodiscard]] static VFSResult<VFSRelativePath> Parse(cstring raw);
        [[nodiscard]] static VFSResult<VFSRelativePath> Parse(cstring raw, size_t length);
        [[nodiscard]] static VFSResult<VFSRelativePath> Parse(Containers::StringView raw);

        // Validate before modifying output, then write directly into its owned
        // storage. Supports views into output itself; no second path or heap.
        // Failure (including null output) leaves the destination unchanged.
        [[nodiscard]] static VFSResult<void>            ParseInto(Containers::StringView raw, VFSRelativePath* output);

        [[nodiscard]] bool                              IsValid() const
        {
            return m_length != 0;
        }
        [[nodiscard]] cstring CStr() const
        {
            return m_buffer;
        }
        [[nodiscard]] size_t Length() const
        {
            return m_length;
        }
        [[nodiscard]] uint32_t ComponentCount() const
        {
            return m_component_count;
        }
        [[nodiscard]] Containers::StringView View() const
        {
            return {m_buffer, m_length};
        }

        [[nodiscard]] VFSResult<VFSRelativePath> Parent() const;
        [[nodiscard]] VFSResult<VFSRelativePath> Append(Containers::StringView relative) const;
        [[nodiscard]] VFSResult<VFSRelativePath> Append(const VFSRelativePath& relative) const;
        [[nodiscard]] VFSResult<VFSRelativePath> Sibling(Containers::StringView filename) const;

        [[nodiscard]] bool                       operator==(const VFSRelativePath& other) const;
        [[nodiscard]] bool                       operator!=(const VFSRelativePath& other) const
        {
            return !(*this == other);
        }
        [[nodiscard]] bool operator<(const VFSRelativePath& other) const;

    private:
        [[nodiscard]] static VFSResult<uint16_t>        ValidateComponents(cstring raw, size_t length);
        [[nodiscard]] static VFSResult<VFSRelativePath> Join(Containers::StringView prefix, const VFSRelativePath& suffix);

        char                                            m_buffer[VFS_MAX_PATH] = {};
        uint16_t                                        m_length               = 0;
        uint16_t                                        m_component_count      = 0;
    };
} // namespace ZEngine::Core::VFS
