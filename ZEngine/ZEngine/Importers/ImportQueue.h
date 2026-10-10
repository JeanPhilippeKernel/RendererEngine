#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Importers/ImportJob.h>
#include <mutex>

namespace ZEngine::Importers
{
    // Thread-safe max-heap ordered by ImportPriority with O(1) path-hash deduplication.
    // Enqueueing a path already in the queue upgrades its priority if the new priority
    // is higher; otherwise it is a no-op. All operations are guarded by m_mutex.
    class ImportQueue
    {
    public:
        void     Initialize(Core::Memory::ArenaAllocator* arena);

        // Insert a job. If the same path is already queued at a lower priority,
        // upgrade it. If priority is equal or lower, no-op.
        void     Enqueue(ImportJob job);

        // Remove and return the highest-priority job. Returns false if empty.
        bool     TryPop(ImportJob& out_job);

        bool     IsEmpty() const;
        uint32_t Size() const;
        bool     Contains(const Core::VFS::VFSPath& path) const;

    private:
        mutable std::mutex                                     m_mutex;
        Core::Containers::Array<ImportJob>                     m_heap;  // max-heap by Priority
        Core::Containers::UnorderedHashMap<uint64_t, uint32_t> m_index; // path hash → heap pos

        void                                                   SiftUp(uint32_t pos);
        void                                                   SiftDown(uint32_t pos);
        void                                                   SwapAt(uint32_t a, uint32_t b);
    };
} // namespace ZEngine::Importers
