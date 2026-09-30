#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#if defined(__linux__)
#include <linux/fs.h>
#include <sys/syscall.h>
#endif

namespace ZEngine::Core::VFS
{
    namespace
    {
#if defined(_WIN32)
        bool WindowsPathFromUtf8(cstring text, wchar_t* output)
        {
            size_t length = 0;
            if (!text || !output)
                return false;
            while (length < MAX_FILE_PATH_COUNT && text[length])
                ++length;
            output[0] = L'\0';
            // The byte budget is unchanged. UTF-16 cannot exceed that many
            // code units; include the terminator and reject malformed input.
            return length > 0 && length < MAX_FILE_PATH_COUNT && MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, static_cast<int>(length + 1), output, MAX_FILE_PATH_COUNT) > 0;
        }
#endif

        bool FilenameToUtf8(const std::filesystem::path& path, char (&output)[MAX_FILE_PATH_COUNT])
        {
            const auto  filename = path.filename();
            const auto& name     = filename.native();
            if (name.empty() || name.size() >= MAX_FILE_PATH_COUNT)
            {
                return false;
            }
#if defined(_WIN32)
            const int count = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, name.data(), static_cast<int>(name.size()), output, MAX_FILE_PATH_COUNT - 1, nullptr, nullptr);
            if (count <= 0)
                return false;
            output[count] = '\0';
#else
            if (Helpers::secure_memcpy(output, sizeof(output), name.data(), name.size()) != Helpers::MEMORY_OP_SUCCESS)
            {
                return false;
            }
            output[name.size()] = '\0';
#endif
            return true;
        }
    } // namespace

    VFSResult<size_t> VFSDiskFile::Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset)
    {
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        OVERLAPPED ov    = {};
        ov.Offset        = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh    = static_cast<DWORD>(offset >> 32);
        DWORD read_bytes = 0;
        if (!ReadFile(m_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read_bytes, &ov))
        {
            if (GetLastError() == ERROR_HANDLE_EOF)
            {
                return VFSResult<size_t>::Ok(0);
            }
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        return VFSResult<size_t>::Ok(static_cast<size_t>(read_bytes));
#else
        if (m_fd < 0)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        const ssize_t n = pread(m_fd, buffer.data(), buffer.size(), static_cast<off_t>(offset));
        if (n < 0)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        return VFSResult<size_t>::Ok(static_cast<size_t>(n));
#endif
    }

    VFSResult<size_t> VFSDiskFile::Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset)
    {
        if (!m_writable)
        {
            return VFSResult<size_t>::Fail(VFSError::Unsupported);
        }
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        OVERLAPPED ov = {};
        ov.Offset     = static_cast<DWORD>(offset & 0xFFFFFFFF);
        ov.OffsetHigh = static_cast<DWORD>(offset >> 32);
        DWORD written = 0;
        if (!WriteFile(m_handle, buffer.data(), static_cast<DWORD>(buffer.size()), &written, &ov))
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        return VFSResult<size_t>::Ok(static_cast<size_t>(written));
#else
        if (m_fd < 0)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        const ssize_t n = pwrite(m_fd, buffer.data(), buffer.size(), static_cast<off_t>(offset));
        if (n < 0)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }
        return VFSResult<size_t>::Ok(static_cast<size_t>(n));
#endif
    }

    VFSResult<uint64_t> VFSDiskFile::Size() const
    {
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<uint64_t>::Fail(VFSError::IOError);
        }
#else
        if (m_fd < 0)
        {
            return VFSResult<uint64_t>::Fail(VFSError::IOError);
        }
#endif
        return VFSResult<uint64_t>::Ok(m_size);
    }

    VFSResult<VFSFileStat> VFSDiskFile::Stat() const
    {
        VFSFileStat stat = {};
        stat.SizeBytes   = m_size;
        stat.IsReadOnly  = !m_writable;
        stat.IsDirectory = false;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    VFSResult<void> VFSDiskFile::Flush()
    {
        if (!m_writable)
        {
            return VFSResult<void>::Ok();
        }
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE || !FlushFileBuffers(m_handle))
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
#else
        if (m_fd < 0 || fsync(m_fd) != 0)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
#endif
        return VFSResult<void>::Ok();
    }

    const VFSPath& VFSDiskFile::Path() const
    {
        return m_path;
    }

    VFSResult<void> VFSDiskFile::Close()
    {
        if (m_mapped_ptr)
        {
            Unmap(m_mapped_ptr, m_mapped_size);
            m_mapped_ptr  = nullptr;
            m_mapped_size = 0;
        }
#if defined(_WIN32)
        if (m_handle != INVALID_HANDLE_VALUE)
        {
            CloseHandle(m_handle);
            m_handle = INVALID_HANDLE_VALUE;
        }
#else
        if (m_fd >= 0)
        {
            ::close(m_fd);
            m_fd = -1;
        }
#endif
        return VFSResult<void>::Ok();
    }

    VFSResult<void*> VFSDiskFile::MemoryMap(uint64_t& out_size)
    {
        if (m_mapped_ptr)
        {
            out_size = m_mapped_size;
            return VFSResult<void*>::Ok(m_mapped_ptr);
        }
#if defined(_WIN32)
        if (m_handle == INVALID_HANDLE_VALUE)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
        HANDLE mapping = CreateFileMappingW(m_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (!mapping)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
        void* ptr = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
        CloseHandle(mapping);
        if (!ptr)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
#else
        if (m_fd < 0 || m_size == 0)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
        void* ptr = mmap(nullptr, m_size, PROT_READ, MAP_PRIVATE, m_fd, 0);
        if (ptr == MAP_FAILED)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
#endif
        m_mapped_ptr  = ptr;
        m_mapped_size = m_size;
        out_size      = m_size;
        return VFSResult<void*>::Ok(ptr);
    }

    void VFSDiskFile::Unmap(void* ptr, uint64_t size)
    {
        if (!ptr)
        {
            return;
        }
#if defined(_WIN32)
        (void) size;
        UnmapViewOfFile(ptr);
#else
        munmap(ptr, size);
#endif
    }

    VFSDiskFile::~VFSDiskFile()
    {
        Close();
    }

    void VFSDiskBackend::Initialize(cstring native_root, VFSBackendCaps caps, Memory::ArenaAllocator* arena, size_t max_open_files)
    {
        m_native_root_len = Helpers::secure_strlen(native_root);
        ZENGINE_VALIDATE_ASSERT(m_native_root_len < MAX_FILE_PATH_COUNT, "VFSDiskBackend native_root exceeds MAX_FILE_PATH_COUNT")
        Helpers::secure_memcpy(m_native_root, MAX_FILE_PATH_COUNT, native_root, m_native_root_len);
        m_native_root[m_native_root_len] = '\0';

        if (m_native_root_len > 1 && (m_native_root[m_native_root_len - 1] == '/' || m_native_root[m_native_root_len - 1] == '\\'))
        {
            --m_native_root_len;
            m_native_root[m_native_root_len] = '\0';
        }

        m_caps                   = caps;
        m_arena                  = arena;
        m_case_sensitive         = ProbeCaseSensitivity(m_native_root);

        const size_t file_stride = Helpers::memory_align_size_t(sizeof(VFSDiskFile), ZAlignof<VFSDiskFile>());
        m_file_pool.Initialize(m_arena, file_stride * max_open_files, file_stride, ZAlignof<VFSDiskFile>());
    }

    bool VFSDiskBackend::ResolveNativePath(const VFSPath& relative, NativePathChar* out_buf) const
    {
        if (!out_buf || !relative.IsValid())
        {
            return false;
        }
        char native[MAX_FILE_PATH_COUNT] = {};
        relative.ToNative(native, sizeof(native));

        const size_t native_len = Helpers::secure_strlen(native);
        if (m_native_root_len + native_len + 1 > MAX_FILE_PATH_COUNT)
        {
            return false;
        }

#if defined(_WIN32)
        char utf8[MAX_FILE_PATH_COUNT] = {};
#else
        char* utf8 = out_buf;
#endif
        if (Helpers::secure_memcpy(utf8, MAX_FILE_PATH_COUNT, m_native_root, m_native_root_len) != Helpers::MEMORY_OP_SUCCESS || Helpers::secure_memcpy(utf8 + m_native_root_len, MAX_FILE_PATH_COUNT - m_native_root_len, native, native_len) != Helpers::MEMORY_OP_SUCCESS)
        {
            return false;
        }
        utf8[m_native_root_len + native_len] = '\0';

        // Defense-in-depth: verify the composed path stays within m_native_root.
        // VFSPath::Parse already rejects '..' segments, but this backend must not rely
        // solely on every upstream caller using Parse — any future code path that
        // constructs a VFSPath from raw bytes could bypass that check.
        if (Helpers::secure_memcmp(utf8, MAX_FILE_PATH_COUNT, m_native_root, sizeof(m_native_root), m_native_root_len) != 0)
        {
            return false;
        }
        const char next = utf8[m_native_root_len];
        if (next != '\0' && next != '/' && next != '\\')
        {
            return false;
        }
#if defined(_WIN32)
        return WindowsPathFromUtf8(utf8, out_buf);
#else
        return true;
#endif
    }

    bool VFSDiskBackend::ProbeCaseSensitivity(cstring native_root)
    {
#if defined(__APPLE__) || defined(_WIN32)
        char probe[MAX_FILE_PATH_COUNT] = {};
        Helpers::secure_strcpy(probe, MAX_FILE_PATH_COUNT, native_root);

        for (size_t i = 0; probe[i] != '\0'; ++i)
        {
            if (probe[i] >= 'a' && probe[i] <= 'z')
            {
                probe[i] = static_cast<char>(probe[i] - 'a' + 'A');
                break;
            }
            if (probe[i] >= 'A' && probe[i] <= 'Z')
            {
                probe[i] = static_cast<char>(probe[i] - 'A' + 'a');
                break;
            }
        }

        std::error_code ec = {};
#if defined(_WIN32)
        wchar_t native_probe[MAX_FILE_PATH_COUNT] = {};
        return !WindowsPathFromUtf8(probe, native_probe) || !std::filesystem::exists(native_probe, ec);
#else
        return !std::filesystem::exists(probe, ec);
#endif
#else
        (void) native_root;
        return true;
#endif
    }

    VFSResult<IVFSFile*> VFSDiskBackend::Open(const VFSPath& relative_path, VFSOpenFlags flags)
    {
        const bool exclusive = HasFlag(flags, VFSOpenFlags::Exclusive);
        if (exclusive && (!HasFlag(flags, VFSOpenFlags::Write) || !HasFlag(flags, VFSOpenFlags::Create)))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::Unsupported);
        }
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        }

        const bool wants_write = HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Append) || HasFlag(flags, VFSOpenFlags::Create) || HasFlag(flags, VFSOpenFlags::Truncate);
        if (wants_write && !HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        }

        void* mem = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_file_pool_mutex);
            if (!m_file_pool.head)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            mem = m_file_pool.Allocate();
        }
        if (!mem)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
        }
        VFSDiskFile* file = ZConstruct<VFSDiskFile>(mem);
        file->Owner       = this;
        file->m_path      = relative_path;
        file->m_writable  = wants_write;

#if defined(_WIN32)
        const DWORD access   = GENERIC_READ | (wants_write ? GENERIC_WRITE : 0);
        DWORD       creation = OPEN_EXISTING;
        if (exclusive)
            creation = CREATE_NEW;
        else if (HasFlag(flags, VFSOpenFlags::Create) && HasFlag(flags, VFSOpenFlags::Truncate))
            creation = CREATE_ALWAYS; // create or truncate
        else if (HasFlag(flags, VFSOpenFlags::Truncate))
            creation = TRUNCATE_EXISTING; // truncate must-exist file
        else if (wants_write || HasFlag(flags, VFSOpenFlags::Create))
            creation = OPEN_ALWAYS; // create if absent, open if present
        else
            creation = OPEN_EXISTING;
        file->m_handle = CreateFileW(native, access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file->m_handle == INVALID_HANDLE_VALUE)
        {
            const DWORD native_error = GetLastError();
            const auto  error        = native_error == ERROR_FILE_EXISTS || native_error == ERROR_ALREADY_EXISTS ? VFSError::AlreadyExists : native_error == ERROR_FILE_NOT_FOUND || native_error == ERROR_PATH_NOT_FOUND ? VFSError::NotFound : native_error == ERROR_ACCESS_DENIED ? VFSError::PermissionDenied : VFSError::IOError;
            file->~VFSDiskFile();
            std::lock_guard<std::mutex> lock(m_file_pool_mutex);
            m_file_pool.Free(file);
            return VFSResult<IVFSFile*>::Fail(error);
        }
        LARGE_INTEGER sz = {};
        if (GetFileSizeEx(file->m_handle, &sz))
        {
            file->m_size = static_cast<uint64_t>(sz.QuadPart);
        }
#else
        int oflags = wants_write ? O_RDWR : O_RDONLY;
        if (HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Create))
        {
            oflags |= O_CREAT;
        }
        if (HasFlag(flags, VFSOpenFlags::Truncate))
        {
            oflags |= O_TRUNC;
        }
        if (HasFlag(flags, VFSOpenFlags::Append))
        {
            oflags |= O_CREAT | O_APPEND;
        }
        if (exclusive)
        {
            oflags |= O_EXCL;
        }
        file->m_fd = ::open(native, oflags, 0644);
        if (file->m_fd < 0)
        {
            const int  native_error = errno;
            const auto error        = native_error == EEXIST ? VFSError::AlreadyExists : native_error == ENOENT ? VFSError::NotFound : native_error == EACCES || native_error == EPERM ? VFSError::PermissionDenied : native_error == ENOTDIR ? VFSError::NotADirectory : native_error == EISDIR ? VFSError::NotAFile : VFSError::IOError;
            file->~VFSDiskFile();
            std::lock_guard<std::mutex> lock(m_file_pool_mutex);
            m_file_pool.Free(file);
            return VFSResult<IVFSFile*>::Fail(error);
        }
        struct stat st = {};
        if (::fstat(file->m_fd, &st) == 0)
        {
            file->m_size = static_cast<uint64_t>(st.st_size);
        }
#endif
        return VFSResult<IVFSFile*>::Ok(file);
    }

    void VFSDiskBackend::Close(IVFSFile* file)
    {
        if (!file)
        {
            return;
        }
        file->~IVFSFile();
        std::lock_guard<std::mutex> lock(m_file_pool_mutex);
        m_file_pool.Free(file);
    }

    VFSResult<VFSFileStat> VFSDiskBackend::Stat(const VFSPath& path) const
    {
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(path, native))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::InvalidPath);
        }

        std::error_code              ec     = {};
        std::filesystem::file_status status = std::filesystem::status(native, ec);
        if (ec || !std::filesystem::exists(status))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotFound);
        }

        VFSFileStat stat = {};
        stat.IsDirectory = std::filesystem::is_directory(status);
        if (!stat.IsDirectory)
        {
            stat.SizeBytes = std::filesystem::file_size(native, ec);
            if (ec)
            {
                stat.SizeBytes = 0;
            }
        }

        const std::filesystem::file_time_type wt = std::filesystem::last_write_time(native, ec);
        if (!ec)
        {
            stat.MTimeNs = std::chrono::duration_cast<std::chrono::nanoseconds>(wt.time_since_epoch()).count();
        }
        stat.IsReadOnly = (status.permissions() & std::filesystem::perms::owner_write) == std::filesystem::perms::none;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    bool VFSDiskBackend::Exists(const VFSPath& path) const
    {
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(path, native))
        {
            return false;
        }
        std::error_code ec = {};
        return std::filesystem::exists(native, ec) && !ec;
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSDiskBackend::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {
        using ResultT                              = VFSResult<Core::Containers::Array<VFSDirEntry>>;

        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(dir, native))
        {
            return ResultT::Fail(VFSError::InvalidPath);
        }

        std::error_code ec = {};
        if (!std::filesystem::is_directory(native, ec) || ec)
        {
            return ResultT::Fail(VFSError::NotADirectory);
        }

        std::filesystem::directory_iterator iterator(native, ec);
        if (ec)
        {
            return ResultT::Fail(VFSError::IOError);
        }
        Core::Containers::Array<VFSDirEntry> entries = {};
        entries.init(arena, 16);
        const std::filesystem::directory_iterator end = {};
        for (; iterator != end; iterator.increment(ec))
        {
            if (ec)
            {
                return ResultT::Fail(VFSError::IOError);
            }
            const auto& de                            = *iterator;
            char        filename[MAX_FILE_PATH_COUNT] = {};
            if (!FilenameToUtf8(de.path(), filename))
            {
                continue;
            }

            VFSResult<VFSPath> child = dir.Append(filename);
            if (child.Failed())
            {
                continue;
            }

            VFSDirEntry entry      = {};
            entry.Path             = child.Value();
            entry.IsDirectory      = de.is_directory(ec);
            entry.Stat.IsDirectory = entry.IsDirectory;
            if (!entry.IsDirectory)
            {
                entry.Stat.SizeBytes = de.file_size(ec);
                if (ec)
                {
                    entry.Stat.SizeBytes = 0;
                }
            }
            entries.push(entry);
        }
        if (ec)
        {
            return ResultT::Fail(VFSError::IOError);
        }

        return ResultT::Ok(std::move(entries));
    }

    VFSResult<void> VFSDiskBackend::CreateDir(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec = {};
        std::filesystem::create_directories(native, ec);
        if (ec)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::CreateDirExclusive(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        if (!relative_path.IsValid())
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        // create_directory issues a single native mkdir, not an Exists + create.
        // Keep recursive, idempotent CreateDir unchanged for importer/cache callers.
        std::error_code error   = {};
        const bool      created = std::filesystem::create_directory(native, error);
        if (!error)
        {
            return created ? VFSResult<void>::Ok() : VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        return VFSResult<void>::Fail(error == std::errc::file_exists ? VFSError::AlreadyExists : error == std::errc::no_such_file_or_directory ? VFSError::NotFound : error == std::errc::not_a_directory ? VFSError::NotADirectory : error == std::errc::permission_denied || error == std::errc::operation_not_permitted ? VFSError::PermissionDenied : VFSError::IOError);
    }

    VFSResult<void> VFSDiskBackend::Remove(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec = {};
        if (!std::filesystem::remove(native, ec) || ec)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::RemoveAll(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        NativePathChar native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec = {};
        std::filesystem::remove_all(native, ec);
        return ec ? VFSResult<void>::Fail(VFSError::IOError) : VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::Rename(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        NativePathChar native_src[MAX_FILE_PATH_COUNT] = {};
        NativePathChar native_dst[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(rel_src, native_src) || !ResolveNativePath(rel_dst, native_dst))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
#if defined(_WIN32)
        if (!MoveFileExW(native_src, native_dst, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
#else
        std::error_code ec = {};
        std::filesystem::rename(native_src, native_dst, ec);
        if (ec)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
#endif
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::RenameNoReplace(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        if (!rel_src.IsValid() || !rel_dst.IsValid() || rel_src.IsRoot() || rel_dst.IsRoot() || rel_src.IsPrefixOf(rel_dst))
        {
            return VFSResult<void>::Fail(VFSError::InvalidPath);
        }
        NativePathChar native_src[MAX_FILE_PATH_COUNT] = {};
        NativePathChar native_dst[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(rel_src, native_src) || !ResolveNativePath(rel_dst, native_dst))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
#if defined(_WIN32)
        if (MoveFileExW(native_src, native_dst, MOVEFILE_WRITE_THROUGH))
            return VFSResult<void>::Ok();
        const DWORD error = GetLastError();
        if (error == ERROR_ACCESS_DENIED && GetFileAttributesW(native_dst) != INVALID_FILE_ATTRIBUTES)
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        return VFSResult<void>::Fail(error == ERROR_ALREADY_EXISTS || error == ERROR_FILE_EXISTS ? VFSError::AlreadyExists : error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? VFSError::NotFound : error == ERROR_ACCESS_DENIED ? VFSError::PermissionDenied : VFSError::IOError);
#else
#if defined(__APPLE__)
        const int result = ::renamex_np(native_src, native_dst, RENAME_EXCL);
#elif defined(__linux__) && defined(SYS_renameat2)
        const long result = ::syscall(SYS_renameat2, AT_FDCWD, native_src, AT_FDCWD, native_dst, RENAME_NOREPLACE);
#else
        return VFSResult<void>::Fail(VFSError::Unsupported);
#endif
#if defined(__APPLE__) || (defined(__linux__) && defined(SYS_renameat2))
        if (result == 0)
        {
            return VFSResult<void>::Ok();
        }
        const int error = errno;
        return VFSResult<void>::Fail(error == EEXIST || error == ENOTEMPTY ? VFSError::AlreadyExists : error == ENOENT ? VFSError::NotFound : error == EACCES || error == EPERM ? VFSError::PermissionDenied : error == ENOSYS || error == ENOTSUP || error == EINVAL ? VFSError::Unsupported : VFSError::IOError);
#endif
#endif
    }

    cstring VFSDiskBackend::BackendType() const
    {
        return "disk";
    }

    VFSBackendCaps VFSDiskBackend::Capabilities() const
    {
        return m_caps;
    }

} // namespace ZEngine::Core::VFS
