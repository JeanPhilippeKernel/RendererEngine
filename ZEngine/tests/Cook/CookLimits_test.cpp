#include <ZEngine/Cook/CookLimits.h>
#include <gtest/gtest.h>
#include <limits>

namespace ZEngine::Cook
{
    TEST(CookLimitsTest, WorkingMemoryMustBeExplicitAndDoesNotAllocate)
    {
        CookLimits limits = {};
        EXPECT_EQ(limits.MaxWorkingBytes, 0u);
        EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
        limits.MaxWorkingBytes = 1;
        EXPECT_TRUE(ValidateCookLimits(limits).Succeeded());
        limits.MaxWorkingBytes = std::numeric_limits<size_t>::max();
        EXPECT_TRUE(ValidateCookLimits(limits).Succeeded());
    }

    TEST(CookLimitsTest, EveryCountAndByteHardCapRejectsAnIncrease)
    {
        const auto counts = {
            &CookLimits::MaxScenes,
            &CookLimits::MaxCatalogEntries,
            &CookLimits::MaxAssets,
            &CookLimits::MaxEdges,
            &CookLimits::MaxReferenceOccurrences,
            &CookLimits::MaxImageDimension,
        };
        for (const auto member : counts)
        {
            CookLimits limits = {.MaxWorkingBytes = ZMega(1)};
            ++(limits.*member);
            EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
        }
        const auto bytes = {
            &CookLimits::MaxManifestBytes,
            &CookLimits::MaxSidecarBytes,
            &CookLimits::MaxMaterialBytes,
            &CookLimits::MaxEncodedAssetBytes,
            &CookLimits::MaxDecodedAssetBytes,
            &CookLimits::MaxCapturedBytes,
            &CookLimits::MaxStagedBytes,
        };
        for (const auto member : bytes)
        {
            CookLimits limits = {.MaxWorkingBytes = ZMega(1)};
            ++(limits.*member);
            EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
        }
    }

    TEST(CookLimitsTest, ExistingSceneLimitsCanOnlyBeLoweredAndRetainTheirRequiredMinima)
    {
        CookLimits limits      = {.MaxWorkingBytes = ZMega(1)};
        const auto byte_limits = {&ECS::SceneDocumentLimits::MaxFileBytes, &ECS::SceneDocumentLimits::MaxDecodedBytes};
        for (const auto member : byte_limits)
        {
            limits.SceneLimits = {};
            ++(limits.SceneLimits.*member);
            EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
            limits.SceneLimits.*member = 0;
            EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
        }
        const auto counts = {
            &ECS::SceneDocumentLimits::MaxEntities,
            &ECS::SceneDocumentLimits::MaxComponentsPerEntity,
            &ECS::SceneDocumentLimits::MaxNodes,
            &ECS::SceneDocumentLimits::MaxDepth,
            &ECS::SceneDocumentLimits::MaxCollectionItems,
            &ECS::SceneDocumentLimits::MaxScalarBytes,
        };
        for (const auto member : counts)
        {
            limits.SceneLimits = {};
            ++(limits.SceneLimits.*member);
            EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
            limits.SceneLimits.*member = 0;
            EXPECT_EQ(ValidateCookLimits(limits).Succeeded(), member == &ECS::SceneDocumentLimits::MaxEntities);
        }
    }

    TEST(CookLimitsTest, ZeroAssetAndEdgeCapsAreValidForAnAssetFreeCook)
    {
        CookLimits limits        = {.MaxWorkingBytes = ZMega(1)};
        limits.MaxCatalogEntries = limits.MaxAssets = limits.MaxEdges = limits.MaxReferenceOccurrences = 0;
        EXPECT_TRUE(ValidateCookLimits(limits).Succeeded());
        limits.MaxScenes = 0;
        EXPECT_EQ(ValidateCookLimits(limits).Error(), Core::VFS::VFSError::InvalidPath);
    }
} // namespace ZEngine::Cook
