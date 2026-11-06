#pragma once
#include <ZEngine/Cook/CookDependencyGraph.h>
#include <ZEngine/Cook/CookFingerprint.h>

namespace ZEngine::Cook
{
    inline constexpr uint32_t CookManifestVersion = 1;

    // Scene-indexed, like CookAssetResult. Captured source bytes and encoded
    // object bytes have different digests. No parsed document is retained here.
    struct CookSceneResult
    {
        Digest256            SourceDigest = {};
        Digest256            InputKey     = {};
        CookObjectDescriptor Object       = {};
    };

    struct CookManifestInput
    {
        const CookDependencyGraph*                         Graph  = nullptr;
        CookRecipeKeys                                     Recipe = {};
        Core::Containers::ArrayView<const CookAssetResult> Assets = {};
        Core::Containers::ArrayView<const CookSceneResult> Scenes = {};
        const CookLimits*                                  Limits = nullptr;
    };

    struct CookManifestOutput
    {
        Digest256 Digest = {};
        uint64_t  Bytes  = 0;
    };

    // Allocation-free canonical manifest v1 emission from finalized graph and
    // index-stable results. Supply NodeCount + SceneCount uint32_t words, charged
    // by the caller. Reuses them for descriptor-conflict and scene-order sorts;
    // never mutates graph/result tables. Validates/measures before the first
    // sink write, including MaxManifestBytes. Sink output is staging-only; this
    // function has no publication authority and does not verify physical bytes.
    // The mandatory allocator reserves principal fixed stream/writer/hash/result
    // state before measurement/emission; no Allocate call is made. This is not
    // a complete bound for validation/helper/compiler/hash-internal frames.
    // Failure preserves output. The caller must seal inputs before calling.
    [[nodiscard]] Core::VFS::VFSResult<void> WriteCookManifest(const CookManifestInput&, Core::Containers::ArrayView<uint32_t> workspace, ICookByteSink&, CookManifestOutput*, ICookAllocator&);

    enum class CookManifestReadStatus : uint8_t
    {
        Valid = 0,
        Rebuild,
    };

    struct CookManifestScene
    {
        uuids::uuid                UUID      = {};
        Core::VFS::VFSRelativePath Source    = {};
        CookSceneResult            Result    = {};
        uint32_t                   FirstRoot = 0;
        uint32_t                   RootCount = 0;
    };

    struct CookManifestAsset
    {
        CookAssetIdentity      Identity        = {};
        const CookAssetFormat* Format          = nullptr;
        CookAssetResult        Result          = {};
        uint32_t               FirstDependency = 0;
        uint32_t               DependencyCount = 0;
    };

    // Strict ledger reader, not a package-object verifier. One exact retained
    // block holds records/dependency text; no DOM, per-entry allocation, or
    // borrowed input strings. Temporary syntax-key indexing and graph validation
    // are allocator-routed and released before return. Allocator outlives owner.
    // Valid means structurally/semantically valid only: reuse must still verify
    // actual keys/objects. Missing/unreadable files are handled by the publisher,
    // not passed as empty input. Read failure preserves this owner; Rebuild is
    // successful with a CacheMiss warning and clears all old records/paths.
    class CookManifest
    {
    public:
        CookManifest() = default;
        ~CookManifest();
        CookManifest(const CookManifest&)            = delete;
        CookManifest& operator=(const CookManifest&) = delete;
        CookManifest(CookManifest&&) noexcept;
        CookManifest&                                              operator=(CookManifest&&) noexcept;

        [[nodiscard]] Core::VFS::VFSResult<CookManifestReadStatus> Read(Core::Containers::ArrayView<const uint8_t>, CookTarget, const CookLimits&, ICookAllocator&, CookDiagnostics*);
        void                                                       Reset() noexcept;
        [[nodiscard]] bool                                         IsValid() const noexcept
        {
            return m_block != nullptr;
        }
        [[nodiscard]] const CookRecipeKeys& Recipe() const noexcept
        {
            return m_recipe;
        }
        [[nodiscard]] Core::Containers::ArrayView<const CookManifestScene> Scenes() const noexcept
        {
            return {m_scenes, m_scene_count};
        }
        [[nodiscard]] Core::Containers::ArrayView<const CookManifestAsset> Assets() const noexcept
        {
            return {m_assets, m_asset_count};
        }
        [[nodiscard]] Core::Containers::ArrayView<const CookAssetIdentity> Roots(uint32_t scene) const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const CookDependency>    Dependencies(uint32_t asset) const noexcept;
        [[nodiscard]] const CookManifestScene*                             FindScene(const uuids::uuid&) const noexcept;
        [[nodiscard]] const CookManifestAsset*                             FindAsset(const CookAssetIdentity&) const noexcept;

    private:
        ICookAllocator*    m_allocator    = nullptr;
        void*              m_block        = nullptr;
        CookManifestScene* m_scenes       = nullptr;
        CookManifestAsset* m_assets       = nullptr;
        CookAssetIdentity* m_roots        = nullptr;
        CookDependency*    m_dependencies = nullptr;
        uint32_t           m_scene_count  = 0;
        uint32_t           m_asset_count  = 0;
        CookRecipeKeys     m_recipe       = {};
    };
} // namespace ZEngine::Cook
