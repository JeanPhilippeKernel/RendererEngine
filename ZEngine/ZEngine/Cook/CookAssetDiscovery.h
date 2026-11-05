#pragma once
#include <ZEngine/Cook/CookDependencyGraph.h>

namespace ZEngine::Cook
{
    // Run-owned captures keyed by stable graph node indexes. Lazy blocks avoid
    // per-asset owner/control-block allocations and never relocate file owners.
    // Discovers each unique identity once, validates every original UUID/path
    // pair before committing edges, and retains no parsed/decoded documents.
    // Does not seal inputs, encode, stage, or publish. The caller seals after
    // discovery; captures remain usable independently of that input lease.
    // Allocator, capture store and scratch-root pin outlive this owner. Count
    // sizeof(this) in fixed run accounting. One synchronous use, then Reset.
    class CookAssetDiscovery
    {
    public:
        CookAssetDiscovery() = default;
        ~CookAssetDiscovery();
        CookAssetDiscovery(const CookAssetDiscovery&)                                 = delete;
        CookAssetDiscovery&                      operator=(const CookAssetDiscovery&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void> Initialize(ICookAllocator&, const CookLimits&);
        // Graph already contains all scenes/roots and uses the same run limits.
        // Failure after traversal starts is terminal: discard graph/Reset owner.
        [[nodiscard]] Core::VFS::VFSResult<void> Discover(CookDependencyGraph&, CookInputLease&, const CookAdapterRegistry&, ICookAllocator& scratch, CookDiagnostics*);
        // Available only after successful discovery. Reparse the owned captured
        // sidecar into caller storage; no original input lookup/reopen. On failure
        // preserve both outputs. Input borrows metadata and this owner's captures.
        [[nodiscard]] Core::VFS::VFSResult<void> ReadInput(uint32_t node, ICookAllocator& scratch, Core::VFS::StrictMetaFileData*, CookAssetInput*) const;
        void                                     Reset() noexcept;
        [[nodiscard]] uint32_t                   Count() const noexcept;
        // First capture cleanup failure retained by Reset.
        // Retained until the next successful Initialize; publication policy can
        // report a warning without depending on the concrete input provider.
        [[nodiscard]] Core::VFS::VFSError        CleanupError() const noexcept
        {
            return m_cleanup_error;
        }

    private:
        static constexpr uint32_t AssetsPerBlock = 32;
        static constexpr uint32_t BlockCount     = (CookLimits{}.MaxAssets + AssetsPerBlock - 1) / AssetsPerBlock;

        enum class State : uint8_t
        {
            Empty = 0,
            Ready,
            Running,
            Complete,
            Failed
        };
        struct CapturedAsset
        {
            CookAssetIdentity          Identity    = {};
            Core::VFS::VFSRelativePath Source      = {};
            CookCapturedFile           Artifact    = {};
            CookCapturedFile           Metadata    = {};
            CookCapturedFile           Environment = {};
        };

        [[nodiscard]] Core::VFS::VFSResult<void>                 Append();
        [[nodiscard]] Core::VFS::VFSResult<void>                 PrepareInput(uint32_t, ICookAllocator&, Core::VFS::StrictMetaFileData*, CookAssetInput*) const;
        [[nodiscard]] Core::VFS::VFSResult<void>                 Fail(const CookDependencyGraph&, uint32_t, Core::VFS::VFSError, CookDiagnostics*, cstring, const CookAssetIdentity* dependency = nullptr);
        [[nodiscard]] const CapturedAsset&                       Asset(uint32_t) const noexcept;

        ICookAllocator*                                          m_allocator     = nullptr;
        CookLimits                                               m_limits        = {};
        Core::Containers::FixedArray<CapturedAsset*, BlockCount> m_blocks        = {};
        uint32_t                                                 m_count         = 0;
        State                                                    m_state         = State::Empty;
        Core::VFS::VFSError                                      m_cleanup_error = Core::VFS::VFSError::OK;
    };
} // namespace ZEngine::Cook
