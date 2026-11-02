#pragma once
#include <ZEngine/Core/VFS/IVFSContext.h>
#include <atomic>
#include <memory>

namespace Tetragrama::Panels
{
    struct DirectoryEntry
    {
        char name[ZEngine::Core::VFS::VFS_MAX_PATH]      = {};
        char full_path[ZEngine::Core::VFS::VFS_MAX_PATH] = {};
        bool is_dir                                      = false;
    };

    /// One worker lease and one coalesced desired request. Main-thread methods
    /// never list, sort, wait, or fall back to inline execution on pool pressure.
    /// The borrowed VFS and this owner must survive until workers have joined.
    /// Arena-owned panels explicitly Close(), then ShutdownAfterWorkersJoined().
    class DirectoryListing
    {
    public:
        static constexpr uint32_t Capacity = 4096;
        using Submitter                    = bool (*)(void*, void*, void (*)(void*));
        enum class Status
        {
            Idle = 0,
            Loading,
            Ready,
            Failed
        };
        struct Result
        {
            DirectoryEntry Entries[Capacity]{};
            uint32_t       Count     = 0;
            bool           Truncated = false;
            bool           Succeeded = false;
        };

        DirectoryListing();
        ~DirectoryListing();
        DirectoryListing(const DirectoryListing&)            = delete;
        DirectoryListing& operator=(const DirectoryListing&) = delete;

        void              Request(ZEngine::Core::VFS::IVFSContext& vfs, const ZEngine::Core::VFS::VFSPath& path, bool directories_only = false);
        void              Invalidate();
        /// Returns a newly completed, current-generation result once. Valid until
        /// the next Request/Poll/Shutdown; callers publish only prepared live entries.
        const Result*     Poll();
        Status            GetStatus() const
        {
            return m_status;
        }
        void        Close();
        void        ShutdownAfterWorkersJoined();
        /// Injectable bounded executor for deterministic tests; set only when idle.
        void        SetSubmitter(Submitter submit, void* context = nullptr);

        static bool EntryLess(const DirectoryEntry& a, const DirectoryEntry& b);
        static void Prepare(const ZEngine::Core::Containers::Array<ZEngine::Core::VFS::VFSDirEntry>& raw, DirectoryEntry* output, uint32_t capacity, uint32_t& count, bool& truncated, bool directories_only = false, const ZEngine::Core::VFS::VFSPath* parent = nullptr);

    private:
        struct Work;
        std::unique_ptr<Work>            m_work{};
        ZEngine::Core::VFS::IVFSContext* m_vfs              = nullptr;
        ZEngine::Core::VFS::VFSPath      m_path             = {};
        uint64_t                         m_generation       = 0;
        bool                             m_directories_only = false;
        bool                             m_pending          = false;
        bool                             m_requested        = false;
        bool                             m_closed           = false;
        Status                           m_status           = Status::Idle;
        Submitter                        m_submit           = &Submit;
        void*                            m_submit_context   = nullptr;
        static bool                      Submit(void*, void*, void (*)(void*));
        static void                      Run(void*);
    };
} // namespace Tetragrama::Panels
