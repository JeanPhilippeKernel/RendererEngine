#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Helpers/UTF8.h>
#include <charconv>
#include <cmath>

namespace ZEngine::Core::Serialization
{
    JSONReader::JSONReader(Containers::ArrayView<const uint8_t> input, JSONReadLimits limits)
    {
        Reset(input, limits);
    }

    void JSONReader::Reset(Containers::ArrayView<const uint8_t> input, JSONReadLimits limits)
    {
        m_input         = input;
        m_limits        = limits;
        m_position      = 0;
        m_depth         = 0;
        m_root_consumed = false;
        m_error         = VFS::VFSError::OK;
        // Begin overwrites each frame before use; inactive frames need no clear.
        if (!limits.MaxDepth || limits.MaxDepth > m_stack.size() || !limits.MaxStringBytes)
        {
            m_error = VFS::VFSError::InvalidPath;
        }
        else if (!Helpers::IsValidUTF8(reinterpret_cast<cstring>(input.data()), input.size()))
        {
            m_error = VFS::VFSError::Corrupted;
        }
    }

    JSONValueKind JSONReader::PeekValueKind() const noexcept
    {
        if (m_error != VFS::VFSError::OK)
        {
            return JSONValueKind::Invalid;
        }
        size_t position = m_position;
        while (position < m_input.size() && (m_input[position] == ' ' || m_input[position] == '\t' || m_input[position] == '\r' || m_input[position] == '\n'))
        {
            ++position;
        }
        if (position == m_input.size())
        {
            return JSONValueKind::Invalid;
        }
        switch (m_input[position])
        {
            case '{':
                return JSONValueKind::Object;
            case '[':
                return JSONValueKind::Array;
            case '"':
                return JSONValueKind::String;
            case 't':
            case 'f':
                return JSONValueKind::Boolean;
            case 'n':
                return JSONValueKind::Null;
            default:
                return m_input[position] == '-' || (m_input[position] >= '0' && m_input[position] <= '9') ? JSONValueKind::Number : JSONValueKind::Invalid;
        }
    }

    bool JSONReader::Reject(VFS::VFSError error)
    {
        if (m_error == VFS::VFSError::OK)
        {
            m_error = error;
        }
        return false;
    }

    bool JSONReader::At(char token) const
    {
        return m_position < m_input.size() && m_input[m_position] == static_cast<uint8_t>(token);
    }

    void JSONReader::Whitespace()
    {
        while (At(' ') || At('\t') || At('\r') || At('\n'))
        {
            ++m_position;
        }
    }

    bool JSONReader::Consume(char token)
    {
        Whitespace();
        if (!At(token))
        {
            return Reject();
        }
        ++m_position;
        return true;
    }

    bool JSONReader::BeforeValue()
    {
        if (m_error != VFS::VFSError::OK)
        {
            return false;
        }
        if (m_depth)
        {
            auto& frame = m_stack[m_depth - 1];
            if (!frame.AwaitingValue)
            {
                return Reject();
            }
            frame.AwaitingValue = false;
        }
        else
        {
            if (m_root_consumed)
            {
                return Reject();
            }
            m_root_consumed = true;
        }
        Whitespace();
        return true;
    }

    bool JSONReader::Begin(ContainerKind kind, char token)
    {
        if (!BeforeValue() || !Consume(token))
        {
            return false;
        }
        if (m_depth == m_limits.MaxDepth)
        {
            return Reject(VFS::VFSError::SizeLimitExceeded);
        }
        m_stack[m_depth++] = {.Kind = kind};
        return true;
    }

    bool JSONReader::BeginObject()
    {
        return Begin(ContainerKind::Object, '{');
    }

    bool JSONReader::BeginArray()
    {
        return Begin(ContainerKind::Array, '[');
    }

    bool JSONReader::Next(ContainerKind kind, char closing)
    {
        if (m_error != VFS::VFSError::OK)
        {
            return false;
        }
        if (!m_depth || m_stack[m_depth - 1].Kind != kind || m_stack[m_depth - 1].AwaitingValue)
        {
            return Reject();
        }
        auto& frame = m_stack[m_depth - 1];
        Whitespace();
        if (At(closing))
        {
            ++m_position;
            --m_depth;
            return false;
        }
        if (frame.HasEntry && !Consume(','))
        {
            return false;
        }
        frame.HasEntry      = true;
        frame.AwaitingValue = true;
        return true;
    }

    bool JSONReader::Member(Containers::ArrayView<char> buffer, Containers::StringView* key)
    {
        if (!Next(ContainerKind::Object, '}'))
        {
            return false;
        }
        return DecodeString(buffer, key) && Consume(':');
    }

    bool JSONReader::Element()
    {
        return Next(ContainerKind::Array, ']');
    }

    bool JSONReader::HexQuad(uint32_t* value)
    {
        if (m_input.size() - m_position < 4)
        {
            return Reject();
        }
        uint32_t number = 0;
        for (uint32_t i = 0; i < 4; ++i)
        {
            const auto byte  = m_input[m_position++];
            uint32_t   digit = 0;
            if (byte >= '0' && byte <= '9')
            {
                digit = byte - '0';
            }
            else if (byte >= 'a' && byte <= 'f')
            {
                digit = byte - 'a' + 10;
            }
            else if (byte >= 'A' && byte <= 'F')
            {
                digit = byte - 'A' + 10;
            }
            else
            {
                return Reject();
            }
            number = (number << 4) | digit;
        }
        *value = number;
        return true;
    }

    bool JSONReader::DecodeString(Containers::ArrayView<char> buffer, Containers::StringView* value)
    {
        if (!value || !buffer.data() || !buffer.size())
        {
            return Reject(VFS::VFSError::InvalidPath);
        }
        if (!Consume('"'))
        {
            return false;
        }
        size_t     length = 0;
        const auto append = [&](uint8_t byte) {
            if (length >= buffer.size() - 1 || length >= m_limits.MaxStringBytes)
            {
                return Reject(VFS::VFSError::SizeLimitExceeded);
            }
            buffer[length++] = static_cast<char>(byte);
            return true;
        };
        while (m_position < m_input.size())
        {
            auto byte = m_input[m_position++];
            if (byte == '"')
            {
                buffer[length] = '\0';
                *value         = {buffer.data(), length};
                return true;
            }
            if (byte < 0x20)
            {
                return Reject();
            }
            if (byte != '\\')
            {
                if (!append(byte))
                {
                    return false;
                }
                continue;
            }
            if (m_position == m_input.size())
            {
                return Reject();
            }
            byte = m_input[m_position++];
            switch (byte)
            {
                case '"':
                case '\\':
                case '/':
                    break;
                case 'b':
                    byte = '\b';
                    break;
                case 'f':
                    byte = '\f';
                    break;
                case 'n':
                    byte = '\n';
                    break;
                case 'r':
                    byte = '\r';
                    break;
                case 't':
                    byte = '\t';
                    break;
                case 'u':
                {
                    uint32_t code = 0;
                    if (!HexQuad(&code))
                    {
                        return false;
                    }
                    if (code >= 0xd800 && code <= 0xdbff)
                    {
                        if (!At('\\'))
                        {
                            return Reject();
                        }
                        ++m_position;
                        if (!At('u'))
                        {
                            return Reject();
                        }
                        ++m_position;
                        uint32_t low = 0;
                        if (!HexQuad(&low) || low < 0xdc00 || low > 0xdfff)
                        {
                            return Reject();
                        }
                        code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                    }
                    else if (code >= 0xdc00 && code <= 0xdfff)
                    {
                        return Reject();
                    }
                    if (code <= 0x7f)
                    {
                        if (!append(static_cast<uint8_t>(code)))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        const uint32_t continuation_count = code <= 0x7ff ? 1 : code <= 0xffff ? 2 : 3;
                        const uint8_t  prefix             = continuation_count == 1 ? 0xc0 : continuation_count == 2 ? 0xe0 : 0xf0;
                        if (!append(static_cast<uint8_t>(prefix | (code >> (6 * continuation_count)))))
                        {
                            return false;
                        }
                        for (uint32_t i = continuation_count; i > 0; --i)
                        {
                            if (!append(static_cast<uint8_t>(0x80 | ((code >> (6 * (i - 1))) & 0x3f))))
                            {
                                return false;
                            }
                        }
                    }
                    continue;
                }
                default:
                    return Reject();
            }
            if (!append(byte))
            {
                return false;
            }
        }
        return Reject();
    }

    bool JSONReader::String(Containers::ArrayView<char> buffer, Containers::StringView* value)
    {
        return BeforeValue() && DecodeString(buffer, value);
    }

    bool JSONReader::Delimiter() const
    {
        return m_position == m_input.size() || At(',') || At(']') || At('}') || At(' ') || At('\t') || At('\r') || At('\n');
    }

    bool JSONReader::Number(Containers::StringView* value)
    {
        if (!value)
        {
            return Reject(VFS::VFSError::InvalidPath);
        }
        if (!BeforeValue())
        {
            return false;
        }
        const size_t start = m_position;
        if (At('-'))
        {
            ++m_position;
        }
        const auto digits = [&] {
            const size_t first = m_position;
            while (m_position < m_input.size() && m_input[m_position] >= '0' && m_input[m_position] <= '9')
            {
                ++m_position;
            }
            return m_position != first;
        };
        if (At('0'))
        {
            ++m_position;
        }
        else if (!digits())
        {
            return Reject();
        }
        if (At('.'))
        {
            ++m_position;
            if (!digits())
            {
                return Reject();
            }
        }
        if (At('e') || At('E'))
        {
            ++m_position;
            if (At('+') || At('-'))
            {
                ++m_position;
            }
            if (!digits())
            {
                return Reject();
            }
        }
        if (!Delimiter())
        {
            return Reject();
        }
        *value = {reinterpret_cast<cstring>(m_input.data() + start), m_position - start};
        return true;
    }

    bool JSONReader::Float32(float* value)
    {
        if (!value)
        {
            return Reject(VFS::VFSError::InvalidPath);
        }
        Containers::StringView number = {};
        if (!Number(&number))
        {
            return false;
        }
        float      candidate = 0.f;
        const auto parsed    = std::from_chars(number.data(), number.data() + number.size(), candidate, std::chars_format::general);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size() || !std::isfinite(candidate))
        {
            return Reject();
        }
        *value = candidate;
        return true;
    }

    template <typename Integer>
    bool JSONReader::ReadInteger(Integer* value)
    {
        if (!value)
        {
            return Reject(VFS::VFSError::InvalidPath);
        }
        Containers::StringView number = {};
        if (!Number(&number))
        {
            return false;
        }
        Integer    candidate = 0;
        const auto parsed    = std::from_chars(number.data(), number.data() + number.size(), candidate);
        if (parsed.ec != std::errc{} || parsed.ptr != number.data() + number.size())
        {
            return Reject();
        }
        *value = candidate;
        return true;
    }

    bool JSONReader::UInt64(uint64_t* value)
    {
        return ReadInteger(value);
    }

    bool JSONReader::Int64(int64_t* value)
    {
        return ReadInteger(value);
    }

    bool JSONReader::Literal(cstring text, size_t length)
    {
        if (!BeforeValue())
        {
            return false;
        }
        if (length > m_input.size() - m_position)
        {
            return Reject();
        }
        for (size_t i = 0; i < length; ++i)
        {
            if (m_input[m_position++] != static_cast<uint8_t>(text[i]))
            {
                return Reject();
            }
        }
        return Delimiter() || Reject();
    }

    bool JSONReader::Boolean(bool* value)
    {
        if (!value)
        {
            return Reject(VFS::VFSError::InvalidPath);
        }
        Whitespace();
        const bool candidate = At('t');
        if (!(candidate ? Literal("true", 4) : Literal("false", 5)))
        {
            return false;
        }
        *value = candidate;
        return true;
    }

    bool JSONReader::Null()
    {
        return Literal("null", 4);
    }

    bool JSONReader::Finish()
    {
        if (m_error != VFS::VFSError::OK)
        {
            return false;
        }
        Whitespace();
        return (m_root_consumed && !m_depth && m_position == m_input.size()) || Reject();
    }
} // namespace ZEngine::Core::Serialization
