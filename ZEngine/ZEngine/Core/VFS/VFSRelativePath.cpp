#include <ZEngine/Core/VFS/VFSRelativePath.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/UTF8.h>
#include <algorithm>

namespace ZEngine::Core::VFS
{
    namespace
    {
        size_t BoundedLength(cstring raw)
        {
            size_t length = 0;
            if (raw)
            {
                while (length < VFS_MAX_PATH && raw[length])
                {
                    ++length;
                }
            }
            return length;
        }
    } // namespace

    VFSResult<void> VFSRelativePath::Validate(cstring raw)
    {
        return Validate(raw, BoundedLength(raw));
    }

    VFSResult<void> VFSRelativePath::Validate(Containers::StringView raw)
    {
        return Validate(raw.data(), raw.size());
    }

    VFSResult<void> VFSRelativePath::Validate(cstring raw, size_t length)
    {
        const auto validated = ValidateComponents(raw, length);
        return validated.Succeeded() ? VFSResult<void>::Ok() : VFSResult<void>::Fail(validated.Error());
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Parse(cstring raw)
    {
        return Parse(raw, BoundedLength(raw));
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Parse(Containers::StringView raw)
    {
        return Parse(raw.data(), raw.size());
    }

    VFSResult<uint16_t> VFSRelativePath::ValidateComponents(cstring raw, size_t length)
    {
        if (!raw || !length || length >= VFS_MAX_PATH || !Helpers::IsValidUTF8(raw, length, false))
        {
            return VFSResult<uint16_t>::Fail(VFSError::InvalidPath);
        }
        size_t   start = 0;
        uint16_t count = 0;
        for (size_t i = 0; i <= length; ++i)
        {
            if (i != length && (raw[i] == '\\' || raw[i] == ':'))
            {
                return VFSResult<uint16_t>::Fail(VFSError::InvalidPath);
            }
            if (i == length || raw[i] == '/')
            {
                const size_t size = i - start;
                if (!size || (size == 1 && raw[start] == '.') || (size == 2 && raw[start] == '.' && raw[start + 1] == '.') || count == VFS_MAX_COMPONENTS)
                {
                    return VFSResult<uint16_t>::Fail(VFSError::InvalidPath);
                }
                ++count;
                start = i + 1;
            }
        }
        return VFSResult<uint16_t>::Ok(count);
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Parse(cstring raw, size_t length)
    {
        VFSRelativePath result = {};
        const auto      parsed = ParseInto({raw, length}, &result);
        return parsed.Succeeded() ? VFSResult<VFSRelativePath>::Ok(result) : VFSResult<VFSRelativePath>::Fail(parsed.Error());
    }

    VFSResult<void> VFSRelativePath::ParseInto(Containers::StringView raw, VFSRelativePath* output)
    {
        if (!output)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        const auto validated = ValidateComponents(raw.data(), raw.size());
        if (validated.Failed())
        {
            return VFSResult<void>::Fail(validated.Error());
        }
        // Move first: raw may be output->View() or one of its suffixes. Clear
        // only afterwards, including the terminator and any old trailing text.
        if (Helpers::secure_memmove(output->m_buffer, sizeof(output->m_buffer), raw.data(), raw.size()) != Helpers::MEMORY_OP_SUCCESS)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        std::fill(output->m_buffer + raw.size(), output->m_buffer + sizeof(output->m_buffer), '\0');
        output->m_length          = static_cast<uint16_t>(raw.size());
        output->m_component_count = validated.Value();
        return VFSResult<void>::Ok();
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Parent() const
    {
        if (m_component_count > 1)
        {
            for (size_t i = m_length; i > 0; --i)
            {
                if (m_buffer[i - 1] == '/')
                {
                    return Parse(m_buffer, i - 1);
                }
            }
        }
        return VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Join(Containers::StringView prefix, const VFSRelativePath& suffix)
    {
        if (!suffix.IsValid() || prefix.size() >= VFS_MAX_PATH || suffix.Length() >= VFS_MAX_PATH - prefix.size() - (prefix.empty() ? 0 : 1))
        {
            return VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
        }
        if (prefix.empty())
        {
            return VFSResult<VFSRelativePath>::Ok(suffix);
        }
        char         buffer[VFS_MAX_PATH] = {};
        const size_t offset               = prefix.size() + 1;
        if (Helpers::secure_memcpy(buffer, sizeof(buffer), prefix.data(), prefix.size()) != Helpers::MEMORY_OP_SUCCESS || Helpers::secure_memcpy(buffer + offset, sizeof(buffer) - offset, suffix.CStr(), suffix.Length()) != Helpers::MEMORY_OP_SUCCESS)
        {
            return VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
        }
        buffer[prefix.size()] = '/';
        return Parse(buffer, offset + suffix.Length());
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Append(Containers::StringView relative) const
    {
        const auto suffix = Parse(relative);
        if (!IsValid() || suffix.Failed())
        {
            return VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
        }
        return Append(suffix.Value());
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Append(const VFSRelativePath& relative) const
    {
        return IsValid() ? Join(View(), relative) : VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
    }

    VFSResult<VFSRelativePath> VFSRelativePath::Sibling(Containers::StringView filename) const
    {
        const auto suffix = Parse(filename);
        if (!IsValid() || suffix.Failed() || suffix.Value().ComponentCount() != 1)
        {
            return VFSResult<VFSRelativePath>::Fail(VFSError::InvalidPath);
        }
        size_t prefix_length = m_length;
        while (prefix_length && m_buffer[prefix_length - 1] != '/')
        {
            --prefix_length;
        }
        return Join({m_buffer, prefix_length ? prefix_length - 1 : 0}, suffix.Value());
    }

    bool VFSRelativePath::operator==(const VFSRelativePath& other) const
    {
        return m_length == other.m_length && Helpers::secure_memcmp(m_buffer, sizeof(m_buffer), other.m_buffer, sizeof(other.m_buffer), m_length) == 0;
    }

    bool VFSRelativePath::operator<(const VFSRelativePath& other) const
    {
        const int compared = Helpers::secure_memcmp(m_buffer, sizeof(m_buffer), other.m_buffer, sizeof(other.m_buffer), std::min(m_length, other.m_length));
        return compared < 0 || (compared == 0 && m_length < other.m_length);
    }
} // namespace ZEngine::Core::VFS
