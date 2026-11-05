#include <ZEngine/Core/Hash/SHA256.h>
#include <limits>

namespace ZEngine::Core::Hash
{
    VFS::VFSResult<Digest256> Digest256::ParseHex(Containers::StringView text)
    {
        if (!text.data() || text.size() != 64)
            return VFS::VFSResult<Digest256>::Fail(VFS::VFSError::Corrupted);
        Digest256 digest = {};
        for (size_t i = 0; i < text.size(); ++i)
        {
            const char character = text[i];
            uint8_t    nibble    = 0;
            if (character >= '0' && character <= '9')
                nibble = static_cast<uint8_t>(character - '0');
            else if (character >= 'a' && character <= 'f')
                nibble = static_cast<uint8_t>(character - 'a' + 10);
            else
                return VFS::VFSResult<Digest256>::Fail(VFS::VFSError::Corrupted);
            digest.Bytes[i / 2] = static_cast<uint8_t>((digest.Bytes[i / 2] << 4) | nibble);
        }
        return VFS::VFSResult<Digest256>::Ok(digest);
    }

    Containers::FixedArray<char, 65> Digest256::Hex() const noexcept
    {
        constexpr char                   digits[] = "0123456789abcdef";
        Containers::FixedArray<char, 65> text     = {};
        for (size_t i = 0; i < Bytes.size(); ++i)
        {
            text[i * 2]     = digits[Bytes[i] >> 4];
            text[i * 2 + 1] = digits[Bytes[i] & 0x0f];
        }
        return text;
    }

    SHA256::SHA256() noexcept
    {
        Reset();
    }

    void SHA256::Reset() noexcept
    {
        // Upstream initializes every live state field. The chunk is overwritten
        // by input/padding before compression and close overwrites the digest;
        // neither needs a second aggregate clear (and O0 stack temporary).
        // Reset discards a stream; it is not a secure-erasure API.
        m_bytes     = 0;
        m_finalized = false;
        sha_256_init(&m_state, m_digest.Bytes.data());
    }

    VFS::VFSResult<void> SHA256::Update(Containers::ArrayView<const uint8_t> bytes) noexcept
    {
        if (m_finalized || (bytes.size() && !bytes.data()))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        // The algorithm's encoded bit count must fit u64. Reject overflow before
        // passing a range to upstream or modifying any stream state.
        constexpr uint64_t maximum_bytes = std::numeric_limits<uint64_t>::max() / 8;
        if (bytes.size() > maximum_bytes - m_bytes)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        if (bytes.size())
            sha_256_write(&m_state, bytes.data(), bytes.size());
        m_bytes += bytes.size();
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> SHA256::Finalize(Digest256* output) noexcept
    {
        if (!output || m_finalized)
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        sha_256_close(&m_state);
        m_finalized = true;
        *output     = m_digest;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Core::Hash
