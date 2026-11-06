#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Containers/Strings.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <sha-256.h>

namespace ZEngine::Core::Hash
{
    struct Digest256
    {
        Containers::FixedArray<uint8_t, 32>            Bytes                              = {};

        bool                                           operator==(const Digest256&) const = default;
        // Canonical lowercase hexadecimal only; no whitespace, suffix, or NUL.
        [[nodiscard]] static VFS::VFSResult<Digest256> ParseHex(Containers::StringView text);
        // Exactly 64 hexadecimal characters followed by NUL, without allocation.
        [[nodiscard]] Containers::FixedArray<char, 65> Hex() const noexcept;
    };

    // Inline streaming state around the pinned portable C implementation. Copies
    // and moves are deliberately disabled: upstream state contains self-pointers.
    // Budgeted callers reserve FixedWorkingBytes(), not only sizeof(SHA256):
    // upstream compression uses bounded temporary storage outside this object.
    class SHA256
    {
    public:
        SHA256() noexcept;
        SHA256(const SHA256&)                                          = delete;
        SHA256&                               operator=(const SHA256&) = delete;

        // Principal stream + compression-buffer bound for pinned sha-2 revision
        // 565f65009bdd98267361b17d50cddd7c9beb3e6c. consume_chunk uses ah[8]
        // and w[16], simultaneously, once per nonrecursive write/close path.
        // Audit this query when updating that dependency. No heap allocation;
        // excludes scalar/helper/compiler frames, not a complete stack bound.
        [[nodiscard]] static constexpr size_t FixedWorkingBytes() noexcept
        {
            return sizeof(SHA256) + (8 + 16) * sizeof(uint32_t);
        }

        void                               Reset() noexcept;
        [[nodiscard]] VFS::VFSResult<void> Update(Containers::ArrayView<const uint8_t> bytes) noexcept;
        // Terminal on success. Failure leaves both output and stream unchanged.
        [[nodiscard]] VFS::VFSResult<void> Finalize(Digest256* output) noexcept;
        [[nodiscard]] uint64_t             BytesWritten() const noexcept
        {
            return m_bytes;
        }

    private:
        Sha_256   m_state     = {};
        Digest256 m_digest    = {};
        uint64_t  m_bytes     = 0;
        bool      m_finalized = false;
    };
} // namespace ZEngine::Core::Hash
