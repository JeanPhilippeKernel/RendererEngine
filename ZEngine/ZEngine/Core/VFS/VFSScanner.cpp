#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/ThreadPool.h>

namespace ZEngine::Core::VFS
{
    void VFSScanner::Initialize(Core::Memory::ArenaAllocator* page_source)
    {
        ZENGINE_VALIDATE_ASSERT(page_source != nullptr, "VFSScanner::Initialize requires a valid arena for its page size")
        for (int i = 0; i < MaxConcurrentDirLists; ++i)
        {
            m_slot_arenas[i].Initialize(SlotArenaReserve, page_source->m_mem_page_size);
            m_slot_in_use[i].store(false, std::memory_order_relaxed);
        }
        m_arenas_ready = true;
    }

    int VFSScanner::AcquireSlot()
    {
        for (;;)
        {
            for (int i = 0; i < MaxConcurrentDirLists; ++i)
            {
                bool expected = false;
                if (m_slot_in_use[i].compare_exchange_strong(expected, true, std::memory_order_acquire))
                {
                    return i;
                }
            }
            std::this_thread::yield();
        }
    }

    void VFSScanner::ReleaseSlot(int slot)
    {
        m_slot_in_use[slot].store(false, std::memory_order_release);
    }

    void VFSScanner::Scan(IVFSContext* context, VFSPath root, VFSDirectoryCache* cache)
    {
        ZENGINE_VALIDATE_ASSERT(m_arenas_ready, "VFSScanner::Initialize must be called before Scan")

        if (IsScanning())
        {
            m_cancel_requested = true;
            while (m_pending_tasks.load() > 0)
            {
                std::this_thread::yield();
            }
        }

        m_cancel_requested = false;
        m_files_found      = 0;
        m_dirs_found       = 0;
        m_pending_tasks    = 1;
        m_is_scanning      = true;
        m_scan_start       = std::chrono::steady_clock::now();

        ScanContext ctx{context, root, cache};

        ZEngine::Helpers::ThreadPoolHelper::Submit([this, ctx, root] {
            ScanDirectory(ctx, root);
            OnTaskComplete(m_cancel_requested.load());
        });
    }

    void VFSScanner::ScanDirectory(ScanContext ctx, VFSPath dir)
    {
        if (m_cancel_requested)
        {
            return;
        }

        m_dir_semaphore.acquire();
        const int slot   = AcquireSlot();
        auto      result = ctx.Context->List(dir, &m_slot_arenas[slot]);
        ReleaseSlot(slot);
        m_dir_semaphore.release();

        if (result.Failed())
        {
            return;
        }

        Containers::Array<VFSDirEntry>& entries = result.Value();

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (m_cancel_requested)
            {
                return;
            }

            if (entries[i].IsDirectory)
            {
                m_dirs_found++;
                m_pending_tasks++;
                VFSPath sub = entries[i].Path;
                ZEngine::Helpers::ThreadPoolHelper::Submit([this, ctx, sub] {
                    ScanDirectory(ctx, sub);
                    OnTaskComplete(m_cancel_requested.load());
                });
            }
            else
            {
                m_files_found++;
            }
        }

        ctx.Cache->SetListing(dir, std::move(entries));
    }

    void VFSScanner::OnTaskComplete(bool cancelled)
    {
        const int32_t remaining = m_pending_tasks.fetch_sub(1) - 1;
        if (remaining > 0)
        {
            return;
        }

        m_is_scanning = false;

        if (!cancelled && m_complete_callback)
        {
            ScanStats stats;
            stats.FilesFound = m_files_found.load();
            stats.DirsFound  = m_dirs_found.load();
            stats.DurationMs = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_scan_start).count());
            m_complete_callback(stats);
        }
    }

    void VFSScanner::Cancel()
    {
        m_cancel_requested = true;
    }

    bool VFSScanner::IsScanning() const
    {
        return m_is_scanning.load();
    }

    void VFSScanner::SetOnScanComplete(std::function<void(ScanStats)> callback)
    {
        m_complete_callback = std::move(callback);
    }

    VFSScanner::VFSScanner() = default;

    VFSScanner::~VFSScanner()
    {
        m_cancel_requested = true;
        while (m_pending_tasks.load() > 0)
        {
            std::this_thread::yield();
        }
    }

} // namespace ZEngine::Core::VFS