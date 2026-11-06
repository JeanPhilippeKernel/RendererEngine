#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/VFS/VFSError.h>

namespace ZEngine::Core::Hash
{
    // Existing import receipt hash: rapidhash chained at EXACT 4096-byte
    // boundaries, with zero seed. Not equivalent to one-shot rapidhash and not
    // a cryptographic identity. Arbitrarily fragmented Update calls preserve
    // that wire contract. Inline state, no allocation; charge containing storage.
    class SourceContentHash
    {
    public:
        static constexpr size_t            ChunkBytes = 4096;

        [[nodiscard]] VFS::VFSResult<void> Update(Containers::ArrayView<const uint8_t>) noexcept;
        [[nodiscard]] VFS::VFSResult<void> Finalize(uint64_t*) noexcept;

    private:
        Containers::FixedArray<uint8_t, ChunkBytes> m_partial   = {};
        size_t                                      m_count     = 0;
        uint64_t                                    m_hash      = 0;
        bool                                        m_finalized = false;
    };
} // namespace ZEngine::Core::Hash
