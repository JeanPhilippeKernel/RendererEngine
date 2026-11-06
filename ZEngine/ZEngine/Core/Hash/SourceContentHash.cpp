#include <ZEngine/Core/Hash/SourceContentHash.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <rapidhash.h>
#include <algorithm>

namespace ZEngine::Core::Hash
{
    VFS::VFSResult<void> SourceContentHash::Update(Containers::ArrayView<const uint8_t> bytes) noexcept
    {
        if (m_finalized || (bytes.size() && !bytes.data()))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        size_t offset = 0;
        while (offset < bytes.size())
        {
            if (!m_count && bytes.size() - offset >= ChunkBytes)
            {
                m_hash  = rapidhash_withSeed(bytes.data() + offset, ChunkBytes, m_hash);
                offset += ChunkBytes;
                continue;
            }
            const size_t count = std::min(ChunkBytes - m_count, bytes.size() - offset);
            Helpers::secure_memcpy(m_partial.data() + m_count, ChunkBytes - m_count, bytes.data() + offset, count);
            offset  += count;
            m_count += count;
            if (m_count == ChunkBytes)
            {
                m_hash  = rapidhash_withSeed(m_partial.data(), ChunkBytes, m_hash);
                m_count = 0;
            }
        }
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> SourceContentHash::Finalize(uint64_t* output) noexcept
    {
        if (!output || m_finalized)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        *output     = m_count ? rapidhash_withSeed(m_partial.data(), m_count, m_hash) : m_hash;
        m_finalized = true;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Core::Hash
