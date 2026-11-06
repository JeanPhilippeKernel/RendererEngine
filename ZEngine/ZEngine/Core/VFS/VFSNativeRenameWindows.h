#pragma once
#if defined(_WIN32)
#include <ZEngine/Core/VFS/VFSNativeStat.h>
#include <winternl.h>
#include <cstddef>
#include <new>

namespace ZEngine::Core::VFS::Detail
{
    // The desktop SDK omits NtSetInformationFile and most native information
    // classes. Use its documented 32-bit ABI, not the truncated SDK enum.
    using NativeSetInformationFn = NTSTATUS(NTAPI*)(HANDLE, PIO_STATUS_BLOCK, PVOID, ULONG, ULONG);

    inline VFSResult<void> RenameWindowsHandle(NativeSetInformationFn set_information, decltype(&::RtlNtStatusToDosError) status_to_error, HANDLE file, HANDLE parent, const wchar_t* name, size_t length, bool replace)
    {
        if (!set_information || !status_to_error)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        if (!name || !length || length >= VFS_MAX_PATH)
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        // A counted leaf relative to a retained directory. No Win32 DOS-path
        // conversion, copy/delete, existence-check fallback, or rename retry.
        struct NativeRenameInformation
        {
            ULONG  Flags          = 0;
            HANDLE RootDirectory  = nullptr;
            ULONG  FileNameLength = 0;
            WCHAR  FileName[1]    = {};
        };
        static_assert(offsetof(NativeRenameInformation, RootDirectory) == offsetof(FILE_RENAME_INFO, RootDirectory));
        static_assert(offsetof(NativeRenameInformation, FileNameLength) == offsetof(FILE_RENAME_INFO, FileNameLength));
        static_assert(offsetof(NativeRenameInformation, FileName) == offsetof(FILE_RENAME_INFO, FileName));
        alignas(NativeRenameInformation) uint8_t storage[sizeof(NativeRenameInformation) + VFS_MAX_PATH * sizeof(wchar_t)] = {};
        auto*                                    rename                                                                    = new (storage) NativeRenameInformation{};
        // FileRenameInformationEx requires both flags to preserve open readers'
        // old object. FILE_SHARE_DELETE alone is insufficient for legacy rename.
        constexpr ULONG                          replace_if_exists                                                         = 0x00000001;
        constexpr ULONG                          posix_semantics                                                           = 0x00000002;
        rename->Flags                                                                                                      = replace ? replace_if_exists | posix_semantics : 0;
        rename->RootDirectory                                                                                              = parent;
        rename->FileNameLength                                                                                             = static_cast<ULONG>(length * sizeof(wchar_t));
        (void) Helpers::secure_memcpy(storage + offsetof(NativeRenameInformation, FileName), sizeof(storage) - offsetof(NativeRenameInformation, FileName), name, rename->FileNameLength);
        IO_STATUS_BLOCK io          = {};
        // FileRenameInformationEx is class 65. Class 10 handles no-replace;
        // zero flags also mean ReplaceIfExists=false in its legacy layout.
        const ULONG     information = replace ? 65 : 10;
        // Include the fixed header even when the short leaf ends in its padding.
        const auto      status      = set_information(file, &io, storage, static_cast<ULONG>(sizeof(NativeRenameInformation) + rename->FileNameLength), information);
        return status < 0 ? VFSResult<void>::Fail(NativeWindowsError(status_to_error(status))) : VFSResult<void>::Ok();
    }
} // namespace ZEngine::Core::VFS::Detail
#endif
