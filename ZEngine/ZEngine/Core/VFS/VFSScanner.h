#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <ZEngine/Core/VFS/VFSDirectoryCache.h>
#include <ZEngine/ZEngineDef.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <mutex>
#include <semaphore>

namespace ZEngine::Core::VFS
{
    struct ScanStats
    {
        uint64_t FilesFound = 0;
        uint64_t DirsFound  = 0;
        uint64_t DurationMs = 0;
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

        void SetOnScanComplete(std::function<void(ScanStats)> callback);

    private:
        struct ScanContext
        {
            IVFSContext*       Context = nullptr;
            VFSPath            Root    = {};
            VFSDirectoryCache* Cache   = nullptr;
        };

        void                                           ScanDirectory(ScanContext ctx, VFSPath dir);
        void                                           OnTaskComplete(bool cancelled);

        int                                            AcquireSlot();
        void                                           ReleaseSlot(int slot);

        std::function<void(ScanStats)>                 m_complete_callback;

        std::atomic<bool>                              m_is_scanning{false};
        std::atomic<bool>                              m_cancel_requested{false};
        std::atomic<int32_t>                           m_pending_tasks{0};
        std::atomic<uint64_t>                          m_files_found{0};
        std::atomic<uint64_t>                          m_dirs_found{0};
        std::chrono::steady_clock::time_point          m_scan_start{};

        std::counting_semaphore<MaxConcurrentDirLists> m_dir_semaphore{MaxConcurrentDirLists};

        Core::Memory::ArenaAllocator                   m_slot_arenas[MaxConcurrentDirLists];
        PaddedAtomic<bool>                             m_slot_in_use[MaxConcurrentDirLists];
        bool                                           m_arenas_ready = false;
    };

} // namespace ZEngine::Core::VFS