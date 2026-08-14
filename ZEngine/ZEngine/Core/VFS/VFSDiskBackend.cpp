#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <chrono>
#include <filesystem>

namespace ZEngine::Core::VFS
{
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
        VFSFileStat stat;
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
        HANDLE mapping = CreateFileMappingA(m_handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
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

        m_caps           = caps;
        m_arena          = arena;
        m_case_sensitive = ProbeCaseSensitivity(m_native_root);

        m_file_pool.Initialize(m_arena, sizeof(VFSDiskFile) * max_open_files, sizeof(VFSDiskFile));
    }

    bool VFSDiskBackend::ResolveNativePath(const VFSPath& relative, char* out_buf) const
    {
        char native[MAX_FILE_PATH_COUNT] = {};
        relative.ToNative(native, sizeof(native));

        const size_t native_len = Helpers::secure_strlen(native);
        if (m_native_root_len + native_len + 1 > MAX_FILE_PATH_COUNT)
        {
            return false;
        }

        Helpers::secure_memcpy(out_buf, MAX_FILE_PATH_COUNT, m_native_root, m_native_root_len);
        Helpers::secure_memcpy(out_buf + m_native_root_len, MAX_FILE_PATH_COUNT - m_native_root_len, native, native_len);
        out_buf[m_native_root_len + native_len] = '\0';
        return true;
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

        std::error_code ec;
        return !std::filesystem::exists(probe, ec);
#else
        (void) native_root;
        return true;
#endif
    }

    VFSResult<IVFSFile*> VFSDiskBackend::Open(const VFSPath& relative_path, VFSOpenFlags flags)
    {
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        }

        const bool wants_write = HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Append);
        if (wants_write && !HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        }

        void* mem = m_file_pool.Allocate();
        if (!mem)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
        }
        VFSDiskFile* file = ZConstruct(mem, VFSDiskFile);
        file->Owner       = this;
        file->m_path      = relative_path;
        file->m_writable  = wants_write;

#if defined(_WIN32)
        const DWORD access   = GENERIC_READ | (wants_write ? GENERIC_WRITE : 0);
        const DWORD creation = wants_write ? OPEN_ALWAYS : OPEN_EXISTING;
        file->m_handle       = CreateFileA(native, access, FILE_SHARE_READ, nullptr, creation, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file->m_handle == INVALID_HANDLE_VALUE)
        {
            file->~VFSDiskFile();
            m_file_pool.Free(file);
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        LARGE_INTEGER sz;
        if (GetFileSizeEx(file->m_handle, &sz))
        {
            file->m_size = static_cast<uint64_t>(sz.QuadPart);
        }
#else
        int oflags = wants_write ? O_RDWR : O_RDONLY;
        if (HasFlag(flags, VFSOpenFlags::Write))
        {
            oflags |= O_CREAT;
        }
        if (HasFlag(flags, VFSOpenFlags::Append))
        {
            oflags |= O_CREAT | O_APPEND;
        }
        file->m_fd = ::open(native, oflags, 0644);
        if (file->m_fd < 0)
        {
            file->~VFSDiskFile();
            m_file_pool.Free(file);
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        struct stat st;
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
        m_file_pool.Free(file);
    }

    VFSResult<VFSFileStat> VFSDiskBackend::Stat(const VFSPath& path) const
    {
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(path, native))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::InvalidPath);
        }

        std::error_code              ec;
        std::filesystem::file_status status = std::filesystem::status(native, ec);
        if (ec || !std::filesystem::exists(status))
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotFound);
        }

        VFSFileStat stat;
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
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(path, native))
        {
            return false;
        }
        std::error_code ec;
        return std::filesystem::exists(native, ec) && !ec;
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSDiskBackend::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {
        using ResultT                    = VFSResult<Core::Containers::Array<VFSDirEntry>>;

        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(dir, native))
        {
            return ResultT::Fail(VFSError::InvalidPath);
        }

        std::error_code ec;
        if (!std::filesystem::is_directory(native, ec) || ec)
        {
            return ResultT::Fail(VFSError::NotADirectory);
        }

        Core::Containers::Array<VFSDirEntry> entries;
        entries.init(arena, 16);

        for (const std::filesystem::directory_entry& de : std::filesystem::directory_iterator(native, ec))
        {
            if (ec)
            {
                break;
            }

            VFSResult<VFSPath> child = dir.Append(de.path().filename().string().c_str());
            if (child.Failed())
            {
                continue;
            }

            VFSDirEntry entry;
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

        return ResultT::Ok(std::move(entries));
    }

    VFSResult<void> VFSDiskBackend::CreateDir(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec;
        std::filesystem::create_directories(native, ec);
        if (ec)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::Remove(const VFSPath& relative_path)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec;
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
        char native[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(relative_path, native))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec;
        std::filesystem::remove_all(native, ec);
        return ec ? VFSResult<void>::Fail(VFSError::IOError) : VFSResult<void>::Ok();
    }

    VFSResult<void> VFSDiskBackend::Rename(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        if (!HasCap(m_caps, VFSBackendCaps::Write))
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }
        char native_src[MAX_FILE_PATH_COUNT] = {};
        char native_dst[MAX_FILE_PATH_COUNT] = {};
        if (!ResolveNativePath(rel_src, native_src) || !ResolveNativePath(rel_dst, native_dst))
        {
            return VFSResult<void>::Fail(VFSError::PermissionDenied);
        }
        std::error_code ec;
        std::filesystem::rename(native_src, native_dst, ec);
        if (ec)
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        return VFSResult<void>::Ok();
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
