#include <ZEngine/Core/VFS/IVFSFile.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <algorithm>
#include <bit>
#include <cmath>
#include <limits>

namespace ZEngine::Importers::AssetCodec
{
    namespace VFS = Core::VFS;

    VFS::VFSResult<void> ReadEnvironmentMapAsset(const AssetByteSource& source, Core::Containers::ArrayView<uint8_t> stream, EnvironmentMapFileHeader* output, uint64_t expected_source_hash, const EnvironmentMapReadLimits& limits)
    {
        if (!source.Read || !output || !stream.data() || stream.size() < sizeof(float) || stream.size() % sizeof(float))
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        if constexpr (std::endian::native != std::endian::little || sizeof(size_t) != 8 || sizeof(float) != 4 || !std::numeric_limits<float>::is_iec559)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        if (source.Size > limits.MaxFileBytes)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        EnvironmentMapFileHeader header = {};
        auto*                    bytes  = reinterpret_cast<uint8_t*>(&header);
        auto                     read   = ReadAssetByteRange(source, 0, {bytes, 8});
        if (read.Failed())
        {
            return read;
        }
        if (header.MagicNumber != ZENVMAP_MAGIC)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (header.Version != ENVIRONMENT_MAP_FILE_VERSION)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        read = ReadAssetByteRange(source, 8, {bytes + 8, sizeof(header) - 8});
        if (read.Failed())
        {
            return read;
        }
        if (header.ImporterVersion != ENVIRONMENT_MAP_IMPORTER_VERSION)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Unsupported);
        }
        if (!DoesEnvironmentMapHeaderMatchSource(header, expected_source_hash) || header.Reserved || source.Size != sizeof(header) + header.BufferByteSize)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }
        if (header.FaceWidth > limits.MaxFaceDimension || header.BufferByteSize > limits.MaxDecodedBytes)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
        }
        for (uint64_t offset = sizeof(header); offset < source.Size;)
        {
            const size_t count = static_cast<size_t>(std::min<uint64_t>(stream.size(), source.Size - offset));
            read               = ReadAssetByteRange(source, offset, stream.first(count));
            if (read.Failed())
            {
                return read;
            }
            for (size_t i = 0; i < count; i += sizeof(float))
            {
                float value = 0;
                Helpers::secure_memcpy(&value, sizeof(value), stream.data() + i, sizeof(value));
                if (!std::isfinite(value))
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            offset += count;
        }
        *output = header;
        return VFS::VFSResult<void>::Ok();
    }

    VFS::VFSResult<void> ReadEnvironmentMapFileVFS(VFS::IVFSContext& context, const VFS::VFSPath& path, Core::Containers::ArrayView<uint8_t> stream, EnvironmentMapFileHeader* output, uint64_t expected_source_hash, const EnvironmentMapReadLimits& limits)
    {
        if (!output)
        {
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        }
        const auto opened = context.Open(path, VFS::VFSOpenFlags::Read);
        if (opened.Failed())
        {
            return VFS::VFSResult<void>::Fail(opened.Error());
        }
        const auto               size   = opened.Value()->Size();
        EnvironmentMapFileHeader header = {};
        VFS::VFSResult<void>     read   = VFS::VFSResult<void>::Ok();
        if (size.Succeeded())
        {
            AssetByteSource source = {opened.Value(), [](void* file, Core::Containers::ArrayView<uint8_t> bytes, uint64_t offset) { return static_cast<VFS::IVFSFile*>(file)->Read(bytes, offset); }, size.Value()};
            read                   = ReadEnvironmentMapAsset(source, stream, &header, expected_source_hash, limits);
        }
        else
        {
            read = VFS::VFSResult<void>::Fail(size.Error());
        }
        const auto closed = context.CloseChecked(opened.Value());
        if (read.Failed() || closed.Failed())
        {
            return VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : closed.Error());
        }
        *output = header;
        return VFS::VFSResult<void>::Ok();
    }
} // namespace ZEngine::Importers::AssetCodec
