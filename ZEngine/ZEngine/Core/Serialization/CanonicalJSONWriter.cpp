#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>
#include <ZEngine/Helpers/UTF8.h>
#include <algorithm>
#include <charconv>
#include <cmath>

namespace ZEngine::Core::Serialization
{
    namespace
    {
        bool ValidJSONString(Containers::StringView text)
        {
            // Unlike paths/C strings, length-delimited JSON strings may contain
            // U+0000. Validate each intervening UTF-8 segment without weakening
            // the existing shared path/text validator's embedded-NUL policy.
            size_t start = 0;
            for (size_t i = 0; i < text.size(); ++i)
            {
                if (text[i])
                    continue;
                if (!Helpers::IsValidUTF8(text.data() + start, i - start))
                    return false;
                start = i + 1;
            }
            return text.empty() || Helpers::IsValidUTF8(text.data() + start, text.size() - start);
        }
    } // namespace

    CanonicalJSONWriter::CanonicalJSONWriter(JSONWriteFn write, void* context) noexcept : m_write(write), m_context(context), m_error(write ? VFS::VFSError::OK : VFS::VFSError::InvalidPath) {}

    bool CanonicalJSONWriter::Reject(VFS::VFSError error)
    {
        if (m_error == VFS::VFSError::OK)
            m_error = error;
        return false;
    }

    bool CanonicalJSONWriter::CanEmit()
    {
        return m_error == VFS::VFSError::OK && (!m_writing || Reject(VFS::VFSError::InvalidPath));
    }

    bool CanonicalJSONWriter::Raw(Containers::StringView text)
    {
        if (!CanEmit())
            return false;
        if (text.size() && !text.data())
            return Reject(VFS::VFSError::InvalidPath);
        if (!text.size())
            return true;
        m_writing          = true;
        const auto written = m_write(m_context, {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
        m_writing          = false;
        return written.Failed() ? Reject(written.Error()) : m_error == VFS::VFSError::OK;
    }

    bool CanonicalJSONWriter::String(Containers::StringView text)
    {
        if (!CanEmit())
            return false;
        if (text.size() && !text.data())
            return Reject(VFS::VFSError::InvalidPath);
        if (!ValidJSONString(text))
            return Reject(VFS::VFSError::Corrupted);
        if (!Raw("\""))
            return false;
        size_t start = 0;
        for (size_t i = 0; i < text.size(); ++i)
        {
            const auto byte = static_cast<uint8_t>(text[i]);
            if (byte >= 0x20 && byte != '"' && byte != '\\')
                continue;
            if (!Raw({text.data() + start, i - start}))
                return false;
            m_scratch[0] = '\\';
            if (byte < 0x20)
            {
                constexpr cstring hex = "0123456789abcdef";
                m_scratch[1]          = 'u';
                m_scratch[2] = m_scratch[3] = '0';
                m_scratch[4]                = hex[byte >> 4];
                m_scratch[5]                = hex[byte & 15];
                if (!Raw({m_scratch.data(), 6}))
                    return false;
            }
            else
            {
                m_scratch[1] = static_cast<char>(byte);
                if (!Raw({m_scratch.data(), 2}))
                    return false;
            }
            start = i + 1;
        }
        return (text.empty() || Raw({text.data() + start, text.size() - start})) && Raw("\"");
    }

    bool CanonicalJSONWriter::UInt64(uint64_t value)
    {
        if (!CanEmit())
            return false;
        const auto formatted = std::to_chars(m_scratch.data(), m_scratch.data() + m_scratch.size(), value);
        return formatted.ec == std::errc{} ? Raw({m_scratch.data(), static_cast<size_t>(formatted.ptr - m_scratch.data())}) : Reject(VFS::VFSError::Corrupted);
    }

    bool CanonicalJSONWriter::Float32(float value)
    {
        if (!CanEmit())
            return false;
        if (!std::isfinite(value))
            return Reject(VFS::VFSError::Corrupted);
        if (value == 0.f)
            return Raw("0");
        auto*      bytes     = m_scratch.data();
        const auto formatted = std::to_chars(bytes, bytes + m_scratch.size(), value, std::chars_format::scientific, 8);
        if (formatted.ec != std::errc{})
            return Reject(VFS::VFSError::Corrupted);
        char* exponent = bytes;
        while (exponent != formatted.ptr && *exponent != 'e')
            ++exponent;
        if (exponent == formatted.ptr)
            return Reject(VFS::VFSError::Corrupted);
        ++exponent;
        const char* digits = exponent + (*exponent == '+' ? 1 : 0);
        int         power  = 0;
        const auto  parsed = std::from_chars(digits, formatted.ptr, power);
        if (parsed.ec != std::errc{} || parsed.ptr != formatted.ptr)
            return Reject(VFS::VFSError::Corrupted);
        const auto normalized = std::to_chars(exponent, bytes + m_scratch.size(), power);
        return normalized.ec == std::errc{} ? Raw({bytes, static_cast<size_t>(normalized.ptr - bytes)}) : Reject(VFS::VFSError::Corrupted);
    }

    bool CanonicalJSONWriter::Boolean(bool value)
    {
        return Raw(value ? "true" : "false");
    }

    bool CanonicalJSONWriter::UUID(const uuids::uuid& uuid)
    {
        if (!CanEmit())
            return false;
        (void) uuids::to_string<char>(uuid, m_scratch.data());
        return String({m_scratch.data(), 36});
    }

    bool CanonicalJSONWriter::HexString(Containers::ArrayView<const uint8_t> bytes)
    {
        if (!CanEmit())
            return false;
        if (bytes.size() && !bytes.data())
            return Reject(VFS::VFSError::InvalidPath);
        if (!Raw("\""))
            return false;
        constexpr cstring hex = "0123456789abcdef";
        for (size_t offset = 0; offset < bytes.size();)
        {
            const size_t count = std::min(bytes.size() - offset, m_scratch.size() / 2);
            for (size_t i = 0; i < count; ++i)
            {
                m_scratch[2 * i]     = hex[bytes[offset + i] >> 4];
                m_scratch[2 * i + 1] = hex[bytes[offset + i] & 15];
            }
            if (!Raw({m_scratch.data(), count * 2}))
                return false;
            offset += count;
        }
        return Raw("\"");
    }
} // namespace ZEngine::Core::Serialization
