#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/ZEngineDef.h>
#include <cstddef>

namespace ZEngine::Core::VFS
{

    VFSResult<VFSPath> VFSPath::Parse(cstring raw)
    {
        if (!raw)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        return Parse(raw, Helpers::secure_strlen(raw));
    }

    VFSResult<VFSPath> VFSPath::Parse(cstring raw, size_t length)
    {
        if (!raw || length == 0)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        if (length >= VFS_MAX_PATH)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        char tmp[VFS_MAX_PATH] = {};
        Helpers::secure_memcpy(tmp, sizeof(tmp), raw, length);
        tmp[length] = '\0';

        for (size_t i = 0; i < length; ++i)
        {
            if (tmp[i] == '\\')
            {
                tmp[i] = '/';
            }
        }

        // Strip a leading  drive prefix
        cstring p = tmp;
        if (((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) && p[1] == ':')
        {
            p += 2;
        }

        cstring  comp_ptr[VFS_MAX_COMPONENTS] = {};
        uint16_t comp_len[VFS_MAX_COMPONENTS] = {};
        size_t   comp_count                   = 0;

        cstring  cur                          = p;
        while (*cur != '\0')
        {
            while (*cur == '/')
            {
                ++cur;
            }
            if (*cur == '\0')
            {
                break;
            }

            cstring tok = cur;
            while (*cur != '\0' && *cur != '/')
            {
                ++cur;
            }
            const size_t tok_len = static_cast<size_t>(cur - tok);

            if (tok_len == 1 && tok[0] == '.')
            {
                continue; // "."  -> current dir, skip
            }
            if (tok_len == 2 && tok[0] == '.' && tok[1] == '.')
            {
                if (comp_count == 0)
                {
                    return VFSResult<VFSPath>::Fail(VFSError::InvalidPath); // ".." above root
                }
                --comp_count;
                continue;
            }
            if (comp_count >= VFS_MAX_COMPONENTS)
            {
                return VFSResult<VFSPath>::Fail(VFSError::InvalidPath); // too many components
            }
            comp_ptr[comp_count] = tok;
            comp_len[comp_count] = static_cast<uint16_t>(tok_len);
            ++comp_count;
        }

        // Rebuild the normalized path
        VFSPath result;
        size_t  out = 0;
        if (comp_count == 0)
        {
            result.m_buffer[out++] = '/';
        }
        else
        {
            for (size_t c = 0; c < comp_count; ++c)
            {
                if (out + 1 + comp_len[c] >= VFS_MAX_PATH)
                {
                    return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
                }
                result.m_buffer[out++] = '/';
                Helpers::secure_memcpy(result.m_buffer + out, VFS_MAX_PATH - out, comp_ptr[c], comp_len[c]);
                out += comp_len[c];
            }
        }
        result.m_buffer[out] = '\0';
        result.m_length      = out;

        if (!result.BuildComponents())
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        return VFSResult<VFSPath>::Ok(result);
    }

    bool VFSPath::BuildComponents()
    {
        uint16_t* len_out = m_component_lengths;
        uint16_t* off_out = m_component_offsets;
        size_t    count   = 0;

        size_t    start   = 1;
        while (start < m_length)
        {
            size_t end = start;
            while (end < m_length && m_buffer[end] != '/')
            {
                ++end;
            }

            *len_out++ = static_cast<uint16_t>(end - start);
            *off_out++ = static_cast<uint16_t>(start);

            start      = end + 1;

            if (++count >= VFS_MAX_COMPONENTS)
            {
                return false;
            }
        }

        m_component_count = count;
        m_hash            = ComputeHash();
        return true;
    }

    uint64_t VFSPath::ComputeHash() const
    {
        constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
        constexpr uint64_t FNV_PRIME        = 1099511628211ULL;

        uint64_t           hash             = FNV_OFFSET_BASIS;
        for (size_t i = 0; i < m_length; ++i)
        {
            hash ^= static_cast<uint8_t>(m_buffer[i]);
            hash *= FNV_PRIME;
        }
        return hash;
    }

    VFSResult<VFSPath> VFSPath::FromNative(cstring native_path)
    {
        return Parse(native_path);
    }

    VFSPath VFSPath::Root()
    {
        VFSPath result;
        result.m_buffer[0] = '/';
        result.m_buffer[1] = '\0';
        result.m_length    = 1;
        result.BuildComponents();
        return result;
    }

    VFSPathComponent VFSPath::Filename() const
    {
        VFSPathComponent comp;
        if (m_component_count == 0)
        {
            return comp;
        }
        const uint32_t last = m_component_count - 1;
        comp.Data           = m_buffer + m_component_offsets[last];
        comp.Length         = m_component_lengths[last];
        return comp;
    }

    void VFSPath::CopyFilename(char* out_buffer, size_t out_size) const
    {
        VFSPathComponent fn = Filename();
        size_t           n  = (fn.Length < out_size - 1) ? fn.Length : (out_size - 1);
        if (fn.Data && n > 0)
        {
            Helpers::secure_memcpy(out_buffer, out_size, fn.Data, n);
        }
        out_buffer[n] = '\0';
    }

    VFSPathComponent VFSPath::Stem() const
    {
        VFSPathComponent file = Filename();

        for (size_t i = file.Length; i-- > 0;)
        {
            if (file.Data[i] == '.')
            {
                file.Length = i;
                break;
            }
        }
        return file;
    }

    VFSPathComponent VFSPath::Extension() const
    {
        const VFSPathComponent file = Filename();

        for (size_t i = file.Length; i-- > 0;)
        {
            if (file.Data[i] == '.')
            {
                return {file.Data + i, file.Length - i};
            }
        }
        return {};
    }

    VFSPath VFSPath::Parent() const
    {
        if (m_component_count <= 1)
        {
            return Root();
        }

        const uint16_t last_offset = m_component_offsets[m_component_count - 1];
        const size_t   parent_len  = last_offset - 1;

        VFSPath        result;
        Helpers::secure_memcpy(result.m_buffer, VFS_MAX_PATH, m_buffer, parent_len);
        result.m_buffer[parent_len] = '\0';
        result.m_length             = parent_len;
        result.BuildComponents();
        return result;
    }

    VFSPathComponent VFSPath::ComponentAt(uint32_t index) const
    {
        VFSPathComponent comp;
        if (index >= m_component_count)
        {
            return comp;
        }
        comp.Length = m_component_lengths[index];
        comp.Data   = m_buffer + m_component_offsets[index];
        return comp;
    }

    VFSResult<VFSPath> VFSPath::Append(cstring segment) const
    {
        if (!segment)
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }

        char   combined[VFS_MAX_PATH * 2] = {};
        size_t n                          = 0;

        Helpers::secure_memcpy(combined, sizeof(combined), m_buffer, m_length);
        n                    = m_length;
        combined[n++]        = '/';

        const size_t seg_len = Helpers::secure_strlen(segment);
        if (n + seg_len >= sizeof(combined))
        {
            return VFSResult<VFSPath>::Fail(VFSError::InvalidPath);
        }
        Helpers::secure_memcpy(combined + n, sizeof(combined) - n, segment, seg_len);
        n += seg_len;

        return Parse(combined, n);
    }

    VFSResult<VFSPath> VFSPath::Append(const VFSPath& other) const
    {
        return Append(other.m_buffer);
    }

    VFSPath VFSPath::operator/(cstring segment) const
    {
        VFSResult<VFSPath> result = Append(segment);
        ZENGINE_VALIDATE_ASSERT(result.Succeeded(), "operator/ : invalid path segment")
        return result.Value();
    }

    bool VFSPath::operator==(const VFSPath& other) const
    {
        return m_length == other.m_length && Helpers::secure_memcmp(m_buffer, VFS_MAX_PATH, other.m_buffer, VFS_MAX_PATH, m_length) == 0;
    }

    bool VFSPath::operator!=(const VFSPath& other) const
    {
        return !(*this == other);
    }

    bool VFSPath::operator<(const VFSPath& other) const
    {
        return Helpers::secure_strcmp(m_buffer, other.m_buffer) < 0;
    }

    bool VFSPath::IsPrefixOf(const VFSPath& other) const
    {
        if (m_length > other.m_length)
        {
            return false;
        }
        if (Helpers::secure_memcmp(m_buffer, VFS_MAX_PATH, other.m_buffer, VFS_MAX_PATH, m_length) != 0)
        {
            return false;
        }
        if (m_length == other.m_length)
        {
            return true; // identical paths
        }
        return IsRoot() || other.m_buffer[m_length] == '/';
    }

    void VFSPath::ToNative(char* out_buffer, size_t out_size) const
    {
        if (!out_buffer || out_size == 0)
        {
            return;
        }
        const size_t n = (m_length < out_size - 1) ? m_length : out_size - 1;
        for (size_t i = 0; i < n; ++i)
        {
            const char c  = m_buffer[i];
            out_buffer[i] = (c == '/') ? PLATFORM_OS_BACKSLASH : c;
        }
        out_buffer[n] = '\0';
    }

    void VFSPath::ResolveNative(cstring native_root, char* out_buffer, size_t out_size) const
    {
        if (!out_buffer || out_size == 0)
        {
            return;
        }

        size_t root_len = native_root ? Helpers::secure_strlen(native_root) : 0;
        if (root_len >= out_size)
        {
            root_len = out_size - 1;
        }
        Helpers::secure_memcpy(out_buffer, out_size, native_root, root_len);
        out_buffer[root_len] = '\0';

        if (!IsRoot())
        {
            char rel[VFS_MAX_PATH];
            ToNative(rel, sizeof(rel)); // logical '/foo' -> native separators
            Helpers::secure_strcpy(out_buffer + root_len, out_size - root_len, rel);
        }
    }

    bool VFSPathComponent::Equals(cstring other) const
    {
        if (!other)
        {
            return false;
        }
        const size_t other_len = Helpers::secure_strlen(other);
        if (other_len != Length)
        {
            return false;
        }
        return Length == 0 || Helpers::secure_memcmp(Data, Length, other, other_len, Length) == 0;
    }

} // namespace ZEngine::Core::VFS