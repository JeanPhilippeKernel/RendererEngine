#pragma once
#include <ZEngine/Cook/CookDigest.h>
#include <ZEngine/Cook/CookObject.h>
#include <ZEngine/Cook/CookTypes.h>
#include <uuid.h>

namespace ZEngine::Core::VFS
{
    struct StrictMetaFileData;
}

namespace ZEngine::Importers::AssetCodec
{
    struct MaterialAssetDocument;
}

namespace ZEngine::ECS
{
    class SceneComponentSchemaRegistry;
}

namespace ZEngine::Cook
{
    class CookDependencyGraph;
    class ICookAllocator;

    // Reviewed engine-wide revision, not a caller quality setting. Bump when
    // codec/migration/canonicalization behavior changes without another version.
    inline constexpr uint32_t CookRecipeRevision = 1;

    struct CookRecipeKeys
    {
        uint32_t   Revision    = CookRecipeRevision;
        CookTarget Target      = CookTarget::Invalid;
        Digest256  SchemaKey   = {};
        Digest256  SettingsKey = {};
    };

    // One run-owned result per stable graph index. Identity/format/edges already
    // live in the finalized graph, not duplicated here. Empty Object means not
    // yet encoded/reused; key computation requires each direct child result.
    struct CookAssetResult
    {
        Digest256            InputKey = {};
        CookObjectDescriptor Object   = {};
    };

    // Only {} is supported in recipe v1. Bytes and their verified digest are
    // static: no runtime hash state or allocation. Manifests omit the final LF.
    [[nodiscard]] Core::Containers::StringView CookSettingsJSON() noexcept;
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookSettingsKey(CookTarget, Digest256*);
    // Workspace is uint32_t sorting indexes for the largest field table, reused
    // across schemas. No hard maximum-field reservation or hidden heap fallback.
    [[nodiscard]] Core::VFS::VFSResult<size_t> CookSchemaKeyWorkspaceWords(const ECS::SceneComponentSchemaRegistry&);
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookSchemaKey(ICookAllocator&, const ECS::SceneComponentSchemaRegistry&, Core::Containers::ArrayView<uint32_t> workspace, Digest256*);
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookRecipeKeys(ICookAllocator&, const ECS::SceneComponentSchemaRegistry&, CookTarget, Core::Containers::ArrayView<uint32_t> workspace, CookRecipeKeys*);

    // Stream exact contract-v1 fields directly from the immutable graph and
    // node-indexed results. No transient resolved-dependency or byte array.
    // Call in leaf-first order after validating/cooking each child's object.
    // These functions validate descriptor spelling, not physical object bytes.
    // The actual run allocator reserves principal fixed hash/temporary state;
    // these helpers never allocate. Lower-level/compiler frames are separate.
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookAssetInputKey(ICookAllocator&, const CookDependencyGraph&, uint32_t node, const CookRecipeKeys&, Core::Containers::ArrayView<const CookAssetResult>, Digest256*);
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookGraphKey(ICookAllocator&, const CookDependencyGraph&, Core::Containers::ArrayView<const CookAssetResult>, Digest256*);
    // Uses only this scene's transitive closure; unrelated scene assets cannot
    // invalidate it. Workspace is 2 * graph.NodeCount uint32_t words, reused by
    // the existing iterative graph closure algorithm. Optional closure output
    // borrows workspace until its next use, avoiding another traversal in reuse.
    // Failure preserves the key and the caller's previous closure view.
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookSceneInputKey(ICookAllocator&, const CookDependencyGraph&, uint32_t scene, const Digest256& source_digest, const CookRecipeKeys&, Core::Containers::ArrayView<const CookAssetResult>, Core::Containers::ArrayView<uint32_t> workspace, Digest256*, Core::Containers::ArrayView<const uint32_t>* closure = nullptr);

    // Canonical JSON streamed straight into SHA-256. Metadata sorting uses only
    // 32 inline byte indexes; no canonical-text buffer or heap allocation. These
    // keys deliberately exclude locations, timestamps, and source provenance.
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookMetadataKey(ICookAllocator&, const Core::VFS::StrictMetaFileData&, Digest256*);
    [[nodiscard]] Core::VFS::VFSResult<void>   ComputeCookMaterialContentKey(ICookAllocator&, const Importers::AssetCodec::MaterialAssetDocument&, Digest256*);

    // Contract v1's typed byte encoding, streamed directly into SHA-256. No
    // native struct layout, temporary strings, aggregate byte buffer, or heap.
    // Domain strings and array counts are explicit writes by the recipe caller.
    // Text/identity semantic validation belongs to the shared readers; this
    // encoder preserves their exact bytes, including length-delimited Unicode.
    class CookFingerprint
    {
    public:
        CookFingerprint() = default;
        // Containing state plus the shared hash's additional compression buffers.
        [[nodiscard]] static constexpr size_t FixedWorkingBytes() noexcept
        {
            return sizeof(CookFingerprint) - sizeof(Core::Hash::SHA256) + Core::Hash::SHA256::FixedWorkingBytes();
        }
        void                                     Reset() noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> UInt32(uint32_t value) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> UInt64(uint64_t value) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> String(Core::Containers::StringView value) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> Digest(const Digest256& value) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> UUID(const uuids::uuid& value) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> Finalize(Digest256* output) noexcept;
        [[nodiscard]] Core::VFS::VFSError        Error() const noexcept
        {
            return m_error;
        }

    private:
        [[nodiscard]] Core::VFS::VFSResult<void> UnsignedLE(uint64_t value, size_t bytes) noexcept;
        [[nodiscard]] Core::VFS::VFSResult<void> Append(Core::Containers::ArrayView<const uint8_t> bytes) noexcept;
        Core::Hash::SHA256                       m_hash    = {};
        Core::Containers::FixedArray<uint8_t, 8> m_encoded = {};
        Core::VFS::VFSError                      m_error   = Core::VFS::VFSError::OK;
    };
} // namespace ZEngine::Cook
