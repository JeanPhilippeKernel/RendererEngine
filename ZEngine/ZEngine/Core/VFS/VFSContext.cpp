#include <Helpers/MemoryOperations.h>
#include <VFSContext.h>

namespace ZEngine::Core::VFS
{
    void VFSContext::Initialize(Memory::ArenaAllocator* arena, size_t mount_table_capacity)
    {
        m_arena = arena;
        m_mount_table.Initialize(m_arena, mount_table_capacity);
    }

    VFSResult<IVFSFile*> VFSContext::Open(const VFSPath& absolute_path, VFSOpenFlags flags)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<IVFSFile*>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return hit.Backend->Open(hit.RelativePath, flags);
    }

    VFSResult<VFSFileStat> VFSContext::Stat(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<VFSFileStat>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return hit.Backend->Stat(hit.RelativePath);
    }

    VFSResult<bool> VFSContext::Exists(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> resolved = m_mount_table.Resolve(absolute_path);
        if (resolved.Failed())
        {
            return VFSResult<bool>::Fail(resolved.Error());
        }

        const ResolveResult& hit = resolved.Value();
        return VFSResult<bool>::Ok(hit.Backend->Exists(hit.RelativePath));
    }

    VFSResult<Containers::Array<VFSDirEntry>> VFSContext::List(const VFSPath& absolute_dir, Memory::ArenaAllocator* out_arena)
    {
        using ResultT                            = VFSResult<Containers::Array<VFSDirEntry>>;

        auto                             scratch = ZGetScratch(m_arena);

        Containers::Array<ResolveResult> matches;
        matches.init(scratch.Arena, 8);

        VFSResult<void> resolved = m_mount_table.ResolveAll(absolute_dir, matches);
        if (resolved.Failed())
        {
            ZReleaseScratch(scratch);
            return ResultT::Fail(resolved.Error());
        }
        if (matches.empty())
        {
            ZReleaseScratch(scratch);
            return ResultT::Fail(VFSError::NotFound);
        }

        Containers::Array<VFSDirEntry> merged;
        merged.init(out_arena, 16);

        for (size_t m = 0; m < matches.size(); ++m)
        {
            VFSResult<Containers::Array<VFSDirEntry>> sub = matches[m].Backend->List(scratch.Arena, matches[m].RelativePath);
            if (sub.Failed())
            {
                continue;
            }

            Containers::Array<VFSDirEntry>& entries = sub.Value();
            for (size_t e = 0; e < entries.size(); ++e)
            {
                VFSPathComponent name = entries[e].Path.Filename();

                char             filename[VFS_MAX_PATH];
                Helpers::secure_memcpy(filename, sizeof(filename), name.Data, name.Length);
                filename[name.Length]    = '\0';

                VFSResult<VFSPath> child = absolute_dir.Append(filename);
                if (child.Failed())
                {
                    continue;
                }

                bool already_present = false;
                for (size_t k = 0; k < merged.size(); ++k)
                {
                    if (merged[k].Path == child.Value())
                    {
                        already_present = true;
                        break;
                    }
                }
                if (already_present)
                {
                    continue;
                }

                VFSDirEntry entry = entries[e];
                entry.Path        = child.Value();
                merged.push(entry);
            }
        }

        ZReleaseScratch(scratch);
        return ResultT::Ok(std::move(merged));
    }

    VFSResult<void> VFSContext::Mount(IVFSBackend* backend, const VFSPath& logical_root, int priority)
    {
        return m_mount_table.Mount(backend, logical_root, priority);
    }

    VFSResult<void> VFSContext::Unmount(const VFSPath& logical_root)
    {
        return m_mount_table.Unmount(logical_root);
    }

    VFSResult<void> VFSContext::CreateDir(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> writable = ResolveWritable(absolute_path);
        if (writable.Failed())
        {
            return VFSResult<void>::Fail(writable.Error());
        }

        const ResolveResult& hit = writable.Value();
        return hit.Backend->CreateDir(hit.RelativePath);
    }

    VFSResult<void> VFSContext::Remove(const VFSPath& absolute_path)
    {
        VFSResult<ResolveResult> writable = ResolveWritable(absolute_path);
        if (writable.Failed())
        {
            return VFSResult<void>::Fail(writable.Error());
        }

        const ResolveResult& hit = writable.Value();
        return hit.Backend->Remove(hit.RelativePath);
    }

    VFSResult<void> VFSContext::Rename(const VFSPath& src, const VFSPath& dst)
    {
        VFSResult<ResolveResult> src_hit = ResolveWritable(src);
        if (src_hit.Failed())
        {
            return VFSResult<void>::Fail(src_hit.Error());
        }

        VFSResult<ResolveResult> dst_hit = ResolveWritable(dst);
        if (dst_hit.Failed())
        {
            return VFSResult<void>::Fail(dst_hit.Error());
        }

        if (src_hit.Value().Backend != dst_hit.Value().Backend)
        {
            return VFSResult<void>::Fail(VFSError::Unsupported);
        }

        return src_hit.Value().Backend->Rename(src_hit.Value().RelativePath, dst_hit.Value().RelativePath);
    }

    VFSResult<ResolveResult> VFSContext::ResolveWritable(const VFSPath& path) const
    {
        auto                             scratch = ZGetScratch(m_arena);

        Containers::Array<ResolveResult> matches;
        matches.init(scratch.Arena, 8);

        VFSResult<void> resolved = m_mount_table.ResolveAll(path, matches);
        if (resolved.Failed())
        {
            ZReleaseScratch(scratch);
            return VFSResult<ResolveResult>::Fail(resolved.Error());
        }

        for (size_t i = 0; i < matches.size(); ++i)
        {
            if (HasCap(matches[i].Backend->Capabilities(), VFSBackendCaps::Write))
            {
                ResolveResult hit = matches[i]; // copy out before scratch is released
                ZReleaseScratch(scratch);
                return VFSResult<ResolveResult>::Ok(hit);
            }
        }

        ZReleaseScratch(scratch);
        return VFSResult<ResolveResult>::Fail(VFSError::PermissionDenied);
    }

} // namespace ZEngine::Core::VFS
