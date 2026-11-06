#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>
#include <ZEngine/Core/Serialization/JSONReader.h>
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/Core/VFS/VFSRelativePath.h>
#include <ZEngine/Rendering/Materials/MaterialAlpha.h>
#include <uuid.h>

namespace ZEngine::Importers::AssetCodec
{
    enum class MaterialTextureSlot : uint8_t
    {
        Albedo = 0,
        Emissive,
        Normal,
        Opacity,
        Specular,
        Count,
    };

    [[nodiscard]] cstring MaterialTextureSlotKey(MaterialTextureSlot) noexcept;

    struct MaterialTextureReference
    {
        uuids::uuid UUID                          = {};
        // Original validated spelling: a logical/packaged relative path or an
        // importer context VFS location. Never normalize/follow it in this codec.
        char        Path[Core::VFS::VFS_MAX_PATH] = {};
    };

    struct MaterialSourceProvenance
    {
        char     Path[256]    = {};
        char     Importer[64] = {};
        uint32_t Index        = 0;
    };

    // Bounded owned wire document, separate from arena-backed runtime material
    // storage. Shared by headless validation/inspection and future strict runtime
    // readers. Parsing allocates nothing and never consults VFS/native files.
    struct MaterialAssetDocument
    {
        uuids::uuid                                                                                             UUID           = {};
        char                                                                                                    Name[4097]     = {};
        Core::Containers::FixedArray<MaterialTextureReference, static_cast<size_t>(MaterialTextureSlot::Count)> Textures       = {};
        Core::Containers::FixedArray<float, 4>                                                                  AmbientColor   = {};
        Core::Containers::FixedArray<float, 4>                                                                  AlbedoColor    = {};
        Core::Containers::FixedArray<float, 4>                                                                  EmissiveColor  = {};
        Core::Containers::FixedArray<float, 4>                                                                  RoughnessColor = {};
        Core::Containers::FixedArray<float, 4>                                                                  SpecularColor  = {};
        Core::Containers::FixedArray<float, 4>                                                                  Factors        = {};
        Rendering::Materials::AlphaMode                                                                         AlphaMode      = Rendering::Materials::AlphaMode::Legacy;
        MaterialSourceProvenance                                                                                Source         = {};
        bool                                                                                                    HasSource      = false;
    };

    // Caller-owned parser/document storage. Read allocates nothing and returns
    // its owned validated document; the borrowed pointer lasts until the next
    // Read or destruction. Input bytes must not overlap this reader. Failure
    // exposes no partially parsed document. Cook
    // constructs this complete type inside its existing charged scratch block,
    // not a second full document on the call stack.
    class MaterialAssetReader
    {
    public:
        MaterialAssetReader()                                                                                  = default;
        MaterialAssetReader(const MaterialAssetReader&)                                                        = delete;
        MaterialAssetReader&                                             operator=(const MaterialAssetReader&) = delete;
        [[nodiscard]] Core::VFS::VFSResult<const MaterialAssetDocument*> Read(Core::Containers::ArrayView<const uint8_t>, const uuids::uuid& expected_uuid = {}, uint64_t maximum_file_bytes = ZMega(1));

    private:
        MaterialAssetDocument                  m_document = {};
        Core::Serialization::JSONReader        m_reader{{}};
        Core::Containers::FixedArray<char, 37> m_text          = {};
        uuids::uuid                            m_expected_uuid = {};
        bool                                   m_document_used = false;
    };

    // Closed current-version schema, canonical UUIDs, all slots/numeric fields
    // required, optional bounded provenance validated but not followed. Nil slots
    // require empty paths; nonnil slots require strictly spelled paths. Catalog
    // UUID/path agreement remains the adapter's responsibility. Failure preserves
    // output. Convenience wrapper with one fixed MaterialAssetReader on its
    // stack; bounded callers instead own that reader and borrow Read's result.
    [[nodiscard]] Core::VFS::VFSResult<void> ReadMaterialAssetDocument(Core::Containers::ArrayView<const uint8_t>, MaterialAssetDocument*, const uuids::uuid& expected_uuid = {}, uint64_t maximum_file_bytes = ZMega(1));

    // Resolved paths are in fixed slot order, including invalid/absent values
    // for nil slots. The adapter verifies child identity/object descriptors;
    // this shared emitter checks slot/path presence and finite document values.
    // Both emitters omit provenance and finish with exactly one LF. Neither
    // allocates. The semantic projection empties all texture paths for hashing
    // only and is deliberately not a loadable runtime material.
    [[nodiscard]] Core::VFS::VFSResult<void> WriteCanonicalMaterialAsset(const MaterialAssetDocument&, Core::Containers::ArrayView<const Core::VFS::VFSRelativePath> resolved_paths, Core::Serialization::JSONWriteFn, void* context = nullptr);
    [[nodiscard]] Core::VFS::VFSResult<void> WriteCanonicalMaterialSemanticProjection(const MaterialAssetDocument&, Core::Serialization::JSONWriteFn, void* context = nullptr);
} // namespace ZEngine::Importers::AssetCodec
