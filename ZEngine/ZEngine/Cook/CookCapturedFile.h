#pragma once
#include <ZEngine/Cook/CookDigest.h>
#include <ZEngine/Cook/CookMemory.h>
#include <ZEngine/Core/VFS/VFSRootLease.h>

namespace ZEngine::Cook
{
    struct CookLimits;
    class CookCaptureStore;

    struct CookCaptureContext
    {
        Core::VFS::VFSScratchRootLease*     Scratch       = nullptr;
        Core::VFS::VFSRelativePath          ScratchRun    = {};
        ICookAllocator*                     Allocator     = nullptr;
        const CookLimits*                   Limits        = nullptr;
        Core::VFS::IVFSWorkingMemoryBudget* WorkingMemory = nullptr;
    };

    class ICookCapturedFile
    {
    public:
        virtual ~ICookCapturedFile()                                                                                                = default;
        [[nodiscard]] virtual uint64_t                     Size() const noexcept                                                    = 0;
        [[nodiscard]] virtual Digest256                    Digest() const noexcept                                                  = 0;
        [[nodiscard]] virtual Core::VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const = 0;
    };

    // Exact checked range transfer shared by captured-byte parsers/adapters.
    // Accept short reads, reject over-reporting/early EOF, and never allocate.
    [[nodiscard]] Core::VFS::VFSResult<void> ReadCookCapturedRange(const ICookCapturedFile&, uint64_t offset, Core::Containers::ArrayView<uint8_t>);

    // Immutable owned bytes, not a view/reopen of the source library. No per-file
    // heap/control block: ownership and digest are inline and move-only. The run's
    // capture store, allocator, and scratch pin outlive these values, independently
    // of the input lease/seal. Disk reads use pinned reopen, not one held FD each.
    class CookCapturedFile final : public ICookCapturedFile
    {
    public:
        CookCapturedFile() = default;
        ~CookCapturedFile() override;
        CookCapturedFile(const CookCapturedFile&)            = delete;
        CookCapturedFile& operator=(const CookCapturedFile&) = delete;
        CookCapturedFile(CookCapturedFile&& other) noexcept;
        CookCapturedFile&                          operator=(CookCapturedFile&& other) noexcept;
        [[nodiscard]] bool                         IsValid() const noexcept;
        [[nodiscard]] uint64_t                     Size() const noexcept override;
        [[nodiscard]] Digest256                    Digest() const noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<size_t> Read(uint64_t offset, Core::Containers::ArrayView<uint8_t> output) const override;
        // Explicit cleanup allows the orchestrator to report warnings. Destruction
        // also cleans up; errors are retained by the store, never silently retried
        // against a live context or used to fail an already-published package.
        [[nodiscard]] Core::VFS::VFSResult<void>   Reset() noexcept;

    private:
        friend class CookCaptureStore;
        CookCaptureStore*          m_store     = nullptr;
        uint8_t*                   m_memory    = nullptr;
        Core::VFS::VFSRelativePath m_disk_path = {};
        uint64_t                   m_size      = 0;
        Digest256                  m_digest    = {};
    };

    // Run-owned storage/budget domain, never input-lease-owned. The frozen input
    // provider uses this only after acquiring its exclusion/root anchors. This
    // storage primitive does not itself establish input consistency or sealing.
    class CookCaptureStore
    {
    public:
        CookCaptureStore() = default;
        ~CookCaptureStore();
        CookCaptureStore(const CookCaptureStore&)                                   = delete;
        CookCaptureStore&                        operator=(const CookCaptureStore&) = delete;
        // Stream storage is already charged to context.Allocator by its caller;
        // reuse it across disk captures. Empty storage permits memory captures.
        // Captures/rechecks separately reserve principal temporary hash/owner
        // state before source I/O. This is not a full helper/compiler-frame
        // bound; an unsupported fixed-state allocator fails closed.
        [[nodiscard]] Core::VFS::VFSResult<void> Initialize(const CookCaptureContext& context, Core::Containers::ArrayView<uint8_t> stream_storage);
        [[nodiscard]] Core::VFS::VFSResult<void> CaptureMemory(Core::VFS::IVFSFile& source, uint64_t maximum_bytes, CookCapturedFile* output);
        [[nodiscard]] Core::VFS::VFSResult<void> CaptureDisk(Core::VFS::IVFSFile& source, uint64_t maximum_bytes, CookCapturedFile* output);
        // Seal-time content recheck through the owner's still-held source root.
        // Reuses the precharged stream; no new capture, allocation, or source
        // close. Changed bytes/size return Corrupted; I/O errors stay intact.
        // Native/catalog/root identities remain the input provider's obligation.
        [[nodiscard]] Core::VFS::VFSResult<void> VerifySource(Core::VFS::IVFSFile& source, uint64_t expected_size, const Digest256& expected_digest) const;
        [[nodiscard]] uint64_t                   LiveBytes() const noexcept
        {
            return m_live_bytes;
        }
        [[nodiscard]] size_t OutstandingCaptures() const noexcept
        {
            return m_outstanding;
        }
        [[nodiscard]] Core::VFS::VFSError CleanupError() const noexcept
        {
            return m_cleanup_error;
        }

    private:
        friend class CookCapturedFile;
        [[nodiscard]] Core::VFS::VFSResult<void> Capture(Core::VFS::IVFSFile&, uint64_t maximum_bytes, bool disk, CookCapturedFile*);
        [[nodiscard]] Core::VFS::VFSResult<void> Release(CookCapturedFile&) noexcept;
        ICookAllocator*                          m_allocator     = nullptr;
        Core::VFS::VFSScratchRootLease*          m_scratch       = nullptr;
        Core::VFS::VFSRelativePath               m_run           = {};
        Core::Containers::ArrayView<uint8_t>     m_stream        = {};
        uint64_t                                 m_limit         = 0;
        uint64_t                                 m_live_bytes    = 0;
        uint64_t                                 m_sequence      = 0;
        size_t                                   m_outstanding   = 0;
        Core::VFS::VFSError                      m_cleanup_error = Core::VFS::VFSError::OK;
    };
} // namespace ZEngine::Cook
