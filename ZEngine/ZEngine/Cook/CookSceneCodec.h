#pragma once
#include <ZEngine/Cook/CookAdapter.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>

namespace ZEngine::Cook
{
    struct CookSceneInput
    {
        const ICookCapturedFile*   Capture = nullptr;
        Core::VFS::VFSRelativePath Source  = {};
        const CookLimits*          Limits  = nullptr;
    };

    struct CookSceneOutput
    {
        uuids::uuid UUID   = {};
        Digest256   Digest = {};
        uint64_t    Bytes  = 0;
    };

    // Run-owned, synchronous bridge from immutable captures to the shared scene
    // reader/binary codec. No live ECS, renderer, source reopen, or publication.
    // Allocator/registries outlive this owner; this owner MUST outlive every
    // returned document (including moves), which retains its allocation route.
    // Keep only one parsed scene at a time: copy roots, release, then reparse the
    // owned capture for encoding after Seal. Encoding never requires the lease.
    // Memory refusal is terminal for this run; the codec route keeps first error.
    // Include sizeof(this) in fixed run accounting (one reused diagnostics buffer).
    class CookSceneCodec
    {
    public:
        CookSceneCodec(ICookAllocator&, const ECS::SceneComponentSchemaRegistry&, const ECS::SceneMigrationRegistry* = nullptr) noexcept;
        CookSceneCodec(const CookSceneCodec&)                                                   = delete;
        CookSceneCodec&                                        operator=(const CookSceneCodec&) = delete;

        // Verifies complete captured bytes/digest before parsing. Temporary
        // source bytes are released before return; document owns every value.
        // Failure preserves the caller's existing document.
        [[nodiscard]] Core::VFS::VFSResult<void>               Read(const CookSceneInput&, ECS::SceneDocument*, CookDiagnostics*, CookStage = CookStage::Discover);
        // Caller must seal all inputs first. Exact measured output allocation,
        // complete binary validation before sink writes, no MaxFileBytes buffer.
        // Failure preserves output; any sink bytes belong only to unpublished
        // staging and must be discarded by the publication transaction.
        [[nodiscard]] Core::VFS::VFSResult<void>               Encode(const CookSceneInput&, const uuids::uuid& expected_uuid, CookTarget, ICookByteSink&, CookSceneOutput*, CookDiagnostics*);
        // Root collection must use the exact registry that validated Read.
        [[nodiscard]] const ECS::SceneComponentSchemaRegistry& Schemas() const noexcept
        {
            return *m_schemas;
        }

    private:
        [[nodiscard]] Core::VFS::VFSResult<void> Fail(const CookSceneInput&, CookStage, CookDiagnostics*, CookDiagnosticCode, Core::VFS::VFSError, cstring, const uuids::uuid& = {});
        [[nodiscard]] Core::VFS::VFSResult<void> SceneFailure(const CookSceneInput&, CookStage, CookDiagnostics*, cstring, const uuids::uuid& = {});
        // Shared allocation-free preflight precedes every fixed reservation;
        // the validated immutable source is not queried/validated twice by Encode.
        [[nodiscard]] Core::VFS::VFSResult<void> ValidateInput(const CookSceneInput&, CookStage, CookDiagnostics*, uint64_t& source_bytes);
        [[nodiscard]] Core::VFS::VFSResult<void> ReadValidated(const CookSceneInput&, uint64_t source_bytes, ECS::SceneDocument*, CookDiagnostics*, CookStage);

        ICookAllocator*                          m_allocator  = nullptr;
        const ECS::SceneComponentSchemaRegistry* m_schemas    = nullptr;
        const ECS::SceneMigrationRegistry*       m_migrations = nullptr;
        CookCodecAllocationRoute                 m_route;
        ECS::SceneDiagnostics                    m_diagnostics = {};
    };
} // namespace ZEngine::Cook
