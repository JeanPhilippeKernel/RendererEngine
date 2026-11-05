#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSDiskRoot.h>
#include <ZEngine/Core/VFS/VFSNativeStat.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cerrno>
#include <cstring>
#include <limits>
#include <new>
#include <utility>
#if defined(__APPLE__)
#include <sys/attr.h>
#elif defined(__linux__)
#include <dirent.h>
#endif
#if defined(__linux__)
#include <linux/fs.h>
#include <linux/openat2.h>
#include <sys/syscall.h>
#endif

namespace ZEngine::Core::VFS
{
#if defined(__APPLE__) || defined(__linux__)
    namespace
    {
        VFSError NativeError(int error)
        {
            switch (error)
            {
                case ENOENT:
                    return VFSError::NotFound;
                case EEXIST:
                case ENOTEMPTY:
                    return VFSError::AlreadyExists;
                case ENOTDIR:
                    return VFSError::NotADirectory;
                case EISDIR:
                    return VFSError::NotAFile;
                case EACCES:
                case EPERM:
                case ELOOP:
                case EXDEV:
                    return VFSError::PermissionDenied;
                case ENOSYS:
                case ENOTSUP:
                case EINVAL:
                    return VFSError::Unsupported;
                case ENOMEM:
                case EMFILE:
                case ENFILE:
                    return VFSError::OutOfMemory;
                default:
                    return VFSError::IOError;
            }
        }

        struct NativeDescriptor
        {
            int Handle         = -1;
            NativeDescriptor() = default;
            explicit NativeDescriptor(int handle) : Handle(handle) {}
            NativeDescriptor(const NativeDescriptor&)            = delete;
            NativeDescriptor& operator=(const NativeDescriptor&) = delete;
            ~NativeDescriptor()
            {
                Reset(-1);
            }
            void Reset(int handle)
            {
                const int old = Handle;
                Handle        = handle;
                if (old >= 0)
                    (void) ::close(old);
            }
            int Release()
            {
                return std::exchange(Handle, -1);
            }
        };

        int OpenAt(int parent, cstring name, int flags, mode_t mode = 0)
        {
#if defined(__linux__)
            // NO_XDEV also rejects bind mounts with an unchanged st_dev. Do not
            // silently downgrade this guarantee on kernels without openat2.
            const open_how how = {.flags = static_cast<uint64_t>(flags), .mode = static_cast<uint64_t>(mode), .resolve = RESOLVE_BENEATH | RESOLVE_NO_SYMLINKS | RESOLVE_NO_MAGICLINKS | RESOLVE_NO_XDEV};
            return static_cast<int>(::syscall(SYS_openat2, parent, name, &how, sizeof(how)));
#else
            return ::openat(parent, name, flags, mode);
#endif
        }

        VFSResult<void> CheckType(const struct stat& stat, dev_t device)
        {
            if (stat.st_dev != device || S_ISLNK(stat.st_mode))
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            if (!S_ISDIR(stat.st_mode) && !S_ISREG(stat.st_mode))
                return VFSResult<void>::Fail(VFSError::NotAFile);
            return VFSResult<void>::Ok();
        }

        VFSResult<void> WalkDirectories(int root, dev_t device, cstring path, size_t length, bool create, NativeDescriptor& output)
        {
            output.Reset(::fcntl(root, F_DUPFD_CLOEXEC, 0));
            if (output.Handle < 0)
                return VFSResult<void>::Fail(NativeError(errno));
            size_t offset = 0;
            while (offset < length)
            {
                size_t end = offset;
                while (end < length && path[end] != '/')
                    ++end;
                char component[VFS_MAX_PATH] = {};
                if (end == offset || Helpers::secure_memcpy(component, sizeof(component), path + offset, end - offset) != Helpers::MEMORY_OP_SUCCESS)
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                if (create && ::mkdirat(output.Handle, component, 0755) != 0 && errno != EEXIST)
                    return VFSResult<void>::Fail(NativeError(errno));
                NativeDescriptor child(OpenAt(output.Handle, component, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
                if (child.Handle < 0)
                    return VFSResult<void>::Fail(NativeError(errno));
                struct stat stat = {};
                if (::fstat(child.Handle, &stat) != 0)
                    return VFSResult<void>::Fail(NativeError(errno));
                const auto check = CheckType(stat, device);
                if (check.Failed())
                    return check;
                output.Reset(child.Release());
                offset = end + 1;
            }
            return VFSResult<void>::Ok();
        }

        VFSResult<void> ResolveParent(int root, dev_t device, const VFSRelativePath& path, NativeDescriptor& parent, char (&leaf)[VFS_MAX_PATH])
        {
            if (!path.IsValid())
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            size_t leaf_offset = path.Length();
            while (leaf_offset && path.CStr()[leaf_offset - 1] != '/')
                --leaf_offset;
            const auto walk = WalkDirectories(root, device, path.CStr(), leaf_offset ? leaf_offset - 1 : 0, false, parent);
            if (walk.Failed())
                return walk;
            if (Helpers::secure_memcpy(leaf, sizeof(leaf), path.CStr() + leaf_offset, path.Length() - leaf_offset + 1) != Helpers::MEMORY_OP_SUCCESS)
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            return VFSResult<void>::Ok();
        }

        VFSResult<struct stat> StatAt(int parent, cstring leaf, dev_t device)
        {
            struct stat stat = {};
            if (::fstatat(parent, leaf, &stat, AT_SYMLINK_NOFOLLOW) != 0)
                return VFSResult<struct stat>::Fail(NativeError(errno));
            const auto check = CheckType(stat, device);
            if (check.Failed())
                return VFSResult<struct stat>::Fail(check.Error());
#if defined(__linux__)
            // fstatat alone would accept a same-device bind mount at the leaf.
            NativeDescriptor checked(OpenAt(parent, leaf, O_PATH | O_CLOEXEC | O_NOFOLLOW));
            if (checked.Handle < 0)
                return VFSResult<struct stat>::Fail(NativeError(errno));
            if (::fstat(checked.Handle, &stat) != 0)
                return VFSResult<struct stat>::Fail(NativeError(errno));
            const auto checked_type = CheckType(stat, device);
            if (checked_type.Failed())
                return VFSResult<struct stat>::Fail(checked_type.Error());
#endif
            return VFSResult<struct stat>::Ok(stat);
        }

        int RenameAt(int from_parent, cstring from, int to_parent, cstring to, bool replace)
        {
            if (replace)
                return ::renameat(from_parent, from, to_parent, to);
#if defined(__APPLE__)
            return ::renameatx_np(from_parent, from, to_parent, to, RENAME_EXCL);
#else
            return static_cast<int>(::syscall(SYS_renameat2, from_parent, from, to_parent, to, RENAME_NOREPLACE));
#endif
        }

        bool SupportsNoReplace(int root)
        {
            // No output mutation: an empty name must fail ENOENT, not unsupported.
            return RenameAt(root, "", root, "", false) < 0 && errno == ENOENT;
        }

        struct DirectoryName
        {
            Containers::StringView Name        = {};
            size_t                 RecordBytes = 0;
        };

        VFSResult<DirectoryName> ParseDirectoryName(Containers::ArrayView<const uint8_t> bytes)
        {
#if defined(__APPLE__)
            constexpr size_t reference_offset = sizeof(uint32_t) + sizeof(attribute_set_t);
            constexpr size_t prefix           = reference_offset + sizeof(attrreference_t);
            if (bytes.size() < prefix)
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
            uint32_t        record_bytes = 0;
            attribute_set_t attributes   = {};
            attrreference_t reference    = {};
            Helpers::secure_memcpy(&record_bytes, sizeof(record_bytes), bytes.data(), sizeof(record_bytes));
            Helpers::secure_memcpy(&attributes, sizeof(attributes), bytes.data() + sizeof(record_bytes), sizeof(attributes));
            Helpers::secure_memcpy(&reference, sizeof(reference), bytes.data() + reference_offset, sizeof(reference));
            if (record_bytes < prefix || record_bytes > bytes.size() || !(attributes.commonattr & ATTR_CMN_NAME) || reference.attr_dataoffset < static_cast<int32_t>(sizeof(reference)))
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
            const size_t name_offset = reference_offset + static_cast<uint32_t>(reference.attr_dataoffset);
            if (name_offset > record_bytes || !reference.attr_length || reference.attr_length > record_bytes - name_offset)
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
            const auto* name = reinterpret_cast<cstring>(bytes.data() + name_offset);
            const auto* end  = static_cast<cstring>(std::memchr(name, 0, reference.attr_length));
            if (!end || end != name + reference.attr_length - 1)
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
#else
            constexpr size_t prefix = offsetof(struct dirent, d_name);
            if (bytes.size() < prefix + 1)
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
            uint16_t record_bytes = 0;
            Helpers::secure_memcpy(&record_bytes, sizeof(record_bytes), bytes.data() + offsetof(struct dirent, d_reclen), sizeof(record_bytes));
            if (record_bytes < prefix + 1 || record_bytes > bytes.size())
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
            const auto* name = reinterpret_cast<cstring>(bytes.data() + prefix);
            const auto* end  = static_cast<cstring>(std::memchr(name, 0, record_bytes - prefix));
            if (!end)
                return VFSResult<DirectoryName>::Fail(VFSError::Corrupted);
#endif
            return VFSResult<DirectoryName>::Ok({
                .Name = {name, static_cast<size_t>(end - name)},
                  .RecordBytes = record_bytes
            });
        }

        template <typename Visitor>
        VFSResult<void> VisitDirectoryNames(int descriptor, Containers::ArrayView<uint8_t> buffer, Visitor&& visit)
        {
            if (!buffer.data() || buffer.size() < ZKilo(4) || buffer.size() > ZKilo(64))
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            for (;;)
            {
#if defined(__APPLE__)
                struct attrlist attributes = {.bitmapcount = ATTR_BIT_MAP_COUNT, .commonattr = ATTR_BULK_REQUIRED};
                const int       records    = ::getattrlistbulk(descriptor, &attributes, buffer.data(), buffer.size(), 0);
                if (records < 0)
                    return VFSResult<void>::Fail(NativeError(errno));
                if (!records)
                    return VFSResult<void>::Ok();
                const size_t batch_bytes = buffer.size();
#else
                const auto bytes = ::syscall(SYS_getdents64, descriptor, buffer.data(), buffer.size());
                if (bytes < 0)
                    return VFSResult<void>::Fail(NativeError(errno));
                if (!bytes)
                    return VFSResult<void>::Ok();
                const size_t batch_bytes = static_cast<size_t>(bytes);
#endif
                size_t offset = 0;
#if defined(__APPLE__)
                for (int record = 0; record < records; ++record)
#else
                while (offset < batch_bytes)
#endif
                {
                    if (offset >= batch_bytes)
                        return VFSResult<void>::Fail(VFSError::Corrupted);
                    const auto name = ParseDirectoryName({buffer.data() + offset, batch_bytes - offset});
                    if (name.Failed())
                        return VFSResult<void>::Fail(name.Error());
                    offset += name.Value().RecordBytes;
                    if (name.Value().Name == "." || name.Value().Name == "..")
                        continue;
                    const auto visited = visit(name.Value().Name);
                    if (visited.Failed())
                        return visited;
                }
            }
        }

        VFSResult<bool> AncestorsContain(int descriptor, const VFSFileIdentity& identity)
        {
            NativeDescriptor current(::fcntl(descriptor, F_DUPFD_CLOEXEC, 0));
            if (current.Handle < 0)
                return VFSResult<bool>::Fail(NativeError(errno));
            for (uint32_t depth = 0; depth < 1024; ++depth)
            {
                struct stat value = {}, parent_value = {};
                if (::fstat(current.Handle, &value) != 0)
                    return VFSResult<bool>::Fail(NativeError(errno));
                if (Detail::NativeIdentity(value) == identity)
                    return VFSResult<bool>::Ok(true);
                // Metadata-only ancestry check, deliberately not rooted content
                // resolution: roots may overlap through different disk facades.
                NativeDescriptor parent(::openat(current.Handle, "..", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
                if (parent.Handle < 0 || ::fstat(parent.Handle, &parent_value) != 0)
                    return VFSResult<bool>::Fail(NativeError(errno));
                if (Detail::NativeIdentity(parent_value) == Detail::NativeIdentity(value))
                    return VFSResult<bool>::Ok(false);
                current.Reset(parent.Release());
            }
            return VFSResult<bool>::Fail(VFSError::SizeLimitExceeded);
        }
    } // namespace

    struct VFSDiskRoot final : IVFSRootOperations, Memory::NativePageObject<VFSDiskRoot>
    {
        static constexpr size_t                           MaxOpenFiles     = 32;
        Helpers::Ref<IVFSRootProvider>                    Provider         = {};
        NativeDescriptor                                  Anchor           = {};
        dev_t                                             Device           = 0;
        VFSPublicationCapabilities                        RootCapabilities = {};
        Containers::FixedArray<VFSDiskFile, MaxOpenFiles> Files            = {};
        Containers::FixedArray<bool, MaxOpenFiles>        Used             = {};
        std::mutex                                        FileMutex        = {};
        VFSRootAccess                                     Access           = VFSRootAccess::ReadOnly;

        VFSPublicationCapabilities                        Capabilities() const override
        {
            return RootCapabilities;
        }
        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            return Provider->MemoryRequirements();
        }

        static VFSResult<Helpers::Scope<IVFSRootOperations>> Create(IVFSRootProvider& provider, NativeDescriptor& anchor, dev_t device, VFSRootAccess access, VFSPublicationCapabilities capabilities)
        {
            using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
            Helpers::Scope<VFSDiskRoot> root(new (std::nothrow) VFSDiskRoot());
            if (!root)
                return Result::Fail(VFSError::OutOfMemory);
            root->Provider = &provider;
            root->Anchor.Reset(anchor.Release());
            root->Device           = device;
            root->RootCapabilities = capabilities;
            root->Access           = access;
            return Result::Ok(std::move(root));
        }

        VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireChild(const VFSRelativePath& path) override
        {
            using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
            if (!path.IsValid())
                return Result::Fail(VFSError::InvalidPath);
            NativeDescriptor anchor = {};
            const auto       walk   = WalkDirectories(Anchor.Handle, Device, path.CStr(), path.Length(), false, anchor);
            if (walk.Failed())
                return Result::Fail(walk.Error());
            return Create(*Provider, anchor, Device, Access, RootCapabilities);
        }

        VFSResult<IVFSFile*> Open(const VFSRelativePath& path, VFSOpenFlags flags) override
        {
            constexpr auto known = VFSOpenFlags::Read | VFSOpenFlags::Write | VFSOpenFlags::Append | VFSOpenFlags::Create | VFSOpenFlags::Truncate | VFSOpenFlags::Exclusive | VFSOpenFlags::AllowReplace;
            const bool     read = HasFlag(flags, VFSOpenFlags::Read), write = HasFlag(flags, VFSOpenFlags::Write);
            const bool     create = HasFlag(flags, VFSOpenFlags::Create), exclusive = HasFlag(flags, VFSOpenFlags::Exclusive);
            if (Access == VFSRootAccess::ReadOnly && write)
                return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
            if ((static_cast<uint32_t>(flags) & ~static_cast<uint32_t>(known)) || (!read && !write) || (HasFlag(flags, VFSOpenFlags::AllowReplace) && flags != (VFSOpenFlags::Read | VFSOpenFlags::AllowReplace)) || (!write && (create || exclusive || HasFlag(flags, VFSOpenFlags::Truncate) || HasFlag(flags, VFSOpenFlags::Append))) || (exclusive && !create))
                return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
            const auto file_path = VFSPath::Parse(path.CStr(), path.Length());
            if (file_path.Failed())
                return VFSResult<IVFSFile*>::Fail(file_path.Error());
            NativeDescriptor parent             = {};
            char             leaf[VFS_MAX_PATH] = {};
            const auto       resolve            = ResolveParent(Anchor.Handle, Device, path, parent, leaf);
            if (resolve.Failed())
                return VFSResult<IVFSFile*>::Fail(resolve.Error());
            std::lock_guard lock(FileMutex);
            size_t          slot = 0;
            while (slot < MaxOpenFiles && Used[slot])
                ++slot;
            if (slot == MaxOpenFiles)
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            int native_flags = (write ? O_RDWR : O_RDONLY) | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK;
            if (create)
                native_flags |= O_CREAT;
            if (exclusive)
                native_flags |= O_EXCL;
            if (HasFlag(flags, VFSOpenFlags::Append))
                native_flags |= O_APPEND;
            NativeDescriptor descriptor(OpenAt(parent.Handle, leaf, native_flags, create ? 0644 : 0));
            if (descriptor.Handle < 0)
                return VFSResult<IVFSFile*>::Fail(NativeError(errno));
            struct stat stat = {};
            if (::fstat(descriptor.Handle, &stat) != 0)
                return VFSResult<IVFSFile*>::Fail(NativeError(errno));
            const auto check = CheckType(stat, Device);
            if (check.Failed())
                return VFSResult<IVFSFile*>::Fail(check.Error());
            if (!S_ISREG(stat.st_mode))
                return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
            // Never O_TRUNC before no-follow resolution and regular-file validation.
            if (HasFlag(flags, VFSOpenFlags::Truncate))
            {
                if (::ftruncate(descriptor.Handle, 0) != 0)
                    return VFSResult<IVFSFile*>::Fail(NativeError(errno));
                stat.st_size = 0;
            }
            auto& file      = Files[slot];
            file.m_fd       = descriptor.Release();
            file.m_writable = write;
            file.m_size     = static_cast<uint64_t>(stat.st_size);
            file.m_path     = file_path.Value();
            Used[slot]      = true;
            return VFSResult<IVFSFile*>::Ok(&file);
        }

        VFSResult<void> CloseChecked(IVFSFile* file) override
        {
            if (!file)
                return VFSResult<void>::Ok();
            std::lock_guard lock(FileMutex);
            for (size_t i = 0; i < MaxOpenFiles; ++i)
                if (file == &Files[i] && Used[i])
                {
                    const auto close = Files[i].Close();
                    Used[i]          = false;
                    return close;
                }
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }

        VFSResult<VFSFileStat> Stat(const VFSRelativePath& path) const override
        {
            NativeDescriptor parent             = {};
            char             leaf[VFS_MAX_PATH] = {};
            const auto       resolve            = ResolveParent(Anchor.Handle, Device, path, parent, leaf);
            if (resolve.Failed())
                return VFSResult<VFSFileStat>::Fail(resolve.Error());
            const auto native = StatAt(parent.Handle, leaf, Device);
            if (native.Failed())
                return VFSResult<VFSFileStat>::Fail(native.Error());
            return Detail::NativeStat(native.Value());
        }

        VFSResult<VFSFileIdentity> RootIdentity() const override
        {
            struct stat value = {};
            if (::fstat(Anchor.Handle, &value) != 0)
                return VFSResult<VFSFileIdentity>::Fail(NativeError(errno));
            return VFSResult<VFSFileIdentity>::Ok(Detail::NativeIdentity(value));
        }

        VFSResult<VFSFileIdentity> Identity(const VFSRelativePath& path) const override
        {
            NativeDescriptor parent             = {};
            char             leaf[VFS_MAX_PATH] = {};
            const auto       resolved           = ResolveParent(Anchor.Handle, Device, path, parent, leaf);
            if (resolved.Failed())
                return VFSResult<VFSFileIdentity>::Fail(resolved.Error());
            const auto value = StatAt(parent.Handle, leaf, Device);
            return value.Succeeded() ? VFSResult<VFSFileIdentity>::Ok(Detail::NativeIdentity(value.Value())) : VFSResult<VFSFileIdentity>::Fail(value.Error());
        }

        VFSResult<void> Visit(const VFSRelativePath* path, uint64_t maximum_entries, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const override
        {
            if (!visitor || (path && !path->IsValid()))
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            NativeDescriptor parent = {};
            const auto       walked = WalkDirectories(Anchor.Handle, Device, path ? path->CStr() : "", path ? path->Length() : 0, false, parent);
            if (walked.Failed())
                return walked;
            // A fresh open description gives each visit its own directory offset;
            // fcntl/dup would share it with other leases/visits of this anchor.
            NativeDescriptor directory(OpenAt(parent.Handle, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (directory.Handle < 0)
                return VFSResult<void>::Fail(NativeError(errno));
            uint64_t   seen    = 0;
            const auto visited = VisitDirectoryNames(directory.Handle, buffer, [&](Containers::StringView name) {
                if (seen == maximum_entries)
                    return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
                ++seen;
                const auto relative = path ? path->Append(name) : VFSRelativePath::Parse(name);
                if (relative.Failed())
                    return VFSResult<void>::Fail(relative.Error());
                const auto leaf = VFSRelativePath::Parse(name);
                if (leaf.Failed() || leaf.Value().ComponentCount() != 1)
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                const auto native = StatAt(directory.Handle, leaf.Value().CStr(), Device);
                if (native.Failed())
                    return VFSResult<void>::Fail(native.Error());
                const auto stat = Detail::NativeStat(native.Value());
                if (stat.Failed())
                    return VFSResult<void>::Fail(stat.Error());
                return visitor(context, {.Path = relative.Value(), .Stat = stat.Value(), .Identity = Detail::NativeIdentity(native.Value())});
            });
            const int  closed  = ::close(directory.Release());
            return visited.Failed() ? visited : closed == 0 ? VFSResult<void>::Ok() : VFSResult<void>::Fail(NativeError(errno));
        }

        VFSResult<bool> Overlaps(const IVFSRootOperations& operations) const override
        {
            const auto* other = dynamic_cast<const VFSDiskRoot*>(&operations);
            if (!other)
                return VFSResult<bool>::Fail(VFSError::Unsupported);
            const auto identity = RootIdentity(), other_identity = other->RootIdentity();
            if (identity.Failed() || other_identity.Failed())
                return VFSResult<bool>::Fail(identity.Failed() ? identity.Error() : other_identity.Error());
            const auto contained = AncestorsContain(other->Anchor.Handle, identity.Value());
            return contained.Failed() || contained.Value() ? contained : AncestorsContain(Anchor.Handle, other_identity.Value());
        }

        VFSResult<void> CreateDir(const VFSRelativePath& path, bool exclusive) override
        {
            if (Access == VFSRootAccess::ReadOnly)
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            if (!path.IsValid())
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            NativeDescriptor parent = {};
            if (!exclusive)
                return WalkDirectories(Anchor.Handle, Device, path.CStr(), path.Length(), true, parent);
            char       leaf[VFS_MAX_PATH] = {};
            const auto resolve            = ResolveParent(Anchor.Handle, Device, path, parent, leaf);
            if (resolve.Failed())
                return resolve;
            return ::mkdirat(parent.Handle, leaf, 0755) == 0 ? VFSResult<void>::Ok() : VFSResult<void>::Fail(NativeError(errno));
        }

        VFSResult<void> Remove(const VFSRelativePath& path) override
        {
            if (Access == VFSRootAccess::ReadOnly)
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            NativeDescriptor parent             = {};
            char             leaf[VFS_MAX_PATH] = {};
            const auto       resolve            = ResolveParent(Anchor.Handle, Device, path, parent, leaf);
            if (resolve.Failed())
                return resolve;
            const auto stat = StatAt(parent.Handle, leaf, Device);
            if (stat.Failed())
                return VFSResult<void>::Fail(stat.Error());
            return ::unlinkat(parent.Handle, leaf, S_ISDIR(stat.Value().st_mode) ? AT_REMOVEDIR : 0) == 0 ? VFSResult<void>::Ok() : VFSResult<void>::Fail(NativeError(errno));
        }

        VFSResult<void> Rename(const VFSRelativePath& from, const VFSRelativePath& to, bool replace) override
        {
            if (Access == VFSRootAccess::ReadOnly)
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            if (!HasGuarantees(RootCapabilities.Guarantees, replace ? VFSPublicationGuarantees::AtomicReplace : VFSPublicationGuarantees::AtomicNoReplace))
                return VFSResult<void>::Fail(VFSError::Unsupported);
            NativeDescriptor source_parent = {}, destination_parent = {};
            char             source[VFS_MAX_PATH] = {}, destination[VFS_MAX_PATH] = {};
            const auto       from_resolve = ResolveParent(Anchor.Handle, Device, from, source_parent, source);
            if (from_resolve.Failed())
                return from_resolve;
            const auto to_resolve = ResolveParent(Anchor.Handle, Device, to, destination_parent, destination);
            if (to_resolve.Failed())
                return to_resolve;
            const auto source_stat = StatAt(source_parent.Handle, source, Device);
            if (source_stat.Failed())
                return VFSResult<void>::Fail(source_stat.Error());
            const auto destination_stat = StatAt(destination_parent.Handle, destination, Device);
            if (destination_stat.Failed() && destination_stat.Error() != VFSError::NotFound)
                return VFSResult<void>::Fail(destination_stat.Error());
            return RenameAt(source_parent.Handle, source, destination_parent.Handle, destination, replace) == 0 ? VFSResult<void>::Ok() : VFSResult<void>::Fail(NativeError(errno));
        }
    };

    struct VFSDiskRootProvider final : IVFSRootProvider, Memory::NativePageObject<VFSDiskRootProvider>
    {
        char                                 NativeRoot[VFS_MAX_PATH] = {};
        VFSBackendCaps                       Caps                     = VFSBackendCaps::None;
        NativeDescriptor                     Anchor                   = {};

        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            const auto root_bytes     = VFSDiskRoot::NativeStorageBytes();
            const auto provider_bytes = NativeStorageBytes();
            if (!root_bytes || !provider_bytes)
                return VFSResult<VFSRootMemoryRequirements>::Fail(VFSError::Unsupported);
            return VFSResult<VFSRootMemoryRequirements>::Ok({
                .StateBytes = root_bytes, .Storage = {this, provider_bytes}
            });
        }

        VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireRoot(const VFSPath& relative, VFSRootAccess access) override
        {
            using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
            if (!HasCap(Caps, VFSBackendCaps::Read) || (access != VFSRootAccess::ReadOnly && !HasCap(Caps, VFSBackendCaps::Write)))
                return Result::Fail(VFSError::Unsupported);
            if (Anchor.Handle < 0)
                Anchor.Reset(::open(NativeRoot, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (Anchor.Handle < 0)
                return Result::Fail(NativeError(errno));
            struct stat stat = {};
            if (::fstat(Anchor.Handle, &stat) != 0)
                return Result::Fail(NativeError(errno));
            NativeDescriptor root = {};
            const auto       walk = WalkDirectories(Anchor.Handle, stat.st_dev, relative.CStr() + 1, relative.Length() - 1, false, root);
            if (walk.Failed())
                return Result::Fail(walk.Error());
#if defined(__linux__)
            // Probe the resolver even when acquiring the backend root itself.
            NativeDescriptor probe(OpenAt(root.Handle, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
            if (probe.Handle < 0)
                return Result::Fail(NativeError(errno));
#endif
            const bool no_replace = access != VFSRootAccess::ReadOnly && SupportsNoReplace(root.Handle);
            if (access == VFSRootAccess::Publication && !no_replace)
                return Result::Fail(VFSError::Unsupported);
            return VFSDiskRoot::Create(*this, root, stat.st_dev, access, {.BackendIdentity = BackendIdentity(), .Guarantees = access == VFSRootAccess::ReadOnly ? VFS_READ_GUARANTEES : (no_replace ? VFS_PUBLICATION_GUARANTEES : VFS_SCRATCH_GUARANTEES) | VFS_READ_GUARANTEES});
        }
    };
#endif

    Helpers::Ref<IVFSRootProvider> CreateDiskRootProvider(cstring native_root, VFSBackendCaps caps)
    {
#if defined(__APPLE__) || defined(__linux__)
        Helpers::Ref<VFSDiskRootProvider> provider(new (std::nothrow) VFSDiskRootProvider());
        if (!provider)
            return {};
        if (Helpers::secure_strcpy(provider->NativeRoot, sizeof(provider->NativeRoot), native_root) != Helpers::MEMORY_OP_SUCCESS)
            return {};
        provider->Caps = caps;
        if (!provider->BackendIdentity())
            return {};
        return provider;
#elif defined(_WIN32)
        return CreateWindowsDiskRootProvider(native_root, caps);
#else
        (void) native_root;
        (void) caps;
        return {};
#endif
    }
} // namespace ZEngine::Core::VFS
