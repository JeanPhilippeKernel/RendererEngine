#if defined(_WIN32)
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/NativePages.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSDiskRoot.h>
#include <ZEngine/Core/VFS/VFSNativeRenameWindows.h>
#include <ZEngine/Core/VFS/VFSNativeStat.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <winternl.h>
#include <algorithm>
#include <cstddef>
#include <cwchar>
#include <new>
#include <utility>

namespace ZEngine::Core::VFS
{
    namespace
    {
        constexpr DWORD DirectoryAccess = FILE_LIST_DIRECTORY | FILE_TRAVERSE | FILE_READ_ATTRIBUTES | SYNCHRONIZE;
        constexpr DWORD SharedMetadata  = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;

        struct NativeHandle
        {
            HANDLE Value   = INVALID_HANDLE_VALUE;
            NativeHandle() = default;
            explicit NativeHandle(HANDLE value) : Value(value) {}
            NativeHandle(const NativeHandle&)            = delete;
            NativeHandle& operator=(const NativeHandle&) = delete;
            ~NativeHandle()
            {
                (void) Close();
            }
            void Reset(HANDLE value)
            {
                (void) Close();
                Value = value;
            }
            HANDLE Release()
            {
                return std::exchange(Value, INVALID_HANDLE_VALUE);
            }
            VFSResult<void> Close()
            {
                const auto previous = Release();
                return previous == INVALID_HANDLE_VALUE || CloseHandle(previous) ? VFSResult<void>::Ok() : VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
        };

        struct NativeFunctions
        {
            decltype(&::NtCreateFile)          Create         = nullptr;
            Detail::NativeSetInformationFn     SetInformation = nullptr;
            decltype(&::RtlNtStatusToDosError) StatusToError  = nullptr;

            bool                               Initialize()
            {
                // ntdll is already loaded in every Windows process. No loader
                // allocation, optional library lifetime, or per-call cache.
                const auto module = GetModuleHandleW(L"ntdll.dll");
                if (!module)
                {
                    return false;
                }
                Create         = reinterpret_cast<decltype(Create)>(GetProcAddress(module, "NtCreateFile"));
                SetInformation = reinterpret_cast<decltype(SetInformation)>(GetProcAddress(module, "NtSetInformationFile"));
                StatusToError  = reinterpret_cast<decltype(StatusToError)>(GetProcAddress(module, "RtlNtStatusToDosError"));
                return Create && SetInformation && StatusToError;
            }

            VFSResult<void> Open(HANDLE parent, const wchar_t* name, size_t length, ACCESS_MASK access, ULONG share, ULONG disposition, ULONG kind, NativeHandle& output) const
            {
                if (!Create || !StatusToError)
                {
                    return VFSResult<void>::Fail(VFSError::Unsupported);
                }
                if (!name || !length || length >= VFS_MAX_PATH)
                {
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                }
                // NtCreateFile borrows a counted, read-only name. Enumeration
                // records need not have a trailing NUL, nor a copied name buffer.
                UNICODE_STRING    text       = {.Length = static_cast<USHORT>(length * sizeof(wchar_t)), .MaximumLength = static_cast<USHORT>(length * sizeof(wchar_t)), .Buffer = const_cast<wchar_t*>(name)};
                OBJECT_ATTRIBUTES attributes = {.Length = sizeof(attributes), .RootDirectory = parent, .ObjectName = &text, .Attributes = OBJ_CASE_INSENSITIVE | OBJ_DONT_REPARSE};
                IO_STATUS_BLOCK   io         = {};
                HANDLE            handle     = INVALID_HANDLE_VALUE;
                // Match CreateFileW(FILE_FLAG_BACKUP_SEMANTICS) for directory
                // anchors. Without backup intent, retained descendant handles
                // can prevent NTFS ancestor renames even with shared deletion.
                // This does not enable any backup/restore token privileges.
                if (kind & FILE_DIRECTORY_FILE)
                {
                    kind |= FILE_OPEN_FOR_BACKUP_INTENT;
                }
                const auto status = Create(&handle, access, &attributes, &io, nullptr, FILE_ATTRIBUTE_NORMAL, share, disposition, kind | FILE_SYNCHRONOUS_IO_NONALERT | FILE_OPEN_REPARSE_POINT, nullptr, 0);
                if (status < 0)
                {
                    if (handle != INVALID_HANDLE_VALUE && handle)
                    {
                        (void) CloseHandle(handle);
                    }
                    // Rtl's DOS mapping loses the file/directory distinction.
                    if (static_cast<ULONG>(status) == 0xc00000baul) // STATUS_FILE_IS_A_DIRECTORY
                    {
                        return VFSResult<void>::Fail(VFSError::NotAFile);
                    }
                    return VFSResult<void>::Fail(Detail::NativeWindowsError(StatusToError(status)));
                }
                if (!handle || handle == INVALID_HANDLE_VALUE)
                {
                    return VFSResult<void>::Fail(VFSError::IOError);
                }
                output.Reset(handle);
                return VFSResult<void>::Ok();
            }

            VFSResult<void> Rename(HANDLE file, HANDLE parent, const wchar_t* name, size_t length, bool replace) const
            {
                return Detail::RenameWindowsHandle(SetInformation, StatusToError, file, parent, name, length, replace);
            }
        };

        struct NativeEntry
        {
            VFSFileStat     Stat     = {};
            VFSFileIdentity Identity = {};
        };

        VFSResult<NativeEntry> InspectNative(HANDLE handle)
        {
            const auto stat = Detail::NativeStat(handle);
            if (stat.Failed())
            {
                return VFSResult<NativeEntry>::Fail(stat.Error());
            }
            const auto identity = Detail::NativeIdentity(handle);
            return identity.Succeeded() ? VFSResult<NativeEntry>::Ok({stat.Value(), identity.Value()}) : VFSResult<NativeEntry>::Fail(identity.Error());
        }

        VFSResult<NativeEntry> InspectNative(HANDLE handle, uint64_t volume)
        {
            auto entry = InspectNative(handle);
            if (entry.Succeeded() && entry.Value().Identity.Volume != volume)
            {
                return VFSResult<NativeEntry>::Fail(VFSError::PermissionDenied);
            }
            return entry;
        }

        VFSResult<size_t> ToWide(Containers::StringView text, wchar_t (&output)[VFS_MAX_PATH])
        {
            if (text.empty() || text.size() >= VFS_MAX_PATH)
            {
                return VFSResult<size_t>::Fail(VFSError::InvalidPath);
            }
            const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), output, VFS_MAX_PATH - 1);
            if (length <= 0)
            {
                return VFSResult<size_t>::Fail(VFSError::InvalidPath);
            }
            output[length] = L'\0';
            return VFSResult<size_t>::Ok(static_cast<size_t>(length));
        }

        VFSResult<void> Duplicate(HANDLE handle, NativeHandle& output)
        {
            HANDLE duplicate = INVALID_HANDLE_VALUE;
            if (!DuplicateHandle(GetCurrentProcess(), handle, GetCurrentProcess(), &duplicate, 0, FALSE, DUPLICATE_SAME_ACCESS))
            {
                return VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
            output.Reset(duplicate);
            return VFSResult<void>::Ok();
        }

        VFSResult<void> WalkDirectories(const NativeFunctions& functions, HANDLE root, uint64_t volume, cstring path, size_t length, bool create, NativeHandle& output)
        {
            const auto duplicate = Duplicate(root, output);
            if (duplicate.Failed())
            {
                return duplicate;
            }
            for (size_t offset = 0; offset < length;)
            {
                size_t end = offset;
                while (end < length && path[end] != '/')
                {
                    ++end;
                }
                wchar_t    component[VFS_MAX_PATH] = {};
                const auto converted               = ToWide({path + offset, end - offset}, component);
                if (converted.Failed())
                {
                    return VFSResult<void>::Fail(converted.Error());
                }
                NativeHandle child  = {};
                const auto   opened = functions.Open(output.Value, component, converted.Value(), DirectoryAccess, SharedMetadata, create ? FILE_OPEN_IF : FILE_OPEN, FILE_DIRECTORY_FILE, child);
                if (opened.Failed())
                {
                    return opened;
                }
                const auto checked = InspectNative(child.Value, volume);
                if (checked.Failed())
                {
                    return VFSResult<void>::Fail(checked.Error());
                }
                if (!checked.Value().Stat.IsDirectory)
                {
                    return VFSResult<void>::Fail(VFSError::NotADirectory);
                }
                output.Reset(child.Release());
                offset = end + 1;
            }
            return VFSResult<void>::Ok();
        }

        struct NativeParent
        {
            NativeHandle Directory          = {};
            wchar_t      Leaf[VFS_MAX_PATH] = {};
            size_t       Length             = 0;
        };

        VFSResult<void> ResolveParent(const NativeFunctions& functions, HANDLE root, uint64_t volume, const VFSRelativePath& path, NativeParent& output)
        {
            if (!path.IsValid())
            {
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            }
            size_t offset = path.Length();
            while (offset && path.CStr()[offset - 1] != '/')
            {
                --offset;
            }
            const auto walked = WalkDirectories(functions, root, volume, path.CStr(), offset ? offset - 1 : 0, false, output.Directory);
            if (walked.Failed())
            {
                return walked;
            }
            const auto converted = ToWide({path.CStr() + offset, path.Length() - offset}, output.Leaf);
            if (converted.Failed())
            {
                return VFSResult<void>::Fail(converted.Error());
            }
            output.Length = converted.Value();
            return VFSResult<void>::Ok();
        }

        VFSResult<NativeEntry> OpenMetadata(const NativeFunctions& functions, HANDLE directory, const wchar_t* name, size_t length, uint64_t volume, NativeHandle& output, ACCESS_MASK access = FILE_READ_ATTRIBUTES | SYNCHRONIZE)
        {
            const auto opened = functions.Open(directory, name, length, access, SharedMetadata, FILE_OPEN, 0, output);
            return opened.Succeeded() ? InspectNative(output.Value, volume) : VFSResult<NativeEntry>::Fail(opened.Error());
        }

        VFSResult<NativeEntry> OpenMetadata(const NativeFunctions& functions, NativeParent& parent, uint64_t volume, NativeHandle& output, ACCESS_MASK access = FILE_READ_ATTRIBUTES | SYNCHRONIZE)
        {
            return OpenMetadata(functions, parent.Directory.Value, parent.Leaf, parent.Length, volume, output, access);
        }

        struct DirectoryEntry
        {
            const wchar_t* Name     = nullptr;
            size_t         Length   = 0;
            FILE_ID_128    Identity = {};
        };

        template <typename Visitor>
        VFSResult<void> VisitDirectoryEntries(HANDLE directory, Containers::ArrayView<uint8_t> storage, Visitor&& visitor)
        {
            if (!storage.data() || storage.size() < ZKilo(4) || storage.size() > ZKilo(64))
            {
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            }
            // The native query requires 8-byte alignment. Adapt caller-owned
            // byte storage without allocating or requiring a stronger API type.
            const auto   address     = reinterpret_cast<uintptr_t>(storage.data());
            const size_t padding     = (alignof(FILE_ID_EXTD_DIR_INFO) - address % alignof(FILE_ID_EXTD_DIR_INFO)) % alignof(FILE_ID_EXTD_DIR_INFO);
            auto         buffer      = storage.subspan(padding);
            auto         information = FileIdExtdDirectoryRestartInfo;
            for (;;)
            {
                if (!GetFileInformationByHandleEx(directory, information, buffer.data(), static_cast<DWORD>(buffer.size())))
                {
                    const auto error = GetLastError();
                    return error == ERROR_NO_MORE_FILES ? VFSResult<void>::Ok() : VFSResult<void>::Fail(Detail::NativeWindowsError(error));
                }
                information = FileIdExtdDirectoryInfo;
                for (size_t offset = 0;;)
                {
                    constexpr size_t prefix = offsetof(FILE_ID_EXTD_DIR_INFO, FileName);
                    if (offset > buffer.size() || buffer.size() - offset < prefix)
                    {
                        return VFSResult<void>::Fail(VFSError::Corrupted);
                    }
                    FILE_ID_EXTD_DIR_INFO header = {};
                    (void) Helpers::secure_memcpy(&header, sizeof(header), buffer.data() + offset, prefix);
                    const size_t available = buffer.size() - offset;
                    const size_t record    = header.NextEntryOffset ? header.NextEntryOffset : available;
                    if (record < prefix || record > available || !header.FileNameLength || header.FileNameLength % sizeof(wchar_t) || header.FileNameLength > record - prefix || (header.NextEntryOffset && header.NextEntryOffset % alignof(FILE_ID_EXTD_DIR_INFO)))
                    {
                        return VFSResult<void>::Fail(VFSError::Corrupted);
                    }
                    const DirectoryEntry entry   = {reinterpret_cast<const wchar_t*>(buffer.data() + offset + prefix), header.FileNameLength / sizeof(wchar_t), header.FileId};
                    const auto           visited = visitor(entry);
                    if (visited.Failed())
                    {
                        return visited;
                    }
                    if (!header.NextEntryOffset)
                    {
                        break;
                    }
                    offset += header.NextEntryOffset;
                }
            }
        }

        bool IsDot(const DirectoryEntry& entry)
        {
            return entry.Length == 1 && entry.Name[0] == L'.';
        }

        bool IsParent(const DirectoryEntry& entry)
        {
            return entry.Length == 2 && entry.Name[0] == L'.' && entry.Name[1] == L'.';
        }

        VFSResult<void> OpenDirectoryByIdentity(HANDLE hint, const FILE_ID_128& identity, FILE_ID_TYPE identity_type, NativeHandle& output)
        {
            FILE_ID_DESCRIPTOR descriptor = {};
            descriptor.dwSize             = sizeof(descriptor);
            descriptor.Type               = identity_type;
            if (identity_type == FileIdType)
            {
                // NTFS uses the full reference, including its sequence number.
                // ExtendedFileIdType is the ReFS profile; never truncate it.
                uint64_t high = 0;
                (void) Helpers::secure_memcpy(&high, sizeof(high), identity.Identifier + sizeof(descriptor.FileId), sizeof(high));
                if (high)
                {
                    return VFSResult<void>::Fail(VFSError::Corrupted);
                }
                (void) Helpers::secure_memcpy(&descriptor.FileId, sizeof(descriptor.FileId), identity.Identifier, sizeof(descriptor.FileId));
            }
            else
            {
                descriptor.ExtendedFileId = identity;
            }
            const auto opened = OpenFileById(hint, &descriptor, DirectoryAccess, SharedMetadata, nullptr, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT);
            if (opened == INVALID_HANDLE_VALUE)
            {
                return VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
            output.Reset(opened);
            return VFSResult<void>::Ok();
        }

        VFSResult<void> ReopenDirectory(HANDLE handle, FILE_ID_TYPE identity_type, NativeHandle& output)
        {
            FILE_ID_INFO identity = {};
            if (!GetFileInformationByHandleEx(handle, FileIdInfo, &identity, sizeof(identity)))
            {
                return VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
            // ReOpenFile's documented input is a CreateFile handle. Our native
            // anchors also come from NtCreateFile and OpenFileById. Reopening by
            // ID works for all of them and creates an independent enumeration
            // cursor, unlike DuplicateHandle, without resolving a saved name.
            return OpenDirectoryByIdentity(handle, identity.FileId, identity_type, output);
        }

        VFSResult<bool> IsVolumeRoot(HANDLE handle)
        {
            struct RootName
            {
                DWORD   Length  = 0;
                wchar_t Name[2] = {};
            } name = {};
            static_assert(offsetof(RootName, Name) == offsetof(FILE_NAME_INFO, FileName));
            if (!GetFileInformationByHandleEx(handle, FileNameInfo, &name, sizeof(name)))
            {
                const auto error = GetLastError();
                return error == ERROR_MORE_DATA || error == ERROR_INSUFFICIENT_BUFFER ? VFSResult<bool>::Ok(false) : VFSResult<bool>::Fail(Detail::NativeWindowsError(error));
            }
            return VFSResult<bool>::Ok(name.Length == sizeof(wchar_t) && name.Name[0] == L'\\');
        }

        VFSResult<void> OpenParentByIdentity(HANDLE handle, uint64_t volume, FILE_ID_TYPE identity_type, NativeHandle& output)
        {
            NativeHandle directory = {};
            const auto   reopened  = ReopenDirectory(handle, identity_type, directory);
            if (reopened.Failed())
            {
                return reopened;
            }
            Containers::FixedArray<uint8_t, ZKilo(4)> buffer    = {};
            FILE_ID_128                               parent_id = {};
            bool                                      found     = false;
            uint32_t                                  entries   = 0;
            const auto                                visited   = VisitDirectoryEntries(directory.Value, buffer, [&](const DirectoryEntry& entry) {
                if (++entries > 4096)
                {
                    return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
                }
                if (!IsParent(entry))
                {
                    return VFSResult<void>::Ok();
                }
                parent_id = entry.Identity;
                found     = true;
                // Stop without scanning/allocating an entire ancestor directory.
                return VFSResult<void>::Fail(VFSError::Cancelled);
            });
            if (!found)
            {
                return visited.Failed() ? visited : VFSResult<void>::Fail(VFSError::Unsupported);
            }
            NativeHandle pending = {};
            const auto   opened  = OpenDirectoryByIdentity(handle, parent_id, identity_type, pending);
            if (opened.Failed())
            {
                return opened;
            }
            const auto checked = InspectNative(pending.Value, volume);
            if (checked.Failed())
            {
                return VFSResult<void>::Fail(checked.Error());
            }
            if (!checked.Value().Stat.IsDirectory)
            {
                return VFSResult<void>::Fail(VFSError::NotADirectory);
            }
            output.Reset(pending.Release());
            return VFSResult<void>::Ok();
        }

        VFSResult<bool> AncestorsContain(HANDLE handle, const VFSFileIdentity& identity, FILE_ID_TYPE identity_type)
        {
            NativeHandle current   = {};
            const auto   duplicate = Duplicate(handle, current);
            if (duplicate.Failed())
            {
                return VFSResult<bool>::Fail(duplicate.Error());
            }
            for (uint32_t depth = 0; depth < 1024; ++depth)
            {
                const auto own = Detail::NativeIdentity(current.Value);
                if (own.Failed())
                {
                    return VFSResult<bool>::Fail(own.Error());
                }
                if (own.Value() == identity)
                {
                    return VFSResult<bool>::Ok(true);
                }
                const auto root = IsVolumeRoot(current.Value);
                if (root.Failed())
                {
                    return VFSResult<bool>::Fail(root.Error());
                }
                if (root.Value())
                {
                    return VFSResult<bool>::Ok(false);
                }
                NativeHandle parent = {};
                const auto   opened = OpenParentByIdentity(current.Value, own.Value().Volume, identity_type, parent);
                if (opened.Failed())
                {
                    return VFSResult<bool>::Fail(opened.Error());
                }
                const auto parent_identity = Detail::NativeIdentity(parent.Value);
                if (parent_identity.Failed())
                {
                    return VFSResult<bool>::Fail(parent_identity.Error());
                }
                if (parent_identity.Value() == own.Value())
                {
                    return VFSResult<bool>::Ok(false);
                }
                current.Reset(parent.Release());
            }
            return VFSResult<bool>::Fail(VFSError::SizeLimitExceeded);
        }

        VFSResult<void> CheckFilesystem(HANDLE directory, FILE_ID_TYPE& identity_type)
        {
            // Native local NTFS/ReFS rename and identity semantics are the
            // selected Windows profile. Do not advertise these guarantees for
            // FAT, remote redirects or arbitrary third-party filesystems.
            wchar_t filesystem[16] = {};
            DWORD   flags          = 0;
            if (!GetVolumeInformationByHandleW(directory, nullptr, 0, nullptr, nullptr, &flags, filesystem, static_cast<DWORD>(std::size(filesystem))))
            {
                return VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
            if (!(flags & FILE_SUPPORTS_OPEN_BY_FILE_ID) || (std::wcscmp(filesystem, L"NTFS") != 0 && std::wcscmp(filesystem, L"ReFS") != 0))
            {
                return VFSResult<void>::Fail(VFSError::Unsupported);
            }
            identity_type                    = std::wcscmp(filesystem, L"NTFS") == 0 ? FileIdType : ExtendedFileIdType;
            FILE_REMOTE_PROTOCOL_INFO remote = {};
            if (GetFileInformationByHandleEx(directory, FileRemoteProtocolInfo, &remote, sizeof(remote)))
            {
                if (remote.Protocol != 0)
                {
                    return VFSResult<void>::Fail(VFSError::Unsupported);
                }
            }
            else
            {
                const auto error = GetLastError();
                if (error != ERROR_INVALID_PARAMETER && error != ERROR_INVALID_FUNCTION && error != ERROR_NOT_SUPPORTED)
                {
                    return VFSResult<void>::Fail(Detail::NativeWindowsError(error));
                }
            }
            NativeHandle reopened = {};
            const auto   opened   = ReopenDirectory(directory, identity_type, reopened);
            if (opened.Failed())
            {
                return opened;
            }
            Containers::FixedArray<uint8_t, ZKilo(4)> buffer = {};
            // Probe the actual bounded query without scanning a large directory
            // or changing its retained enumeration offset. Empty is valid too.
            const auto                                query  = VisitDirectoryEntries(reopened.Value, buffer, [](const DirectoryEntry&) { return VFSResult<void>::Fail(VFSError::Cancelled); });
            return query.Failed() && query.Error() != VFSError::Cancelled ? query : VFSResult<void>::Ok();
        }
    } // namespace

    struct VFSDiskRoot final : IVFSRootOperations, Memory::NativePageObject<VFSDiskRoot>
    {
        static constexpr size_t                              MaxOpenFiles     = 32;
        Helpers::Ref<IVFSRootProvider>                       Provider         = {};
        NativeFunctions                                      Functions        = {};
        NativeHandle                                         Anchor           = {};
        uint64_t                                             Volume           = 0;
        FILE_ID_TYPE                                         IdentityType     = FileIdType;
        VFSPublicationCapabilities                           RootCapabilities = {};
        Containers::FixedArray<VFSDiskFile, MaxOpenFiles>    Files            = {};
        Containers::FixedArray<bool, MaxOpenFiles>           Used             = {};
        std::mutex                                           FileMutex        = {};
        VFSRootAccess                                        Access           = VFSRootAccess::ReadOnly;

        static VFSResult<Helpers::Scope<IVFSRootOperations>> Create(IVFSRootProvider& provider, const NativeFunctions& functions, NativeHandle& anchor, uint64_t volume, FILE_ID_TYPE identity_type, VFSRootAccess access, VFSPublicationCapabilities capabilities)
        {
            using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
            Helpers::Scope<VFSDiskRoot> root(new (std::nothrow) VFSDiskRoot());
            if (!root)
            {
                return Result::Fail(VFSError::OutOfMemory);
            }
            root->Provider  = &provider;
            root->Functions = functions;
            root->Anchor.Reset(anchor.Release());
            root->Volume           = volume;
            root->IdentityType     = identity_type;
            root->Access           = access;
            root->RootCapabilities = capabilities;
            return Result::Ok(std::move(root));
        }

        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            return Provider->MemoryRequirements();
        }
        VFSPublicationCapabilities Capabilities() const override
        {
            return RootCapabilities;
        }
        VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireChild(const VFSRelativePath& path) override
        {
            if (!path.IsValid())
            {
                return VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(VFSError::InvalidPath);
            }
            NativeHandle anchor = {};
            const auto   walked = WalkDirectories(Functions, Anchor.Value, Volume, path.CStr(), path.Length(), false, anchor);
            return walked.Succeeded() ? Create(*Provider, Functions, anchor, Volume, IdentityType, Access, RootCapabilities) : VFSResult<Helpers::Scope<IVFSRootOperations>>::Fail(walked.Error());
        }

        VFSResult<IVFSFile*> Open(const VFSRelativePath& path, VFSOpenFlags flags) override
        {
            constexpr auto known = VFSOpenFlags::Read | VFSOpenFlags::Write | VFSOpenFlags::Append | VFSOpenFlags::Create | VFSOpenFlags::Truncate | VFSOpenFlags::Exclusive | VFSOpenFlags::AllowReplace;
            const bool     read = HasFlag(flags, VFSOpenFlags::Read), write = HasFlag(flags, VFSOpenFlags::Write);
            const bool     create = HasFlag(flags, VFSOpenFlags::Create), exclusive = HasFlag(flags, VFSOpenFlags::Exclusive);
            if (Access == VFSRootAccess::ReadOnly && write)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
            }
            if ((static_cast<uint32_t>(flags) & ~static_cast<uint32_t>(known)) || (!read && !write) || (HasFlag(flags, VFSOpenFlags::AllowReplace) && flags != (VFSOpenFlags::Read | VFSOpenFlags::AllowReplace)) || (!write && (create || exclusive || HasFlag(flags, VFSOpenFlags::Truncate) || HasFlag(flags, VFSOpenFlags::Append))) || (exclusive && !create))
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
            }
            const auto file_path = VFSPath::Parse(path.CStr(), path.Length());
            if (file_path.Failed())
            {
                return VFSResult<IVFSFile*>::Fail(file_path.Error());
            }
            NativeParent parent   = {};
            const auto   resolved = ResolveParent(Functions, Anchor.Value, Volume, path, parent);
            if (resolved.Failed())
            {
                return VFSResult<IVFSFile*>::Fail(resolved.Error());
            }
            std::lock_guard lock(FileMutex);
            size_t          slot = 0;
            while (slot < MaxOpenFiles && Used[slot])
            {
                ++slot;
            }
            if (slot == MaxOpenFiles)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            NativeHandle handle = {};
            const auto   opened = Functions.Open(parent.Directory.Value, parent.Leaf, parent.Length, FILE_GENERIC_READ | (write ? FILE_GENERIC_WRITE : 0), FILE_SHARE_READ | (HasFlag(flags, VFSOpenFlags::AllowReplace) ? FILE_SHARE_DELETE : 0), exclusive ? FILE_CREATE : create ? FILE_OPEN_IF : FILE_OPEN, 0, handle);
            if (opened.Failed())
            {
                return VFSResult<IVFSFile*>::Fail(opened.Error());
            }
            const auto checked = InspectNative(handle.Value, Volume);
            if (checked.Failed())
            {
                return VFSResult<IVFSFile*>::Fail(checked.Error());
            }
            if (checked.Value().Stat.IsDirectory)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
            }
            uint64_t size = checked.Value().Stat.SizeBytes;
            if (HasFlag(flags, VFSOpenFlags::Truncate))
            {
                FILE_END_OF_FILE_INFO end = {};
                if (!SetFileInformationByHandle(handle.Value, FileEndOfFileInfo, &end, sizeof(end)))
                {
                    return VFSResult<IVFSFile*>::Fail(Detail::NativeWindowsError(GetLastError()));
                }
                size = 0;
            }
            auto& file      = Files[slot];
            file.m_handle   = handle.Release();
            file.m_writable = write;
            file.m_append   = HasFlag(flags, VFSOpenFlags::Append);
            file.m_size     = size;
            file.m_path     = file_path.Value();
            Used[slot]      = true;
            return VFSResult<IVFSFile*>::Ok(&file);
        }

        VFSResult<void> CloseChecked(IVFSFile* file) override
        {
            if (!file)
            {
                return VFSResult<void>::Ok();
            }
            std::lock_guard lock(FileMutex);
            for (size_t i = 0; i < MaxOpenFiles; ++i)
            {
                if (file == &Files[i] && Used[i])
                {
                    const auto closed = Files[i].Close();
                    Used[i]           = false;
                    return closed;
                }
            }
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }

        VFSResult<NativeEntry> Inspect(const VFSRelativePath& path) const
        {
            NativeParent parent   = {};
            const auto   resolved = ResolveParent(Functions, Anchor.Value, Volume, path, parent);
            if (resolved.Failed())
            {
                return VFSResult<NativeEntry>::Fail(resolved.Error());
            }
            NativeHandle handle = {};
            return OpenMetadata(Functions, parent, Volume, handle);
        }
        VFSResult<VFSFileStat> Stat(const VFSRelativePath& path) const override
        {
            const auto entry = Inspect(path);
            return entry.Succeeded() ? VFSResult<VFSFileStat>::Ok(entry.Value().Stat) : VFSResult<VFSFileStat>::Fail(entry.Error());
        }
        VFSResult<VFSFileIdentity> Identity(const VFSRelativePath& path) const override
        {
            const auto entry = Inspect(path);
            return entry.Succeeded() ? VFSResult<VFSFileIdentity>::Ok(entry.Value().Identity) : VFSResult<VFSFileIdentity>::Fail(entry.Error());
        }
        VFSResult<VFSFileIdentity> RootIdentity() const override
        {
            return Detail::NativeIdentity(Anchor.Value);
        }
        VFSResult<bool> Overlaps(const IVFSRootOperations& operations) const override
        {
            const auto* other = dynamic_cast<const VFSDiskRoot*>(&operations);
            if (!other)
            {
                return VFSResult<bool>::Fail(VFSError::Unsupported);
            }
            const auto own = RootIdentity(), theirs = other->RootIdentity();
            if (own.Failed() || theirs.Failed())
            {
                return VFSResult<bool>::Fail(own.Failed() ? own.Error() : theirs.Error());
            }
            if (own.Value().Volume != theirs.Value().Volume)
            {
                return VFSResult<bool>::Ok(false);
            }
            const auto contained = AncestorsContain(other->Anchor.Value, own.Value(), other->IdentityType);
            return contained.Failed() || contained.Value() ? contained : AncestorsContain(Anchor.Value, theirs.Value(), IdentityType);
        }

        VFSResult<void> CreateDir(const VFSRelativePath& path, bool exclusive) override
        {
            if (Access == VFSRootAccess::ReadOnly)
            {
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            }
            if (!path.IsValid())
            {
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            }
            if (!exclusive)
            {
                NativeHandle directory = {};
                return WalkDirectories(Functions, Anchor.Value, Volume, path.CStr(), path.Length(), true, directory);
            }
            NativeParent parent   = {};
            const auto   resolved = ResolveParent(Functions, Anchor.Value, Volume, path, parent);
            if (resolved.Failed())
            {
                return resolved;
            }
            NativeHandle handle  = {};
            const auto   created = Functions.Open(parent.Directory.Value, parent.Leaf, parent.Length, DirectoryAccess, SharedMetadata, FILE_CREATE, FILE_DIRECTORY_FILE, handle);
            if (created.Failed())
            {
                return created;
            }
            const auto checked = InspectNative(handle.Value, Volume);
            return checked.Succeeded() ? handle.Close() : VFSResult<void>::Fail(checked.Error());
        }

        VFSResult<void> Remove(const VFSRelativePath& path) override
        {
            if (Access == VFSRootAccess::ReadOnly)
            {
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            }
            NativeParent parent   = {};
            const auto   resolved = ResolveParent(Functions, Anchor.Value, Volume, path, parent);
            if (resolved.Failed())
            {
                return resolved;
            }
            NativeHandle handle  = {};
            const auto   checked = OpenMetadata(Functions, parent, Volume, handle, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE);
            if (checked.Failed())
            {
                return VFSResult<void>::Fail(checked.Error());
            }
            FILE_DISPOSITION_INFO disposition = {.DeleteFile = TRUE};
            if (!SetFileInformationByHandle(handle.Value, FileDispositionInfo, &disposition, sizeof(disposition)))
            {
                return VFSResult<void>::Fail(Detail::NativeWindowsError(GetLastError()));
            }
            return handle.Close();
        }

        VFSResult<void> Rename(const VFSRelativePath& from, const VFSRelativePath& to, bool replace) override
        {
            if (Access == VFSRootAccess::ReadOnly)
            {
                return VFSResult<void>::Fail(VFSError::PermissionDenied);
            }
            NativeParent source = {}, target = {};
            const auto   resolved_source = ResolveParent(Functions, Anchor.Value, Volume, from, source);
            if (resolved_source.Failed())
            {
                return resolved_source;
            }
            const auto resolved_target = ResolveParent(Functions, Anchor.Value, Volume, to, target);
            if (resolved_target.Failed())
            {
                return resolved_target;
            }
            NativeHandle source_handle = {}, target_handle = {};
            const auto   checked_source = OpenMetadata(Functions, source, Volume, source_handle, DELETE | FILE_READ_ATTRIBUTES | SYNCHRONIZE);
            if (checked_source.Failed())
            {
                return VFSResult<void>::Fail(checked_source.Error());
            }
            const auto checked_target = OpenMetadata(Functions, target, Volume, target_handle);
            if (checked_target.Failed() && checked_target.Error() != VFSError::NotFound)
            {
                return VFSResult<void>::Fail(checked_target.Error());
            }
            const auto closed_target = target_handle.Close();
            if (closed_target.Failed())
            {
                return closed_target;
            }
            // One handle-relative filesystem rename, never copy/delete or an
            // existence-check replacement fallback. The destination is a leaf.
            const auto renamed = Functions.Rename(source_handle.Value, target.Directory.Value, target.Leaf, target.Length, replace);
            if (renamed.Failed())
            {
                return renamed;
            }
            // Commit is the rename itself. A successful publication must not be
            // reported failed merely because this metadata handle's close fails.
            return VFSResult<void>::Ok();
        }

        VFSResult<void> Visit(const VFSRelativePath* path, uint64_t maximum, Containers::ArrayView<uint8_t> buffer, VFSRootVisitorFn visitor, void* context) const override
        {
            if (!visitor || (path && !path->IsValid()) || !buffer.data() || buffer.size() < ZKilo(4) || buffer.size() > ZKilo(64))
            {
                return VFSResult<void>::Fail(VFSError::InvalidPath);
            }
            NativeHandle parent = {}, directory = {};
            const auto   walked = WalkDirectories(Functions, Anchor.Value, Volume, path ? path->CStr() : "", path ? path->Length() : 0, false, parent);
            if (walked.Failed())
            {
                return walked;
            }
            const auto reopened = ReopenDirectory(parent.Value, IdentityType, directory);
            if (reopened.Failed())
            {
                return reopened;
            }
            uint64_t   seen    = 0;
            const auto visited = VisitDirectoryEntries(directory.Value, buffer, [&](const DirectoryEntry& entry) {
                if (IsDot(entry) || IsParent(entry))
                {
                    return VFSResult<void>::Ok();
                }
                if (seen == maximum)
                {
                    return VFSResult<void>::Fail(VFSError::SizeLimitExceeded);
                }
                ++seen;
                if (!entry.Length || entry.Length >= VFS_MAX_PATH)
                {
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                }
                char      name[VFS_MAX_PATH] = {};
                const int bytes              = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, entry.Name, static_cast<int>(entry.Length), name, sizeof(name) - 1, nullptr, nullptr);
                if (bytes <= 0)
                {
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                }
                const auto leaf = VFSRelativePath::Parse(name, static_cast<size_t>(bytes));
                if (leaf.Failed() || leaf.Value().ComponentCount() != 1)
                {
                    return VFSResult<void>::Fail(VFSError::InvalidPath);
                }
                const auto relative = path ? path->Append(leaf.Value()) : leaf;
                if (relative.Failed())
                {
                    return VFSResult<void>::Fail(relative.Error());
                }
                NativeHandle handle  = {};
                const auto   checked = OpenMetadata(Functions, directory.Value, entry.Name, entry.Length, Volume, handle);
                if (checked.Failed())
                {
                    return VFSResult<void>::Fail(checked.Error());
                }
                return visitor(context, {.Path = relative.Value(), .Stat = checked.Value().Stat, .Identity = checked.Value().Identity});
            });
            const auto closed  = directory.Close();
            return visited.Failed() ? visited : closed;
        }
    };

    struct WindowsRootProvider final : IVFSRootProvider, Memory::NativePageObject<WindowsRootProvider>
    {
        wchar_t                              NativeRoot[VFS_MAX_PATH] = {};
        VFSBackendCaps                       Caps                     = VFSBackendCaps::None;
        NativeFunctions                      Functions                = {};
        NativeHandle                         Anchor                   = {};

        VFSResult<VFSRootMemoryRequirements> MemoryRequirements() const override
        {
            const auto root_bytes     = VFSDiskRoot::NativeStorageBytes();
            const auto provider_bytes = NativeStorageBytes();
            if (!root_bytes || !provider_bytes)
            {
                return VFSResult<VFSRootMemoryRequirements>::Fail(VFSError::Unsupported);
            }
            return VFSResult<VFSRootMemoryRequirements>::Ok({
                .StateBytes = root_bytes, .Storage = {this, provider_bytes}
            });
        }

        VFSResult<Helpers::Scope<IVFSRootOperations>> AcquireRoot(const VFSPath& relative, VFSRootAccess access) override
        {
            using Result = VFSResult<Helpers::Scope<IVFSRootOperations>>;
            if (!HasCap(Caps, VFSBackendCaps::Read) || (access != VFSRootAccess::ReadOnly && !HasCap(Caps, VFSBackendCaps::Write)))
            {
                return Result::Fail(VFSError::Unsupported);
            }
            if (Anchor.Value == INVALID_HANDLE_VALUE)
            {
                const auto handle = CreateFileW(NativeRoot, DirectoryAccess, SharedMetadata, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
                if (handle == INVALID_HANDLE_VALUE)
                {
                    return Result::Fail(Detail::NativeWindowsError(GetLastError()));
                }
                Anchor.Reset(handle);
            }
            const auto own = InspectNative(Anchor.Value);
            if (own.Failed())
            {
                return Result::Fail(own.Error());
            }
            if (!own.Value().Stat.IsDirectory)
            {
                return Result::Fail(VFSError::NotADirectory);
            }
            NativeHandle root   = {};
            const auto   walked = WalkDirectories(Functions, Anchor.Value, own.Value().Identity.Volume, relative.CStr() + 1, relative.Length() - 1, false, root);
            if (walked.Failed())
            {
                return Result::Fail(walked.Error());
            }
            FILE_ID_TYPE identity_type = FileIdType;
            const auto   supported     = CheckFilesystem(root.Value, identity_type);
            if (supported.Failed())
            {
                return Result::Fail(supported.Error());
            }
            return VFSDiskRoot::Create(*this, Functions, root, own.Value().Identity.Volume, identity_type, access, {.BackendIdentity = BackendIdentity(), .Guarantees = access == VFSRootAccess::ReadOnly ? VFS_READ_GUARANTEES : VFS_PUBLICATION_GUARANTEES | VFS_READ_GUARANTEES});
        }
    };

    Helpers::Ref<IVFSRootProvider> CreateWindowsDiskRootProvider(cstring native_root, VFSBackendCaps caps)
    {
        if (!native_root)
        {
            return {};
        }
        size_t length = 0;
        while (length < VFS_MAX_PATH && native_root[length])
        {
            ++length;
        }
        Helpers::Ref<WindowsRootProvider> provider(new (std::nothrow) WindowsRootProvider());
        if (!provider || !provider->BackendIdentity() || !provider->Functions.Initialize() || ToWide({native_root, length}, provider->NativeRoot).Failed())
        {
            return {};
        }
        provider->Caps = caps;
        return provider;
    }
} // namespace ZEngine::Core::VFS
#endif
