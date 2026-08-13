#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Core/VFS/Registry/AssetRecord.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Helpers/ThreadPool.h>
#include <ZEngine/Importers/ImportCoordinator.h>
#include <ZEngine/Logging/LoggerDefinition.h>
#include <ZEngine/ZEngineDef.h>
#include <cstdio>
#include <cstring>

namespace ZEngine::Importers
{
    void ImportCoordinator::Initialize(Core::Memory::ArenaAllocator* arena, Core::VFS::IVFSContext* vfs_ctx, Core::VFS::AssetRegistry* registry)
    {
        m_arena    = arena;
        m_vfs_ctx  = vfs_ctx;
        m_registry = registry;
        m_importers.init(arena, MAX_IMPORTERS);
        m_queue.Initialize(arena);
        m_total.value.store(0, std::memory_order_relaxed);
        m_completed.value.store(0, std::memory_order_relaxed);
        m_failed.value.store(0, std::memory_order_relaxed);
    }

    void ImportCoordinator::RegisterImporter(IAssetImporter* importer)
    {
        ZENGINE_VALIDATE_ASSERT(importer != nullptr, "ImportCoordinator::RegisterImporter: null importer")
        ZENGINE_VALIDATE_ASSERT(m_importers.size() < MAX_IMPORTERS, "ImportCoordinator::RegisterImporter: MAX_IMPORTERS reached")
        m_importers.push(importer);
    }

    uuids::uuid ImportCoordinator::Enqueue(Core::VFS::VFSPath path, ImportPriority priority, ImportCallback cb)
    {
        if (!m_vfs_ctx)
            return {};

        // Compute source hash and read/create .meta
        auto hash_result = Core::VFS::MetaFileIO::ComputeHash(*m_vfs_ctx, path);
        auto meta_result = Core::VFS::MetaFileIO::GetOrCreate(*m_vfs_ctx, path, "ImportCoordinator", hash_result.Succeeded() ? hash_result.Value() : 0);
        if (!meta_result.Succeeded())
        {
            ZENGINE_CORE_ERROR("[ImportCoordinator] Enqueue: MetaFileIO::GetOrCreate failed for '{}' (VFS write permission?)", path.CStr())
            return {};
        }

        ImportJob job;
        job.Path               = path;
        job.Meta               = meta_result.Value();
        job.Priority           = priority;
        job.Callback           = cb;

        uuids::uuid asset_uuid = job.Meta.AssetUUID;

        m_queue.Enqueue(job);
        m_total.value.fetch_add(1, std::memory_order_relaxed);

        return asset_uuid;
    }

    void ImportCoordinator::EnqueueBatch(const Core::Containers::Array<Core::VFS::VFSPath>& paths)
    {
        for (size_t i = 0; i < paths.size(); ++i)
            Enqueue(paths[i], ImportPriority::Normal);
    }

    IAssetImporter* ImportCoordinator::Route(const char* ext) const
    {
        for (size_t i = 0; i < m_importers.size(); ++i)
            if (m_importers[i]->CanImport(ext))
                return m_importers[i];
        return nullptr;
    }

    bool ImportCoordinator::DependenciesSatisfied(const Core::VFS::VFSPath& path) const
    {
        if (!m_registry)
            return true;

        const Core::VFS::AssetRecord* rec = m_registry->FindByPath(path);
        if (!rec)
            return true; // not yet registered — no known deps, allow

        Core::Containers::Array<uuids::uuid> deps;
        deps.init(m_arena, 8);
        m_registry->GetGraph().CopyDependencies(rec->UUID, deps);

        for (size_t i = 0; i < deps.size(); ++i)
        {
            const Core::VFS::AssetRecord* dep = m_registry->FindByUUID(deps[i]);
            if (!dep || dep->State != Core::VFS::AssetState::Loaded)
                return false;
        }
        return true;
    }

    void ImportCoordinator::ExtractExtension(const Core::VFS::VFSPath& path, char out_ext[16])
    {
        out_ext[0]                      = '\0';
        Core::VFS::VFSPathComponent ext = path.Extension();
        if (ext.Empty() || !ext.Data)
            return;
        // Skip leading dot
        const char* src = ext.Data;
        size_t      len = ext.Length;
        if (len > 0 && src[0] == '.')
        {
            ++src;
            --len;
        }
        size_t copy = len < 15 ? len : 15;
        Helpers::secure_memcpy(out_ext, 16, src, copy);
        out_ext[copy] = '\0';
    }

    void ImportCoordinator::Tick()
    {
        if (!m_vfs_ctx)
            return;

        for (uint32_t i = 0; i < JOBS_PER_TICK; ++i)
        {
            ImportJob job;
            if (!m_queue.TryPop(job))
                break;

            // Dependency check
            if (!DependenciesSatisfied(job.Path))
            {
                if (job.RequeueCount >= MAX_REQUEUE_COUNT)
                {
                    std::snprintf(job.DiagnosticMessage, sizeof(job.DiagnosticMessage), "Dependency cycle or unresolvable upstream asset");
                    ZENGINE_CORE_ERROR("[ImportCoordinator] {} — {}", job.Path.CStr(), job.DiagnosticMessage)
                    if (m_registry)
                        m_registry->SetState(job.Meta.AssetUUID, Core::VFS::AssetState::Failed);
                    job.Callback.Invoke(false);
                    m_failed.value.fetch_add(1, std::memory_order_relaxed);
                    m_total.value.fetch_sub(1, std::memory_order_relaxed);
                }
                else
                {
                    job.RequeueCount++;
                    m_queue.Enqueue(job);
                }
                continue;
            }

            // Route to importer
            char ext[16];
            ExtractExtension(job.Path, ext);
            IAssetImporter* importer = Route(ext);

            if (!importer)
            {
                std::snprintf(job.DiagnosticMessage, sizeof(job.DiagnosticMessage), "No importer registered for extension: %s", ext);
                ZENGINE_CORE_WARN("[ImportCoordinator] {} — {}", job.Path.CStr(), job.DiagnosticMessage)
                if (m_registry)
                    m_registry->SetState(job.Meta.AssetUUID, Core::VFS::AssetState::Failed);
                job.Callback.Invoke(false);
                m_failed.value.fetch_add(1, std::memory_order_relaxed);
                m_total.value.fetch_sub(1, std::memory_order_relaxed);
                continue;
            }

            // Mark as importing
            if (m_registry)
                m_registry->SetState(job.Meta.AssetUUID, Core::VFS::AssetState::Importing);

            // Dispatch to thread pool
            Helpers::ThreadPoolHelper::Submit([this, job, importer]() mutable {
                auto result = importer->Import(*m_vfs_ctx, job.Path, job.Meta);

                if (result.Succeeded())
                {
                    ZENGINE_CORE_INFO("[ImportCoordinator] Imported '{}'", job.Path.CStr())
                    if (m_registry)
                        m_registry->SetState(job.Meta.AssetUUID, Core::VFS::AssetState::Loaded);
                    job.Callback.Invoke(true);
                    m_completed.value.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    std::snprintf(const_cast<char*>(job.DiagnosticMessage), sizeof(job.DiagnosticMessage), "Import failed");
                    ZENGINE_CORE_ERROR("[ImportCoordinator] Failed to import '{}' — {}", job.Path.CStr(), job.DiagnosticMessage)
                    if (m_registry)
                        m_registry->SetState(job.Meta.AssetUUID, Core::VFS::AssetState::Failed);
                    job.Callback.Invoke(false);
                    m_failed.value.fetch_add(1, std::memory_order_relaxed);
                }
                m_total.value.fetch_sub(1, std::memory_order_relaxed);
            });
        }
    }

    ImportProgress ImportCoordinator::GetProgress() const
    {
        return {
            m_total.value.load(std::memory_order_relaxed),
            m_completed.value.load(std::memory_order_relaxed),
            m_failed.value.load(std::memory_order_relaxed),
        };
    }
} // namespace ZEngine::Importers
