#include <ZEngine/Core/VFS/IVFSFile.h>

namespace ZEngine::Core::VFS
{
    VFSResult<size_t> IVFSFile::ReadAll(Core::Containers::ArrayView<uint8_t> out_buffer)
    {
        if (out_buffer.size() && !out_buffer.data())
            return VFSResult<size_t>::Fail(VFSError::InvalidPath);
        size_t total = 0;
        while (total < out_buffer.size())
        {
            Core::Containers::ArrayView<uint8_t> remaining(out_buffer.data() + total, out_buffer.size() - total);

            VFSResult<size_t>                    result = Read(remaining, total);
            if (result.Failed())
            {
                return VFSResult<size_t>::Fail(result.Error());
            }

            const size_t chunk = result.Value();
            if (chunk > remaining.size())
                return VFSResult<size_t>::Fail(VFSError::Corrupted);
            if (chunk == 0)
            {
                break; // end of file
            }
            total += chunk;
        }
        return VFSResult<size_t>::Ok(total);
    }
} // namespace ZEngine::Core::VFS
