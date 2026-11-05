#pragma once
#include <ZEngine/Cook/CookInputLease.h>
#include <ZEngine/Cook/CookLimits.h>
#include <ZEngine/Core/VFS/Meta/MetaFileData.h>

namespace ZEngine::Cook
{
    struct CookAssetFormat
    {
        CookAssetKind Kind      = CookAssetKind::Unknown;
        cstring       Key       = nullptr;
        cstring       Extension = nullptr;
        uint32_t      Revision  = 0;
    };

    // One shared wire-format table for adapters, manifests and object paths.
    [[nodiscard]] Core::Containers::ArrayView<const CookAssetFormat> CookAssetFormats() noexcept;
    [[nodiscard]] const CookAssetFormat*                             FindCookAssetFormat(Core::Containers::StringView) noexcept;
    [[nodiscard]] bool                                               IsCookObjectDescriptorValid(const CookAssetFormat&, const Core::VFS::VFSRelativePath&, const Digest256&, uint64_t size) noexcept;

    struct CookAssetInput
    {
        CookAssetIdentity                    Identity          = {};
        const ICookCapturedFile*             Artifact          = nullptr;
        const ICookCapturedFile*             EnvironmentSource = nullptr;
        const Core::VFS::StrictMetaFileData* Metadata          = nullptr;
        const CookLimits*                    Limits            = nullptr;
        Core::VFS::VFSRelativePath           Source            = {};
    };

    struct CookDependency
    {
        CookAssetIdentity            Identity   = {};
        Core::Containers::StringView Slot       = {};
        // Material's original UUID/path pair, verified against the frozen
        // catalog before sealing. Empty for a UUID-only mesh dependency.
        Core::Containers::StringView SourcePath = {};
    };

    [[nodiscard]] bool CookDependencySlotBefore(Core::Containers::StringView, Core::Containers::StringView) noexcept;
    // Wire-level spelling/parent-child validation, shared by manifest readers
    // and writers. Actual mesh submesh bounds still require the encoded asset.
    [[nodiscard]] bool IsCookDependencySlotValid(CookAssetKind parent, CookAssetKind child, Core::Containers::StringView slot) noexcept;

    struct CookAssetInspectionView
    {
        cstring                                           Format          = nullptr;
        Digest256                                         LocalContentKey = {};
        Digest256                                         MetadataKey     = {};
        Core::Containers::ArrayView<const CookDependency> Dependencies    = {};
    };

    // One exact-size dependency/text allocation in the supplied reusable
    // scratch allocator; zero result allocations for a leaf. No Ref/control
    // block, parsed-document retention, or per-edge allocation. Copy edges into
    // persistent graph storage and Reset before releasing/reusing scratch.
    class CookAssetInspection
    {
    public:
        CookAssetInspection() = default;
        ~CookAssetInspection();
        CookAssetInspection(const CookAssetInspection&)            = delete;
        CookAssetInspection& operator=(const CookAssetInspection&) = delete;
        CookAssetInspection(CookAssetInspection&&) noexcept;
        CookAssetInspection&                     operator=(CookAssetInspection&&) noexcept;

        // Dependencies must be unique and strictly ordered by slot. Deep-copy
        // both text views. Failure preserves this inspection and all its views.
        [[nodiscard]] Core::VFS::VFSResult<void> Assign(cstring format, const Digest256& local_key, const Digest256& metadata_key, Core::Containers::ArrayView<const CookDependency>, ICookAllocator&);
        void                                     Reset() noexcept;
        [[nodiscard]] cstring                    Format() const noexcept
        {
            return m_format;
        }
        [[nodiscard]] const Digest256& LocalContentKey() const noexcept
        {
            return m_local_key;
        }
        [[nodiscard]] const Digest256& MetadataKey() const noexcept
        {
            return m_metadata_key;
        }
        [[nodiscard]] Core::Containers::ArrayView<const CookDependency> Dependencies() const noexcept
        {
            return {m_dependencies, m_count};
        }

    private:
        ICookAllocator* m_allocator    = nullptr;
        CookDependency* m_dependencies = nullptr;
        size_t          m_count        = 0;
        cstring         m_format       = nullptr;
        Digest256       m_local_key    = {};
        Digest256       m_metadata_key = {};
    };

    struct CookResolvedDependency
    {
        Core::Containers::StringView Slot         = {};
        CookAssetIdentity            Identity     = {};
        Digest256                    InputKey     = {};
        cstring                      Format       = nullptr;
        Core::VFS::VFSRelativePath   ObjectPath   = {};
        Digest256                    ObjectDigest = {};
        uint64_t                     ObjectSize   = 0;
    };

    class ICookByteSink
    {
    public:
        virtual ~ICookByteSink()                                                                           = default;
        [[nodiscard]] virtual Core::VFS::VFSResult<void> Write(Core::Containers::ArrayView<const uint8_t>) = 0;
    };

    struct CookAdapterFns
    {
        CookAssetKind Kind                                                                                                                                                                                 = CookAssetKind::Unknown;
        uint32_t      Revision                                                                                                                                                                             = 0;
        const void*   Context                                                                                                                                                                              = nullptr;
        Core::VFS::VFSResult<void> (*Inspect)(const void*, const CookAssetInput&, ICookAllocator&, CookAssetInspection*, CookDiagnostics*)                                                                 = nullptr;
        Core::VFS::VFSResult<void> (*Encode)(const void*, const CookAssetInput&, Core::Containers::ArrayView<const CookResolvedDependency>, CookTarget, ICookByteSink&, ICookAllocator&, CookDiagnostics*) = nullptr;
    };

    // All four kinds required; checked once before any capture. Immutable after
    // initialization, constant-time lookup, no heap or static registration.
    class CookAdapterRegistry
    {
    public:
        [[nodiscard]] Core::VFS::VFSResult<void>                        Initialize(Core::Containers::ArrayView<const CookAdapterFns>);
        [[nodiscard]] const CookAdapterFns*                             Find(CookAssetKind) const noexcept;
        [[nodiscard]] Core::Containers::ArrayView<const CookAdapterFns> Entries() const noexcept
        {
            return m_initialized ? Core::Containers::ArrayView<const CookAdapterFns>(m_entries.data(), m_entries.size()) : Core::Containers::ArrayView<const CookAdapterFns>{};
        }
        [[nodiscard]] bool IsInitialized() const noexcept
        {
            return m_initialized;
        }

    private:
        Core::Containers::FixedArray<CookAdapterFns, 4> m_entries     = {};
        bool                                            m_initialized = false;
    };

    // Discovery-only lookup. If SourcePath is present, its original logical or
    // importer-context spelling must resolve to this typed UUID's exact artifact.
    // Never used during Encode or after Seal. Failure preserves the output.
    [[nodiscard]] Core::VFS::VFSResult<void> ResolveCookDependency(CookInputLease&, const CookDependency&, CookAssetCatalogEntry*, CookDiagnostics*);

    [[nodiscard]] CookAdapterFns             CookMaterialAdapter() noexcept;
    [[nodiscard]] CookAdapterFns             CookMeshAdapter() noexcept;
    [[nodiscard]] CookAdapterFns             CookTextureAdapter() noexcept;
    [[nodiscard]] CookAdapterFns             CookEnvironmentAdapter() noexcept;
} // namespace ZEngine::Cook
