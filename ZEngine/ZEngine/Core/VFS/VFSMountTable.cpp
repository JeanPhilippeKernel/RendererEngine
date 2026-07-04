#include <Helpers/MemoryOperations.h>
#include <VFSMountTable.h>
#include <mutex>
#include "VFSError.h"

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

    VFSResult<void> VFSMountTable::Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority)
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

        MountPoint mount{logical_root, backend, priority};

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

        for (size_t i = 0; i < m_mounts.size(); ++i)
        {
            if (IsPrefixOf(m_mounts[i].LogicalRoot, path))
            {
                VFSResult<VFSPath> rel = StripPrefix(m_mounts[i].LogicalRoot, path);
                if (rel.Failed())
                {
                    return VFSResult<ResolveResult>::Fail(rel.Error());
                }
                return VFSResult<ResolveResult>::Ok(ResolveResult{m_mounts[i].Backend, rel.Value()});
            }
        }
        return VFSResult<ResolveResult>::Fail(VFSError::NotFound);
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