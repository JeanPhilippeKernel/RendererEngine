#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Core/VFS/VFSScanner.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/ZEngineDef.h>
#include <nlohmann/json.hpp>
#include <uuid.h>
#include <cstring>
#include <optional>
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

        // .zemesh/.zematerial embed their own UUID at cook time. Peeking it here
        // lets the caller pre-seed a .meta instead of GetOrCreate minting an
        // unrelated random one (#755). Duplicated rather than depending on
        // Importers from Core::VFS — ZEMESH_MAGIC/ASSET_FILE_VERSION are already
        // shared, low-level constants (ZEngineDef.h).
        static std::optional<uuids::uuid> PeekEmbeddedUUID(IVFSContext& ctx, const VFSPath& path, Managers::AssetType type)
        {
            if (type != Managers::AssetType::MESH && type != Managers::AssetType::MATERIAL)
                return std::nullopt;

            auto open_result = ctx.Open(path, VFSOpenFlags::Read);
            if (open_result.Failed())
                return std::nullopt;
            IVFSFile*                  file = open_result.Value();

            std::optional<uuids::uuid> result;

            if (type == Managers::AssetType::MESH)
            {
                // Layout matches AssetCodec::AssetMeshFileHeader: uint32 magic, uint32 version, 16-byte uuid.
                uint8_t buf[24];
                auto    read_result = file->Read({buf, sizeof(buf)}, 0);
                if (read_result.Succeeded() && read_result.Value() == sizeof(buf))
                {
                    uint32_t magic = 0, version = 0;
                    std::memcpy(&magic, buf, sizeof(magic));
                    std::memcpy(&version, buf + sizeof(magic), sizeof(version));
                    if (magic == ZEMESH_MAGIC && version == ASSET_FILE_VERSION)
                    {
                        uuids::uuid id;
                        std::memcpy(&id, buf + sizeof(magic) + sizeof(version), sizeof(id));
                        if (!id.is_nil())
                            result = id;
                    }
                }
            }
            else // MATERIAL — JSON, top-level "uuid" field (matches AssetCodec::SerializeMaterialAssetFile)
            {
                static constexpr size_t kReadCap    = 16384;
                auto                    size_result = file->Size();
                if (size_result.Succeeded() && size_result.Value() < kReadCap)
                {
                    uint8_t buf[kReadCap];
                    size_t  size        = (size_t) size_result.Value();
                    auto    read_result = file->ReadAll({buf, size});
                    if (read_result.Succeeded())
                    {
                        buf[size] = '\0';
                        auto j    = nlohmann::json::parse(reinterpret_cast<const char*>(buf), nullptr, /*allow_exceptions=*/false);
                        if (!j.is_discarded() && j.contains("uuid") && j["uuid"].is_string())
                        {
                            auto parsed = uuids::uuid::from_string(j["uuid"].get<std::string>());
                            if (parsed.has_value() && !parsed.value().is_nil())
                                result = parsed.value();
                        }
                    }
                }
            }

            ctx.Close(file);
            return result;
        }
    } // namespace

    void VFSScanner::Initialize(Core::Memory::ArenaAllocator* page_source)
    {
        ZENGINE_VALIDATE_ASSERT(page_source != nullptr, "VFSScanner::Initialize requires a valid arena for its page size")
        for (int i = 0; i < MaxConcurrentDirLists; ++i)
        {
            m_slot_arenas[i].Initialize(SlotArenaReserve, page_source->m_mem_page_size);
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
            return;

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
                    ZEngine::Managers::AssetType type = ZEngine::Core::VFS::AssetRegistry::InferTypeFromExtension(entries[i].Path);

                    // Pre-seed .meta from the embedded UUID before GetOrCreate mints an
                    // unrelated random one (#755).
                    if (MetaFileIO::Read(*ctx.Context, entries[i].Path).Failed())
                    {
                        auto embedded = PeekEmbeddedUUID(*ctx.Context, entries[i].Path, type);
                        if (embedded.has_value())
                        {
                            MetaFileData seed = {};
                            seed.AssetUUID    = *embedded;
                            Helpers::secure_strncpy(seed.ImporterName, sizeof(seed.ImporterName), "VFSScanner", sizeof(seed.ImporterName) - 1);
                            MetaFileIO::Write(*ctx.Context, entries[i].Path, seed);
                        }
                    }

                    auto hash_result = MetaFileIO::ComputeHash(*ctx.Context, entries[i].Path);
                    auto meta        = MetaFileIO::GetOrCreate(*ctx.Context, entries[i].Path, "VFSScanner", hash_result.Succeeded() ? hash_result.Value() : 0);
                    if (meta.Succeeded())
                    {
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
        }

        ctx.Cache->SetListing(dir, std::move(entries));
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
