#include <ZEngine/Cook/CookDiagnostics.h>
#include <ZEngine/Helpers/UTF8.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <cstring>
#include <limits>
#include <type_traits>

namespace ZEngine::Cook
{
    TEST(CookTypesTest, ExactWireKeysAndLogicalRootSpellingRequireNoNormalization)
    {
        EXPECT_EQ(CookTargetKey(CookTarget::Invalid), nullptr);
        EXPECT_STREQ(CookTargetKey(CookTarget::DesktopVulkanLE64), "desktop-vulkan-le64");
        EXPECT_EQ(CookAssetKindKey(CookAssetKind::Unknown), nullptr);
        for (auto kind : {CookAssetKind::EnvironmentMap, CookAssetKind::Material, CookAssetKind::Mesh, CookAssetKind::Texture})
            EXPECT_EQ(ParseCookAssetKind(CookAssetKindKey(kind)), kind);
        for (cstring key : {"Mesh", "mesh ", "mesh\n", "", "other"})
            EXPECT_EQ(ParseCookAssetKind(key), CookAssetKind::Unknown);
        EXPECT_EQ(ParseCookAssetKind({"mesh\0suffix", 11}), CookAssetKind::Unknown);
        EXPECT_TRUE(IsCookRootKey("a"));
        EXPECT_TRUE(IsCookRootKey("assets_02-library"));
        for (cstring key : {"", "Assets", "1assets", "_assets", "assets.root", "assets/path", "assets\n"})
            EXPECT_FALSE(IsCookRootKey(key));
        char key[33] = {};
        std::fill_n(key, sizeof(key), 'a');
        EXPECT_TRUE(IsCookRootKey({key, 32}));
        EXPECT_FALSE(IsCookRootKey({key, 33}));
        EXPECT_FALSE(IsCookRootKey({nullptr, 1}));
        EXPECT_FALSE(IsCookLogicalPath({}));
        EXPECT_FALSE(IsCookLogicalPath(Core::VFS::VFSRelativePath::Parse("scenes").Value()));
        EXPECT_FALSE(IsCookLogicalPath(Core::VFS::VFSRelativePath::Parse("Scenes/main.yaml").Value()));
        EXPECT_TRUE(IsCookLogicalPath(Core::VFS::VFSRelativePath::Parse("scenes/Main.yaml").Value()));
    }

    TEST(CookTypesTest, AssetIdentitiesUseCanonicalKindThenUUIDByteOrder)
    {
        const auto              first       = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
        const auto              second      = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
        const CookAssetIdentity environment = {CookAssetKind::EnvironmentMap, second};
        const CookAssetIdentity material    = {CookAssetKind::Material, first};
        const CookAssetIdentity mesh        = {CookAssetKind::Mesh, first};
        const CookAssetIdentity texture     = {CookAssetKind::Texture, first};
        EXPECT_FALSE(CookAssetIdentity{}.IsValid());
        EXPECT_FALSE((CookAssetIdentity{CookAssetKind::Mesh, {}}).IsValid());
        EXPECT_FALSE((CookAssetIdentity{static_cast<CookAssetKind>(255), first}).IsValid());
        EXPECT_TRUE(environment.IsValid());
        EXPECT_TRUE(environment < material);
        EXPECT_TRUE(material < mesh);
        EXPECT_TRUE(mesh < texture);
        EXPECT_TRUE((CookAssetIdentity{CookAssetKind::Mesh, first} < CookAssetIdentity{CookAssetKind::Mesh, second}));
        EXPECT_FALSE(mesh < mesh);
        EXPECT_EQ(mesh, (CookAssetIdentity{CookAssetKind::Mesh, first}));
    }

    TEST(CookDiagnosticsTest, DefaultsFixedCapacityOverflowAndClearingPreserveFailureVisibility)
    {
        static_assert(std::is_trivially_copyable_v<CookDiagnostic>);
        CookDiagnostics diagnostics = {};
        EXPECT_EQ(diagnostics.Count, 0u);
        EXPECT_FALSE(diagnostics.HasErrors());
        CookDiagnostic warning = {.Severity = CookSeverity::Warning, .Code = CookDiagnosticCode::IOFailure};
        ASSERT_TRUE(warning.SetMessage("retained warning"));
        for (size_t i = 0; i < diagnostics.Entries.size(); ++i)
            diagnostics.Add(warning);
        EXPECT_EQ(diagnostics.Count, 64u);
        EXPECT_FALSE(diagnostics.HasErrors());
        CookDiagnostic failure = {.Code = CookDiagnosticCode::InvalidAsset};
        diagnostics.Add(failure);
        EXPECT_TRUE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
        EXPECT_EQ(diagnostics.Count, 64u);
        EXPECT_EQ(diagnostics.DroppedEntries, 1u);
        EXPECT_STREQ(diagnostics.Entries[63].Message, "retained warning");
        diagnostics.ErrorCount = diagnostics.DroppedEntries = std::numeric_limits<uint64_t>::max();
        diagnostics.Add(failure);
        EXPECT_EQ(diagnostics.ErrorCount, std::numeric_limits<uint64_t>::max());
        EXPECT_EQ(diagnostics.DroppedEntries, std::numeric_limits<uint64_t>::max());
        diagnostics.Clear();
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.Count, 0u);
        EXPECT_EQ(diagnostics.DroppedEntries, 0u);
        EXPECT_STREQ(diagnostics.Entries[63].Message, "");
        diagnostics.Clear();
    }

    TEST(CookDiagnosticsTest, CleanupAndCacheMissCodesCannotTurnWarningsIntoFailure)
    {
        CookDiagnostics diagnostics = {};
        diagnostics.Add({.Code = CookDiagnosticCode::CacheMiss});
        diagnostics.Add({.Code = CookDiagnosticCode::CleanupWarning});
        EXPECT_EQ(diagnostics.Count, 2u);
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.Entries[0].Severity, CookSeverity::Warning);
        EXPECT_EQ(diagnostics.Entries[1].Severity, CookSeverity::Warning);
    }

    TEST(CookDiagnosticsTest, EmplaceConstructsInCallerStorageAndResetsEveryPayloadField)
    {
        CookDiagnostics diagnostics = {};
        auto&           old         = diagnostics.Entries[0];
        old.Source                  = Core::VFS::VFSRelativePath::Parse("scenes/old.yaml").Value();
        old.SceneUUID = old.EntityUUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
        old.Asset                      = {CookAssetKind::Mesh, old.SceneUUID};
        ASSERT_TRUE(old.SetKeys("OldSchema", "old_field"));
        ASSERT_TRUE(old.SetMessage("old message"));
        for (auto& identity : old.Chain)
            identity = old.Asset;
        old.ChainCount     = 32;
        old.ChainTruncated = true;
        auto* entry        = diagnostics.Emplace(CookStage::Publish, CookDiagnosticCode::IOFailure, Core::VFS::VFSError::IOError);
        ASSERT_EQ(entry, &diagnostics.Entries[0]);
        EXPECT_EQ(diagnostics.Count, 1u);
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
        EXPECT_EQ(entry->Severity, CookSeverity::Error);
        EXPECT_EQ(entry->Stage, CookStage::Publish);
        EXPECT_EQ(entry->Code, CookDiagnosticCode::IOFailure);
        EXPECT_EQ(entry->Underlying, Core::VFS::VFSError::IOError);
        EXPECT_FALSE(entry->Source.IsValid());
        EXPECT_TRUE(entry->SceneUUID.is_nil());
        EXPECT_TRUE(entry->EntityUUID.is_nil());
        EXPECT_EQ(entry->Asset, CookAssetIdentity{});
        EXPECT_EQ(entry->ChainCount, 0u);
        EXPECT_FALSE(entry->ChainTruncated);
        for (const auto& identity : entry->Chain)
            EXPECT_EQ(identity, CookAssetIdentity{});
        EXPECT_TRUE(std::all_of(std::begin(entry->SchemaKey), std::end(entry->SchemaKey), [](char c) { return c == '\0'; }));
        EXPECT_TRUE(std::all_of(std::begin(entry->FieldKey), std::end(entry->FieldKey), [](char c) { return c == '\0'; }));
        EXPECT_TRUE(std::all_of(std::begin(entry->Message), std::end(entry->Message), [](char c) { return c == '\0'; }));
        ASSERT_TRUE(entry->SetMessage("retained"));
        diagnostics.Clear();
        EXPECT_EQ(diagnostics.Count, 0u);
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_STREQ(diagnostics.Entries[0].Message, "");
        ASSERT_EQ(diagnostics.Emplace(CookStage::Capture, CookDiagnosticCode::InvalidPath), &diagnostics.Entries[0]);
        EXPECT_EQ(diagnostics.Entries[0].Underlying, Core::VFS::VFSError::OK);
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
    }

    TEST(CookDiagnosticsTest, EmplaceOverflowNormalizesWarningsAndNeverHidesOrWrapsErrors)
    {
        CookDiagnostics diagnostics = {};
        for (size_t i = 0; i < diagnostics.Entries.size(); ++i)
        {
            auto* entry = diagnostics.Emplace(CookStage::Setup, CookDiagnosticCode::CacheMiss);
            ASSERT_EQ(entry, &diagnostics.Entries[i]);
            EXPECT_EQ(entry->Severity, CookSeverity::Warning);
            ASSERT_TRUE(entry->SetMessage("retained cache miss"));
        }
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.Emplace(CookStage::Cleanup, CookDiagnosticCode::CleanupWarning), nullptr);
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.DroppedEntries, 1u);
        EXPECT_EQ(diagnostics.Emplace(CookStage::Encode, CookDiagnosticCode::InvalidAsset), nullptr);
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
        EXPECT_EQ(diagnostics.DroppedEntries, 2u);
        EXPECT_EQ(diagnostics.Count, 64u);
        for (const auto& entry : diagnostics.Entries)
        {
            EXPECT_EQ(entry.Code, CookDiagnosticCode::CacheMiss);
            EXPECT_EQ(entry.Severity, CookSeverity::Warning);
            EXPECT_STREQ(entry.Message, "retained cache miss");
        }
        diagnostics.ErrorCount = diagnostics.DroppedEntries = UINT64_MAX;
        EXPECT_EQ(diagnostics.Emplace(CookStage::Encode, CookDiagnosticCode::InvalidAsset), nullptr);
        EXPECT_EQ(diagnostics.ErrorCount, UINT64_MAX);
        EXPECT_EQ(diagnostics.DroppedEntries, UINT64_MAX);
    }

    TEST(CookDiagnosticsTest, AddAndEmplaceShareSeverityAndCounterSemanticsEvenForAliasedCopies)
    {
        CookDiagnostics diagnostics = {};
        auto*           warning     = diagnostics.Emplace(CookStage::Cleanup, CookDiagnosticCode::CleanupWarning, Core::VFS::VFSError::IOError);
        ASSERT_NE(warning, nullptr);
        ASSERT_TRUE(warning->SetMessage("owned warning"));
        diagnostics.Add(*warning);
        EXPECT_EQ(diagnostics.ErrorCount, 0u);
        EXPECT_STREQ(diagnostics.Entries[1].Message, "owned warning");
        // Add also accepts its as-yet-unretained destination as the source.
        auto& destination = diagnostics.Entries[diagnostics.Count];
        destination.Stage = CookStage::Discover;
        destination.Code  = CookDiagnosticCode::InvalidAsset;
        ASSERT_TRUE(destination.SetMessage("self copy"));
        diagnostics.Add(destination);
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
        EXPECT_STREQ(diagnostics.Entries[2].Message, "self copy");
        ASSERT_NE(diagnostics.Emplace(CookStage::Setup, CookDiagnosticCode::IOFailure, Core::VFS::VFSError::OK, CookSeverity::Info), nullptr);
        EXPECT_EQ(diagnostics.ErrorCount, 1u);
        EXPECT_EQ(diagnostics.Entries[3].Severity, CookSeverity::Info);
        auto* malformed = diagnostics.Emplace(CookStage::Discover, CookDiagnosticCode::InvalidAsset);
        ASSERT_NE(malformed, nullptr);
        EXPECT_FALSE(malformed->SetMessage({"\xff", 1}));
        EXPECT_STREQ(malformed->Message, "");
        EXPECT_EQ(diagnostics.ErrorCount, 2u);
    }

    TEST(CookDiagnosticsTest, KeysAreLengthBoundedUTF8AndTransactionalEvenWithAliasedViews)
    {
        CookDiagnostic entry = {};
        ASSERT_TRUE(entry.SetKeys("MeshRenderer", "mesh_uuid"));
        char oversized[128] = {};
        std::fill_n(oversized, sizeof(oversized), 'a');
        EXPECT_FALSE(entry.SetKeys("valid", {oversized, sizeof(oversized)}));
        EXPECT_FALSE(entry.SetKeys({"a\0b", 3}, "valid"));
        EXPECT_FALSE(entry.SetKeys("a\n", "valid"));
        EXPECT_FALSE(entry.SetKeys({nullptr, 1}, "valid"));
        EXPECT_FALSE(entry.SetKeys({"\xc0\x80", 2}, "valid"));
        EXPECT_STREQ(entry.SchemaKey, "MeshRenderer");
        EXPECT_STREQ(entry.FieldKey, "mesh_uuid");
        ASSERT_TRUE(entry.SetKeys(entry.FieldKey, entry.SchemaKey));
        EXPECT_STREQ(entry.SchemaKey, "mesh_uuid");
        EXPECT_STREQ(entry.FieldKey, "MeshRenderer");
        ASSERT_TRUE(entry.SetKeys({oversized, 127}, {}));
        EXPECT_EQ(std::strlen(entry.SchemaKey), 127u);
        EXPECT_STREQ(entry.FieldKey, "");
    }

    TEST(CookDiagnosticsTest, KeyAssignmentNeedsOnlyOneSnapshotForCrossedPartialAndSharedAliases)
    {
        CookDiagnostic entry = {};
        ASSERT_TRUE(entry.SetKeys("SchemaOriginal", "FieldOriginal"));
        ASSERT_TRUE(entry.SetKeys({entry.FieldKey + 5, 8}, {entry.SchemaKey + 6, 8}));
        EXPECT_STREQ(entry.SchemaKey, "Original");
        EXPECT_STREQ(entry.FieldKey, "Original");

        ASSERT_TRUE(entry.SetKeys("SchemaOriginal", "FieldOriginal"));
        ASSERT_TRUE(entry.SetKeys({entry.FieldKey + 1, 4}, {entry.FieldKey + 5, 8}));
        EXPECT_STREQ(entry.SchemaKey, "ield");
        EXPECT_STREQ(entry.FieldKey, "Original");

        ASSERT_TRUE(entry.SetKeys("SchemaOriginal", "FieldOriginal"));
        ASSERT_TRUE(entry.SetKeys({entry.SchemaKey + 6, 8}, {entry.SchemaKey + 1, 5}));
        EXPECT_STREQ(entry.SchemaKey, "Original");
        EXPECT_STREQ(entry.FieldKey, "chema");

        ASSERT_TRUE(entry.SetKeys({entry.SchemaKey + 1, 3}, {entry.FieldKey + 1, 2}));
        EXPECT_STREQ(entry.SchemaKey, "rig");
        EXPECT_STREQ(entry.FieldKey, "he");
        EXPECT_TRUE(std::all_of(entry.SchemaKey + 4, std::end(entry.SchemaKey), [](char c) { return c == '\0'; }));
        EXPECT_TRUE(std::all_of(entry.FieldKey + 3, std::end(entry.FieldKey), [](char c) { return c == '\0'; }));

        const auto before = entry;
        EXPECT_FALSE(entry.SetKeys({entry.FieldKey, 128}, entry.SchemaKey));
        EXPECT_STREQ(entry.SchemaKey, before.SchemaKey);
        EXPECT_STREQ(entry.FieldKey, before.FieldKey);
    }

    TEST(CookDiagnosticsTest, ExplanatoryMessagesShortenAtUnicodeBoundariesAndOwnTheirText)
    {
        CookDiagnostic entry        = {};
        char           message[260] = {};
        std::fill_n(message, 253, 'a');
        message[253] = '\xf0';
        message[254] = '\x9f';
        message[255] = '\x98';
        message[256] = '\x80';
        ASSERT_TRUE(entry.SetMessage({message, 257}));
        EXPECT_EQ(std::strlen(entry.Message), 253u);
        EXPECT_TRUE(Helpers::IsValidUTF8(entry.Message, std::strlen(entry.Message)));
        message[0] = 'b';
        EXPECT_EQ(entry.Message[0], 'a');
        EXPECT_FALSE(entry.SetMessage({"\xff", 1}));
        EXPECT_EQ(entry.Message[0], 'a');
        ASSERT_TRUE(entry.SetMessage({entry.Message + 1, 3}));
        EXPECT_STREQ(entry.Message, "aaa");
        ASSERT_TRUE(entry.SetMessage({}));
        EXPECT_STREQ(entry.Message, "");
    }

    TEST(CookDiagnosticsTest, LongChainsKeepFirst31AncestorsAndTheFinalFailingIdentity)
    {
        Core::Containers::FixedArray<CookAssetIdentity, 40> chain = {};
        for (size_t i = 0; i < chain.size(); ++i)
        {
            uint8_t bytes[16] = {};
            bytes[15]         = static_cast<uint8_t>(i + 1);
            chain[i]          = {CookAssetKind::Mesh, uuids::uuid(bytes, bytes + 16)};
        }
        CookDiagnostic entry = {};
        ASSERT_TRUE(entry.SetChain({chain.data(), chain.size()}));
        EXPECT_EQ(entry.ChainCount, 32u);
        EXPECT_TRUE(entry.ChainTruncated);
        for (size_t i = 0; i < 31; ++i)
            EXPECT_EQ(entry.Chain[i], chain[i]);
        EXPECT_EQ(entry.Chain[31], chain[39]);
        chain[1] = {};
        EXPECT_FALSE(entry.SetChain({chain.data(), 2}));
        EXPECT_EQ(entry.ChainCount, 32u);
        EXPECT_EQ(entry.Chain[31], chain[39]);
        EXPECT_FALSE(entry.SetChain({nullptr, 1}));
        ASSERT_TRUE(entry.SetChain({chain.data(), 1}));
        EXPECT_EQ(entry.ChainCount, 1u);
        EXPECT_FALSE(entry.ChainTruncated);
        EXPECT_EQ(entry.Chain[31], CookAssetIdentity{});
        ASSERT_TRUE(entry.SetChain({}));
        EXPECT_EQ(entry.ChainCount, 0u);
    }

    TEST(CookDiagnosticsTest, DiagnosticCodesHaveTheSelectedReturnCategories)
    {
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::InvalidRequest), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::InvalidBudget), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::InputBusy), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::OutputBusy), Core::VFS::VFSError::AlreadyExists);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::UnsupportedTarget), Core::VFS::VFSError::Unsupported);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::InputChanged), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::IdentityConflict), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::MissingDependency), Core::VFS::VFSError::NotFound);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::BudgetExceeded), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::AllocationFailed), Core::VFS::VFSError::OutOfMemory);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::IOFailure, Core::VFS::VFSError::PermissionDenied), Core::VFS::VFSError::PermissionDenied);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::IOFailure), Core::VFS::VFSError::IOError);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::CacheMiss), Core::VFS::VFSError::OK);
        EXPECT_EQ(CookDiagnosticError(CookDiagnosticCode::CleanupWarning), Core::VFS::VFSError::OK);
        EXPECT_EQ(CookDiagnosticError(static_cast<CookDiagnosticCode>(255)), Core::VFS::VFSError::InvalidPath);
    }
} // namespace ZEngine::Cook
