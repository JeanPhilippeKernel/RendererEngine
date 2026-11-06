#pragma once
#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookSceneCodec.h>

namespace ZEngine::Cook
{
    // Run-owned immutable scene captures. One exact allocation holds stable
    // owners and a reusable path-order index; no per-scene control blocks or
    // retained parsed documents. Map and reject all duplicate logical paths
    // before the first scene capture, then parse one document at a time into
    // the shared graph. The existing graph owns UUID conflict/reference checks.
    //
    // Requires an active input lease and an initialized empty graph using the
    // same limits. Does not discover assets, seal, encode or publish. A failed
    // Capture is terminal: discard the partial graph and Reset this owner.
    // Source/provider, scratch pin and allocator outlive every captured owner,
    // independently of input-lease release. Count sizeof(this) in run accounting.
    class CookSceneCaptureSet
    {
    public:
        CookSceneCaptureSet() = default;
        ~CookSceneCaptureSet();
        CookSceneCaptureSet(const CookSceneCaptureSet&)                                          = delete;
        CookSceneCaptureSet&                               operator=(const CookSceneCaptureSet&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>           Capture(ICookAllocator&, Core::Containers::ArrayView<const Core::VFS::VFSPath>, CookInputLease&, CookSceneCodec&, CookDependencyGraph&, const CookLimits&, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<CookSceneInput> Input(uint32_t) const noexcept;
        // Fill caller-reserved storage without an owned result/path temporary.
        // Invalid arguments preserve output. Borrowed capture/limits die at Reset.
        [[nodiscard]] Core::VFS::VFSResult<void>           InputInto(uint32_t, CookSceneInput*) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void>           Reset() noexcept;
        [[nodiscard]] uint32_t                             Count() const noexcept
        {
            return m_complete ? m_count : 0;
        }
        [[nodiscard]] Core::VFS::VFSError CleanupError() const noexcept
        {
            return m_cleanup_error;
        }

    private:
        struct Entry
        {
            Core::VFS::VFSRelativePath Source  = {};
            CookCapturedFile           Capture = {};
        };
        [[nodiscard]] Core::VFS::VFSResult<void> Fail(Core::VFS::VFSError, CookDiagnosticCode, cstring, CookDiagnostics*, const Core::VFS::VFSRelativePath& = {}, const uuids::uuid& = {});

        ICookAllocator*                          m_allocator     = nullptr;
        Entry*                                   m_entries       = nullptr;
        CookLimits                               m_limits        = {};
        uint32_t                                 m_count         = 0;
        Core::VFS::VFSError                      m_cleanup_error = Core::VFS::VFSError::OK;
        bool                                     m_complete      = false;
    };
} // namespace ZEngine::Cook
