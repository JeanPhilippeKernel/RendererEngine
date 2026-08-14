#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSMountTable.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <mutex>

namespace ZEngine::Core::VFS
{
    void VFSMountTable::Initialize(Memory::ArenaAllocator* arena, size_t initial_capacity)
    {
        m_arena = arena;
        m_mounts.init(arena, initial_capacity);
    }

    size_t VFSMountTable::Count() const
    {
        return m_mounts.size();
    }

    void VFSMountTable::Clear()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_mounts.clear();
        m_arena = nullptr;
    }

    VFSResult<void> VFSMountTable::Mount(IVFSBackend* const backend, const VFSPath& logical_root, int priority)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (m_mounts[i].LogicalRoot == logical_root)
            {
                return VFSResult<void>::Fail(VFSError::AlreadyExists);
            }
        }

        ZENGINE_VALIDATE_ASSERT(m_mounts.size() < kMaxMountPoints, "VFSMountTable: maximum mount point count (256) exceeded");

        MountPoint mount{.Backend = backend, .LogicalRoot = logical_root, .Priority = priority};

        size_t     insert_index = m_mounts.size();
        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (m_mounts[i].Priority < priority)
            {
                insert_index = i;
                break;
            }
        }

        m_mounts.insert(insert_index, mount);
        return VFSResult<void>::Ok();
    }

    VFSResult<void> VFSMountTable::Unmount(const VFSPath& logical_root)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (m_mounts[i].LogicalRoot == logical_root)
            {
                m_mounts.erase(i);
                return VFSResult<void>::Ok();
            }
        }
        return VFSResult<void>::Fail(VFSError::NotFound);
    }

    bool VFSMountTable::IsPrefixOf(const VFSPath& prefix, const VFSPath& path)
    {
        return prefix.IsPrefixOf(path);
    }

    VFSResult<VFSPath> VFSMountTable::StripPrefix(const VFSPath& prefix, const VFSPath& path)
    {
        cstring path_buf = path.CStr();
        size_t  skip     = prefix.Length();

        if (path_buf[skip] == '/')
        {
            ++skip;
        }

        if (path_buf[skip] == '\0')
        {
            return VFSResult<VFSPath>::Ok(VFSPath::Root());
        }

        return VFSPath::Parse(path_buf + skip);
    }

    VFSResult<ResolveResult> VFSMountTable::Resolve(const VFSPath& path) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        // Select the mount with the longest matching prefix (most specific).
        // Among equal-length prefixes, the higher-priority mount wins (mounts are
        // stored highest-priority-first so the first same-length match is best).
        size_t                              best_idx    = SIZE_MAX;
        size_t                              best_prefix = 0;

        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (!IsPrefixOf(m_mounts[i].LogicalRoot, path))
                continue;
            const size_t prefix_len = m_mounts[i].LogicalRoot.Length();
            if (prefix_len > best_prefix)
            {
                best_prefix = prefix_len;
                best_idx    = i;
            }
        }

        if (best_idx == SIZE_MAX)
            return VFSResult<ResolveResult>::Fail(VFSError::NotFound);

        VFSResult<VFSPath> rel = StripPrefix(m_mounts[best_idx].LogicalRoot, path);
        if (rel.Failed())
            return VFSResult<ResolveResult>::Fail(rel.Error());
        return VFSResult<ResolveResult>::Ok(ResolveResult{m_mounts[best_idx].Backend, rel.Value()});
    }

    VFSResult<void> VFSMountTable::ResolveAll(const VFSPath& dir_path, Containers::Array<ResolveResult>& out_results) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (IsPrefixOf(m_mounts[i].LogicalRoot, dir_path))
            {
                VFSResult<VFSPath> rel = StripPrefix(m_mounts[i].LogicalRoot, dir_path);
                if (rel.Succeeded())
                {
                    out_results.push(ResolveResult{m_mounts[i].Backend, rel.Value()});
                }
            }
        }
        return VFSResult<void>::Ok();
    }

} // namespace ZEngine::Core::VFS