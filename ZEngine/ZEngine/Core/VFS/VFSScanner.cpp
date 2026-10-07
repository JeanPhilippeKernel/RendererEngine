#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/ZEngineDef.h>
#include <thread>

namespace ZEngine::Core::VFS
{
    namespace
    {
        static bool IsAssetExtension(const VFSPath& path)
        {
            const char*      exts[] = {".glb", ".gltf", ".fbx", ".png", ".jpg", ".jpeg", ".hdr", ".ktx", ".zemesh", ".zematerial"};
            VFSPathComponent ext    = path.Extension();
            for (const char* candidate : exts)
                if (ext.Equals(candidate))
                    return true;
            return false;
        }
    } // namespace

    void VFSScanner::Initialize(Core::Memory::ArenaAllocator* page_source)
    {
        ZENGINE_VALIDATE_ASSERT(page_source != nullptr, "VFSScanner::Initialize requires a valid arena for its page size")
        for (int i = 0; i < MaxConcurrentDirLists; ++i)
        {
            page_source->CreateSubArena(SlotArenaReserve, &m_slot_arenas[i], "AssetManager/VFSScannerSlot");
            m_slot_in_use[i].value.store(false, std::memory_order_relaxed);
        }
        for (uint32_t i = 0; i < MaxScanTasks; ++i)
            m_task_in_use[i].value.store(false, std::memory_order_relaxed);
        m_arenas_ready = true;
    }

    int VFSScanner::AcquireSlot()
    {
        for (;;)
        {
            for (int i = 0; i < MaxConcurrentDirLists; ++i)
            {
                bool expected = false;
                if (m_slot_in_use[i].value.compare_exchange_strong(expected, true, std::memory_order_acquire))
                    return i;
            }
            std::this_thread::yield();
        }
    }

    void VFSScanner::ReleaseSlot(int slot)
    {
        m_slot_in_use[slot].value.store(false, std::memory_order_release);
    }

    bool VFSScanner::TryAcquireTask(uint32_t& out_slot)
    {
        for (uint32_t i = 0; i < MaxScanTasks; ++i)
        {
            bool available = false;
            if (m_task_in_use[i].value.compare_exchange_strong(available, true, std::memory_order_acq_rel))
            {
                out_slot = i;
                return true;
            }
        }
        return false;
    }

    void VFSScanner::ReleaseTask(uint32_t slot)
    {
        ZENGINE_VALIDATE_ASSERT(slot < MaxScanTasks, "VFSScanner::ReleaseTask: invalid task slot")
        m_task_in_use[slot].value.store(false, std::memory_order_release);
    }

    void VFSScanner::RunScanTask(void* context)
    {
        ScanTask*   task    = static_cast<ScanTask*>(context);
        VFSScanner* scanner = task->Scanner;

        scanner->ScanDirectory(task->Context, task->Directory);
        scanner->ReleaseTask(task->Slot);
        scanner->OnTaskComplete(scanner->m_cancel_requested.value.load(std::memory_order_relaxed));
    }

    bool VFSScanner::TrySubmitDirectory(ScanContext ctx, VFSPath dir)
    {
        uint32_t slot = 0;
        if (!TryAcquireTask(slot))
            return false;

        ScanTask& task = m_tasks[slot];
        task.Scanner   = this;
        task.Context   = ctx;
        task.Directory = dir;
        task.Slot      = slot;

        m_pending_tasks.value.fetch_add(1, std::memory_order_relaxed);
        if (!ZEngine::Helpers::ThreadPoolHelper::Submit(&task, &VFSScanner::RunScanTask))
        {
            m_pending_tasks.value.fetch_sub(1, std::memory_order_relaxed);
            ReleaseTask(slot);
            return false;
        }
        return true;
    }

    void VFSScanner::Scan(IVFSContext* context, VFSPath root, VFSDirectoryCache* cache)
    {
        ZENGINE_VALIDATE_ASSERT(m_arenas_ready, "VFSScanner::Initialize must be called before Scan")

        if (IsScanning())
        {
            m_cancel_requested.value.store(true, std::memory_order_relaxed);
            while (m_pending_tasks.value.load(std::memory_order_relaxed) > 0)
                std::this_thread::yield();
        }

        m_cancel_requested.value.store(false, std::memory_order_relaxed);
        m_files_found.value.store(0, std::memory_order_relaxed);
        m_dirs_found.value.store(0, std::memory_order_relaxed);
        m_metas_created.value.store(0, std::memory_order_relaxed);
        m_metas_updated.value.store(0, std::memory_order_relaxed);
        m_metas_up_to_date.value.store(0, std::memory_order_relaxed);
        m_errors.value.store(0, std::memory_order_relaxed);
        m_pending_tasks.value.store(0, std::memory_order_relaxed);
        m_is_scanning.value.store(true, std::memory_order_release);
        m_scan_start = std::chrono::steady_clock::now();

        ScanContext ctx{context, root, cache};
        if (!TrySubmitDirectory(ctx, root))
        {
            // All task contexts are occupied. Running this branch inline keeps the
            // scanner bounded without blocking a worker that could drain queued work.
            m_pending_tasks.value.store(1, std::memory_order_relaxed);
            ScanDirectory(ctx, root);
            OnTaskComplete(m_cancel_requested.value.load(std::memory_order_relaxed));
        }
    }

    void VFSScanner::ScanDirectory(ScanContext ctx, VFSPath dir)
    {
        if (m_cancel_requested.value.load(std::memory_order_relaxed))
            return;

        m_dir_semaphore.acquire();
        const int slot   = AcquireSlot();
        auto      result = ctx.Context->List(dir, &m_slot_arenas[slot]);
        ReleaseSlot(slot);
        m_dir_semaphore.release();

        if (result.Failed())
        {
            ReportError(dir, "list directory", result.Error());
            return;
        }

        Containers::Array<VFSDirEntry>& entries = result.Value();

        for (size_t i = 0; i < entries.size(); ++i)
        {
            if (m_cancel_requested.value.load(std::memory_order_relaxed))
                return;

            if (entries[i].IsDirectory)
            {
                m_dirs_found.value.fetch_add(1, std::memory_order_relaxed);
                VFSPath sub = entries[i].Path;
                if (!TrySubmitDirectory(ctx, sub))
                    ScanDirectory(ctx, sub);
            }
            else
            {
                m_files_found.value.fetch_add(1, std::memory_order_relaxed);

                if (IsAssetExtension(entries[i].Path))
                {
                    ZEngine::Managers::AssetType type        = ZEngine::Core::VFS::AssetRegistry::InferTypeFromExtension(entries[i].Path);

                    // Never write metadata using a fabricated hash after an I/O failure.
                    auto                         hash_result = MetaFileIO::ComputeHash(*ctx.Context, entries[i].Path);
                    if (hash_result.Failed())
                    {
                        ReportError(entries[i].Path, "hash asset", hash_result.Error());
                        continue;
                    }

                    // GetOrCreate reads and, if necessary, seeds the embedded identity
                    // under its lock, avoiding redundant metadata reads and unchecked writes.
                    auto meta = MetaFileIO::GetOrCreate(*ctx.Context, entries[i].Path, "VFSScanner", hash_result.Value(), /*use_embedded_identity=*/true);
                    if (meta.Failed())
                    {
                        ReportError(entries[i].Path, "resolve metadata", meta.Error());
                        continue;
                    }
                    switch (meta.Value().Status)
                    {
                        case ImportStatus::New:
                            m_metas_created.value.fetch_add(1, std::memory_order_relaxed);
                            break;
                        case ImportStatus::Stale:
                            m_metas_updated.value.fetch_add(1, std::memory_order_relaxed);
                            break;
                        case ImportStatus::UpToDate:
                            m_metas_up_to_date.value.fetch_add(1, std::memory_order_relaxed);
                            break;
                        default:
                            break;
                    }

                    if (m_registry != nullptr)
                    {
                        m_registry->OnScanFileDiscovered(*ctx.Context, entries[i].Path, type);
                    }
                }
            }
        }

        ctx.Cache->SetListing(dir, std::move(entries));
    }

    void VFSScanner::ReportError(const VFSPath& path, const char* operation, VFSError error)
    {
        m_errors.value.fetch_add(1, std::memory_order_relaxed);
        ZENGINE_LOG_VFS_WARN("VFS scan failed to {} for '{}' (VFS error {})", operation, path.CStr(), static_cast<uint32_t>(error))
    }

    void VFSScanner::OnTaskComplete(bool cancelled)
    {
        const int32_t remaining = m_pending_tasks.value.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (remaining > 0)
            return;

        m_is_scanning.value.store(false, std::memory_order_release);

        if (!cancelled && m_complete_callback)
        {
            ScanStats stats;
            stats.FilesFound    = m_files_found.value.load(std::memory_order_relaxed);
            stats.DirsFound     = m_dirs_found.value.load(std::memory_order_relaxed);
            stats.DurationMs    = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - m_scan_start).count());
            stats.MetasCreated  = m_metas_created.value.load(std::memory_order_relaxed);
            stats.MetasUpdated  = m_metas_updated.value.load(std::memory_order_relaxed);
            stats.MetasUpToDate = m_metas_up_to_date.value.load(std::memory_order_relaxed);
            stats.Errors        = m_errors.value.load(std::memory_order_relaxed);
            m_complete_callback(m_complete_callback_ctx, stats);
        }
    }

    void VFSScanner::Cancel()
    {
        m_cancel_requested.value.store(true, std::memory_order_relaxed);
    }

    bool VFSScanner::IsScanning() const
    {
        return m_is_scanning.value.load(std::memory_order_acquire);
    }

    void VFSScanner::SetOnScanComplete(void* context, void (*callback)(void*, ScanStats))
    {
        m_complete_callback_ctx = context;
        m_complete_callback     = callback;
    }

    VFSScanner::VFSScanner() = default;

    VFSScanner::~VFSScanner()
    {
        m_cancel_requested.value.store(true, std::memory_order_relaxed);
        while (m_pending_tasks.value.load(std::memory_order_relaxed) > 0)
            std::this_thread::yield();
    }

} // namespace ZEngine::Core::VFS
