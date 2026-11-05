#pragma once
#include <ZEngine/Core/VFS/VFSError.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <cstdint>

namespace ZEngine::Cook
{
    struct CookLimits
    {
        // Required caller policy, not a reservation or an implicit 768 MiB default.
        uint64_t                 MaxWorkingBytes         = 0;
        uint32_t                 MaxScenes               = 256;
        uint32_t                 MaxCatalogEntries       = 65536;
        uint32_t                 MaxAssets               = 8192;
        uint32_t                 MaxEdges                = 65536;
        uint32_t                 MaxReferenceOccurrences = 65536;
        uint64_t                 MaxManifestBytes        = ZMega(8);
        uint64_t                 MaxSidecarBytes         = ZKilo(64);
        uint64_t                 MaxMaterialBytes        = ZMega(1);
        uint64_t                 MaxEncodedAssetBytes    = ZMega(512);
        uint64_t                 MaxDecodedAssetBytes    = ZMega(512);
        uint64_t                 MaxCapturedBytes        = ZGiga(8);
        uint64_t                 MaxStagedBytes          = ZGiga(16);
        uint32_t                 MaxImageDimension       = 16384;
        ECS::SceneDocumentLimits SceneLimits             = {};
    };

    // Configuration validation only; reaching any valid limit during a cook is a
    // SizeLimitExceeded failure. Zero asset/edge caps can forbid those resources.
    [[nodiscard]] Core::VFS::VFSResult<void> ValidateCookLimits(const CookLimits& limits);
} // namespace ZEngine::Cook
