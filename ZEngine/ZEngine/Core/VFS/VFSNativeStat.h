#pragma once
#include <ZEngine/Core/VFS/IVFSFile.h>
#include <limits>
#if defined(_WIN32)
#include <Windows.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#else
#include <sys/stat.h>
#endif

namespace ZEngine::Core::VFS::Detail
{
#if defined(_WIN32)
    [[nodiscard]] inline VFSError NativeWindowsError(DWORD error)
    {
        switch (error)
        {
            case ERROR_FILE_NOT_FOUND:
            case ERROR_PATH_NOT_FOUND:
                return VFSError::NotFound;
            case ERROR_FILE_EXISTS:
            case ERROR_ALREADY_EXISTS:
            case ERROR_DIR_NOT_EMPTY:
                return VFSError::AlreadyExists;
            case ERROR_DIRECTORY:
                return VFSError::NotADirectory;
            case ERROR_ACCESS_DENIED:
            case ERROR_SHARING_VIOLATION:
            case ERROR_LOCK_VIOLATION:
            case ERROR_NOT_SAME_DEVICE:
            case ERROR_CANT_ACCESS_FILE:
            case ERROR_CANT_RESOLVE_FILENAME:
            case ERROR_STOPPED_ON_SYMLINK:
            case ERROR_REPARSE_POINT_ENCOUNTERED:
                return VFSError::PermissionDenied;
            case ERROR_INVALID_NAME:
            case ERROR_BAD_PATHNAME:
            case ERROR_FILENAME_EXCED_RANGE:
                return VFSError::InvalidPath;
            case ERROR_NOT_SUPPORTED:
            case ERROR_INVALID_FUNCTION:
            case ERROR_INVALID_PARAMETER:
                return VFSError::Unsupported;
            case ERROR_NOT_ENOUGH_MEMORY:
            case ERROR_OUTOFMEMORY:
            case ERROR_TOO_MANY_OPEN_FILES:
                return VFSError::OutOfMemory;
            default:
                return VFSError::IOError;
        }
    }

    [[nodiscard]] inline VFSResult<VFSFileIdentity> NativeIdentity(HANDLE handle)
    {
        if (!handle || handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<VFSFileIdentity>::Fail(VFSError::IOError);
        }
        FILE_ID_INFO value = {};
        if (!GetFileInformationByHandleEx(handle, FileIdInfo, &value, sizeof(value)))
        {
            return VFSResult<VFSFileIdentity>::Fail(NativeWindowsError(GetLastError()));
        }
        VFSFileIdentity identity = {.Volume = value.VolumeSerialNumber};
        static_assert(sizeof(value.FileId.Identifier) == sizeof(identity.ObjectLow) + sizeof(identity.ObjectHigh));
        (void) Helpers::secure_memcpy(&identity.ObjectLow, sizeof(identity.ObjectLow), value.FileId.Identifier, sizeof(identity.ObjectLow));
        (void) Helpers::secure_memcpy(&identity.ObjectHigh, sizeof(identity.ObjectHigh), value.FileId.Identifier + sizeof(identity.ObjectLow), sizeof(identity.ObjectHigh));
        return identity.IsValid() ? VFSResult<VFSFileIdentity>::Ok(identity) : VFSResult<VFSFileIdentity>::Fail(VFSError::Corrupted);
    }

    [[nodiscard]] inline VFSResult<VFSFileStat> NativeStat(HANDLE handle)
    {
        if (!handle || handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::IOError);
        }
        FILE_BASIC_INFO    basic    = {};
        FILE_STANDARD_INFO standard = {};
        if (!GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic)) || !GetFileInformationByHandleEx(handle, FileStandardInfo, &standard, sizeof(standard)))
        {
            return VFSResult<VFSFileStat>::Fail(NativeWindowsError(GetLastError()));
        }
        if (basic.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::PermissionDenied);
        }
        if ((basic.FileAttributes & FILE_ATTRIBUTE_DEVICE) || GetFileType(handle) != FILE_TYPE_DISK)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotAFile);
        }
        if (standard.EndOfFile.QuadPart < 0 || basic.LastWriteTime.QuadPart < 0)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Corrupted);
        }
        // FILETIME ticks are 100 ns since 1601; the shared VFS uses signed
        // nanoseconds since 1970. Refuse out-of-range times, never wrap them.
        constexpr int64_t epoch_ticks = 116444736000000000ll;
        const int64_t     ticks       = basic.LastWriteTime.QuadPart - epoch_ticks;
        if (ticks > std::numeric_limits<int64_t>::max() / 100 || ticks < std::numeric_limits<int64_t>::min() / 100)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Corrupted);
        }
        return VFSResult<VFSFileStat>::Ok({
            .SizeBytes   = standard.Directory ? 0 : static_cast<uint64_t>(standard.EndOfFile.QuadPart),
            .MTimeNs     = ticks * 100,
            .IsDirectory = standard.Directory != FALSE,
            .IsReadOnly  = (basic.FileAttributes & FILE_ATTRIBUTE_READONLY) != 0,
        });
    }
#else
    [[nodiscard]] inline VFSFileIdentity NativeIdentity(const struct stat& value)
    {
        return {.Volume = static_cast<uint64_t>(value.st_dev), .ObjectLow = static_cast<uint64_t>(value.st_ino)};
    }

    [[nodiscard]] inline VFSResult<VFSFileStat> NativeStat(const struct stat& value)
    {
        if (value.st_size < 0)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Corrupted);
        }
        if (!S_ISREG(value.st_mode) && !S_ISDIR(value.st_mode))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotAFile);
        }
#if defined(__APPLE__)
        const auto time = value.st_mtimespec;
#else
        const auto time = value.st_mtim;
#endif
        constexpr int64_t second = 1'000'000'000;
        if (time.tv_sec > std::numeric_limits<int64_t>::max() / second || time.tv_sec < std::numeric_limits<int64_t>::min() / second || time.tv_nsec < 0 || time.tv_nsec >= second)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Corrupted);
        }
        const auto seconds = static_cast<int64_t>(time.tv_sec) * second;
        if (seconds > std::numeric_limits<int64_t>::max() - time.tv_nsec)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::Corrupted);
        }
        return VFSResult<VFSFileStat>::Ok({.SizeBytes = S_ISDIR(value.st_mode) ? 0 : static_cast<uint64_t>(value.st_size), .MTimeNs = seconds + time.tv_nsec, .IsDirectory = S_ISDIR(value.st_mode), .IsReadOnly = (value.st_mode & S_IWUSR) == 0});
    }
#endif
} // namespace ZEngine::Core::VFS::Detail
