#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/VFS/VFSError.h>

namespace ZEngine::Importers::AssetCodec
{
    // Synchronous borrowed random-access bytes. No filename, reopen authority,
    // heap owner or GPU dependency. Read may return short reads, never more than
    // the destination size. The owner/context outlives every reader callback.
    struct AssetByteSource
    {
        void* Context                                                                                      = nullptr;
        Core::VFS::VFSResult<size_t> (*Read)(void*, Core::Containers::ArrayView<uint8_t>, uint64_t offset) = nullptr;
        uint64_t Size                                                                                      = 0;
    };
    // Exact bounded borrowed read shared by CPU codecs. Short provider reads
    // are completed; unexpected EOF/overreport is corruption. No allocation.
    [[nodiscard]] inline Core::VFS::VFSResult<void> ReadAssetByteRange(const AssetByteSource& source, uint64_t offset, Core::Containers::ArrayView<uint8_t> bytes)
    {
        if (!source.Read || (bytes.size() && !bytes.data()))
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
        if (offset > source.Size || bytes.size() > source.Size - offset)
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Corrupted);
        for (size_t filled = 0; filled < bytes.size();)
        {
            const auto read = source.Read(source.Context, {bytes.data() + filled, bytes.size() - filled}, offset + filled);
            if (read.Failed() || !read.Value() || read.Value() > bytes.size() - filled)
                return Core::VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : Core::VFS::VFSError::Corrupted);
            filled += read.Value();
        }
        return Core::VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers::AssetCodec
