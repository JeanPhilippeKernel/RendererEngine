#include <ZEngine/Cook/CookLimits.h>
#include <limits>

namespace ZEngine::Cook
{
    Core::VFS::VFSResult<void> ValidateCookLimits(const CookLimits& limits)
    {
        constexpr CookLimits caps       = {};
        const auto&          scene      = limits.SceneLimits;
        const auto&          scene_caps = caps.SceneLimits;
        const bool           valid      = limits.MaxWorkingBytes > 0 && limits.MaxWorkingBytes <= std::numeric_limits<size_t>::max() && limits.MaxScenes > 0 && limits.MaxScenes <= caps.MaxScenes && limits.MaxCatalogEntries <= caps.MaxCatalogEntries && limits.MaxAssets <= caps.MaxAssets && limits.MaxEdges <= caps.MaxEdges && limits.MaxReferenceOccurrences <= caps.MaxReferenceOccurrences && limits.MaxManifestBytes <= caps.MaxManifestBytes && limits.MaxSidecarBytes <= caps.MaxSidecarBytes && limits.MaxMaterialBytes <= caps.MaxMaterialBytes &&
                                          limits.MaxEncodedAssetBytes <= caps.MaxEncodedAssetBytes && limits.MaxDecodedAssetBytes <= caps.MaxDecodedAssetBytes && limits.MaxCapturedBytes <= caps.MaxCapturedBytes && limits.MaxStagedBytes <= caps.MaxStagedBytes && limits.MaxImageDimension <= caps.MaxImageDimension && scene.MaxFileBytes > 0 && scene.MaxFileBytes <= scene_caps.MaxFileBytes && scene.MaxEntities <= scene_caps.MaxEntities && scene.MaxComponentsPerEntity > 0 && scene.MaxComponentsPerEntity <= scene_caps.MaxComponentsPerEntity && scene.MaxNodes > 0 &&
                                          scene.MaxNodes <= scene_caps.MaxNodes && scene.MaxDepth > 0 && scene.MaxDepth <= scene_caps.MaxDepth && scene.MaxCollectionItems > 0 && scene.MaxCollectionItems <= scene_caps.MaxCollectionItems && scene.MaxScalarBytes > 0 && scene.MaxScalarBytes <= scene_caps.MaxScalarBytes && scene.MaxDecodedBytes > 0 && scene.MaxDecodedBytes <= scene_caps.MaxDecodedBytes;
        return valid ? Core::VFS::VFSResult<void>::Ok() : Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::InvalidPath);
    }
} // namespace ZEngine::Cook
