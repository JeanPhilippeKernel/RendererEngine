#include <ZEngine/Cook/CookFingerprint.h>
#include <ZEngine/Core/Serialization/CanonicalJSONWriter.h>
#include <ZEngine/Core/VFS/Meta/MetaFileIO.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include "CookTestAllocator.h"

namespace ZEngine::Cook
{
    namespace
    {
        constexpr cstring          FingerprintMetadata = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"MeshImporter","settings":[{"key":"z","value":"2"},{"key":"a","value":"1"}]})";

        Core::VFS::VFSResult<void> ParseFingerprintMetadata(Core::VFS::StrictMetaFileData* output)
        {
            return Core::VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(FingerprintMetadata), Helpers::secure_strlen(FingerprintMetadata)}, output);
        }

        Core::VFS::MetaKeyValuePair OrderedMetadataSetting(uint32_t index)
        {
            Core::VFS::MetaKeyValuePair setting = {};
            setting.Key[0]                      = 'k';
            setting.Value[0]                    = 'v';
            setting.Key[1] = setting.Value[1] = static_cast<char>('0' + index / 10);
            setting.Key[2] = setting.Value[2] = static_cast<char>('0' + index % 10);
            return setting;
        }
    } // namespace

    class CookFingerprintTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(4), 1> Allocator = {};
    };

    TEST_F(CookFingerprintTest, MetadataAndMaterialReserveExactFixedStateWithoutAllocationsAndPreserveKeysOnRefusal)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        Importers::AssetCodec::MaterialAssetDocument material = {};
        material.UUID                                         = metadata.Data.AssetUUID;
        for (const bool semantic_material : {false, true})
        {
            Tests::FixedStateAllocator budget   = {};
            const auto                 compute  = [&](Digest256* output) { return semantic_material ? ComputeCookMaterialContentKey(budget, material, output) : ComputeCookMetadataKey(budget, metadata, output); };
            Digest256                  expected = {};
            ASSERT_TRUE(compute(&expected).Succeeded());
            const auto fixed = budget.Bytes;
            EXPECT_EQ(budget.ChargeCalls, 1u);
            EXPECT_EQ(budget.Category, CookMemoryCategory::Codecs);
            EXPECT_EQ(fixed, Core::Hash::SHA256::FixedWorkingBytes() + sizeof(Core::Serialization::CanonicalJSONWriter) + sizeof(CookMemoryCharge) + (semantic_material ? 0 : sizeof(Core::Containers::FixedArray<uint8_t, Core::VFS::META_MAX_SETTINGS>)));
            EXPECT_EQ(budget.Statistics().PeakBytes, fixed);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            for (const auto error : {Core::VFS::VFSError::Unsupported, Core::VFS::VFSError::OutOfMemory, Core::VFS::VFSError::SizeLimitExceeded})
            {
                budget.Failure    = error;
                Digest256 output  = {};
                output.Bytes[0]   = 123;
                const auto before = output;
                const auto calls  = budget.ChargeCalls;
                EXPECT_EQ(compute(&output).Error(), error);
                EXPECT_EQ(budget.ChargeCalls, calls + 1);
                EXPECT_EQ(output, before);
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            budget.Failure = Core::VFS::VFSError::OK;
            for (const auto available : {fixed - 1, fixed})
            {
                ASSERT_TRUE(budget.Ledger.Initialize(available).Succeeded());
                Digest256 output  = {};
                output.Bytes[0]   = 123;
                const auto before = output;
                const auto result = compute(&output);
                EXPECT_EQ(result.Succeeded(), available == fixed);
                EXPECT_EQ(output, available == fixed ? expected : before);
                if (result.Failed())
                {
                    EXPECT_EQ(result.Error(), Core::VFS::VFSError::SizeLimitExceeded);
                }
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
            EXPECT_EQ(budget.Allocations, 0u);
        }
    }

    TEST_F(CookFingerprintTest, FormerObjectOnlyBudgetCannotOmitCompressionBuffersOrPublishPartialKeys)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        Importers::AssetCodec::MaterialAssetDocument material = {};
        material.UUID                                         = metadata.Data.AssetUUID;
        for (const bool semantic_material : {false, true})
        {
            const uint64_t             former    = sizeof(Core::Hash::SHA256) + sizeof(Core::Serialization::CanonicalJSONWriter) + sizeof(CookMemoryCharge) + (semantic_material ? 0 : sizeof(Core::Containers::FixedArray<uint8_t, Core::VFS::META_MAX_SETTINGS>));
            Tests::FixedStateAllocator allocator = {};
            ASSERT_TRUE(allocator.Ledger.Initialize(former).Succeeded());
            Digest256 output  = {};
            output.Bytes[0]   = 123;
            const auto before = output;
            const auto status = semantic_material ? ComputeCookMaterialContentKey(allocator, material, &output) : ComputeCookMetadataKey(allocator, metadata, &output);
            EXPECT_EQ(status.Error(), Core::VFS::VFSError::SizeLimitExceeded);
            EXPECT_EQ(output, before);
            EXPECT_EQ(allocator.Bytes, former + 96);
            EXPECT_EQ(allocator.ChargeCalls, 1u);
            EXPECT_EQ(allocator.Allocations, 0u);
            EXPECT_EQ(allocator.Statistics().LiveBytes, 0u);
        }
    }

    TEST_F(CookFingerprintTest, InvalidMetadataPreflightDoesNotQueryAnUnsupportedBudget)
    {
        Tests::FixedStateAllocator budget      = {};
        budget.Failure                         = Core::VFS::VFSError::Unsupported;
        Core::VFS::StrictMetaFileData metadata = {};
        Digest256                     output   = {};
        EXPECT_EQ(ComputeCookMetadataKey(budget, metadata, &output).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(ComputeCookMetadataKey(budget, metadata, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(ComputeCookMaterialContentKey(budget, {}, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(budget.ChargeCalls, 0u);
        EXPECT_EQ(budget.Allocations, 0u);
    }

    TEST_F(CookFingerprintTest, MetadataKeyMatchesIndependentCanonicalJSONGoldenWithoutMutatingSettingsOrder)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        Digest256 output = {};
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &output).Succeeded());
        // Independent shasum -a 256 over the exact following bytes + LF:
        // {"importer":"MeshImporter","settings":{"a":"1","z":"2"}}
        EXPECT_STREQ(output.Hex().data(), "d63fc33bb78d0c6a6d01df2b6c98c23d7dfb46d26fff4dcd9118d65f03165960");
        EXPECT_STREQ(metadata.Data.Settings[0].Key, "z");
        EXPECT_STREQ(metadata.Data.Settings[1].Key, "a");
        const auto first          = metadata.Data.Settings[0];
        metadata.Data.Settings[0] = metadata.Data.Settings[1];
        metadata.Data.Settings[1] = first;
        Digest256 reordered       = {};
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &reordered).Succeeded());
        EXPECT_EQ(reordered, output);
    }

    class CookMetadataOrderingTest : public CookFingerprintTest, public testing::WithParamInterface<uint32_t>
    {
    };

    TEST_P(CookMetadataOrderingTest, BoundedIndexInsertionMatchesCanonicalOrderWithoutAllocationsOrCallerMutation)
    {
        const auto count = GetParam();
        ASSERT_LE(count, Core::VFS::META_MAX_SETTINGS);
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        metadata.Data.SettingsCount                   = count;

        // Independent order oracle: generate canonical keys directly, without
        // calling either a comparison sort or the metadata fingerprint helper.
        Core::Hash::SHA256                       hash = {};
        Core::Serialization::CanonicalJSONWriter writer([](void* context, Core::Containers::ArrayView<const uint8_t> bytes) { return static_cast<Core::Hash::SHA256*>(context)->Update(bytes); }, &hash);
        ASSERT_TRUE(writer.Raw("{\"importer\":\"MeshImporter\",\"settings\":{"));
        for (uint32_t i = 0; i < count; ++i)
        {
            const auto setting = OrderedMetadataSetting(i);
            ASSERT_TRUE((!i || writer.Raw(",")) && writer.String(setting.Key) && writer.Raw(":") && writer.String(setting.Value));
        }
        ASSERT_TRUE(writer.Raw("}}\n"));
        Digest256 expected = {};
        ASSERT_TRUE(hash.Finalize(&expected).Succeeded());

        for (uint32_t pattern = 0; pattern < 5; ++pattern)
        {
            SCOPED_TRACE(pattern);
            Core::Containers::FixedArray<uint32_t, Core::VFS::META_MAX_SETTINGS> order = {};
            for (uint32_t i = 0; i < count; ++i)
            {
                order[i] = pattern == 1 ? count - 1 - i : pattern == 2 ? (i + count / 2) % count : pattern == 3 ? (i % 2 ? count - 1 - i / 2 : i / 2) : i;
            }
            if (pattern == 4)
            {
                uint32_t seed = 0x12345678;
                for (uint32_t i = count; i > 1; --i)
                {
                    seed ^= seed << 13;
                    seed ^= seed >> 17;
                    seed ^= seed << 5;
                    std::swap(order[i - 1], order[seed % i]);
                }
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                metadata.Data.Settings[i] = OrderedMetadataSetting(order[i]);
            }
            Tests::FixedStateAllocator budget = {};
            Digest256                  actual = {};
            ASSERT_TRUE(ComputeCookMetadataKey(budget, metadata, &actual).Succeeded());
            EXPECT_EQ(actual, expected);
            EXPECT_EQ(budget.ChargeCalls, 1u);
            EXPECT_EQ(budget.Allocations, 0u);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            EXPECT_EQ(metadata.Data.SettingsCount, count);
            for (uint32_t i = 0; i < count; ++i)
            {
                const auto setting = OrderedMetadataSetting(order[i]);
                EXPECT_STREQ(metadata.Data.Settings[i].Key, setting.Key);
                EXPECT_STREQ(metadata.Data.Settings[i].Value, setting.Value);
            }
            const auto fixed = budget.Bytes;
            for (const auto available : {fixed - 1, fixed})
            {
                ASSERT_TRUE(budget.Ledger.Initialize(available).Succeeded());
                actual.Bytes[0]   = 123;
                const auto before = actual;
                const auto result = ComputeCookMetadataKey(budget, metadata, &actual);
                EXPECT_EQ(result.Succeeded(), available == fixed);
                if (result.Failed())
                {
                    EXPECT_EQ(result.Error(), Core::VFS::VFSError::SizeLimitExceeded);
                }
                EXPECT_EQ(actual, available == fixed ? expected : before);
                EXPECT_EQ(budget.Bytes, fixed);
                EXPECT_EQ(budget.Allocations, 0u);
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
        }
    }

    INSTANTIATE_TEST_SUITE_P(MetadataSettingBoundaries, CookMetadataOrderingTest, testing::Values(0u, 1u, 2u, 15u, 16u, 17u, 31u, 32u));

    TEST_F(CookFingerprintTest, EverySixKeyPermutationProducesTheSameCanonicalFingerprint)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        Core::Containers::FixedArray<uint32_t, 6> order = {0, 1, 2, 3, 4, 5};
        metadata.Data.SettingsCount                     = static_cast<uint32_t>(order.size());
        Tests::FixedStateAllocator budget               = {};
        Digest256                  expected             = {};
        uint32_t                   permutations         = 0;
        do
        {
            for (uint32_t i = 0; i < order.size(); ++i)
            {
                metadata.Data.Settings[i] = OrderedMetadataSetting(order[i]);
            }
            Digest256 actual = {};
            ASSERT_TRUE(ComputeCookMetadataKey(budget, metadata, &actual).Succeeded());
            if (!permutations)
            {
                expected = actual;
            }
            EXPECT_EQ(actual, expected);
            EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            ++permutations;
        } while (std::next_permutation(order.begin(), order.end()));
        EXPECT_EQ(permutations, 720u);
        EXPECT_EQ(budget.ChargeCalls, permutations);
        EXPECT_EQ(budget.Allocations, 0u);
    }

    TEST_F(CookFingerprintTest, DuplicateKeysAtEveryFullCapacityPairFailWithoutPublishingOrAllocating)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        metadata.Data.SettingsCount       = Core::VFS::META_MAX_SETTINGS;
        Tests::FixedStateAllocator budget = {};
        for (uint32_t second = 1; second < metadata.Data.SettingsCount; ++second)
        {
            for (uint32_t first = 0; first < second; ++first)
            {
                SCOPED_TRACE(first);
                SCOPED_TRACE(second);
                for (uint32_t i = 0; i < metadata.Data.SettingsCount; ++i)
                {
                    metadata.Data.Settings[i] = OrderedMetadataSetting(metadata.Data.SettingsCount - 1 - i);
                }
                auto&       destination = metadata.Data.Settings[second];
                const auto& source      = metadata.Data.Settings[first];
                ASSERT_EQ(Helpers::secure_strcpy(destination.Key, sizeof(destination.Key), source.Key), Helpers::MEMORY_OP_SUCCESS);
                // Different values cannot make duplicate setting keys valid.
                ASSERT_STRNE(destination.Value, source.Value);
                Digest256 actual  = {};
                actual.Bytes[0]   = 123;
                const auto before = actual;
                EXPECT_EQ(ComputeCookMetadataKey(budget, metadata, &actual).Error(), Core::VFS::VFSError::Corrupted);
                EXPECT_EQ(actual, before);
                EXPECT_EQ(budget.Statistics().LiveBytes, 0u);
            }
        }
        EXPECT_EQ(budget.ChargeCalls, Core::VFS::META_MAX_SETTINGS * (Core::VFS::META_MAX_SETTINGS - 1) / 2);
        EXPECT_EQ(budget.Allocations, 0u);
    }

    TEST_F(CookFingerprintTest, MetadataLocationsTimestampsHashesAndAssetIdentityDoNotChangeItsSemanticKey)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        Digest256 original = {}, changed = {};
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &original).Succeeded());
        ASSERT_EQ(Helpers::secure_strcpy(metadata.Data.SourcePath, sizeof(metadata.Data.SourcePath), "/missing/native/model.glb"), Helpers::MEMORY_OP_SUCCESS);
        ASSERT_EQ(Helpers::secure_strcpy(metadata.Data.ArtifactPath, sizeof(metadata.Data.ArtifactPath), "/other/mesh.zemesh"), Helpers::MEMORY_OP_SUCCESS);
        metadata.Data.SourceHash       = UINT64_MAX;
        metadata.Data.LastImportTimeNs = INT64_MIN;
        metadata.Data.AssetUUID        = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
        metadata.Data.Status           = Core::VFS::ImportStatus::Unknown;
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &changed).Succeeded());
        EXPECT_EQ(changed, original);
        metadata.Data.Settings[0].Value[0] = '3';
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &changed).Succeeded());
        EXPECT_NE(changed, original);
        metadata.Data.Settings[0].Value[0] = '2';
        metadata.Data.ImporterName[0]      = 'X';
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &changed).Succeeded());
        EXPECT_NE(changed, original);
    }

    TEST_F(CookFingerprintTest, MetadataKeysUseUnsignedUTF8OrderAndCanonicalEscaping)
    {
        constexpr cstring             text     = R"({"uuid":"00000000-0000-0000-0000-000000000001","importer":"I","settings":[{"key":"\u00e9","value":"\n"},{"key":"z","value":"\"\\"}]})";
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(Core::VFS::MetaFileIO::ParseStrict({reinterpret_cast<const uint8_t*>(text), Helpers::secure_strlen(text)}, &metadata).Succeeded());
        Digest256 actual = {}, expected = {};
        ASSERT_TRUE(ComputeCookMetadataKey(Allocator, metadata, &actual).Succeeded());
        constexpr cstring  canonical = R"({"importer":"I","settings":{"z":"\"\\",")"
                                       "\xc3\xa9"
                                       R"(":"\u000a"}})"
                                       "\n";
        Core::Hash::SHA256 hash      = {};
        ASSERT_TRUE(hash.Update({reinterpret_cast<const uint8_t*>(canonical), Helpers::secure_strlen(canonical)}).Succeeded());
        ASSERT_TRUE(hash.Finalize(&expected).Succeeded());
        EXPECT_EQ(actual, expected);
    }

    TEST_F(CookFingerprintTest, MalformedMetadataNeverPublishesAPartialKeyOrReadsBeyondInlineFields)
    {
        Core::VFS::StrictMetaFileData metadata = {};
        ASSERT_TRUE(ParseFingerprintMetadata(&metadata).Succeeded());
        for (uint32_t invalid = 0; invalid < 7; ++invalid)
        {
            auto candidate = metadata;
            if (invalid == 0)
            {
                candidate.Fields = 0;
            }
            if (invalid == 1)
            {
                candidate.Data.AssetUUID = {};
            }
            if (invalid == 2)
            {
                candidate.Data.ImporterName[0] = '\0';
            }
            if (invalid == 3)
            {
                candidate.Data.SettingsCount = Core::VFS::META_MAX_SETTINGS + 1;
            }
            if (invalid == 4)
            {
                candidate.Data.Settings[1] = candidate.Data.Settings[0];
            }
            if (invalid == 5)
            {
                Helpers::secure_memset(candidate.Data.ImporterName, 'x', sizeof(candidate.Data.ImporterName), sizeof(candidate.Data.ImporterName));
            }
            if (invalid == 6)
            {
                Helpers::secure_memset(candidate.Data.Settings[0].Value, 'x', sizeof(candidate.Data.Settings[0].Value), sizeof(candidate.Data.Settings[0].Value));
            }
            Digest256 output  = {};
            output.Bytes[0]   = 123;
            const auto before = output;
            EXPECT_EQ(ComputeCookMetadataKey(Allocator, candidate, &output).Error(), invalid == 3 ? Core::VFS::VFSError::SizeLimitExceeded : Core::VFS::VFSError::Corrupted);
            EXPECT_EQ(output, before);
        }
        EXPECT_EQ(ComputeCookMetadataKey(Allocator, metadata, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
    }

    TEST_F(CookFingerprintTest, MaterialContentKeyIgnoresLocationsAndProvenanceButIncludesSemanticValues)
    {
        Importers::AssetCodec::MaterialAssetDocument material = {};
        material.UUID                                         = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
        material.Textures[0].UUID                             = uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value();
        ASSERT_EQ(Helpers::secure_strcpy(material.Textures[0].Path, sizeof(material.Textures[0].Path), "/assets/a.png"), Helpers::MEMORY_OP_SUCCESS);
        Digest256 original = {}, moved = {}, changed = {};
        ASSERT_TRUE(ComputeCookMaterialContentKey(Allocator, material, &original).Succeeded());
        ASSERT_EQ(Helpers::secure_strcpy(material.Textures[0].Path, sizeof(material.Textures[0].Path), "/other/a.png"), Helpers::MEMORY_OP_SUCCESS);
        material.HasSource = true;
        ASSERT_EQ(Helpers::secure_strcpy(material.Source.Path, sizeof(material.Source.Path), "/missing/native/model.glb"), Helpers::MEMORY_OP_SUCCESS);
        ASSERT_TRUE(ComputeCookMaterialContentKey(Allocator, material, &moved).Succeeded());
        EXPECT_EQ(moved, original);
        material.Factors[0] = 1.f;
        ASSERT_TRUE(ComputeCookMaterialContentKey(Allocator, material, &changed).Succeeded());
        EXPECT_NE(changed, original);
        material.Factors[0]       = 0.f;
        material.Textures[0].UUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000003").value();
        ASSERT_TRUE(ComputeCookMaterialContentKey(Allocator, material, &changed).Succeeded());
        EXPECT_NE(changed, original);
        const auto before = changed;
        material.UUID     = {};
        EXPECT_EQ(ComputeCookMaterialContentKey(Allocator, material, &changed).Error(), Core::VFS::VFSError::Corrupted);
        EXPECT_EQ(changed, before);
        EXPECT_EQ(ComputeCookMaterialContentKey(Allocator, material, nullptr).Error(), Core::VFS::VFSError::InvalidPath);
    }

    TEST_F(CookFingerprintTest, TypedEncodingMatchesIndependentLittleEndianAndRFCUUIDGolden)
    {
        // Reference: Ruby Digest::SHA256 over [length].pack("V"), raw text,
        // [u32].pack("V"), [u64].pack("Q<"), length-prefixed "abc", display-order
        // UUID bytes, and a 32-byte zero digest. No native struct packing.
        CookFingerprint fingerprint = {};
        const auto      identity    = uuids::uuid::from_string("00112233-4455-6677-8899-aabbccddeeff");
        ASSERT_TRUE(identity.has_value());
        ASSERT_TRUE(fingerprint.String("ze.cook.test.v1").Succeeded());
        ASSERT_TRUE(fingerprint.UInt32(0x78563412).Succeeded());
        ASSERT_TRUE(fingerprint.UInt64(0x0123456789abcdef).Succeeded());
        ASSERT_TRUE(fingerprint.String("abc").Succeeded());
        ASSERT_TRUE(fingerprint.UUID(*identity).Succeeded());
        ASSERT_TRUE(fingerprint.Digest({}).Succeeded());
        Digest256 output = {};
        ASSERT_TRUE(fingerprint.Finalize(&output).Succeeded());
        EXPECT_STREQ(output.Hex().data(), "693b18df88ec7f4ae4803caf8556b6841fcc51933b820d07eedf94c02664ef11");
    }

    TEST_F(CookFingerprintTest, StringLengthsPreventAmbiguousConcatenation)
    {
        CookFingerprint first = {}, second = {};
        ASSERT_TRUE(first.String("ab").Succeeded());
        ASSERT_TRUE(first.String("c").Succeeded());
        ASSERT_TRUE(second.String("a").Succeeded());
        ASSERT_TRUE(second.String("bc").Succeeded());
        Digest256 first_digest = {}, second_digest = {};
        ASSERT_TRUE(first.Finalize(&first_digest).Succeeded());
        ASSERT_TRUE(second.Finalize(&second_digest).Succeeded());
        EXPECT_NE(first_digest, second_digest);
    }

    TEST_F(CookFingerprintTest, InvalidStringsAreStickyAndNeverExposePartialDigest)
    {
        CookFingerprint fingerprint = {};
        EXPECT_EQ(fingerprint.String({nullptr, 1}).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(fingerprint.UInt32(1).Error(), Core::VFS::VFSError::InvalidPath);
        Digest256 output = {};
        EXPECT_EQ(fingerprint.Finalize(&output).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(output, Digest256{});
        fingerprint.Reset();
        const char byte = 'x';
        EXPECT_EQ(fingerprint.String({&byte, size_t(std::numeric_limits<uint32_t>::max()) + 1}).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(fingerprint.Finalize(&output).Error(), Core::VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(output, Digest256{});
        fingerprint.Reset();
        ASSERT_TRUE(fingerprint.String({}).Succeeded());
        ASSERT_TRUE(fingerprint.Finalize(&output).Succeeded());
        const auto before = output;
        EXPECT_EQ(fingerprint.Finalize(&output).Error(), Core::VFS::VFSError::InvalidPath);
        EXPECT_EQ(output, before);
    }
} // namespace ZEngine::Cook
