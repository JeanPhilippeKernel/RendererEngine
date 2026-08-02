#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    bool VFSMemoryFile::IsWriteMode() const
    {
        return HasFlag(m_flags, VFSOpenFlags::Write) || HasFlag(m_flags, VFSOpenFlags::Append);
    }

    VFSResult<size_t> VFSMemoryFile::Read(Core::Containers::ArrayView<uint8_t> buffer, uint64_t offset)
    {
        if (!m_node || m_closed)
        {
            return VFSResult<size_t>::Fail(VFSError::IOError);
        }

        const Core::Containers::Array<uint8_t>& data = m_node->Data;
        if (offset >= data.size())
        {
            return VFSResult<size_t>::Ok(0);
        }

        const size_t available = data.size() - static_cast<size_t>(offset);
        const size_t n         = buffer.size() < available ? buffer.size() : available;
        if (n > 0)
        {
            Helpers::secure_memcpy(buffer.data(), buffer.size(), data.data() + offset, n);
        }
        return VFSResult<size_t>::Ok(n);
    }

    VFSResult<size_t> VFSMemoryFile::Write(Core::Containers::ArrayView<const uint8_t> buffer, uint64_t offset)
    {
        if (!IsWriteMode() || m_closed)
        {
            return VFSResult<size_t>::Fail(VFSError::Unsupported);
        }

        const size_t end = static_cast<size_t>(offset) + buffer.size();
        if (m_write_buf.capacity() < end)
        {
            m_write_buf.reserve(end);
        }
        while (m_write_buf.size() < end)
        {
            m_write_buf.push(0);
        }
        if (buffer.size() > 0)
        {
            Helpers::secure_memcpy(m_write_buf.data() + offset, m_write_buf.size() - static_cast<size_t>(offset), buffer.data(), buffer.size());
        }
        return VFSResult<size_t>::Ok(buffer.size());
    }

    VFSResult<uint64_t> VFSMemoryFile::Size() const
    {
        if (!m_node)
        {
            return VFSResult<uint64_t>::Fail(VFSError::IOError);
        }
        const uint64_t sz = IsWriteMode() ? m_write_buf.size() : m_node->Data.size();
        return VFSResult<uint64_t>::Ok(sz);
    }

    VFSResult<VFSFileStat> VFSMemoryFile::Stat() const
    {
        if (!m_node)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::IOError);
        }
        VFSFileStat stat;
        stat.IsDirectory = m_node->NodeKind == MemNode::Kind::Directory;
        stat.SizeBytes   = IsWriteMode() ? m_write_buf.size() : m_node->Data.size();
        stat.IsReadOnly  = false;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    VFSResult<void> VFSMemoryFile::Commit()
    {
        if (!IsWriteMode() || !m_node || !m_backend_mutex)
        {
            return VFSResult<void>::Ok();
        }

        std::unique_lock<std::shared_mutex> lock(*m_backend_mutex);
        const size_t                        n = m_write_buf.size();
        m_node->Data.init(m_arena, n, n);
        if (n > 0)
        {
            Helpers::secure_memcpy(m_node->Data.data(), n, m_write_buf.data(), n);
        }
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryFile::Flush()
    {
        if (m_closed)
        {
            return VFSResult<void>::Ok();
        }
        return Commit();
    }

    const VFSPath& VFSMemoryFile::Path() const
    {
        return m_node->Path;
    }

    VFSResult<void> VFSMemoryFile::Close()
    {
        if (m_closed)
        {
            return VFSResult<void>::Ok();
        }

        VFSResult<void> result = VFSResult<void>::Ok();
        if (IsWriteMode())
        {
            result = Commit();
        }
        m_closed = true;
        if (m_read_lock.owns_lock())
        {
            m_read_lock.unlock();
        }
        return result;
    }

    VFSResult<void*> VFSMemoryFile::MemoryMap(uint64_t& out_size)
    {
        if (IsWriteMode())
        {
            return VFSResult<void*>::Fail(VFSError::Unsupported);
        }
        if (!m_node)
        {
            return VFSResult<void*>::Fail(VFSError::IOError);
        }
        out_size = m_node->Data.size();
        return VFSResult<void*>::Ok(static_cast<void*>(m_node->Data.data()));
    }

    VFSMemoryFile::~VFSMemoryFile()
    {
        if (!m_closed && IsWriteMode())
        {
            Commit();
        }
    }

    VFSMemoryBackend::VFSMemoryBackend() = default;

    VFSMemoryBackend::~VFSMemoryBackend()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (const auto& kv : m_nodes)
        {
            kv.second->~MemNode();
        }
        m_nodes.clear();
    }

    void VFSMemoryBackend::Initialize(Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "VFSMemoryBackend requires a valid arena")
        m_arena = arena;
        m_nodes.init(arena, 64);
        m_file_pool.Initialize(arena, sizeof(VFSMemoryFile) * kVFSMaxOpenMemoryFiles, sizeof(VFSMemoryFile));
    }

    MemNode* VFSMemoryBackend::FindNode(const VFSPath& path)
    {
        MemNode** slot = m_nodes.find(path.CStr());
        return slot ? *slot : nullptr;
    }

    const MemNode* VFSMemoryBackend::FindNode(const VFSPath& path) const
    {
        const MemNode* const* slot = m_nodes.find(path.CStr());
        return slot ? *slot : nullptr;
    }

    MemNode* VFSMemoryBackend::CreateNode(const VFSPath& path, MemNode::Kind kind)
    {
        MemNode* node  = ZPushStructCtor(m_arena, MemNode);
        node->NodeKind = kind;
        node->Path     = path;
        m_nodes.insert(node->Path.CStr(), node);
        return node;
    }

    VFSMemoryFile* VFSMemoryBackend::AllocFile()
    {
        std::lock_guard<std::mutex> pool_lock(m_file_pool_mutex);
        void*                       mem = m_file_pool.Allocate();
        if (!mem)
        {
            return nullptr;
        }
        return ZConstruct(mem, VFSMemoryFile);
    }

    void VFSMemoryBackend::FreeFile(IVFSFile* file)
    {
        std::lock_guard<std::mutex> pool_lock(m_file_pool_mutex);
        m_file_pool.Free(file);
    }

    void VFSMemoryBackend::EnsureParentExists(const VFSPath& path)
    {
        VFSPath parent = path.Parent();
        while (parent.IsValid() && !parent.IsRoot())
        {
            if (FindNode(parent))
            {
                break;
            }
            CreateNode(parent, MemNode::Kind::Directory);
            parent = parent.Parent();
        }
    }

    bool VFSMemoryBackend::HasChildren(const VFSPath& dir) const
    {
        for (const auto& kv : m_nodes)
        {
            if (kv.second->Path.Parent() == dir)
            {
                return true;
            }
        }
        return false;
    }

    VFSResult<IVFSFile*> VFSMemoryBackend::Open(const VFSPath& relative_path, VFSOpenFlags flags)
    {
        const bool wants_write = HasFlag(flags, VFSOpenFlags::Write) || HasFlag(flags, VFSOpenFlags::Append);

        if (wants_write)
        {
            std::unique_lock<std::shared_mutex> lock(m_mutex);

            MemNode*                            node = FindNode(relative_path);
            if (!node)
            {
                if (!HasFlag(flags, VFSOpenFlags::Write))
                {
                    return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
                }
                EnsureParentExists(relative_path);
                node = CreateNode(relative_path, MemNode::Kind::File);
            }
            if (node->NodeKind == MemNode::Kind::Directory)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
            }

            VFSMemoryFile* file = AllocFile();
            if (!file)
            {
                return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
            }
            file->Owner           = this;
            file->m_node          = node;
            file->m_flags         = flags;
            file->m_arena         = m_arena;
            file->m_backend_mutex = &m_mutex;

            if (HasFlag(flags, VFSOpenFlags::Append) && node->Data.size() > 0)
            {
                const size_t n = node->Data.size();
                file->m_write_buf.init(m_arena, n, n);
                Helpers::secure_memcpy(file->m_write_buf.data(), n, node->Data.data(), n);
            }
            else
            {
                file->m_write_buf.init(m_arena, 16);
            }
            return VFSResult<IVFSFile*>::Ok(file);
        }

        std::shared_lock<std::shared_mutex> lock(m_mutex);
        MemNode*                            node = FindNode(relative_path);
        if (!node)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotFound);
        }
        if (node->NodeKind == MemNode::Kind::Directory)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::NotAFile);
        }

        VFSMemoryFile* file = AllocFile();
        if (!file)
        {
            return VFSResult<IVFSFile*>::Fail(VFSError::OutOfMemory);
        }
        file->Owner           = this;
        file->m_node          = node;
        file->m_flags         = flags;
        file->m_arena         = m_arena;
        file->m_backend_mutex = &m_mutex;
        file->m_read_lock     = std::move(lock);
        return VFSResult<IVFSFile*>::Ok(file);
    }

    void VFSMemoryBackend::Close(IVFSFile* file)
    {
        if (!file)
        {
            return;
        }
        file->Close();
        file->~IVFSFile();
        FreeFile(file);
    }

    VFSResult<VFSFileStat> VFSMemoryBackend::Stat(const VFSPath& path) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const MemNode*                      node = FindNode(path);
        if (!node)
        {
            return VFSResult<VFSFileStat>::Fail(VFSError::NotFound);
        }
        VFSFileStat stat;
        stat.IsDirectory = node->NodeKind == MemNode::Kind::Directory;
        stat.SizeBytes   = stat.IsDirectory ? 0 : node->Data.size();
        stat.IsReadOnly  = false;
        return VFSResult<VFSFileStat>::Ok(stat);
    }

    bool VFSMemoryBackend::Exists(const VFSPath& path) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return FindNode(path) != nullptr;
    }

    VFSResult<Core::Containers::Array<VFSDirEntry>> VFSMemoryBackend::List(Core::Memory::ArenaAllocator* arena, const VFSPath& dir) const
    {

        std::shared_lock<std::shared_mutex> lock(m_mutex);

        if (!dir.IsRoot())
        {
            const MemNode* dir_node = FindNode(dir);
            if (!dir_node)
            {
                return VFSResult<Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::NotFound);
            }
            if (dir_node->NodeKind != MemNode::Kind::Directory)
            {
                return VFSResult<Core::Containers::Array<VFSDirEntry>>::Fail(VFSError::NotADirectory);
            }
        }

        Core::Containers::Array<VFSDirEntry> entries;
        entries.init(arena, 16);

        for (const auto& kv : m_nodes)
        {
            const MemNode* node = kv.second;
            if (node->Path.Parent() == dir)
            {
                VFSDirEntry entry;
                entry.Path             = node->Path;
                entry.IsDirectory      = node->NodeKind == MemNode::Kind::Directory;
                entry.Stat.IsDirectory = entry.IsDirectory;
                entry.Stat.SizeBytes   = entry.IsDirectory ? 0 : node->Data.size();
                entries.push(entry);
            }
        }
        return VFSResult<Core::Containers::Array<VFSDirEntry>>::Ok(std::move(entries));
    }

    VFSResult<void> VFSMemoryBackend::CreateDir(const VFSPath& relative_path)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        if (FindNode(relative_path))
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }
        EnsureParentExists(relative_path);
        CreateNode(relative_path, MemNode::Kind::Directory);
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryBackend::Remove(const VFSPath& relative_path)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        MemNode*                            node = FindNode(relative_path);
        if (!node)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        if (node->NodeKind == MemNode::Kind::Directory && HasChildren(relative_path))
        {
            return VFSResult<void>::Fail(VFSError::IOError);
        }
        m_nodes.remove(relative_path.CStr());
        node->~MemNode();
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMemoryBackend::Rename(const VFSPath& rel_src, const VFSPath& rel_dst)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        MemNode*                            src = FindNode(rel_src);
        if (!src)
        {
            return VFSResult<void>::Fail(VFSError::NotFound);
        }
        if (FindNode(rel_dst))
        {
            return VFSResult<void>::Fail(VFSError::AlreadyExists);
        }

        m_nodes.remove(rel_src.CStr());
        src->Path = rel_dst;
        EnsureParentExists(rel_dst);
        m_nodes.insert(src->Path.CStr(), src);
        return VFSResult<void>::Ok();
    }

    cstring VFSMemoryBackend::BackendType() const
    {
        return "memory";
    }

    VFSBackendCaps VFSMemoryBackend::Capabilities() const
    {
        return VFSBackendCaps::Read | VFSBackendCaps::Write | VFSBackendCaps::List | VFSBackendCaps::MemoryMap;
    }

    VFSResult<void> VFSMemoryBackend::WriteFile(const VFSPath& path, Core::Containers::ArrayView<const uint8_t> data)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        MemNode*                            node = FindNode(path);
        if (!node)
        {
            EnsureParentExists(path);
            node = CreateNode(path, MemNode::Kind::File);
        }
        else if (node->NodeKind == MemNode::Kind::Directory)
        {
            return VFSResult<void>::Fail(VFSError::NotAFile);
        }

        const size_t n = data.size();
        node->Data.init(m_arena, n, n);
        if (n > 0)
        {
            Helpers::secure_memcpy(node->Data.data(), n, data.data(), n);
        }
        return VFSResult<void>::Ok();
    }

} // namespace ZEngine::Core::VFS
