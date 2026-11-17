#pragma once
#include <ZEngine/Managers/CookedAssetReader.h>

namespace ZEngine::Managers
{
    class CookedCpuClosureCache;
    class CookedCpuAssetPool;
    /// Independent CPU versions for one selected package closure. These limits
    /// are runtime admission policy; they do not inherit the cooker's budget.
    struct CookedCpuAssetLimits
    {
        uint32_t                                        MaxAssets             = 0;
        size_t                                          MaxObjectBytes        = 0;
        size_t                                          MaxMeshWorkspaceBytes = 0;
        Importers::AssetCodec::MeshAssetReadLimits      Mesh                  = {};
        Importers::ImageCodec::EncodedImageLimits       Texture               = {};
        Importers::AssetCodec::EnvironmentMapReadLimits Environment           = {};
    };

    struct CookedCpuAssetVersion
    {
        Cook::CookAssetIdentity      Identity     = {};
        Cook::Digest256              ObjectDigest = {};
        // Recipe hint; resolve dependency versions within this owner rather
        // than treating this unverified key as a content identity.
        Cook::Digest256              InputKey     = {};
        const Cook::CookAssetFormat* Format       = nullptr;
        uint64_t                     ObjectBytes  = 0;
    };

    /// Move-only owner of a completely decoded selected closure. The route and
    /// its working budget outlive this owner and its borrowers. No registry,
    /// renderer, shared cache or GPU state is involved in standalone Capture.
    /// A successful capture can outlive its package and root.
    class CookedCpuAssetSet final
    {
    public:
        CookedCpuAssetSet() = default;
        ~CookedCpuAssetSet();
        CookedCpuAssetSet(const CookedCpuAssetSet&)            = delete;
        CookedCpuAssetSet& operator=(const CookedCpuAssetSet&) = delete;
        CookedCpuAssetSet(CookedCpuAssetSet&&) noexcept;
        CookedCpuAssetSet&                              operator=(CookedCpuAssetSet&&) noexcept;

        /// Worker-only transaction. The route must provide a working budget.
        /// Failure releases every candidate allocation and preserves output.
        /// Cancellation is observed between assets and around individual reads;
        /// synchronous codec calls are allowed to complete before returning.
        [[nodiscard]] static Core::VFS::VFSResult<void> Capture(CookedPackageSnapshot&, Core::Memory::IAllocationRoute&, const CookedCpuAssetLimits&, CookedCpuAssetSet*, const std::atomic<bool>* cancelled = nullptr);
        void                                            Reset() noexcept;
        [[nodiscard]] bool                              IsValid() const noexcept
        {
            return m_valid;
        }
        [[nodiscard]] size_t Size() const noexcept
        {
            return m_count;
        }
        [[nodiscard]] const Cook::Digest256& ManifestDigest() const noexcept
        {
            return m_manifest_digest;
        }
        [[nodiscard]] const uuids::uuid& SceneUUID() const noexcept
        {
            return m_scene_uuid;
        }
        [[nodiscard]] const CookedCpuAssetVersion*                           VersionAt(size_t index) const noexcept;
        [[nodiscard]] const CookedCpuAssetVersion*                           FindVersion(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] const Importers::AssetMesh*                            FindMesh(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] const Importers::AssetNodeHierarchy*                   FindHierarchy(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] const Importers::AssetCodec::MaterialAssetDocument*    FindMaterial(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] const Importers::ImageCodec::DecodedImage*             FindTexture(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] const Importers::AssetCodec::EnvironmentMapFileHeader* FindEnvironment(const Cook::CookAssetIdentity&) const noexcept;
        [[nodiscard]] std::span<const uint8_t>                               EnvironmentObject(const Cook::CookAssetIdentity&) const noexcept;

    private:
        friend class CookedCpuClosureCache;
        struct Record;
        struct BuildContext;
        [[nodiscard]] static Core::VFS::VFSResult<void> CaptureShared(CookedPackageSnapshot&, Core::Memory::IAllocationRoute&, CookedCpuAssetPool&, const CookedCpuAssetLimits&, CookedCpuAssetSet*, const std::atomic<bool>*);
        [[nodiscard]] static Core::VFS::VFSResult<void> CaptureImpl(CookedPackageSnapshot&, Core::Memory::IAllocationRoute&, CookedCpuAssetPool*, const CookedCpuAssetLimits&, CookedCpuAssetSet*, const std::atomic<bool>*);
        [[nodiscard]] static Core::VFS::VFSResult<void> BuildAsset(void*, const Cook::CookManifestAsset&);
        [[nodiscard]] const Record*                     Find(const Cook::CookAssetIdentity&) const noexcept;
        Core::Memory::IAllocationRoute*                 m_route           = nullptr;
        CookedCpuAssetPool*                             m_pool            = nullptr;
        Record*                                         m_records         = nullptr;
        size_t                                          m_count           = 0;
        size_t                                          m_capacity        = 0;
        Cook::Digest256                                 m_manifest_digest = {};
        uuids::uuid                                     m_scene_uuid      = {};
        bool                                            m_valid           = false;
    };
} // namespace ZEngine::Managers
