
#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSBackend.h>
#include <ZEngine/ZEngineDef.h>
#include <functional>
#include <shared_mutex>

namespace ZEngine::Core::VFS
{
    struct VFSDirectoryCache
    {
        VFSDirectoryCache()  = default;
        ~VFSDirectoryCache() = default;

        void                                           Initialize(Memory::ArenaAllocator* arena);
        Core::Containers::ArrayView<const VFSDirEntry> GetListing(const VFSPath& dir) const;

        void                                           SetListing(const VFSPath& dir, Core::Containers::Array<VFSDirEntry>&& entries);

        void                                           Invalidate(const VFSPath& dir);

        bool                                           IsStale(const VFSPath& dir) const;

        void                                           Clear();

        size_t                                         Size() const;

        void                                           ForEachDir(std::function<void(const VFSPath&, Core::Containers::ArrayView<const VFSDirEntry>)> visitor) const;

    private:
        struct CacheEntry
        {
            VFSPath                              Dir     = {};
            Core::Containers::Array<VFSDirEntry> Entries = {};
            bool                                 Stale   = true;
        };

        Core::Containers::UnorderedHashMap<uint64_t, CacheEntry> m_entries;
        mutable std::shared_mutex                                m_mutex;
        Memory::ArenaAllocator*                                  m_arena = nullptr;
    };

} // namespace ZEngine::Core::VFS