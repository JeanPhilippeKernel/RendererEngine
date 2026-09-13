#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/Registry/AssetRegistry.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Importers/ImportJob.h>
#include <ZEngine/Importers/ImportQueue.h>
#include <ZEngine/ZEngineDef.h>
#include <uuid.h>
#include <atomic>

namespace ZEngine::Importers
{
    struct ImportProgress
    {
        uint32_t Total;
        uint32_t Completed;
        uint32_t Failed;
    };

    class ImportCoordinator
    {
    public:
        static constexpr uint32_t MAX_IMPORTERS      = 16;
        static constexpr uint32_t JOBS_PER_TICK      = 4;
        static constexpr uint32_t MAX_REQUEUE_COUNT  = 3;
        static constexpr uint32_t MAX_IN_FLIGHT_JOBS = 64;

        void                      Initialize(Core::Memory::ArenaAllocator* arena, Core::VFS::IVFSContext* vfs_ctx, Core::VFS::AssetRegistry* registry);

        // Register a concrete importer. Ownership is not transferred; caller ensures lifetime.
        void                      RegisterImporter(IAssetImporter* importer);

        // Enqueue a single asset. Returns the asset UUID from the meta file so callers
        // can act immediately (e.g. create a scene instance) regardless of whether the
        // import runs now or was already cached. Returns a null UUID on failure.
        uuids::uuid               Enqueue(Core::VFS::VFSPath path, ImportPriority priority = ImportPriority::Normal, ImportCallback cb = {});

        // Enqueue multiple paths at Normal priority (scanner batch).
        void                      EnqueueBatch(const Core::Containers::Array<Core::VFS::VFSPath>& paths);

        // Called once per render frame from MainThreadRun.
        // Pops up to JOBS_PER_TICK jobs and dispatches them to ThreadPoolHelper.
        // Jobs whose dependencies are not satisfied are requeued with RequeueCount++.
        void                      Tick();

        // Thread-safe snapshot of import counters.
        ImportProgress            GetProgress() const;

        uint32_t                  QueueSize() const
        {
            return m_queue.Size();
        }

    private:
        struct ImportTask
        {
            ImportCoordinator* Owner    = nullptr;
            ImportJob          Job      = {};
            IAssetImporter*    Importer = nullptr;
            uint32_t           Slot     = 0;
        };

        Core::Containers::Array<IAssetImporter*> m_importers;
        ImportQueue                              m_queue;
        Core::VFS::IVFSContext*                  m_vfs_ctx  = nullptr;
        Core::VFS::AssetRegistry*                m_registry = nullptr;
        Core::Memory::ArenaAllocator*            m_arena    = nullptr;

        PaddedAtomic<uint32_t>                   m_total{};
        PaddedAtomic<uint32_t>                   m_completed{};
        PaddedAtomic<uint32_t>                   m_failed{};
        ImportTask                               m_tasks[MAX_IN_FLIGHT_JOBS]       = {};
        PaddedAtomic<bool>                       m_task_in_use[MAX_IN_FLIGHT_JOBS] = {};

        IAssetImporter*                          Route(const char* ext) const;
        bool                                     DependenciesSatisfied(const Core::VFS::VFSPath& path) const;
        bool                                     TryAcquireTask(uint32_t& out_slot);
        void                                     ReleaseTask(uint32_t slot);
        static void                              RunImportTask(void* context);
        static void                              ExtractExtension(const Core::VFS::VFSPath& path, char out_ext[16]);
    };
} // namespace ZEngine::Importers
