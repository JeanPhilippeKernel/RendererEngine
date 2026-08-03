#include <ZEngine/Core/VFS/VFSZipBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstddef>

namespace ZEngine::Core::VFS
{

    VFSResult<const uint8_t*> VFSZipFile::EnsureDecompressed()
    {
        if (m_decompressed)
        {
            return VFSResult<const uint8_t*>::Ok(m_data);
        }

        if (m_entry->UncompSize == 0)
        {
            m_data         = static_cast<uint8_t*>(m_arena->Allocate(1));
            m_decompressed = true;
            return VFSResult<const uint8_t*>::Ok(m_data);
        }

        uint8_t* out = static_cast<uint8_t*>(m_arena->Allocate(m_entry->UncompSize));
        if (!out)
        {
            return VFSResult<const uint8_t*>::Fail(VFSError::OutOfMemory);
        }
        if (!m_backend->ExtractEntry(m_entry->FileIndex, out, static_cast<size_t>(m_entry->UncompSize)))
        {
            return VFSResult<const uint8_t*>::Fail(VFSError::Corrupted);
        }

        m_data         = out;
        m_decompressed = true;
        return VFSResult<const uint8_t*>::Ok(m_data);
    }

    VFSResult<size_t> VFSZipFile::Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset)
    {
        VFSResult<const uint8_t*> data = EnsureDecompressed();
        if (data.Failed())
        {
            return VFSResult<size_t>::Fail(data.Error());
        }
        if (offset >= m_entry->UncompSize)
        {
            return VFSResult<size_t>::Ok(0); // past EOF
        }
        const size_t available = static_cast<size_t>(m_entry->UncompSize - offset);
        const size_t to_copy   = available < buffer.size() ? available : buffer.size();
        Helpers::secure_memcpy(buffer.data(), buffer.size(), data.Value() + offset, to_copy);
        return VFSResult<size_t>::Ok(to_copy);
    }

    VFSResult<size_t> VFSZipFile::Write(Core::Containers::ArrayView<const uint8_t>, uint64_t)
    {
        return VFSResult<size_t>::Fail(VFSError::Unsupported);
    }

    VFSResult<uint64_t> VFSZipFile::Size() const
    {
        return VFSResult<uint64_t>::Ok(m_entry->UncompSize);
    }

    VFSResult<VFSFileStat> VFSZipFile::Stat() const
    {
        VFSFileStat stat;
        stat.SizeBytes   = m_entry->UncompSize;
        stat.IsDirectory = false;
        stat.IsReadOnly  = true;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    VFSResult<void> VFSZipFile::Flush()
    {
        return VFSResult<void>::Ok();
    }

    const VFSPath& VFSZipFile::Path() const
    {
        return m_path;
    }

    VFSResult<void> VFSZipFile::Close()
    {
        return VFSResult<void>::Ok();
    }

    // convert a raw ZIP entry name into what VFSPath::Parse expects:
    void VFSZipBackend::NormalizeZipEntryName(const char* raw, char* out, size_t out_size)
    {
        const char* src = raw;

        // Some archive tools prefix every entry with "./"
        if (src[0] == '.' && src[1] == '/')
        {
            src += 2;
        }

        // convert slashes '\' to '/'
        size_t j = 0;
        for (; src[0] != '\0' && j + 1 < out_size; ++src)
        {
            const char c = *src;
            out[j++]     = (c == '\\') ? '/' : c;
        }
        out[j] = '\0';

        while (j > 0 && out[j - 1] == '/')
        {
            out[--j] = '\0';
        }
    }

    void VFSZipBackend::MaybeLower(const VFSPath& path, char* out_buf) const
    {
        Helpers::secure_strcpy(out_buf, MAX_FILE_PATH_COUNT, path.CStr());
        if (!m_case_sensitive)
        {
            for (size_t i = 0; out_buf[i] != '\0'; ++i)
            {
                const char c = out_buf[i];
                if (c >= 'A' && c <= 'Z')
                {
                    out_buf[i] = static_cast<char>(c - 'A' + 'a');
                }
            }
        }
    }

    uint64_t VFSZipBackend::HashOf(const VFSPath& p) const
    {
        char buf[MAX_FILE_PATH_COUNT] = {};
        MaybeLower(p, buf);
        return rapidhash(buf, Helpers::secure_strlen(buf));
    }

    void VFSZipBackend::AddChildToDir(uint64_t parent_hash, const VFSDirEntry& child)
    {
        Containers::Array<VFSDirEntry>* list = m_dir_cache.find(parent_hash);
        if (!list)
        {
            Containers::Array<VFSDirEntry> fresh;
            fresh.init(m_arena, 8);
            m_dir_cache.insert(parent_hash, fresh);
            list = m_dir_cache.find(parent_hash);
        }
        for (size_t i = 0; i < list->size(); ++i)
        {
            if ((*list)[i].Path == child.Path)
            {
                return;
            }
        }
        list->push(child);
    }

    void VFSZipBackend::IndexEntry(const VFSPath& vp, const ZipEntry& entry)
    {
        const uint64_t h = HashOf(vp);
        if (!m_central_dir.contains(h))
        {
            m_central_dir.insert(h, entry);
        }

        VFSPath child = vp;
        while (!child.IsRoot())
        {
            const VFSPath  parent      = child.Parent();
            const uint64_t parent_hash = HashOf(parent);

            if (!m_central_dir.contains(parent_hash))
            {
                ZipEntry dir_entry;
                dir_entry.IsDirectory = true;
                m_central_dir.insert(parent_hash, dir_entry);
            }

            const ZipEntry* ce = m_central_dir.find(HashOf(child));
            VFSDirEntry     de;
            de.Path             = child;
            de.IsDirectory      = ce ? ce->IsDirectory : false;
            de.Stat.IsDirectory = de.IsDirectory;
            de.Stat.SizeBytes   = (ce && !ce->IsDirectory) ? ce->UncompSize : 0;
            AddChildToDir(parent_hash, de);

            child = parent;
        }
    }

    void VFSZipBackend::Initialize(cstring archive_path, Memory::ArenaAllocator* arena, bool case_sensitive, size_t max_open_files)
    {
        m_arena          = arena;
        m_case_sensitive = case_sensitive;
        Helpers::secure_strcpy(m_archive_path, MAX_FILE_PATH_COUNT, archive_path);

        m_file_pool.Initialize(m_arena, sizeof(VFSZipFile) * max_open_files, sizeof(VFSZipFile));

        m_central_dir.init(arena, 256);
        m_dir_cache.init(arena, 64);

        Helpers::secure_memset(&m_zip, 0, sizeof(m_zip), sizeof(m_zip));
        if (!mz_zip_reader_init_file(&m_zip, archive_path, 0))
        {
            m_ready = false;
            return;
        }
        m_ready                 = true;

        const mz_uint num_files = mz_zip_reader_get_num_files(&m_zip);
        for (mz_uint i = 0; i < num_files; ++i)
        {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&m_zip, i, &st))
            {
                continue;
            }

            char normalized[MAX_FILE_PATH_COUNT] = {};
            NormalizeZipEntryName(st.m_filename, normalized, sizeof(normalized));
            if (normalized[0] == '\0')
            {
                continue;
            }

            VFSResult<VFSPath> parsed = VFSPath::Parse(normalized);
            if (parsed.Failed())
            {
                continue;
            }
            const VFSPath vp = parsed.Value();
            if (vp.IsRoot())
            {
                continue;
            }

            ZipEntry entry;
            entry.FileIndex    = i;
            entry.CompSize     = st.m_comp_size;
            entry.UncompSize   = st.m_uncomp_size;
            entry.LocalHdrOfs  = st.m_local_header_ofs;
            entry.Crc32        = st.m_crc32;
            entry.IsDirectory  = mz_zip_reader_is_file_a_directory(&m_zip, i) != 0;
            entry.IsCompressed = st.m_comp_size != st.m_uncomp_size;

            IndexEntry(vp, entry);
        }
    }

    void VFSZipBackend::Shutdown()
    {
        if (m_ready)
        {
            mz_zip_reader_end(&m_zip);
            m_ready = false;
        }
    }

    VFSZipBackend::~VFSZipBackend()
    {
        Shutdown();
    }

    bool VFSZipBackend::ExtractEntry(uint32_t file_index, void* out, size_t out_size)
    {
        if (!m_ready)
        {
            return false;
        }
        std::lock_guard<std::mutex> lock(m_archive_mutex);
        return mz_zip_reader_extract_to_mem(&m_zip, file_index, out, out_size, 0) != 0;
    }

    VFSResult<IVFSFile*> VFSZipBackend::Open(const VFSPath& relative_path, VFSOpenFlags flags)
    {
        if (HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Append))
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::PermissionDenied);
        }

        const ZipEntry* entry = m_central_dir.find(HashOf(relative_path));
        if (!entry)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        if (entry->IsDirectory)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
        }

        void* mem = m_file_pool.Allocate();
        if (!mem)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
        }
        VFSZipFile* file = ZConstruct(mem, VFSZipFile);
        file->Owner      = this;
        file->m_entry    = entry;
        file->m_backend  = this;
        file->m_arena    = m_arena;
        file->m_path     = relative_path;
        return VFSResult<IVFSFile*>::Ok(file);
    }

    void VFSZipBackend::Close(IVFSFile* file)
    {
        if (!file)
        {
            return;
        }
        file->~IVFSFile();
        m_file_pool.Free(file);
    }

    VFSResult<VFSFileStat> VFSZipBackend::Stat(const VFSPath& relative_path) const
    {
        const ZipEntry* entry = m_central_dir.find(HashOf(relative_path));
        if (!entry)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotFound);
        }
        VFSFileStat stat;
        stat.IsDirectory = entry->IsDirectory;
        stat.SizeBytes   = entry->IsDirectory ? 0 : entry->UncompSize;
        stat.IsReadOnly  = true;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    bool VFSZipBackend::Exists(const VFSPath& relative_path) const
    {
        return m_central_dir.contains(HashOf(relative_path));
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSZipBackend::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {
        using ResultT                              = VFSResult<Core::Containers::Array<VFSDirEntry>>;

        const uint64_t                        h    = HashOf(dir);
        const Containers::Array<VFSDirEntry>* list = m_dir_cache.find(h);
        if (!list)
        {
            if (dir.IsRoot() || m_central_dir.contains(h))
            {
                Core::Containers::Array<VFSDirEntry> empty;
                empty.init(arena, 1);
                return ResultT::Ok(std::move(empty));
            }
            return ResultT::Fail(VFSError::NotFound);
        }

        Core::Containers::Array<VFSDirEntry> result;
        result.init(arena, list->size() ? list->size() : 1);
        for (size_t i = 0; i < list->size(); ++i)
        {
            result.push((*list)[i]);
        }
        return ResultT::Ok(std::move(result));
    }

    cstring VFSZipBackend::BackendType() const
    {
        return "zip";
    }

    VFSBackendCaps VFSZipBackend::Capabilities() const
    {
        return m_ready ? (VFSBackendCaps::Read | VFSBackendCaps::List) : VFSBackendCaps::None;
    }

    bool VFSZipBackend::WriteArchive(cstring path, const ZipFileSpec* files, size_t count)
    {
        mz_zip_archive zip;
        Helpers::secure_memset(&zip, 0, sizeof(zip), sizeof(zip));
        if (!mz_zip_writer_init_file(&zip, path, 0))
        {
            return false;
        }

        bool ok = true;
        for (size_t i = 0; i < count && ok; ++i)
        {
            ok = mz_zip_writer_add_mem(&zip, files[i].Name, files[i].Data, files[i].Size, MZ_DEFAULT_COMPRESSION) != 0;
        }
        ok = (mz_zip_writer_finalize_archive(&zip) != 0) && ok;
        mz_zip_writer_end(&zip);
        return ok;
    }

} // namespace ZEngine::Core::VFS
