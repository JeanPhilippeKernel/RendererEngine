#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <uuid.h>
#include <random>

namespace ZEngine::Core::VFS
{
    VFSResult<void> WriteFileAtomically(IVFSContext& ctx, const VFSPath& path, Containers::ArrayView<const uint8_t> data)
    {
        if (!path.IsValid() || path.IsRoot())
            return VFSResult<void>::Fail(VFSError::InvalidPath);

        std::random_device                                     random;
        uuids::basic_uuid_random_generator<std::random_device> generator(random);
        const auto                                             temporary = path.Parent().Append(("." + uuids::to_string(generator()) + ".tmp").c_str());
        if (temporary.Failed())
            return VFSResult<void>::Fail(temporary.Error());
        const auto& tmp_path = temporary.Value();
        auto        fail     = [&](VFSError error) {
            (void) ctx.Remove(tmp_path);
            return VFSResult<void>::Fail(error);
        };
        auto open = ctx.Open(tmp_path, VFSOpenFlags::Write | VFSOpenFlags::Create | VFSOpenFlags::Truncate);
        if (open.Failed())
            return fail(open.Error());
        auto* file  = open.Value();
        auto  write = file->Write(data, 0);
        auto  flush = file->Flush();
        auto  close = file->Close();
        ctx.Close(file);
        if (write.Failed())
            return fail(write.Error());
        if (write.Value() != data.size())
            return fail(VFSError::IOError);
        if (flush.Failed())
            return fail(flush.Error());
        if (close.Failed())
            return fail(close.Error());
        auto rename = ctx.Rename(tmp_path, path);
        return rename.Failed() ? fail(rename.Error()) : rename;
    }
} // namespace ZEngine::Core::VFS
