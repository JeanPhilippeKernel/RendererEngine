#pragma once
#include <ZEngine/Cook/CookIncrementalReuse.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>

namespace ZEngine::Cook
{
    inline constexpr cstring CookWriterLockName = ".ze-cook-writer.lock";
    inline constexpr cstring CookManifestName   = "manifest.json";

    // Borrowed synchronous producer (normally a CPU adapter/scene-codec bridge).
    // No closure allocation, GPU work, source reopen or publication authority.
    using CookProduceBytesFn                    = Core::VFS::VFSResult<void> (*)(void*, ICookByteSink&);

    struct CookPublicationInput
    {
        Core::VFS::VFSPublicationRootLease* Root         = nullptr;
        const CookInputLease*               Inputs       = nullptr;
        const CookDependencyGraph*          Graph        = nullptr;
        CookRecipeKeys                      Recipe       = {};
        const CookLimits*                   Limits       = nullptr;
        bool                                ForceRebuild = false;
    };

    struct CookPublicationResult
    {
        CookManifestOutput Manifest              = {};
        uint32_t           ObjectCount           = 0;
        uint64_t           ReferencedObjectBytes = 0;
        bool               Published             = false;
    };

    // Run-owned synchronous transaction through an already-pinned output root.
    // Initialization requires a genuinely sealed input lease, then exclusively
    // creates its writer directory BEFORE reading the previous manifest. It
    // never steals a pre-existing lock. The input lease may be released afterward;
    // root, immutable graph/limits, allocator and borrowed stream outlive Reset.
    // Root/input/scratch physical separation remains the orchestrator's preflight.
    //
    // Persistent ledger/result state is allocator-routed. One caller-charged
    // stream (up to 64 KiB) is reused for buffered staging, physical verification
    // and exact manifest comparison. Producers never run after manifest commit.
    // A failure poisons the run; caller result/previous manifest stay unchanged.
    // New unreferenced immutable objects are retained, never garbage-collected.
    // Reset finishes lock cleanup through the same root and reports bounded
    // warnings; its cleanup status cannot undo a successful PublishManifest.
    // Count sizeof(this) and bounded call-stack helpers in fixed run accounting;
    // VFS wrapper/snapshot storage is separately run-accounted by the backend.
    // Principal previous-read, object staging/hash, comparison, result and
    // manifest staging state reserve checked fixed charges before their I/O.
    // Result/comparison/staging are distinct phases: a canonical no-op never
    // needs staging state, and the manifest sink has no redundant hash object.
    // These charges do not establish full helper/compiler/hash-internal bounds.
    class CookPublication
    {
    public:
        CookPublication() = default;
        ~CookPublication();
        CookPublication(const CookPublication&)                                                            = delete;
        CookPublication&                                                 operator=(const CookPublication&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>                         Initialize(ICookAllocator&, const CookPublicationInput&, Core::Containers::ArrayView<uint8_t> stream, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<CookReuseDecision>            PrepareAsset(uint32_t, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<CookReuseDecision>            PrepareScene(uint32_t, const Digest256& source_digest, CookDiagnostics*);
        // Ready cached results skip the callback. Otherwise prepare, stream into
        // exclusive staging, checked-close, verify actual staged bytes, install
        // without replacement, and accept only a physically verified object.
        [[nodiscard]] Core::VFS::VFSResult<void>                         EncodeAsset(uint32_t, CookProduceBytesFn, void*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void>                         EncodeScene(uint32_t, const Digest256& source_digest, CookProduceBytesFn, void*, CookDiagnostics*);
        // All results must be ready. Workspace: NodeCount + SceneCount words.
        // Prepare the full fixed result before the sole replacing manifest
        // rename. A verified canonical no-op performs no output file writes.
        [[nodiscard]] Core::VFS::VFSResult<void>                         PublishManifest(Core::Containers::ArrayView<uint32_t>, CookPublicationResult*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void>                         Reset(CookDiagnostics* = nullptr) noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const CookAssetResult> AssetResults() const noexcept
        {
            return m_reuse.AssetResults();
        }
        [[nodiscard]] Core::Containers::ArrayView<const CookSceneResult> SceneResults() const noexcept
        {
            return m_reuse.SceneResults();
        }
        [[nodiscard]] uint64_t StagedBytes() const noexcept
        {
            return m_staged_bytes;
        }

    private:
        [[nodiscard]] Core::VFS::VFSResult<void> ReadPrevious(ICookAllocator&, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> Encode(uint32_t, bool scene, CookProduceBytesFn, void*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<bool> CompareManifest(const CookManifestInput&, Core::Containers::ArrayView<uint32_t>, CookManifestOutput*, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void> Fail(CookStage, CookDiagnosticCode, Core::VFS::VFSError, cstring, CookDiagnostics*, const Core::VFS::VFSRelativePath* = nullptr);
        void                                     CleanupStage(Core::VFS::VFSRootAtomicFileWriter&, uint64_t reserved, CookDiagnostics*) noexcept;
        void                                     Warning(Core::VFS::VFSError, const Core::VFS::VFSRelativePath&, cstring, CookDiagnostics*) const noexcept;

        ICookAllocator*                          m_allocator    = nullptr;
        CookPublicationInput                     m_input        = {};
        Core::Containers::ArrayView<uint8_t>     m_stream       = {};
        CookManifest                             m_previous     = {};
        CookIncrementalReuse                     m_reuse        = {};
        uint64_t                                 m_staged_bytes = 0;
        Core::VFS::VFSError                      m_error        = Core::VFS::VFSError::OK;
        bool                                     m_locked       = false;
        bool                                     m_finished     = false;
        bool                                     m_objects      = false;
    };
} // namespace ZEngine::Cook
