#pragma once
#include <ZEngine/Cook/CookCapturedFile.h>
#include <ZEngine/Cook/CookDiagnostics.h>

namespace ZEngine::Cook
{
    struct CookAssetCatalogEntry
    {
        CookAssetIdentity          Identity          = {};
        Core::VFS::VFSRelativePath Artifact          = {};
        Core::VFS::VFSRelativePath Metadata          = {};
        Core::VFS::VFSRelativePath EnvironmentSource = {};
    };

    [[nodiscard]] bool IsCookAssetCatalogEntryValid(const CookAssetCatalogEntry&) noexcept;

    using CookCatalogVisitorFn = Core::VFS::VFSResult<void> (*)(void*, const CookAssetCatalogEntry&);

    namespace Detail
    {
        // Run-owned by an input source, not allocated/deleted by the lease. Its
        // capture store must live independently of this input-exclusion session.
        // Only cooperating providers implement this trusted behavioral contract.
        class ICookInputSession
        {
        public:
            virtual Core::VFS::VFSResult<void> RequireDisjoint(const Core::VFS::VFSRootLease&, CookDiagnostics*)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
            }
            virtual Core::VFS::VFSResult<void> PrepareCapture(const CookCaptureContext&, CookDiagnostics*)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
            }
            virtual Core::VFS::VFSResult<void> MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath*, CookDiagnostics*)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
            }
            virtual Core::VFS::VFSResult<void> FindAsset(const CookAssetIdentity&, CookAssetCatalogEntry*, CookDiagnostics*)
            {
                return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
            }
            virtual Core::VFS::VFSResult<void> EnumerateCatalog(CookCatalogVisitorFn, void*, CookDiagnostics*)                 = 0;
            virtual Core::VFS::VFSResult<void> Capture(const Core::VFS::VFSRelativePath&, CookCapturedFile*, CookDiagnostics*) = 0;
            virtual Core::VFS::VFSResult<void> Seal(CookDiagnostics*)                                                          = 0;
            // Exclusion release/cleanup only. Never validates or invokes adapters.
            virtual void                       Release() noexcept                                                              = 0;

        protected:
            virtual ~ICookInputSession() = default;
        };
    } // namespace Detail

    enum class CookInputState : uint8_t
    {
        Invalid = 0,
        Rooted,
        Active,
        Sealed,
        Failed,
    };

    class ICookInputSource;

    // Inline, move-only authority. No boolean-stability constructor or public
    // activation. The input source/session must outlive this synchronous lease.
    class CookInputLease
    {
    public:
        CookInputLease() = default;
        ~CookInputLease();
        CookInputLease(const CookInputLease&)            = delete;
        CookInputLease& operator=(const CookInputLease&) = delete;
        CookInputLease(CookInputLease&& other) noexcept;
        CookInputLease&              operator=(CookInputLease&& other) noexcept;

        [[nodiscard]] CookInputState State() const noexcept
        {
            return m_state;
        }
        [[nodiscard]] bool IsValid() const noexcept
        {
            return m_session != nullptr;
        }
        [[nodiscard]] Core::VFS::VFSResult<void> EnumerateCatalog(CookCatalogVisitorFn visitor, void* context, CookDiagnostics* diagnostics);
        // Rooted authority holds input exclusion but has created no capture
        // storage/catalog. Check output/scratch while it remains held, create
        // the private scratch run, then transition once to Active. Preparation
        // failure is terminal; Reset releases exclusion without validation.
        [[nodiscard]] Core::VFS::VFSResult<void> RequireDisjoint(const Core::VFS::VFSRootLease&, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> PrepareCapture(const CookCaptureContext&, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> MapContextPath(const Core::VFS::VFSPath&, Core::VFS::VFSRelativePath*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> FindAsset(const CookAssetIdentity&, CookAssetCatalogEntry*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> Capture(const Core::VFS::VFSRelativePath& path, CookCapturedFile* output, CookDiagnostics* diagnostics);
        [[nodiscard]] Core::VFS::VFSResult<void> Seal(CookDiagnostics* diagnostics);
        void                                     Reset() noexcept;

    private:
        friend class ICookInputSource;
        Detail::ICookInputSession* m_session = nullptr;
        CookInputState             m_state   = CookInputState::Invalid;
    };

    class ICookInputSource
    {
    public:
        virtual ~ICookInputSource() = default;
        [[nodiscard]] virtual Core::VFS::VFSResult<void> AcquireRoots(CookInputLease*, CookDiagnostics*)
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
        }
        // Provider/pre-owned state not already covered by this run's retained
        // allocator chain. Includes backing overhead, not just payload sizes.
        // Unknown providers fail closed rather than claim zero.
        [[nodiscard]] virtual Core::VFS::VFSResult<uint64_t> FixedWorkingBytes(const ICookAllocator&) const noexcept
        {
            return Core::VFS::VFSResult<uint64_t>::Fail(Core::VFS::VFSError::Unsupported);
        }
        // Native providers may exclude complete fixed objects in separately
        // charged service storage. Opaque providers keep their ordinary profile;
        // containment of the interface alone proves nothing about external state.
        [[nodiscard]] virtual Core::VFS::VFSResult<uint64_t> UncoveredFixedWorkingBytes(const ICookAllocator& covered, const ICookMemoryCoverage&) const noexcept
        {
            return FixedWorkingBytes(covered);
        }
        // Cleanup only, after lease/captures are released and before the call's
        // allocator dies. Idempotent; no validation, retry, or output mutation.
        [[nodiscard]] virtual Core::VFS::VFSResult<void> ReleaseStorage() noexcept
        {
            return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::Unsupported);
        }
        [[nodiscard]] virtual Core::VFS::VFSResult<void> Acquire(const CookCaptureContext&, CookInputLease*, CookDiagnostics*) = 0;

    protected:
        // Called only after the concrete provider has acquired real exclusion and
        // rooted input authority. Failure leaves an existing lease unchanged.
        [[nodiscard]] static Core::VFS::VFSResult<void> BindLease(Detail::ICookInputSession& session, CookInputLease* output, CookInputState initial = CookInputState::Active);
    };
} // namespace ZEngine::Cook
