#include <ZEngine/Cook/CookAssetCatalog.h>
#include <ZEngine/Cook/CookSort.h>
#include <gtest/gtest.h>
#include <type_traits>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace
    {
        struct CatalogSortAllocator final : ICookAllocator
        {
            Tests::FixedAllocator<ZKilo(16), 1> Backing      = {};
            Core::VFS::VFSError                 Failure      = Core::VFS::VFSError::OK;
            uint32_t                            ChargeCalls  = 0;
            uint64_t                            ChargedBytes = 0;

            Core::VFS::VFSResult<void*>         Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                return Backing.Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Backing.Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Backing.Statistics();
            }
            Core::VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                ++ChargeCalls;
                ChargedBytes = bytes;
                EXPECT_EQ(category, CookMemoryCategory::CatalogGraph);
                return Failure == Core::VFS::VFSError::OK ? Backing.TryCharge(bytes, category, output) : Core::VFS::VFSResult<void>::Fail(Failure);
            }
        };

        CookAssetCatalogEntry CatalogEntry(uint8_t number, CookAssetKind kind = CookAssetKind::Mesh)
        {
            uint8_t bytes[16] = {};
            bytes[15]         = number;
            char artifact[]   = "assets/0.zemesh";
            char metadata[]   = "assets/0.zemesh.meta";
            artifact[7] = metadata[7] = static_cast<char>('0' + number);
            return {
                {kind, uuids::uuid(bytes, bytes + 16)},
                Core::VFS::VFSRelativePath::Parse(artifact).Value(), Core::VFS::VFSRelativePath::Parse(metadata).Value(), {}
            };
        }
    } // namespace

    TEST(CookAssetCatalogTest, DefaultsAndZeroCapacityNeverAllocate)
    {
        static_assert(!std::is_move_constructible_v<CookAssetCatalog>);
        Tests::FixedAllocator<ZKilo(16), 1> allocator = {};
        CookAssetCatalog                    catalog   = {};
        EXPECT_FALSE(catalog.IsFinalized());
        EXPECT_EQ(catalog.Count(), 0u);
        EXPECT_EQ(catalog.Find({}), nullptr);
        EXPECT_EQ(catalog.FindArtifact({}), nullptr);
        EXPECT_EQ(catalog.Finalize(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(catalog.Append(CatalogEntry(1), nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        ASSERT_TRUE(catalog.Initialize(allocator, 0, 0).Succeeded());
        EXPECT_EQ(catalog.Append(CatalogEntry(1), nullptr).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        ASSERT_TRUE(catalog.Finalize(nullptr).Succeeded());
        uint32_t   count = 0;
        const auto visit = [](void* context, const CookAssetCatalogEntry&) {
            ++*static_cast<uint32_t*>(context);
            return Core::VFS::VFSResult<void>::Ok();
        };
        EXPECT_TRUE(catalog.Enumerate(visit, &count).Succeeded());
        EXPECT_EQ(count, 0u);
        EXPECT_EQ(allocator.Allocations, 0u);
        catalog.Reset();
        catalog.Reset();
    }

    TEST(CookAssetCatalogTest, OneAllocationCopiesRecordsAndBuildsCanonicalAllocationFreeIndexes)
    {
        Tests::FixedAllocator<ZKilo(16), 1> allocator = {};
        CookAssetCatalog                    catalog   = {};
        ASSERT_TRUE(catalog.Initialize(allocator, 8, 8).Succeeded());
        for (uint8_t number : {8, 6, 3, 7, 4, 1, 5, 2})
        {
            auto entry = CatalogEntry(number, number % 2 ? CookAssetKind::Mesh : CookAssetKind::Texture);
            ASSERT_TRUE(catalog.Append(entry, nullptr).Succeeded());
            entry = {}; // The catalog copied the complete inline record.
        }
        ASSERT_TRUE(catalog.Finalize(nullptr).Succeeded());
        EXPECT_EQ(catalog.Count(), 8u);
        EXPECT_EQ(allocator.Allocations, 1u);
        struct Enumeration
        {
            CookAssetIdentity Previous = {};
            uint32_t          Count    = 0;
        } enumeration    = {};
        const auto visit = [](void* context, const CookAssetCatalogEntry& entry) {
            auto& state = *static_cast<Enumeration*>(context);
            if (state.Count)
            {
                EXPECT_TRUE(state.Previous < entry.Identity);
            }
            state.Previous = entry.Identity;
            ++state.Count;
            return Core::VFS::VFSResult<void>::Ok();
        };
        ASSERT_TRUE(catalog.Enumerate(visit, &enumeration).Succeeded());
        EXPECT_EQ(enumeration.Count, 8u);
        for (uint8_t number = 1; number <= 8; ++number)
        {
            const auto  expected = CatalogEntry(number, number % 2 ? CookAssetKind::Mesh : CookAssetKind::Texture);
            const auto* identity = catalog.Find(expected.Identity);
            ASSERT_NE(identity, nullptr);
            EXPECT_EQ(identity->Metadata, expected.Metadata);
            EXPECT_EQ(catalog.FindArtifact(expected.Artifact), identity);
        }
        EXPECT_EQ(catalog.Find(CatalogEntry(9).Identity), nullptr);
        EXPECT_EQ(catalog.FindArtifact(CatalogEntry(9).Artifact), nullptr);
        EXPECT_EQ(allocator.Allocations, 1u);
        EXPECT_EQ(allocator.Frees, 0u);
        catalog.Reset();
        EXPECT_EQ(allocator.Frees, 1u);
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
    }

    TEST(CookAssetCatalogTest, OneSharedSortReservationRefusesBeforeMutationAndAllowsExactBudgetRetry)
    {
        CatalogSortAllocator allocator = {};
        CookAssetCatalog     catalog   = {};
        ASSERT_TRUE(catalog.Initialize(allocator, 2, 2).Succeeded());
        ASSERT_TRUE(catalog.Append(CatalogEntry(2), nullptr).Succeeded());
        ASSERT_TRUE(catalog.Append(CatalogEntry(1), nullptr).Succeeded());
        const auto         retained = allocator.Statistics().LiveBytes;
        constexpr uint64_t fixed    = sizeof(Detail::CookSortState<uint32_t>) + sizeof(CookMemoryCharge);
        for (const auto error : {Core::VFS::VFSError::Unsupported, Core::VFS::VFSError::OutOfMemory, Core::VFS::VFSError::SizeLimitExceeded})
        {
            allocator.Failure = error;
            const auto calls  = allocator.ChargeCalls;
            EXPECT_EQ(catalog.Finalize(nullptr).Error(), error);
            EXPECT_EQ(allocator.ChargeCalls, calls + 1);
            EXPECT_EQ(allocator.ChargedBytes, fixed);
            EXPECT_EQ(allocator.Statistics().LiveBytes, retained);
            EXPECT_EQ(catalog.Count(), 2u);
            EXPECT_FALSE(catalog.IsFinalized());
        }
        allocator.Failure = Core::VFS::VFSError::OK;
        for (const uint64_t available : {fixed - 1, fixed})
        {
            CookMemoryCharge occupied = {};
            ASSERT_TRUE(allocator.Backing.TryCharge(ZKilo(16) - retained - available, CookMemoryCategory::CatalogGraph, &occupied).Succeeded());
            const auto before = allocator.Statistics().LiveBytes;
            const auto calls  = allocator.ChargeCalls;
            const auto result = catalog.Finalize(nullptr);
            EXPECT_EQ(result.Succeeded(), available == fixed);
            if (result.Failed())
            {
                EXPECT_EQ(result.Error(), Core::VFS::VFSError::SizeLimitExceeded);
            }
            EXPECT_EQ(allocator.ChargeCalls, calls + 1);
            EXPECT_EQ(allocator.Statistics().LiveBytes, before);
            EXPECT_EQ(catalog.IsFinalized(), available == fixed);
            EXPECT_EQ(allocator.Backing.Allocations, 1u);
            EXPECT_EQ(allocator.Backing.Frees, 0u);
        }
        ASSERT_NE(catalog.Find(CatalogEntry(1).Identity), nullptr);
        ASSERT_NE(catalog.Find(CatalogEntry(2).Identity), nullptr);
        const auto* artifact = catalog.FindArtifact(CatalogEntry(1).Artifact);
        ASSERT_NE(artifact, nullptr);
        EXPECT_EQ(artifact->Identity, CatalogEntry(1).Identity);
        catalog.Reset();
        EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(allocator.Backing.Frees, 1u);
    }

    TEST(CookAssetCatalogTest, EmptyAndSingleEntryCatalogsDoNotReserveUnusedSortingState)
    {
        for (const uint32_t count : {0u, 1u})
        {
            CatalogSortAllocator allocator = {};
            CookAssetCatalog     catalog   = {};
            allocator.Failure              = Core::VFS::VFSError::Unsupported;
            EXPECT_EQ(catalog.Finalize(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
            ASSERT_TRUE(catalog.Initialize(allocator, count, count).Succeeded());
            if (count)
            {
                ASSERT_TRUE(catalog.Append(CatalogEntry(1), nullptr).Succeeded());
            }
            ASSERT_TRUE(catalog.Finalize(nullptr).Succeeded());
            EXPECT_EQ(allocator.ChargeCalls, 0u);
            EXPECT_EQ(allocator.Backing.Allocations, count);
            EXPECT_EQ(catalog.Count(), count);
            catalog.Reset();
            EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST(CookAssetCatalogTest, DuplicateUUIDAcrossKindsAndAmbiguousPathsFailClosed)
    {
        for (uint32_t conflict = 0; conflict < 6; ++conflict)
        {
            Tests::FixedAllocator<ZKilo(16), 1> allocator   = {};
            CookAssetCatalog                    catalog     = {};
            CookDiagnostics                     diagnostics = {};
            ASSERT_TRUE(catalog.Initialize(allocator, 2, 2).Succeeded());
            auto first = CatalogEntry(1), second = CatalogEntry(2);
            if (conflict == 0)
            {
                second.Identity.UUID = first.Identity.UUID;
            }
            if (conflict == 1)
            {
                second.Identity.UUID = first.Identity.UUID;
                second.Identity.Kind = CookAssetKind::Texture;
            }
            if (conflict == 2)
            {
                second.Artifact = first.Artifact;
            }
            if (conflict == 3)
            {
                second.Metadata = first.Metadata;
            }
            if (conflict == 4)
            {
                second.Artifact = first.Metadata;
            }
            if (conflict == 5)
            {
                second.Identity.Kind     = CookAssetKind::EnvironmentMap;
                second.EnvironmentSource = first.Artifact;
            }
            ASSERT_TRUE(catalog.Append(first, &diagnostics).Succeeded());
            ASSERT_TRUE(catalog.Append(second, &diagnostics).Succeeded());
            EXPECT_EQ(catalog.Finalize(&diagnostics).Error(), Core::VFS::VFSError::Corrupted);
            EXPECT_FALSE(catalog.IsFinalized());
            EXPECT_EQ(catalog.Find(first.Identity), nullptr);
            EXPECT_EQ(diagnostics.Entries[0].Code, CookDiagnosticCode::IdentityConflict);
            EXPECT_EQ(catalog.Finalize(nullptr).Error(), Core::VFS::VFSError::InvalidPath);
            EXPECT_EQ(catalog.Append(CatalogEntry(3), nullptr).Error(), Core::VFS::VFSError::InvalidPath);
            EXPECT_EQ(allocator.Allocations, 1u);
        }
    }

    TEST(CookAssetCatalogTest, IdentityPathsEnvironmentSourceAndCapacityAreCheckedBeforeAppend)
    {
        Tests::FixedAllocator<ZKilo(16), 1> allocator = {};
        CookAssetCatalog                    catalog   = {};
        ASSERT_TRUE(catalog.Initialize(allocator, 1, 1).Succeeded());
        for (uint32_t fault = 0; fault < 6; ++fault)
        {
            auto entry = CatalogEntry(1);
            if (fault == 0)
            {
                entry.Identity.UUID = {};
            }
            if (fault == 1)
            {
                entry.Artifact = {};
            }
            if (fault == 2)
            {
                entry.Metadata = entry.Artifact;
            }
            if (fault == 3)
            {
                entry.EnvironmentSource = Core::VFS::VFSRelativePath::Parse("assets/source.hdr").Value();
            }
            if (fault == 4)
            {
                entry.Identity.Kind = CookAssetKind::EnvironmentMap;
            }
            if (fault == 5)
            {
                entry.Metadata = Core::VFS::VFSRelativePath::Parse("outside").Value();
            }
            EXPECT_EQ(catalog.Append(entry, nullptr).Error(), Core::VFS::VFSError::Corrupted);
            EXPECT_EQ(catalog.Count(), 0u);
        }
        auto environment              = CatalogEntry(1, CookAssetKind::EnvironmentMap);
        environment.EnvironmentSource = Core::VFS::VFSRelativePath::Parse("assets/source.hdr").Value();
        ASSERT_TRUE(catalog.Append(environment, nullptr).Succeeded());
        EXPECT_EQ(catalog.Append(CatalogEntry(2), nullptr).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(catalog.Count(), 1u);
        ASSERT_TRUE(catalog.Finalize(nullptr).Succeeded());
        ASSERT_NE(catalog.Find(environment.Identity), nullptr);
        EXPECT_EQ(catalog.Find(environment.Identity)->EnvironmentSource, environment.EnvironmentSource);
    }

    TEST(CookAssetCatalogTest, BudgetAndHostRefusalAreFallibleAndLeaveTheCatalogEmpty)
    {
        Tests::FixedAllocator<ZKilo(16), 1> allocator = {};
        CookAssetCatalog                    catalog   = {};
        EXPECT_EQ(catalog.Initialize(allocator, 2, 1).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(catalog.Initialize(allocator, 1, 65537).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Allocations, 0u);
        for (auto error : {Core::VFS::VFSError::OutOfMemory, Core::VFS::VFSError::SizeLimitExceeded})
        {
            allocator.Failure = error;
            EXPECT_EQ(catalog.Initialize(allocator, 1, 1).Error(), error);
            EXPECT_EQ(catalog.Count(), 0u);
            EXPECT_FALSE(catalog.IsFinalized());
        }
        allocator.Failure = Core::VFS::VFSError::OK;
        ASSERT_TRUE(catalog.Initialize(allocator, 1, 1).Succeeded());
        EXPECT_EQ(catalog.Initialize(allocator, 1, 1).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::Codecs)], 0u);
        EXPECT_GT(allocator.Statistics().LiveByCategory[static_cast<size_t>(CookMemoryCategory::CatalogGraph)], 0u);
    }

    TEST(CookAssetCatalogTest, VisitorFailureIsPropagatedWithoutMutatingFinalizedRecords)
    {
        Tests::FixedAllocator<ZKilo(16), 1> allocator = {};
        CookAssetCatalog                    catalog   = {};
        ASSERT_TRUE(catalog.Initialize(allocator, 1, 1).Succeeded());
        const auto entry = CatalogEntry(1);
        ASSERT_TRUE(catalog.Append(entry, nullptr).Succeeded());
        ASSERT_TRUE(catalog.Finalize(nullptr).Succeeded());
        const auto fail = [](void*, const CookAssetCatalogEntry&) { return Core::VFS::VFSResult<void>::Fail(Core::VFS::VFSError::PermissionDenied); };
        EXPECT_EQ(catalog.Enumerate(fail, nullptr).Error(), Core::VFS::VFSError::PermissionDenied);
        EXPECT_EQ(catalog.Enumerate(nullptr, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(catalog.Find(entry.Identity)->Artifact, entry.Artifact);
        EXPECT_TRUE(catalog.IsFinalized());
    }
} // namespace ZEngine::Cook
