#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookManifestInternal.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <string>
#include "CookManifestGolden.h"
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace VFS = Core::VFS;

    namespace
    {
        using JSON                        = nlohmann::json;

        constexpr cstring TextureUUID     = "00000000-0000-4000-8000-000000000002";
        constexpr cstring MaterialUUID    = "00000000-0000-4000-8000-000000000003";
        constexpr cstring MeshUUID        = "00000000-0000-4000-8000-000000000004";
        constexpr cstring EnvironmentUUID = "00000000-0000-4000-8000-000000000005";

        JSON              GoldenJSON()
        {
            return JSON::parse(Tests::ManifestGolden.begin(), Tests::ManifestGolden.end());
        }

        JSON IdentityJSON(cstring kind, cstring uuid, cstring slot = nullptr)
        {
            JSON value = {
                {"kind", kind},
                {"uuid", uuid}
            };
            if (slot)
            {
                value["slot"] = slot;
            }
            return value;
        }

        JSON AssetJSON(cstring kind, cstring uuid, cstring format, cstring extension, char digest)
        {
            const std::string hex(64, digest);
            return {
                {"kind", kind},
                {"uuid", uuid},
                {"format", format},
                {"adapter_revision", 1},
                {"input_key", std::string(64, '1')},
                {"object", {{"path", "objects/" + hex + "." + extension}, {"digest", hex}, {"size", 128}}},
                {"dependencies", JSON::array()}
            };
        }

        JSON FullLedger()
        {
            auto ledger                  = GoldenJSON();
            auto material                = AssetJSON("material", MaterialUUID, "zematerial-v1-json", "zematerial", 'a');
            material["dependencies"]     = JSON::array({IdentityJSON("texture", TextureUUID, "albedo"), IdentityJSON("texture", TextureUUID, "normal")});
            auto mesh                    = AssetJSON("mesh", MeshUUID, "zemesh-v1-le64", "zemesh", 'b');
            mesh["dependencies"]         = JSON::array({IdentityJSON("material", MaterialUUID, "submeshes/0/material")});
            ledger["assets"]             = JSON::array({AssetJSON("environment-map", EnvironmentUUID, "zenvmap-v2-le64", "zenvmap", 'c'), material, mesh, AssetJSON("texture", TextureUUID, "image-png", "png", 'd')});
            ledger["scenes"][0]["roots"] = JSON::array({IdentityJSON("environment-map", EnvironmentUUID), IdentityJSON("mesh", MeshUUID)});
            return ledger;
        }

        std::string NestedSyntaxJSON(uint32_t containers, cstring leaf = "0")
        {
            std::string text = R"({"manifest_version":1,"recipe_revision":1,"extra":)";
            for (uint32_t i = 1; i < containers; ++i)
            {
                text += i % 2 ? R"({"child":)" : "[";
            }
            text += leaf;
            for (uint32_t i = containers; i > 1; --i)
            {
                text += (i - 1) % 2 ? R"(,"sibling":false})" : ",null,true]";
            }
            return text + R"(,"tail":[{},[],null,true,false,0,"done"]})";
        }

        struct FailingManifestAllocator final : ICookAllocator
        {
            ICookAllocator*      Backing          = nullptr;
            uint32_t             Calls            = 0;
            uint32_t             FailAt           = UINT32_MAX;
            VFS::VFSError        Failure          = VFS::VFSError::OutOfMemory;
            uint32_t             ChargeCalls      = 0;
            uint32_t             RefuseChargeAt   = UINT32_MAX;
            uint64_t             LastChargeBytes  = 0;
            VFS::VFSError        ChargeFailure    = VFS::VFSError::SizeLimitExceeded;
            CookMemoryLedger*    BoundaryLedger   = nullptr;
            uint32_t             BoundaryChargeAt = UINT32_MAX;
            bool                 OneByteShort     = false;

            VFS::VFSResult<void> TryCharge(uint64_t bytes, CookMemoryCategory category, CookMemoryCharge* output) override
            {
                LastChargeBytes = bytes;
                if (++ChargeCalls == RefuseChargeAt)
                {
                    return VFS::VFSResult<void>::Fail(ChargeFailure);
                }
                // Inject ledger pressure only around the selected reservation;
                // later phases regain their normal budget. No allocation/hook
                // inside production code and no per-charge trace storage.
                CookMemoryCharge pressure = {};
                if (BoundaryLedger && ChargeCalls == BoundaryChargeAt)
                {
                    const auto available = bytes - (OneByteShort ? 1 : 0);
                    const auto remaining = BoundaryLedger->RemainingBytes();
                    if (remaining < available)
                    {
                        return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                    }
                    const auto padded = BoundaryLedger->TryCharge(remaining - available, category, &pressure);
                    if (padded.Failed())
                    {
                        return padded;
                    }
                }
                return Backing->TryCharge(bytes, category, output);
            }

            VFS::VFSResult<void*> Allocate(size_t bytes, size_t alignment, CookMemoryCategory category) noexcept override
            {
                if (++Calls == FailAt)
                {
                    return VFS::VFSResult<void*>::Fail(Failure);
                }
                return Backing->Allocate(bytes, alignment, category);
            }
            void Free(void* pointer) noexcept override
            {
                Backing->Free(pointer);
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return Backing->Statistics();
            }
        };
    } // namespace

    class CookManifestReaderTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(256), 6> Allocator   = {};
        FailingManifestAllocator             Routed      = {};
        CookLimits                           Limits      = {.MaxWorkingBytes = ZKilo(256)};
        CookDiagnostics                      Diagnostics = {};
        CookManifest                         Manifest    = {};

        void                                 SetUp() override
        {
            Routed.Backing = &Allocator;
        }

        VFS::VFSResult<CookManifestReadStatus> Read(Core::Containers::StringView text)
        {
            Diagnostics.Clear();
            return Manifest.Read({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, CookTarget::DesktopVulkanLE64, Limits, Routed, &Diagnostics);
        }

        VFS::VFSResult<CookManifestReadStatus> ReadJSON(const JSON& document)
        {
            const auto text = document.dump();
            return Read({text.data(), text.size()});
        }

        void ExpectRebuild(const JSON& document)
        {
            const auto result = ReadJSON(document);
            ASSERT_TRUE(result.Succeeded()) << static_cast<uint32_t>(result.Error());
            EXPECT_EQ(result.Value(), CookManifestReadStatus::Rebuild);
            EXPECT_FALSE(Manifest.IsValid());
            EXPECT_EQ(Manifest.Scenes().size(), 0u);
            EXPECT_EQ(Manifest.Assets().size(), 0u);
            EXPECT_EQ(Diagnostics.ErrorCount, 0u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CacheMiss);
            EXPECT_EQ(Diagnostics.Entries[0].Severity, CookSeverity::Warning);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    };

    TEST_F(CookManifestReaderTest, ReviewedGoldenRetainsOneExactBlockAndLookupDoesNotAllocate)
    {
        const auto result = Read(Tests::ManifestGolden);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.Value(), CookManifestReadStatus::Valid);
        ASSERT_TRUE(Manifest.IsValid());
        ASSERT_EQ(Manifest.Scenes().size(), 1u);
        EXPECT_TRUE(Manifest.Assets().empty());
        EXPECT_EQ(Manifest.Recipe().Revision, 1u);
        EXPECT_EQ(Manifest.Recipe().Target, CookTarget::DesktopVulkanLE64);
        EXPECT_EQ(Manifest.Recipe().SchemaKey.Hex()[0], '4');
        const auto* scene = &Manifest.Scenes()[0];
        EXPECT_STREQ(scene->Source.CStr(), "scenes/main.yaml");
        EXPECT_EQ(scene->Result.Object.Size, 256u);
        EXPECT_EQ(Manifest.FindScene(scene->UUID), scene);
        EXPECT_EQ(Manifest.FindScene({}), nullptr);
        EXPECT_EQ(Manifest.FindAsset({}), nullptr);
        EXPECT_TRUE(Manifest.Roots(0).empty());
        EXPECT_TRUE(Manifest.Roots(UINT32_MAX).empty());
        EXPECT_TRUE(Manifest.Dependencies(UINT32_MAX).empty());
        EXPECT_EQ(Allocator.Allocations - Allocator.Frees, 1u);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, sizeof(CookManifestScene));
        EXPECT_EQ(Diagnostics.Count, 0u);
        Manifest.Reset();
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
    }

    TEST_F(CookManifestReaderTest, FixedParserStateRefusesOneByteShortBeforeAllocatingAndPreservesThePreviousManifest)
    {
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        const auto* previous  = Manifest.Scenes().data();
        const auto  live      = Allocator.Statistics().LiveBytes;
        Routed.RefuseChargeAt = Routed.ChargeCalls + 1;
        EXPECT_EQ(Read(Tests::ManifestGolden).Error(), VFS::VFSError::SizeLimitExceeded);
        const auto required = Routed.LastChargeBytes;
        ASSERT_GT(required, 4097u);
        const auto       calls   = Routed.Calls;
        CookMemoryCharge padding = {};
        ASSERT_TRUE(Allocator.Ledger.TryCharge(Limits.MaxWorkingBytes - live - required + 1, CookMemoryCategory::StagingManifest, &padding).Succeeded());
        EXPECT_EQ(Read(Tests::ManifestGolden).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Routed.Calls, calls);
        EXPECT_EQ(Manifest.Scenes().data(), previous);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
        padding.Release();
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
    }

    TEST_F(CookManifestReaderTest, EveryFixedStateChargeFailurePreservesThePreviousManifestAndDrainsTemporaryState)
    {
        const auto initialCalls = Routed.ChargeCalls;
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        const auto requiredCharges = Routed.ChargeCalls - initialCalls;
        ASSERT_GT(requiredCharges, 0u);
        const auto* previous = Manifest.Scenes().data();
        const auto  live     = Allocator.Statistics().LiveBytes;
        for (auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory, VFS::VFSError::Unsupported})
        {
            for (uint32_t seam = 1; seam <= requiredCharges; ++seam)
            {
                SCOPED_TRACE(seam);
                SCOPED_TRACE(static_cast<int>(error));
                Routed.RefuseChargeAt = Routed.ChargeCalls + seam;
                Routed.ChargeFailure  = error;
                EXPECT_EQ(Read(Tests::ManifestGolden).Error(), error);
                EXPECT_EQ(Manifest.Scenes().data(), previous);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::SizeLimitExceeded ? CookDiagnosticCode::BudgetExceeded : CookDiagnosticCode::AllocationFailed);
            }
        }
        Routed.RefuseChargeAt = UINT32_MAX;
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
    }

    TEST_F(CookManifestReaderTest, EveryFullLedgerRowReservationPreservesOldRecordsOnAllResourceFailures)
    {
        const auto text = FullLedger().dump();
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto initial_calls = Routed.ChargeCalls;
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto required = Routed.ChargeCalls - initial_calls;
        ASSERT_GT(required, 10u); // Includes non-empty roots/assets/dependencies in both passes.
        const auto* previous = Manifest.Scenes().data();
        const auto  live     = Allocator.Statistics().LiveBytes;
        for (const auto error : {VFS::VFSError::SizeLimitExceeded, VFS::VFSError::OutOfMemory, VFS::VFSError::Unsupported})
        {
            for (uint32_t seam = 1; seam <= required; ++seam)
            {
                SCOPED_TRACE(seam);
                SCOPED_TRACE(static_cast<int>(error));
                Routed.RefuseChargeAt = Routed.ChargeCalls + seam;
                Routed.ChargeFailure  = error;
                EXPECT_EQ(Read({text.data(), text.size()}).Error(), error);
                EXPECT_EQ(Manifest.Scenes().data(), previous);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
                ASSERT_EQ(Manifest.Assets().size(), 4u);
                ASSERT_EQ(Manifest.Dependencies(1).size(), 2u);
                EXPECT_TRUE(Manifest.Dependencies(1)[1].Slot == "normal");
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::Unsupported ? CookDiagnosticCode::UnsupportedBackend : error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            }
        }
        Routed.RefuseChargeAt = UINT32_MAX;
        EXPECT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
    }

    TEST_F(CookManifestReaderTest, EveryFullLedgerFixedReservationFitsExactlyAndRejectsOneByteShortWithoutGrowth)
    {
        const auto text = FullLedger().dump();
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto initial_calls       = Routed.ChargeCalls;
        const auto initial_allocations = Routed.Calls;
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto required    = Routed.ChargeCalls - initial_calls;
        const auto allocations = Routed.Calls - initial_allocations;
        const auto live        = Allocator.Statistics().LiveBytes;
        Routed.BoundaryLedger  = &Allocator.Ledger;
        for (uint32_t seam = 1; seam <= required; ++seam)
        {
            for (const bool short_budget : {true, false})
            {
                SCOPED_TRACE(seam);
                SCOPED_TRACE(short_budget);
                const auto* previous    = Manifest.Scenes().data();
                const auto  calls       = Routed.Calls;
                Routed.BoundaryChargeAt = Routed.ChargeCalls + seam;
                Routed.OneByteShort     = short_budget;
                const auto result       = Read({text.data(), text.size()});
                if (short_budget)
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                    EXPECT_EQ(Manifest.Scenes().data(), previous);
                    ASSERT_EQ(Diagnostics.Count, 1u);
                    EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
                }
                else
                {
                    ASSERT_TRUE(result.Succeeded());
                    EXPECT_EQ(result.Value(), CookManifestReadStatus::Valid);
                    // Neither the row scratch nor its checked reservations add
                    // any heap/block allocation, independent of the selected seam.
                    EXPECT_EQ(Routed.Calls - calls, allocations);
                    EXPECT_EQ(Diagnostics.Count, 0u);
                }
                EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
                ASSERT_EQ(Manifest.Dependencies(1).size(), 2u);
                EXPECT_TRUE(Manifest.Dependencies(1)[0].Slot == "albedo");
            }
        }
        Routed.BoundaryChargeAt = UINT32_MAX;
    }

    TEST_F(CookManifestReaderTest, FullLedgerFitsItsAggregatePeakAndRefusesOneByteShortPreservingPreviousOwner)
    {
        const auto text = FullLedger().dump();
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto peak = Allocator.Statistics().PeakBytes;
        const auto live = Allocator.Statistics().LiveBytes;
        ASSERT_GT(peak, live);
        for (const auto available : {peak - 1, peak})
        {
            CookMemoryCharge pressure = {};
            ASSERT_TRUE(Allocator.Ledger.TryCharge(Limits.MaxWorkingBytes - available, CookMemoryCategory::StagingManifest, &pressure).Succeeded());
            const auto* previous = Manifest.Scenes().data();
            const auto  result   = Read({text.data(), text.size()});
            if (available == peak)
            {
                ASSERT_TRUE(result.Succeeded());
                EXPECT_EQ(result.Value(), CookManifestReadStatus::Valid);
            }
            else
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(Manifest.Scenes().data(), previous);
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live + Limits.MaxWorkingBytes - available);
            pressure.Release();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        }
    }

    TEST_F(CookManifestReaderTest, AllocatorsWithoutAnExternalFixedStateContractFailBeforeAnyParserAllocation)
    {
        struct OpaqueAllocator final : ICookAllocator
        {
            VFS::VFSResult<void*> Allocate(size_t, size_t, CookMemoryCategory) noexcept override
            {
                ADD_FAILURE() << "Unsupported fixed-state accounting must refuse before allocation.";
                return VFS::VFSResult<void*>::Fail(VFS::VFSError::OutOfMemory);
            }
            void Free(void*) noexcept override
            {
                ADD_FAILURE() << "Unsupported fixed-state accounting must not own storage.";
            }
            CookMemoryStatistics Statistics() const noexcept override
            {
                return {};
            }
        } opaque         = {};
        const auto bytes = Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(Tests::ManifestGolden.data()), Tests::ManifestGolden.size());
        EXPECT_EQ(Manifest.Read(bytes, CookTarget::DesktopVulkanLE64, Limits, opaque, &Diagnostics).Error(), VFS::VFSError::Unsupported);
        EXPECT_FALSE(Manifest.IsValid());
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedBackend);
    }

    TEST_F(CookManifestReaderTest, SharedSyntaxDecodeBufferRequiresItsCompleteCapacityBeforeAnyIndexAllocation)
    {
        char       buffer[4097] = {};
        const auto bytes        = Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(Tests::ManifestGolden.data()), Tests::ManifestGolden.size());
        for (const auto view : {
             Core::Containers::ArrayView<char>{},
             Core::Containers::ArrayView<char>{nullptr, sizeof(buffer)},
             Core::Containers::ArrayView<char>{buffer, sizeof(buffer) - 1}
        })
        {
            Diagnostics.Clear();
            EXPECT_EQ(Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, view).Error(), VFS::VFSError::InvalidPath);
            EXPECT_EQ(Routed.Calls, 0u);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidRequest);
        }
        Diagnostics.Clear();
        ASSERT_TRUE(Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer).Succeeded());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        EXPECT_EQ(Diagnostics.Count, 0u);
    }

    TEST_F(CookManifestReaderTest, ReusedSyntaxDecodeBufferHandlesMaximumStringsWithoutWritingPastItsBorrowedRange)
    {
        char guarded[4099]           = {};
        guarded[0]                   = 'L';
        guarded[sizeof(guarded) - 1] = 'R';
        const auto buffer            = Core::Containers::ArrayView<char>(guarded + 1, 4097);
        for (size_t length : {size_t(4096), size_t(4097), size_t(4096), size_t(1)})
        {
            Diagnostics.Clear();
            const auto text   = std::string("{\"manifest_version\":1,\"unknown\":\"") + std::string(length, 'x') + "\"}";
            const auto bytes  = Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
            const auto result = Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer);
            if (length == 4097)
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
            }
            else
            {
                EXPECT_TRUE(result.Succeeded());
            }
            EXPECT_EQ(guarded[0], 'L');
            EXPECT_EQ(guarded[sizeof(guarded) - 1], 'R');
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, IterativeSyntaxWalkHandlesEveryAllowedContainerDepthAndEmptySiblings)
    {
        char guarded[4099]           = {};
        guarded[0]                   = 'L';
        guarded[sizeof(guarded) - 1] = 'R';
        const auto buffer            = Core::Containers::ArrayView<char>(guarded + 1, 4097);
        for (uint32_t depth = 1; depth <= 17; ++depth)
        {
            for (cstring leaf : {"0", "null", "false", "\"escaped\\u00e9\""})
            {
                SCOPED_TRACE(depth);
                SCOPED_TRACE(leaf);
                Diagnostics.Clear();
                const auto text        = NestedSyntaxJSON(depth, leaf);
                const auto bytes       = Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
                const auto allocations = Allocator.Allocations;
                const auto result      = Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer);
                if (depth <= 16)
                {
                    ASSERT_TRUE(result.Succeeded());
                    EXPECT_EQ(result.Value().RecipeRevision, 1u);
                    EXPECT_EQ(Allocator.Allocations, allocations + 1);
                    EXPECT_EQ(Diagnostics.Count, 0u);
                }
                else
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                    EXPECT_EQ(Allocator.Allocations, allocations);
                    ASSERT_EQ(Diagnostics.Count, 1u);
                    EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
                }
                EXPECT_EQ(guarded[0], 'L');
                EXPECT_EQ(guarded[sizeof(guarded) - 1], 'R');
                EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            }
        }
    }

    TEST_F(CookManifestReaderTest, IterativeSyntaxWalkRetainsRootAndAssetRolesAcrossUnknownContainers)
    {
        char buffer[4097] = {};
        for (bool incompatible : {false, true})
        {
            for (bool reverse : {false, true})
            {
                const cstring     nested = R"("ignored":{"assets":[{"adapter_revision":999,"format":"unknown","kind":"unknown"}],"manifest_version":2,"target":"bad","recipe_revision":999})";
                const std::string first  = R"({"adapter_revision":1,)" + std::string(nested) + R"(,"kind":"texture","format":"image-png"})";
                const std::string second = std::string(R"({"kind":"texture","format":"image-png",)") + nested + R"(,"adapter_revision":)" + (incompatible ? "2}" : "1}");
                const auto        text   = std::string(R"({"manifest_version":1,"assets":[)") + (reverse ? second : first) + "," + (reverse ? first : second) + R"(],"target":"desktop-vulkan-le64","recipe_revision":1, )" + nested + "}";
                Diagnostics.Clear();
                const auto result = Detail::CheckCookManifestSyntax({reinterpret_cast<const uint8_t*>(text.data()), text.size()}, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer);
                if (incompatible)
                {
                    EXPECT_EQ(result.Error(), VFS::VFSError::Unsupported);
                    ASSERT_EQ(Diagnostics.Count, 1u);
                    EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedVersion);
                }
                else
                {
                    ASSERT_TRUE(result.Succeeded());
                    EXPECT_EQ(result.Value().RecipeRevision, 1u);
                    EXPECT_EQ(Diagnostics.Count, 0u);
                }
                EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
            }
        }
    }

    TEST_F(CookManifestReaderTest, DeepSyntaxFailuresPreserveTheExistingLedgerAndDrainTraversalState)
    {
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        const auto* previous = Manifest.Scenes().data();
        const auto  live     = Allocator.Statistics().LiveBytes;
        for (cstring leaf : {R"({"x":1,"\u0078":2})", R"([false,])", R"([false})", R"({"x":})"})
        {
            SCOPED_TRACE(leaf);
            const auto text = NestedSyntaxJSON(15, leaf);
            EXPECT_EQ(Read({text.data(), text.size()}).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(Manifest.Scenes().data(), previous);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidManifest);
        }
        ASSERT_EQ(Read(Tests::ManifestGolden).Value(), CookManifestReadStatus::Valid);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
    }

    TEST_F(CookManifestReaderTest, InlineSyntaxTraversalFitsItsObservedPeakAndRefusesOneByteShort)
    {
        char       buffer[4097] = {};
        const auto text         = NestedSyntaxJSON(16);
        const auto bytes        = Core::Containers::ArrayView<const uint8_t>(reinterpret_cast<const uint8_t*>(text.data()), text.size());
        ASSERT_TRUE(Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer).Succeeded());
        const auto peak  = Allocator.Statistics().PeakBytes;
        const auto fixed = Routed.LastChargeBytes;
        ASSERT_GT(peak, fixed);
        for (const auto available : {fixed - 1, peak - 1, peak})
        {
            Diagnostics.Clear();
            CookMemoryCharge padding = {};
            ASSERT_TRUE(Allocator.Ledger.TryCharge(Limits.MaxWorkingBytes - available, CookMemoryCategory::StagingManifest, &padding).Succeeded());
            const auto calls  = Routed.Calls;
            const auto result = Detail::CheckCookManifestSyntax(bytes, CookTarget::DesktopVulkanLE64, Routed, &Diagnostics, buffer);
            if (available == peak)
            {
                ASSERT_TRUE(result.Succeeded());
                EXPECT_EQ(Routed.Calls, calls + 1);
                EXPECT_EQ(Diagnostics.Count, 0u);
            }
            else
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::SizeLimitExceeded);
                EXPECT_EQ(Routed.Calls, calls + (available == fixed - 1 ? 0 : 1));
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::BudgetExceeded);
            }
            EXPECT_EQ(Allocator.Statistics().LiveBytes, Limits.MaxWorkingBytes - available);
            padding.Release();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, AllReviewedNegativeFixturesRespectHardFailureVersusRebuildBoundary)
    {
        const auto cases = JSON::parse(Tests::ManifestNegativeCases.begin(), Tests::ManifestNegativeCases.end());
        for (const auto& test : cases["cases"])
        {
            SCOPED_TRACE(test["name"].get<std::string>());
            Manifest.Reset();
            const auto text   = test.contains("raw") ? test["raw"].get<std::string>() : GoldenJSON().patch(test["patch"]).dump();
            const auto result = Read({text.data(), text.size()});
            const auto code   = test["code"].get<std::string>();
            if (code == "UnsupportedVersion" || code == "UnsupportedTarget")
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::Unsupported);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, code == "UnsupportedTarget" ? CookDiagnosticCode::UnsupportedTarget : CookDiagnosticCode::UnsupportedVersion);
            }
            else if (test.contains("raw"))
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::Corrupted);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidManifest);
            }
            else
            {
                ASSERT_TRUE(result.Succeeded());
                EXPECT_EQ(result.Value(), CookManifestReadStatus::Rebuild);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CacheMiss);
                EXPECT_EQ(Diagnostics.ErrorCount, 0u);
            }
            EXPECT_FALSE(Manifest.IsValid());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, FullClosureOwnsDependencyTextAfterInputTeardownAndSupportsMoves)
    {
        auto       text   = FullLedger().dump();
        const auto result = Read({text.data(), text.size()});
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.Value(), CookManifestReadStatus::Valid);
        std::fill(text.begin(), text.end(), 'X');
        ASSERT_EQ(Manifest.Assets().size(), 4u);
        EXPECT_EQ(Manifest.Assets()[0].Identity.Kind, CookAssetKind::EnvironmentMap);
        ASSERT_EQ(Manifest.Roots(0).size(), 2u);
        ASSERT_EQ(Manifest.Dependencies(1).size(), 2u);
        EXPECT_TRUE(Manifest.Dependencies(1)[0].Slot == "albedo");
        EXPECT_TRUE(Manifest.Dependencies(1)[1].Slot == "normal");
        EXPECT_EQ(Manifest.Dependencies(1)[0].Identity, Manifest.Dependencies(1)[1].Identity);
        const auto              allocations = Allocator.Allocations;
        const CookAssetIdentity identity    = {CookAssetKind::Mesh, uuids::uuid::from_string(MeshUUID).value()};
        const auto*             mesh        = Manifest.FindAsset(identity);
        ASSERT_NE(mesh, nullptr);
        EXPECT_EQ(mesh, &Manifest.Assets()[2]);
        EXPECT_EQ(Manifest.FindAsset({CookAssetKind::Texture, identity.UUID}), nullptr);
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_EQ(Allocator.Allocations - Allocator.Frees, 1u);
        CookManifest moved(std::move(Manifest));
        EXPECT_FALSE(Manifest.IsValid());
        EXPECT_EQ(moved.FindAsset(identity), mesh);
        Manifest = std::move(moved);
        EXPECT_FALSE(moved.IsValid());
        EXPECT_EQ(Manifest.FindAsset(identity), mesh);
    }

    TEST_F(CookManifestReaderTest, SingleDependencySlotPreservesOwnedLongShortTextAcrossBothPasses)
    {
        const Core::Containers::StringView slots[] = {"submeshes/0/material", "submeshes/1/material", "submeshes/1000000000/material", "submeshes/2/material", "submeshes/2000000000/material", "submeshes/3/material"};
        auto                               ledger  = FullLedger();
        ledger["scenes"][0]["source"]              = "scenes/é.yaml";
        ledger["assets"][2]["dependencies"]        = JSON::array();
        for (const auto slot : slots)
        {
            ledger["assets"][2]["dependencies"].push_back(IdentityJSON("material", MaterialUUID, std::string(slot.data(), slot.size()).c_str()));
        }
        auto  ordered      = nlohmann::ordered_json::parse(ledger.dump());
        auto& dependencies = ordered["assets"][2]["dependencies"];
        for (size_t index = 0; index < dependencies.size(); ++index)
        {
            nlohmann::ordered_json record = nlohmann::ordered_json::object();
            // Vary field order: decoding identity text after the slot must not
            // overwrite it. Reusing one slot also exercises short keys following
            // long keys without retaining stale terminators.
            if (index % 2 == 0)
            {
                record["slot"] = dependencies[index]["slot"];
            }
            record["uuid"] = MaterialUUID;
            record["kind"] = "material";
            if (index % 2)
            {
                record["slot"] = dependencies[index]["slot"];
            }
            dependencies[index] = std::move(record);
        }
        auto text = ordered.dump(-1, ' ', true);
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        const auto accepted_allocations = Routed.Calls;
        std::fill(text.begin(), text.end(), 'X');
        ASSERT_EQ(Manifest.Dependencies(2).size(), std::size(slots));
        for (size_t index = 0; index < std::size(slots); ++index)
        {
            EXPECT_TRUE(Manifest.Dependencies(2)[index].Slot == slots[index]);
        }
        EXPECT_EQ(Routed.Calls, accepted_allocations);
        EXPECT_EQ(Allocator.Allocations - Allocator.Frees, 1u);

        // A duplicate after differently sized keys must still be detected during
        // typed measurement, not accepted against overwritten previous text.
        dependencies[4]    = dependencies[3];
        text               = ordered.dump();
        const auto invalid = Read({text.data(), text.size()});
        ASSERT_TRUE(invalid.Succeeded());
        EXPECT_EQ(invalid.Value(), CookManifestReadStatus::Rebuild);
        EXPECT_FALSE(Manifest.IsValid());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CacheMiss);
    }

    TEST_F(CookManifestReaderTest, EqualDependencySlotsCompareIdentityAfterAnyPropertyOrderAndRejectReversedTies)
    {
        constexpr cstring second_texture = "00000000-0000-4000-8000-000000000006";
        auto              ledger         = FullLedger();
        ledger["assets"].push_back(AssetJSON("texture", second_texture, "image-png", "png", 'e'));
        ledger["assets"][1]["dependencies"] = JSON::array({IdentityJSON("texture", TextureUUID, "albedo"), IdentityJSON("texture", second_texture, "albedo"), IdentityJSON("texture", TextureUUID, "normal")});
        auto                   ordered      = nlohmann::ordered_json::parse(ledger.dump());
        auto&                  dependencies = ordered["assets"][1]["dependencies"];
        nlohmann::ordered_json first        = nlohmann::ordered_json::object();
        first["uuid"]                       = TextureUUID;
        first["kind"]                       = "texture";
        first["slot"]                       = "albedo";
        nlohmann::ordered_json second       = nlohmann::ordered_json::object();
        second["slot"]                      = "albedo";
        second["kind"]                      = "texture";
        second["uuid"]                      = second_texture;
        dependencies[0]                     = std::move(first);
        dependencies[1]                     = std::move(second);
        auto text                           = ordered.dump();
        ASSERT_EQ(Read({text.data(), text.size()}).Value(), CookManifestReadStatus::Valid);
        ASSERT_EQ(Manifest.Dependencies(1).size(), 3u);
        EXPECT_TRUE(Manifest.Dependencies(1)[0].Slot == "albedo");
        EXPECT_TRUE(Manifest.Dependencies(1)[1].Slot == "albedo");
        EXPECT_LT(Manifest.Dependencies(1)[0].Identity, Manifest.Dependencies(1)[1].Identity);

        dependencies[0].swap(dependencies[1]);
        text               = ordered.dump();
        const auto invalid = Read({text.data(), text.size()});
        ASSERT_TRUE(invalid.Succeeded());
        EXPECT_EQ(invalid.Value(), CookManifestReadStatus::Rebuild);
        EXPECT_FALSE(Manifest.IsValid());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::CacheMiss);
    }

    TEST_F(CookManifestReaderTest, PropertyOrderWhitespaceAndEscapedUnicodeAreAcceptedButNotNormalized)
    {
        auto document                   = GoldenJSON();
        document["scenes"][0]["source"] = "scenes/é scene.yaml";
        const auto text                 = document.dump(2, ' ', true);
        ASSERT_NE(text.find("\\u00e9"), std::string::npos);
        const auto result = Read({text.data(), text.size()});
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.Value(), CookManifestReadStatus::Valid);
        EXPECT_STREQ(Manifest.Scenes()[0].Source.CStr(), "scenes/é scene.yaml");
        EXPECT_EQ(Diagnostics.Count, 0u);
    }

    TEST_F(CookManifestReaderTest, UnknownAndNewerFormatsWinOverOtherwiseRebuildableShapeErrors)
    {
        for (uint32_t variation = 0; variation < 5; ++variation)
        {
            auto document          = FullLedger();
            document["unexpected"] = true;
            if (variation == 0)
            {
                document["assets"][1]["format"] = "zematerial-v2-json";
            }
            if (variation == 1)
            {
                document["assets"][1]["adapter_revision"] = 2;
            }
            if (variation == 2)
            {
                document["assets"][1]["kind"] = "future-kind";
            }
            if (variation == 3)
            {
                document["recipe_revision"] = 2;
            }
            if (variation == 4)
            {
                document["target"] = "mobile-astc";
            }
            EXPECT_EQ(ReadJSON(document).Error(), VFS::VFSError::Unsupported);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, variation == 4 ? CookDiagnosticCode::UnsupportedTarget : CookDiagnosticCode::UnsupportedVersion);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, OversizedRecipeAndAdapterIntegersNeverGrantRebuildAuthority)
    {
        const cstring cases[] = {
            R"({"manifest_version":1,"recipe_revision":18446744073709551616,"unknown":true})",
            R"({"manifest_version":1,"recipe_revision":99999999999999999999999999999999999999})",
            R"({"manifest_version":1,"assets":[{"adapter_revision":18446744073709551616}]})",
        };
        for (cstring text : cases)
        {
            SCOPED_TRACE(text);
            EXPECT_EQ(Read(text).Error(), VFS::VFSError::Unsupported);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::UnsupportedVersion);
            EXPECT_FALSE(Manifest.IsValid());
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, DuplicateDecodedKeysAreFatalEvenInUnknownObjectsOrFutureVersions)
    {
        const cstring cases[] = {
            R"({"manifest_version":1,"extra":{"a":1,"\u0061":2}})",
            R"({"manifest_version":1,"extra":{"a\u0000b":1,"a\u0000b":2}})",
            R"({"manifest_version":2,"extra":{"x":1,"x":2}})",
            R"({"manifest_version":1,"target":"mobile","target":"desktop-vulkan-le64"})",
            R"({"manifest_version":1,"extra":[{"é":1,"\u00e9":2}]})",
        };
        for (cstring text : cases)
        {
            SCOPED_TRACE(text);
            EXPECT_EQ(Read(text).Error(), VFS::VFSError::Corrupted);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidManifest);
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookManifestReaderTest, MalformedJSONCannotBecomeARebuildDecision)
    {
        const cstring cases[] = {"", " ", "{", "{\"manifest_version\":1,}", "{\"manifest_version\":1}{}", "{\"manifest_version\":01}", "{\"manifest_version\":1,\"x\":\"\\ud800\"}", "{\"manifest_version\":1,\"x\":\"\xc0\x80\"}"};
        for (cstring text : cases)
        {
            SCOPED_TRACE(text);
            EXPECT_EQ(Read(text).Error(), VFS::VFSError::Corrupted);
            ASSERT_EQ(Diagnostics.Count, 1u);
            EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidManifest);
        }
        for (cstring text : {"{}", "[]", "1", "{\"manifest_version\":true}", "{\"manifest_version\":-1}", "{\"manifest_version\":4294967296}"})
        {
            EXPECT_EQ(Read(text).Error(), VFS::VFSError::Unsupported);
        }
    }

    TEST_F(CookManifestReaderTest, SemanticGraphFailuresAreRebuildsWithoutRetainedCleanupPaths)
    {
        for (uint32_t variation = 0; variation < 9; ++variation)
        {
            SCOPED_TRACE(variation);
            auto document = FullLedger();
            if (variation == 0)
            {
                document["assets"].erase(0); // Missing reachable environment.
            }
            if (variation == 1)
            {
                document["scenes"][0]["roots"].erase(0); // Unreachable environment record.
            }
            if (variation == 2)
            {
                document["assets"][1]["uuid"] = EnvironmentUUID; // UUID collision across kinds.
            }
            if (variation == 2)
            {
                document["assets"][2]["dependencies"][0]["uuid"] = EnvironmentUUID;
            }
            if (variation == 3)
            {
                document["assets"][1]["dependencies"][1] = document["assets"][1]["dependencies"][0];
            }
            if (variation == 4)
            {
                std::swap(document["assets"][1], document["assets"][2]);
            }
            if (variation == 5)
            {
                document["scenes"][0]["roots"].push_back(document["scenes"][0]["roots"][1]);
            }
            if (variation == 6)
            {
                document["assets"][2]["dependencies"][0]["slot"] = "submeshes/01/material";
            }
            if (variation == 7)
            {
                document["assets"][1]["dependencies"][0]["kind"] = "mesh";
            }
            if (variation == 7)
            {
                document["assets"][1]["dependencies"][0]["uuid"] = MeshUUID;
            }
            if (variation == 8)
            {
                document["assets"][3]["object"]["path"] = "objects/" + std::string(64, 'a') + ".png";
            }
            ExpectRebuild(document);
        }
    }

    TEST_F(CookManifestReaderTest, SharedPathsAllowSharingButRejectConflictingSizesAndDuplicateSources)
    {
        auto document    = GoldenJSON();
        auto second      = document["scenes"][0];
        second["uuid"]   = TextureUUID;
        second["source"] = "scenes/second.yaml";
        document["scenes"].push_back(second);
        const auto result = ReadJSON(document);
        ASSERT_TRUE(result.Succeeded());
        ASSERT_EQ(result.Value(), CookManifestReadStatus::Valid);
        document["scenes"][1]["object"]["size"] = 257;
        ExpectRebuild(document);
        document["scenes"][1]["object"]["size"] = 256;
        document["scenes"][1]["source"]         = document["scenes"][0]["source"];
        ExpectRebuild(document);
    }

    TEST_F(CookManifestReaderTest, EverySupportedTextureFormatUsesItsReviewedExtension)
    {
        constexpr cstring formats[] = {"image-png", "image-jpeg", "image-bmp", "image-tga", "image-gif", "image-psd", "image-pic"};
        for (cstring key : formats)
        {
            SCOPED_TRACE(key);
            auto        document = GoldenJSON();
            const auto* format   = FindCookAssetFormat(key);
            ASSERT_NE(format, nullptr);
            document["assets"]             = JSON::array({AssetJSON("texture", TextureUUID, key, format->Extension, 'a')});
            document["scenes"][0]["roots"] = JSON::array({IdentityJSON("texture", TextureUUID)});
            const auto result              = ReadJSON(document);
            ASSERT_TRUE(result.Succeeded());
            ASSERT_EQ(result.Value(), CookManifestReadStatus::Valid);
            EXPECT_EQ(Manifest.Assets()[0].Format, format);
        }
    }

    TEST_F(CookManifestReaderTest, EveryAllocationRefusalPreservesOldRecordsAndReleasesTemporaryCapacity)
    {
        ASSERT_TRUE(Read(Tests::ManifestGolden).Succeeded());
        const auto* scene = Manifest.Scenes().data();
        const auto  live  = Allocator.Statistics().LiveBytes;
        Routed.Calls      = 0;
        ASSERT_TRUE(ReadJSON(FullLedger()).Succeeded());
        const auto calls = Routed.Calls;
        ASSERT_GT(calls, 3u);
        ASSERT_TRUE(Read(Tests::ManifestGolden).Succeeded());
        scene = Manifest.Scenes().data();
        for (const auto error : {VFS::VFSError::OutOfMemory, VFS::VFSError::SizeLimitExceeded})
        {
            for (uint32_t call = 1; call <= calls; ++call)
            {
                SCOPED_TRACE(call);
                Routed.Calls   = 0;
                Routed.FailAt  = call;
                Routed.Failure = error;
                EXPECT_EQ(ReadJSON(FullLedger()).Error(), error);
                EXPECT_TRUE(Manifest.IsValid());
                EXPECT_EQ(Manifest.Scenes().data(), scene);
                EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
                ASSERT_EQ(Diagnostics.Count, 1u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, error == VFS::VFSError::OutOfMemory ? CookDiagnosticCode::AllocationFailed : CookDiagnosticCode::BudgetExceeded);
            }
        }
        Routed.FailAt = UINT32_MAX;
    }

    TEST_F(CookManifestReaderTest, ByteSceneAssetReferenceAndDepthLimitsAreHardFailures)
    {
        Limits.MaxManifestBytes = Tests::ManifestGolden.size() - 1;
        EXPECT_EQ(Read(Tests::ManifestGolden).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxManifestBytes = Tests::ManifestGolden.size();
        ASSERT_TRUE(Read(Tests::ManifestGolden).Succeeded());
        const auto* old         = Manifest.Scenes().data();
        Limits.MaxManifestBytes = ZMega(8);
        const auto full         = FullLedger();
        Limits.MaxAssets        = 3;
        EXPECT_EQ(ReadJSON(full).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxAssets = 8192;
        Limits.MaxEdges  = 2;
        EXPECT_EQ(ReadJSON(full).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxEdges                = 65536;
        Limits.MaxReferenceOccurrences = 4;
        EXPECT_EQ(ReadJSON(full).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxReferenceOccurrences = 65536;
        Limits.MaxEncodedAssetBytes    = 127;
        EXPECT_EQ(ReadJSON(full).Error(), VFS::VFSError::SizeLimitExceeded);
        Limits.MaxEncodedAssetBytes = ZMega(512);
        std::string deep            = "{\"manifest_version\":1,\"extra\":" + std::string(16, '[') + "0" + std::string(16, ']') + "}";
        EXPECT_EQ(Read({deep.data(), deep.size()}).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(Manifest.Scenes().data(), old);
    }

    TEST_F(CookManifestReaderTest, InvalidSetupAndHardSyntaxFailuresPreserveExistingLedger)
    {
        ASSERT_TRUE(Read(Tests::ManifestGolden).Succeeded());
        const auto* old         = Manifest.Scenes().data();
        const auto  live        = Allocator.Statistics().LiveBytes;
        const auto  allocations = Allocator.Allocations;
        EXPECT_EQ(Manifest.Read({nullptr, 1}, CookTarget::DesktopVulkanLE64, Limits, Routed, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Manifest.Read({}, CookTarget::Invalid, Limits, Routed, nullptr).Error(), VFS::VFSError::Unsupported);
        auto invalid_limits            = Limits;
        invalid_limits.MaxWorkingBytes = 0;
        EXPECT_EQ(Manifest.Read({}, CookTarget::DesktopVulkanLE64, invalid_limits, Routed, nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(Read("{").Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(Allocator.Allocations, allocations);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, live);
        EXPECT_EQ(Manifest.Scenes().data(), old);
    }

    TEST_F(CookManifestReaderTest, ManyUnknownKeysUseBoundedIndexingAndDoNotBypassDuplicateChecks)
    {
        JSON extra = JSON::object();
        for (uint32_t i = 0; i < 2048; ++i)
        {
            extra["key" + std::to_string(i)] = i;
        }
        auto document     = GoldenJSON();
        document["extra"] = extra;
        ExpectRebuild(document);
        auto text = extra.dump();
        text.pop_back();
        text += ",\"key0\":9}";
        text  = "{\"manifest_version\":1,\"extra\":" + text + "}";
        EXPECT_EQ(Read({text.data(), text.size()}).Error(), VFS::VFSError::Corrupted);
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::InvalidManifest);
    }
} // namespace ZEngine::Cook
