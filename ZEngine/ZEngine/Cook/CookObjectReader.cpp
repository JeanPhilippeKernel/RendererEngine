#include <ZEngine/Cook/CookObjectReader.h>
#include <algorithm>

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        struct ObjectVerificationState
        {
            Core::Hash::SHA256 Hash   = {};
            Digest256          Digest = {};
        };

        constexpr uint64_t ObjectVerificationBytes = sizeof(ObjectVerificationState) - sizeof(Core::Hash::SHA256) + Core::Hash::SHA256::FixedWorkingBytes() + sizeof(CookMemoryCharge);

        template <typename Root>
        VFS::VFSResult<void> CloseRoot(void* context, VFS::IVFSFile* file)
        {
            return static_cast<Root*>(context)->CloseChecked(file);
        }

        VFS::VFSResult<void> VerifyFileBytes(VFS::IVFSFile& file, const CookObjectDescriptor& object, Core::Containers::ArrayView<uint8_t> stream, ObjectVerificationState& state)
        {
            const auto before = file.Stat();
            if (before.Failed())
                return VFS::VFSResult<void>::Fail(before.Error());
            if (before.Value().IsDirectory || before.Value().SizeBytes != object.Size)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            for (uint64_t offset = 0; offset < object.Size;)
            {
                const size_t count = static_cast<size_t>(std::min<uint64_t>(stream.size(), object.Size - offset));
                const auto   bytes = stream.first(count);
                const auto   read  = file.Read(bytes, offset);
                if (read.Failed())
                    return VFS::VFSResult<void>::Fail(read.Error());
                if (!read.Value() || read.Value() > count)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                const auto hashed = state.Hash.Update(bytes.first(read.Value()));
                if (hashed.Failed())
                    return hashed;
                offset += read.Value();
            }
            // Also check EOF on the opened handle, rather than trusting Size/Stat
            // alone. One byte uses the existing workspace, not another allocation.
            const auto eof = file.Read(stream.first(1), object.Size);
            if (eof.Failed())
                return VFS::VFSResult<void>::Fail(eof.Error());
            if (eof.Value())
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            const auto after = file.Stat();
            if (after.Failed())
                return VFS::VFSResult<void>::Fail(after.Error());
            if (after.Value().IsDirectory || after.Value().SizeBytes != object.Size)
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            const auto finalized = state.Hash.Finalize(&state.Digest);
            return finalized.Failed() ? finalized : state.Digest == object.Digest ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
        }

        // Keep the single reservation alive through Open, all byte verification
        // and checked-close. Only Open may produce the caller's NotFound miss.
        VFS::VFSResult<bool> VerifyObjectPath(const CookObjectReadFns& reader, const VFS::VFSRelativePath& path, const CookObjectDescriptor& object, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator& allocator)
        {
            CookMemoryCharge fixed   = {};
            const auto       charged = allocator.TryCharge(ObjectVerificationBytes, CookMemoryCategory::StagingManifest, &fixed);
            if (charged.Failed())
                return VFS::VFSResult<bool>::Fail(charged.Error());
            const auto opened = reader.Open(reader.Context, path);
            if (opened.Failed())
                return opened.Error() == VFS::VFSError::NotFound ? VFS::VFSResult<bool>::Ok(false) : VFS::VFSResult<bool>::Fail(opened.Error() == VFS::VFSError::NotAFile ? VFS::VFSError::Corrupted : opened.Error());
            if (!opened.Value())
                return VFS::VFSResult<bool>::Fail(VFS::VFSError::IOError);
            ObjectVerificationState state    = {};
            const auto              verified = VerifyFileBytes(*opened.Value(), object, stream, state);
            const auto              closed   = reader.CloseChecked(reader.Context, opened.Value());
            const auto              error    = verified.Failed() ? verified.Error() : closed.Error();
            return error == VFS::VFSError::OK ? VFS::VFSResult<bool>::Ok(true) : VFS::VFSResult<bool>::Fail(error);
        }
    } // namespace

    CookObjectReadFns CookObjectReader(VFS::VFSPublicationRootLease& root) noexcept
    {
        return {&root, [](void* context, const VFS::VFSRelativePath& path) { return static_cast<VFS::VFSPublicationRootLease*>(context)->Open(path, VFS::VFSOpenFlags::Read | VFS::VFSOpenFlags::AllowReplace); }, CloseRoot<VFS::VFSPublicationRootLease>};
    }

    CookObjectReadFns CookObjectReader(VFS::VFSReadRootLease& root) noexcept
    {
        return {&root, [](void* context, const VFS::VFSRelativePath& path) { return static_cast<VFS::VFSReadRootLease*>(context)->Open(path); }, CloseRoot<VFS::VFSReadRootLease>};
    }

    VFS::VFSResult<void> VerifyCookObjectFile(VFS::IVFSFile& file, const CookObjectDescriptor& object, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator& allocator)
    {
        if (!stream.data() || !stream.size() || !object.Size || object.Size > ZGiga(16))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        CookMemoryCharge fixed   = {};
        const auto       charged = allocator.TryCharge(ObjectVerificationBytes, CookMemoryCategory::StagingManifest, &fixed);
        if (charged.Failed())
            return charged;
        ObjectVerificationState state = {};
        return VerifyFileBytes(file, object, stream, state);
    }

    VFS::VFSResult<bool> VerifyCookObject(const CookObjectReadFns& reader, const CookObjectDescriptor& object, Core::Containers::StringView extension, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator& allocator)
    {
        if (!reader.Open || !reader.CloseChecked || !stream.data() || !stream.size())
            return VFS::VFSResult<bool>::Fail(VFS::VFSError::InvalidPath);
        if (!IsCookObjectDescriptorValid(object, extension))
            return VFS::VFSResult<bool>::Fail(VFS::VFSError::Corrupted);
        return VerifyObjectPath(reader, object.Path, object, stream, allocator);
    }

    VFS::VFSResult<void> VerifyCookStagedObject(const CookObjectReadFns& reader, const VFS::VFSRelativePath& staging, const CookObjectDescriptor& object, Core::Containers::ArrayView<uint8_t> stream, ICookAllocator& allocator)
    {
        if (!reader.Open || !reader.CloseChecked || !staging.IsValid() || !stream.data() || !stream.size() || !object.Size || object.Size > ZGiga(16))
            return VFS::VFSResult<void>::Fail(VFS::VFSError::InvalidPath);
        const auto verified = VerifyObjectPath(reader, staging, object, stream, allocator);
        return verified.Failed() ? VFS::VFSResult<void>::Fail(verified.Error()) : verified.Value() ? VFS::VFSResult<void>::Ok() : VFS::VFSResult<void>::Fail(VFS::VFSError::NotFound);
    }
} // namespace ZEngine::Cook
