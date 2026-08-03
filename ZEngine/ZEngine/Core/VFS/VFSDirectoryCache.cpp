#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <mutex>

namespace ZEngine::Core::VFS
{

    void VFSDirectoryCache::Initialize(Memory::ArenaAllocator* arena)
    {
        m_arena = arena;
        m_entries.init(arena, 64);
    }

    Core::Containers::ArrayView<const VFSDirEntry> VFSDirectoryCache::GetListing(const VFSPath& dir) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const CacheEntry*                   entry = m_entries.find(dir.Hash());
        if (!entry)
        {
            return Core::Containers::ArrayView<const VFSDirEntry>(nullptr, 0);
        }
        return Core::Containers::ArrayView<const VFSDirEntry>(entry->Entries.data(), entry->Entries.size());
    }

    void VFSDirectoryCache::SetListing(const VFSPath& dir, Core::Containers::Array<VFSDirEntry>&& entries)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);

        CacheEntry&                         entry = m_entries[dir.Hash()];
        entry.Entries                             = std::move(entries);
        entry.Stale                               = false;
        entry.Dir                                 = dir;
    }

    bool VFSDirectoryCache::IsStale(const VFSPath& dir) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        const auto                          entry = m_entries.find(dir.Hash());

        if (!entry)
        {
            return true;
        }

        return entry->Stale;
    }

    void VFSDirectoryCache::Clear()
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        m_entries.clear();
    }

    size_t VFSDirectoryCache::Size() const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);
        return m_entries.size();
    }

    void VFSDirectoryCache::ForEachDir(std::function<void(const VFSPath&, Core::Containers::ArrayView<const VFSDirEntry>)> visitor) const
    {
        std::shared_lock<std::shared_mutex> lock(m_mutex);

        for (const auto& kv : m_entries)
        {
            visitor(kv.second.Dir, {kv.second.Entries.data(), kv.second.Entries.size()});
        }
    }

    void VFSDirectoryCache::Invalidate(const VFSPath& dir)
    {
        std::unique_lock<std::shared_mutex> lock(m_mutex);
        CacheEntry*                         entry = m_entries.find(dir.Hash());
        if (!entry)
        {
            return;
        }
        entry->Stale = true;
    }

} // namespace ZEngine::Core::VFS