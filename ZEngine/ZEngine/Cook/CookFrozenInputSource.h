#pragma once
#include <ZEngine/Cook/CookAssetCatalog.h>
#include <ZEngine/Cook/CookFrozenFileSet.h>
#include <ZEngine/Cook/CookLimits.h>

namespace ZEngine::Cook
{
    // One synchronous RUN's input source and capture-storage owner. Construct a
    // new instance per run; the frozen owner may serve multiple independent
    // sources/readers. The source/allocator/scratch pin outlive all captures and
    // the input lease. Releasing the lease only releases input exclusion/indexes;
    // immutable disk captures remain readable until their own destruction.
    // FixedWorkingBytes includes this object and the retained owner/bindings.
    // Bounded parser call-stack scratch is separate fixed run accounting;
    // retained per-call buffers use the orchestrator's allocator.
    class CookFrozenInputSource final : public ICookInputSource, private Detail::ICookInputSession
    {
    public:
        explicit CookFrozenInputSource(CookFrozenLibraryOwner& owner) noexcept;
        ~CookFrozenInputSource() override;
        CookFrozenInputSource(const CookFrozenInputSource&)                                  = delete;
        CookFrozenInputSource&                       operator=(const CookFrozenInputSource&) = delete;
        [[nodiscard]] Core::VFS::VFSResult<void>     AcquireRoots(CookInputLease*, CookDiagnostics*) override;
        [[nodiscard]] Core::VFS::VFSResult<void>     Acquire(const CookCaptureContext&, CookInputLease*, CookDiagnostics*) override;
        [[nodiscard]] Core::VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator& covered) const noexcept override
        {
            return FixedBytes(covered, nullptr);
        }
        [[nodiscard]] Core::VFS::VFSResult<uint64_t> UncoveredFixedWorkingBytes(const ICookAllocator& covered, const ICookMemoryCoverage& coverage) const noexcept override
        {
            return FixedBytes(covered, &coverage);
        }
        [[nodiscard]] Core::VFS::VFSResult<void> ReleaseStorage() noexcept override;
        [[nodiscard]] Core::VFS::VFSError        CleanupError() const noexcept
        {
            return m_store.CleanupError();
        }

    private:
        [[nodiscard]] Core::VFS::VFSResult<uint64_t> FixedBytes(const ICookAllocator& covered, const ICookMemoryCoverage* coverage) const noexcept
        {
            const auto owner = m_owner->FixedWorkingBytes(covered, coverage);
            if (owner.Failed())
                return owner;
            const uint64_t state = coverage && coverage->Contains(this, sizeof(*this)) ? 0 : sizeof(*this);
            if (owner.Value() > UINT64_MAX - state)
                return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::SizeLimitExceeded);
            return Core::VFS::VFSResult<uint64_t>::Ok(state + owner.Value());
        }

        enum class State : uint8_t
        {
            Idle = 0,
            Rooted,
            Acquiring,
            Active,
            Terminal,
            Released
        };
        Core::VFS::VFSResult<void>               RequireDisjoint(const Core::VFS::VFSRootLease&, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               PrepareCapture(const CookCaptureContext&, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath*, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               FindAsset(const CookAssetIdentity&, CookAssetCatalogEntry*, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               EnumerateCatalog(CookCatalogVisitorFn, void*, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               Capture(const Core::VFS::VFSRelativePath&, CookCapturedFile*, CookDiagnostics*) override;
        Core::VFS::VFSResult<void>               Seal(CookDiagnostics*) override;
        void                                     Release() noexcept override;
        [[nodiscard]] Core::VFS::VFSResult<void> BuildCatalog(CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> CaptureFile(const Core::VFS::VFSRelativePath&, uint64_t, CookCapturedFile*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> Fail(CookDiagnostics*, CookDiagnosticCode, const Core::VFS::VFSRelativePath&, Core::VFS::VFSError, cstring);

        CookFrozenLibraryOwner*                  m_owner                 = nullptr;
        ICookAllocator*                          m_allocator             = nullptr;
        CookFrozenLibraryReadLease               m_read                  = {};
        CookFrozenFileSet                        m_files                 = {};
        CookAssetCatalog                         m_catalog               = {};
        CookCaptureStore                         m_store                 = {};
        CookLimits                               m_limits                = {};
        Core::Containers::ArrayView<uint8_t>     m_stream                = {};
        uint64_t                                 m_unique_captured_bytes = 0;
        Core::VFS::VFSError                      m_error                 = Core::VFS::VFSError::OK;
        Core::VFS::VFSRelativePath               m_error_source          = {};
        State                                    m_state                 = State::Idle;
        bool                                     m_bound                 = false;
        Core::VFS::VFSWorkingMemoryReservation*  m_root_memory           = nullptr;
        uint32_t                                 m_root_memory_count     = 0;
    };
} // namespace ZEngine::Cook
