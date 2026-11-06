#pragma once
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookObjectReader.h>

namespace ZEngine::Cook
{
    enum class CookReuseDecision : uint8_t
    {
        Encode = 0,
        Ready,
    };

    struct CookReuseInput
    {
        const CookDependencyGraph* Graph        = nullptr;
        CookRecipeKeys             Recipe       = {};
        const CookManifest*        Previous     = nullptr;
        CookObjectReadFns          Reader       = {};
        const CookLimits*          Limits       = nullptr;
        bool                       ForceRebuild = false;
    };

    // Synchronous run-owned verified result tables. One exact budgeted block for
    // graph-indexed results/state, reusable scene-closure workspace, and compact
    // open-addressed verified-object indexing. No per-asset/scene allocations.
    // Stream storage is borrowed/charged by caller and reused (up to 64 KiB).
    //
    // Initialize only AFTER input sealing and while the cooperating output
    // writer lock is held. Graph, previous ledger, allocator, reader context and
    // stream outlive this owner; keep that lock until publication/cleanup ends.
    // Verified files are immutable for this run; Reset between runs. No source
    // reopen, staging, deletion, repair, encoding or manifest commit authority.
    // The rooted VFS may allocate wrappers/snapshots, separately run-accounted.
    // Include sizeof(this) in the caller's fixed run accounting as well.
    class CookIncrementalReuse
    {
    public:
        CookIncrementalReuse() = default;
        ~CookIncrementalReuse();
        CookIncrementalReuse(const CookIncrementalReuse&)                                                       = delete;
        CookIncrementalReuse&                                            operator=(const CookIncrementalReuse&) = delete;

        [[nodiscard]] Core::VFS::VFSResult<void>                         Initialize(ICookAllocator&, const CookReuseInput&, Core::Containers::ArrayView<uint8_t> stream);
        void                                                             Reset() noexcept;
        // Leaf-first. Ready means a complete verified result exists (reused or
        // previously accepted). Encode means InputKey is prepared, Object empty.
        // Missing previous files are misses; existing corrupt files are errors.
        // Parent/scene eligibility also compares previous direct edges and child
        // results, never relying on a matching parent input key alone.
        [[nodiscard]] Core::VFS::VFSResult<CookReuseDecision>            PrepareAsset(uint32_t, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<CookReuseDecision>            PrepareScene(uint32_t, const Digest256& source_digest, CookDiagnostics*);
        // Call after encoding and immutable installation, even for ForceRebuild.
        // Re-verifies newly installed bytes; a missing/corrupt file is an error.
        // No ready result can be replaced with a different descriptor. Failures
        // preserve results/state; a failed run must discard its unpublished work.
        [[nodiscard]] Core::VFS::VFSResult<void>                         AcceptInstalledAsset(uint32_t, const CookObjectDescriptor&, CookDiagnostics*);
        [[nodiscard]] Core::VFS::VFSResult<void>                         AcceptInstalledScene(uint32_t, const CookObjectDescriptor&, CookDiagnostics*);
        [[nodiscard]] Core::Containers::ArrayView<const CookAssetResult> AssetResults() const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const CookSceneResult> SceneResults() const noexcept;
        [[nodiscard]] bool                                               IsAssetReady(uint32_t) const noexcept;
        [[nodiscard]] bool                                               IsSceneReady(uint32_t) const noexcept;
        [[nodiscard]] bool                                               WasAssetReused(uint32_t) const noexcept;
        [[nodiscard]] bool                                               WasSceneReused(uint32_t) const noexcept;
        [[nodiscard]] bool                                               AllReady() const noexcept;

    private:
        enum class State : uint8_t
        {
            Empty = 0,
            Keyed,
            Reused,
            Encoded,
        };
        struct VerifiedObject
        {
            Digest256 Digest        = {};
            uint64_t  Size          = 0;
            char      Extension[17] = {};
        };

        [[nodiscard]] Core::VFS::VFSResult<bool> Verify(const CookObjectDescriptor&, cstring extension);
        [[nodiscard]] bool                       PreviousAssetMatches(uint32_t) const noexcept;
        [[nodiscard]] bool                       PreviousDependenciesMatch(uint32_t, uint32_t previous_asset) const noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> AcceptInstalled(uint32_t, bool scene, const CookObjectDescriptor&, CookDiagnostics*);
        [[nodiscard]] static bool                Ready(State state) noexcept
        {
            return state == State::Reused || state == State::Encoded;
        }
        [[nodiscard]] Core::VFS::VFSError     Failure(CookDiagnostics*, uint32_t index, bool scene, CookDiagnosticCode, Core::VFS::VFSError, cstring, const CookObjectDescriptor* = nullptr) const;

        ICookAllocator*                       m_allocator       = nullptr;
        void*                                 m_block           = nullptr;
        CookReuseInput                        m_input           = {};
        Core::Containers::ArrayView<uint8_t>  m_stream          = {};
        CookAssetResult*                      m_assets          = nullptr;
        CookSceneResult*                      m_scenes          = nullptr;
        State*                                m_asset_state     = nullptr;
        State*                                m_scene_state     = nullptr;
        VerifiedObject*                       m_objects         = nullptr;
        uint32_t*                             m_index           = nullptr;
        Core::Containers::ArrayView<uint32_t> m_closure         = {};
        uint32_t                              m_asset_count     = 0;
        uint32_t                              m_scene_count     = 0;
        uint32_t                              m_object_count    = 0;
        uint32_t                              m_object_capacity = 0;
        uint32_t                              m_index_capacity  = 0;
    };
} // namespace ZEngine::Cook
