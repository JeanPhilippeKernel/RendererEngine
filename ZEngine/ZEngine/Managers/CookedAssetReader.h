#pragma once
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>

namespace ZEngine::Managers
{
    /// Strict CPU decoders for verified package objects. These do not publish
    /// cache entries, create GPU resources, or establish first-frame readiness.
    /// All output storage/arenas and reader state are caller owned and bounded.
    class CookedAssetReader final
    {
    public:
        /// Revalidate package-specific edges when an already verified immutable
        /// CPU version is reused by another selected scene closure.
        [[nodiscard]] static bool                       MatchesMeshDependencies(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const Importers::AssetMesh&);
        [[nodiscard]] static bool                       MatchesMaterialDependencies(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const Importers::AssetCodec::MaterialAssetDocument&);
        [[nodiscard]] static Core::VFS::VFSResult<void> ReadMesh(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const CookedObjectSnapshot&, Core::Memory::ArenaAllocator*, Core::Memory::IWorkingMemoryBudget&, Importers::AssetMesh*, Importers::AssetNodeHierarchy*, const Importers::AssetCodec::MeshAssetReadLimits& = {});
        [[nodiscard]] static Core::VFS::VFSResult<void> ReadMaterial(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const CookedObjectSnapshot&, Importers::AssetCodec::MaterialAssetReader&, const Importers::AssetCodec::MaterialAssetDocument**);
        [[nodiscard]] static Core::VFS::VFSResult<void> ValidateTexture(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const CookedObjectSnapshot&, Core::Memory::IAllocationRoute&, const Importers::ImageCodec::EncodedImageLimits&, Importers::ImageCodec::EncodedImageDescription*);
        [[nodiscard]] static Core::VFS::VFSResult<void> ReadTexture(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const CookedObjectSnapshot&, Core::Memory::IAllocationRoute&, const Importers::ImageCodec::EncodedImageLimits&, Importers::ImageCodec::DecodedImage*);
        [[nodiscard]] static Core::VFS::VFSResult<void> ReadEnvironment(const CookedPackageSnapshot&, const Cook::CookAssetIdentity&, const CookedObjectSnapshot&, Core::Containers::ArrayView<uint8_t> stream, Importers::AssetCodec::EnvironmentMapFileHeader*, const Importers::AssetCodec::EnvironmentMapReadLimits& = {});
    };
} // namespace ZEngine::Managers
