#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <chrono>
#include <semaphore>

namespace ZEngine
{
    namespace Core
    {
        namespace VFS
        {
            class AssetRegistry;
        }
    } // namespace Core
} // namespace ZEngine

namespace ZEngine::Core::VFS
{
    struct ScanStats
    {
        uint64_t FilesFound    = 0;
        uint64_t DirsFound     = 0;
        uint64_t DurationMs    = 0;
        uint64_t MetasCreated  = 0; // .meta generated for the first time
        uint64_t MetasUpdated  = 0; // .meta existed but source SHA changed
        uint64_t MetasUpToDate = 0; // .meta existed and SHA matched
    };

    struct VFSScanner
    {
        static constexpr int    MaxConcurrentDirLists = 4;
        static constexpr size_t SlotArenaReserve      = ZMega(128);

        VFSScanner();
        ~VFSScanner();

        void Initialize(Core::Memory::ArenaAllocator* page_source);
        void Scan(IVFSContext* context, VFSPath root, VFSDirectoryCache* cache);

        void Cancel();

        bool IsScanning() const;

        void SetOnScanComplete(void* context, void (*callback)(void*, ScanStats));
        void SetAssetRegistry(ZEngine::Core::VFS::AssetRegistry* registry)
        {
            m_registry = registry;
        }

    private:
        struct ScanContext
        {
            IVFSContext*       Context = nullptr;
            VFSPath            Root    = {};
            VFSDirectoryCache* Cache   = nullptr;
        };

        void  ScanDirectory(ScanContext ctx, VFSPath dir);
        void  OnTaskComplete(bool cancelled);

        int   AcquireSlot();
        void  ReleaseSlot(int slot);

        void* m_complete_callback_ctx                 = nullptr;
        void (*m_complete_callback)(void*, ScanStats) = nullptr;

        PaddedAtomic<bool>                             m_is_scanning{};
        PaddedAtomic<bool>                             m_cancel_requested{};
        PaddedAtomic<int32_t>                          m_pending_tasks{};
        PaddedAtomic<uint64_t>                         m_files_found{};
        PaddedAtomic<uint64_t>                         m_dirs_found{};
        PaddedAtomic<uint64_t>                         m_metas_created{};
        PaddedAtomic<uint64_t>                         m_metas_updated{};
        PaddedAtomic<uint64_t>                         m_metas_up_to_date{};
        std::chrono::steady_clock::time_point          m_scan_start{};

        std::counting_semaphore<MaxConcurrentDirLists> m_dir_semaphore{MaxConcurrentDirLists};

        Core::Memory::ArenaAllocator                   m_slot_arenas[MaxConcurrentDirLists];
        PaddedAtomic<bool>                             m_slot_in_use[MaxConcurrentDirLists];
        bool                                           m_arenas_ready = false;
        ZEngine::Core::VFS::AssetRegistry*             m_registry     = nullptr;
    };

} // namespace ZEngine::Core::VFS