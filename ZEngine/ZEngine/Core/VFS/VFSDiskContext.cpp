#include <Helpers/MemoryOperations.h>
#include <IVFSContext.h>
#include <chrono>
#include <cstdio>
#include <filesystem>

namespace ZEngine::Core::VFS
{
    namespace
    {
        class VFSDiskFile final : public IVFSFile
        {
        public:
            VFSDiskFile(std::FILE* handle, const VFSPath& path) : m_handle(handle), m_path(path) {}

            ~VFSDiskFile() override
            {
                if (m_handle)
                {
                    std::fclose(m_handle);
                }
            }

            VFSResult<size_t> Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset) override
            {
                if (!m_handle || std::fseek(m_handle, static_cast<long>(offset), SEEK_SET) != 0)
                {
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                }
                const size_t read = std::fread(buffer.data(), 1, buffer.size(), m_handle);
                if (read < buffer.size() && std::ferror(m_handle))
                {
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                }
                return VFSResult<size_t>::Ok(read);
            }

            VFSResult<size_t> Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset) override
            {
                if (!m_handle || std::fseek(m_handle, static_cast<long>(offset), SEEK_SET) != 0)
                {
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                }
                const size_t written = std::fwrite(buffer.data(), 1, buffer.size(), m_handle);
                if (written < buffer.size())
                {
                    return VFSResult<size_t>::Fail(VFSError::IOError);
                }
                return VFSResult<size_t>::Ok(written);
            }

            VFSResult<uint64_t> Size() const override
            {
                if (!m_handle)
                {
                    return VFSResult<uint64_t>::Fail(VFSError::IOError);
                }
                const long cur = std::ftell(m_handle);
                std::fseek(m_handle, 0, SEEK_END);
                const long end = std::ftell(m_handle);
                std::fseek(m_handle, cur, SEEK_SET); // restore position
                if (end < 0)
                {
                    return VFSResult<uint64_t>::Fail(VFSError::IOError);
                }
                return VFSResult<uint64_t>::Ok(static_cast<uint64_t>(end));
            }

            VFSResult<VFSFileStat> Stat() const override
            {
                VFSResult<uint64_t> size = Size();
                if (size.Failed())
                {
                    return VFSResult<VFSFileStat>::Fail(size.Error());
                }
                VFSFileStat stat;
                stat.SizeBytes = size.Value();
                return VFSResult<VFSFileStat>::Ok(stat);
            }

            VFSResult<void> Flush() override
            {
                if (m_handle && std::fflush(m_handle) == 0)
                {
                    return VFSResult<void>::Ok();
                }
                return VFSResult<void>::Fail(VFSError::IOError);
            }

            const VFSPath& Path() const override
            {
                return m_path;
            }

        private:
            std::FILE* m_handle = nullptr;
            VFSPath    m_path;
        };

        cstring ToFopenMode(VFSOpenFlags flags)
        {
            if (HasFlag(flags, VFSOpenFlags::Append))
            {
                return "ab";
            }
            if (HasFlag(flags, VFSOpenFlags::Write))
            {
                return HasFlag(flags, VFSOpenFlags::Read) ? "r+b" : "wb";
            }
            return "rb";
        }
    } // namespace

    VFSDiskContext::VFSDiskContext(cstring native_root)
    {
        m_native_root_len = Helpers::secure_strlen(native_root);
        ZENGINE_VALIDATE_ASSERT(m_native_root_len < VFS_MAX_PATH, "VFSDiskContext native_root exceeds VFS_MAX_PATH")
        Helpers::secure_memcpy(m_native_root, VFS_MAX_PATH, native_root, m_native_root_len);
        m_native_root[m_native_root_len] = '\0';

        if (m_native_root_len > 0 && (m_native_root[m_native_root_len - 1] == '/' || m_native_root[m_native_root_len - 1] == '\\'))
        {
            --m_native_root_len;
            m_native_root[m_native_root_len] = '\0';
        }
    }

    VFSDiskContext::~VFSDiskContext() = default;

    bool VFSDiskContext::ToNativePath(const VFSPath& vfs_path, char* out_buffer, size_t out_size) const
    {
        char native[VFS_MAX_PATH];
        vfs_path.ToNative(native, sizeof(native));

        const size_t native_len = Helpers::secure_strlen(native);
        if (m_native_root_len + native_len + 1 > out_size)
        {
            return false;
        }

        Helpers::secure_memcpy(out_buffer, out_size, m_native_root, m_native_root_len);
        Helpers::secure_memcpy(out_buffer + m_native_root_len, out_size - m_native_root_len, native, native_len);
        out_buffer[m_native_root_len + native_len] = '\0';
        return true;
    }

    VFSResult<IVFSFile*> VFSDiskContext::Open(const VFSPath& path, VFSOpenFlags flags)
    {
        char native[VFS_MAX_PATH * 2];
        if (!ToNativePath(path, native, sizeof(native)))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::InvalidPath);
        }

        std::FILE* handle = std::fopen(native, ToFopenMode(flags));
        if (!handle)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        return VFSResult<IVFSFile*>::Ok(new VFSDiskFile(handle, path));
    }

    void VFSDiskContext::Close(IVFSFile* file)
    {
        delete file;
    }

    VFSResult<VFSFileStat> VFSDiskContext::Stat(const VFSPath& path) const
    {
        char native[VFS_MAX_PATH * 2];
        if (!ToNativePath(path, native, sizeof(native)))
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

    bool VFSDiskContext::Exists(const VFSPath& path) const
    {
        char native[VFS_MAX_PATH * 2];
        if (!ToNativePath(path, native, sizeof(native)))
        {
            return false;
        }
        std::error_code ec;
        return std::filesystem::exists(native, ec) && !ec;
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSDiskContext::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {
        using ResultT = VFSResult<Core::Containers::Array<VFSDirEntry>>;

        char native[VFS_MAX_PATH * 2];
        if (!ToNativePath(dir, native, sizeof(native)))
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

} // namespace ZEngine::Core::VFS
