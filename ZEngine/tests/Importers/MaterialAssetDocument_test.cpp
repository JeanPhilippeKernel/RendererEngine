#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <bit>
#include <limits>
#include <string>
#include <string_view>

namespace ZEngine::Importers::AssetCodec
{
    namespace VFS = Core::VFS;

    namespace
    {
        constexpr cstring MaterialFixture =
            R"({"material_version":1,"uuid":"00000000-0000-0000-0000-000000000001","name":"Material","textures":{"albedo":{"uuid":"00000000-0000-0000-0000-000000000002","path":"/Assets/tex.png"},"emissive":{"uuid":"00000000-0000-0000-0000-000000000000","path":""},"normal":{"uuid":"00000000-0000-0000-0000-000000000000","path":""},"opacity":{"uuid":"00000000-0000-0000-0000-000000000000","path":""},"specular":{"uuid":"00000000-0000-0000-0000-000000000000","path":""}},"ambient_color":[0,0,0,0],"albedo_color":[1,1,1,1],"emissive_color":[0,0,0,1],"roughness_color":[1,0.5,0,0],"specular_color":[0,0,0,0],"factors":[0,0,0.5,0],"alpha_mode":"BLEND","alpha_cutoff":0.5})";

        Core::Containers::ArrayView<const uint8_t> MaterialBytes(std::string_view text)
        {
            return {reinterpret_cast<const uint8_t*>(text.data()), text.size()};
        }

        // Heap-owned malformed fixture generation is test-only; the production
        // reader uses no allocator, string construction, DOM, or VFS access.
        std::string ChangedMaterial(std::string_view before, std::string_view after)
        {
            std::string text   = MaterialFixture;
            const auto  offset = text.find(before);
            EXPECT_NE(offset, std::string::npos);
            if (offset != std::string::npos)
                text.replace(offset, before.size(), after);
            return text;
        }

        void ExpectBadMaterial(std::string_view text, VFS::VFSError error = VFS::VFSError::Corrupted)
        {
            MaterialAssetDocument output = {};
            output.Factors[0]            = 123.f;
            output.HasSource             = true;
            EXPECT_EQ(ReadMaterialAssetDocument(MaterialBytes(text), &output).Error(), error) << text;
            EXPECT_EQ(output.Factors[0], 123.f);
            EXPECT_TRUE(output.HasSource);
        }

        struct MaterialSink
        {
            char                        Bytes[16384] = {};
            size_t                      Count        = 0;
            uint32_t                    Calls        = 0;
            uint32_t                    FailAt       = UINT32_MAX;

            static VFS::VFSResult<void> Write(void* context, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                auto& self = *static_cast<MaterialSink*>(context);
                if (++self.Calls == self.FailAt)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied);
                if (bytes.size() > sizeof(self.Bytes) - self.Count)
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                if (bytes.size())
                    Helpers::secure_memcpy(self.Bytes + self.Count, sizeof(self.Bytes) - self.Count, bytes.data(), bytes.size());
                self.Count += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }

            Core::Containers::StringView Text() const
            {
                return {Bytes, Count};
            }
        };

        Core::Containers::FixedArray<VFS::VFSRelativePath, 5> MaterialObjectPaths()
        {
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            paths[0]                                                    = VFS::VFSRelativePath::Parse("objects/1111111111111111111111111111111111111111111111111111111111111111.png").Value();
            return paths;
        }
    } // namespace

    TEST(MaterialAssetDocumentTest, CallerOwnedReaderRetainsOneDocumentAndResetsAfterFailuresAndOptionalFields)
    {
        MaterialAssetReader reader      = {};
        auto                with_source = std::string(MaterialFixture);
        with_source.pop_back();
        with_source += R"(,"source_material":{"path":"source.glb","importer":"GltfImporter","index":7}})";
        for (uint32_t repetition = 0; repetition < 64; ++repetition)
        {
            auto       input = with_source;
            const auto first = reader.Read(MaterialBytes(input));
            ASSERT_TRUE(first.Succeeded());
            const auto address = reinterpret_cast<uintptr_t>(first.Value());
            const auto state   = reinterpret_cast<uintptr_t>(&reader);
            EXPECT_GE(address, state);
            EXPECT_LE(address - state + sizeof(MaterialAssetDocument), sizeof(reader));
            EXPECT_TRUE(first.Value()->HasSource);
            EXPECT_EQ(first.Value()->Source.Index, 7u);
            input.assign(input.size(), 'x');
            EXPECT_STREQ(first.Value()->Source.Path, "source.glb");
            EXPECT_STREQ(first.Value()->Name, "Material");
            EXPECT_EQ(reader.Read(MaterialBytes("{\"uuid\":")).Error(), VFS::VFSError::Corrupted);
            const auto parsed = reader.Read(MaterialBytes(MaterialFixture));
            ASSERT_TRUE(parsed.Succeeded());
            EXPECT_EQ(parsed.Value(), first.Value());
            EXPECT_FALSE(parsed.Value()->HasSource);
            EXPECT_EQ(parsed.Value()->Source.Index, 0u);
            EXPECT_STREQ(parsed.Value()->Source.Path, "");
            EXPECT_STREQ(parsed.Value()->Source.Importer, "");
            EXPECT_STREQ(parsed.Value()->Textures[0].Path, "/Assets/tex.png");
        }
        EXPECT_LT(sizeof(MaterialAssetReader), 2 * sizeof(MaterialAssetDocument));
    }

    TEST(MaterialAssetDocumentTest, ReusedReaderSnapshotsBorrowedExpectedIdentityAndRejectsItsOwnStorageAsInput)
    {
        MaterialAssetReader reader = {};
        const auto          first  = reader.Read(MaterialBytes(MaterialFixture));
        ASSERT_TRUE(first.Succeeded());
        const auto changed = ChangedMaterial("00000000-0000-0000-0000-000000000001", "00000000-0000-0000-0000-000000000003");
        EXPECT_EQ(reader.Read(MaterialBytes(changed), first.Value()->UUID).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(reader.Read({reinterpret_cast<const uint8_t*>(&reader), 1}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(reader.Read({nullptr, 1}).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(reader.Read(MaterialBytes(MaterialFixture), {}, 1).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_TRUE(reader.Read(MaterialBytes(MaterialFixture)).Succeeded());
    }

    TEST(MaterialAssetDocumentTest, ReadsCompleteCurrentMaterialIntoOwnedInlineValues)
    {
        MaterialAssetDocument output   = {};
        const auto            expected = uuids::uuid::from_string("00000000-0000-0000-0000-000000000001").value();
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &output, expected).Succeeded());
        EXPECT_EQ(output.UUID, expected);
        EXPECT_STREQ(output.Name, "Material");
        EXPECT_EQ(output.AlphaMode, Rendering::Materials::AlphaMode::Blend);
        EXPECT_EQ(output.Factors[2], .5f);
        EXPECT_EQ(output.RoughnessColor[1], .5f);
        EXPECT_EQ(output.AlbedoColor[3], 1.f);
        EXPECT_FALSE(output.HasSource);
        EXPECT_STREQ(output.Textures[0].Path, "/Assets/tex.png");
        EXPECT_EQ(output.Textures[0].UUID, uuids::uuid::from_string("00000000-0000-0000-0000-000000000002").value());
        for (size_t i = 1; i < output.Textures.size(); ++i)
        {
            EXPECT_TRUE(output.Textures[i].UUID.is_nil());
            EXPECT_STREQ(output.Textures[i].Path, "");
        }
        constexpr cstring keys[] = {"albedo", "emissive", "normal", "opacity", "specular"};
        for (uint32_t i = 0; i < static_cast<uint32_t>(MaterialTextureSlot::Count); ++i)
            EXPECT_STREQ(MaterialTextureSlotKey(static_cast<MaterialTextureSlot>(i)), keys[i]);
        EXPECT_EQ(MaterialTextureSlotKey(MaterialTextureSlot::Count), nullptr);
        EXPECT_EQ(MaterialTextureSlotKey(static_cast<MaterialTextureSlot>(255)), nullptr);
    }

    TEST(MaterialAssetDocumentTest, DoesNotRetainBorrowedSourceOrTextureStrings)
    {
        MaterialAssetDocument output = {};
        std::string           bytes  = MaterialFixture;
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(bytes), &output).Succeeded());
        bytes.assign(bytes.size(), 'x');
        EXPECT_STREQ(output.Name, "Material");
        EXPECT_STREQ(output.Textures[0].Path, "/Assets/tex.png");
    }

    TEST(MaterialAssetDocumentTest, MissingExtraDuplicateAndWrongTypedFieldsFailWithoutChangingOutput)
    {
        for (const auto& text : {
             ChangedMaterial("\"name\":\"Material\",", ""),
             ChangedMaterial("\"name\":\"Material\"", "\"name\":\"Material\",\"name\":\"other\""),
             ChangedMaterial("\"name\":\"Material\"", R"("name":"Material","\u006eame":"other")"),
             ChangedMaterial("\"name\":\"Material\"", "\"unknown\":1,\"name\":\"Material\""),
             ChangedMaterial("\"name\":\"Material\"", "\"name\":null"),
             ChangedMaterial("\"alpha_cutoff\":0.5", "\"alpha_cutoff\":true"),
             ChangedMaterial("\"material_version\":1", "\"material_version\":1.0"),
             ChangedMaterial("\"material_version\":1", "\"material_version\":true"),
             ChangedMaterial("\"material_version\":1,", ""),
             ChangedMaterial("\"alpha_mode\":\"BLEND\",", ""),
             ChangedMaterial("\"alpha_cutoff\":0.5", "\"alpha_cutoff\":0.5,\"alpha_cutoff\":0.5"),
             })
            ExpectBadMaterial(text);
        ExpectBadMaterial(std::string(MaterialFixture) + "{}");
        ExpectBadMaterial(std::string(MaterialFixture) + "x");
        ExpectBadMaterial(ChangedMaterial("\"material_version\":1", "\"material_version\":2"), VFS::VFSError::Unsupported);
        ExpectBadMaterial(ChangedMaterial("\"material_version\":1", "\"material_version\":0"), VFS::VFSError::Unsupported);
    }

    TEST(MaterialAssetDocumentTest, UUIDsRequireCanonicalSpellingAndExpectedIdentityAgreement)
    {
        for (cstring invalid : {"00000000-0000-0000-0000-000000000000", "AAAAAAAA-0000-0000-0000-000000000001", "00000000000000000000000000000001", "00000000-0000-0000-0000-00000000000g"})
            ExpectBadMaterial(ChangedMaterial("00000000-0000-0000-0000-000000000001", invalid));
        MaterialAssetDocument output = {};
        const auto            wrong  = uuids::uuid::from_string("00000000-0000-0000-0000-000000000099").value();
        output.Factors[0]            = 123.f;
        EXPECT_EQ(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &output, wrong).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(output.Factors[0], 123.f);
    }

    TEST(MaterialAssetDocumentTest, TextureSchemaIsClosedAndAllFiveSlotsAreRequired)
    {
        for (const auto& text : {
             ChangedMaterial("\"albedo\":{\"uuid\"", "\"other\":{\"uuid\""),
             ChangedMaterial("\"path\":\"/Assets/tex.png\"", "\"path\":\"/Assets/tex.png\",\"unknown\":0"),
             ChangedMaterial("\"path\":\"/Assets/tex.png\"", "\"path\":\"/Assets/tex.png\",\"path\":\"different\""),
             ChangedMaterial("\"path\":\"/Assets/tex.png\"", "\"path\":12"),
             ChangedMaterial("\"path\":\"/Assets/tex.png\"", "\"path\":\"\""),
             ChangedMaterial("\"uuid\":\"00000000-0000-0000-0000-000000000002\",", ""),
             ChangedMaterial("00000000-0000-0000-0000-000000000002", "00000000-0000-0000-0000-000000000000"),
             ChangedMaterial(",\"specular\":{\"uuid\":\"00000000-0000-0000-0000-000000000000\",\"path\":\"\"}", ""),
             ChangedMaterial("\"textures\":{", "\"textures\":{\"albedo\":{\"uuid\":\"00000000-0000-0000-0000-000000000000\",\"path\":\"\"},"),
             })
            ExpectBadMaterial(text);
    }

    TEST(MaterialAssetDocumentTest, TexturePathsAreValidatedBeforeNormalizationAndPreserveUnicodeSpelling)
    {
        for (cstring path : {"/Assets/../tex.png", "/Assets//tex.png", "/Assets/./tex.png", "/Assets/tex.png/", "Assets/../tex.png", "C:\\tex.png", "/", "assets/tex\\name.png", "assets/tex\nname.png"})
            ExpectBadMaterial(ChangedMaterial("/Assets/tex.png", path));
        for (cstring path : {"assets/T\u00e9x.png", "/Assets/T\u00e9x.png", "objects/digest.png"})
        {
            const auto            text   = ChangedMaterial("/Assets/tex.png", path);
            MaterialAssetDocument output = {};
            EXPECT_TRUE(ReadMaterialAssetDocument(MaterialBytes(text), &output).Succeeded());
        }
    }

    TEST(MaterialAssetDocumentTest, NumericArraysRequireExactlyFourCheckedFiniteBinary32Values)
    {
        for (cstring values : {"[]", "[0,0,0]", "[0,0,0,0,0]", "[0,0,0,true]", "[0,0,0,null]", "[0,0,0,\"1\"]", "[0,0,0,1e999]", "[0,0,0,NaN]", "[0,0,0,3.402824e38]"})
            ExpectBadMaterial(ChangedMaterial("[0,0,0,0]", values));
        auto                  text   = ChangedMaterial("[0,0,0,0]", "[-0,1.40129846e-45,-3.402823466e38,1.000000059604644775390625]");
        MaterialAssetDocument output = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(text), &output).Succeeded());
        EXPECT_EQ(std::bit_cast<uint32_t>(output.AmbientColor[0]), 0x80000000u);
        EXPECT_EQ(std::bit_cast<uint32_t>(output.AmbientColor[1]), 1u);
        EXPECT_EQ(std::bit_cast<uint32_t>(output.AmbientColor[2]), 0xff7fffffu);
        EXPECT_EQ(std::bit_cast<uint32_t>(output.AmbientColor[3]), 0x3f800000u);
    }

    TEST(MaterialAssetDocumentTest, AlphaModeIsExplicitAndCutoffAgreesAfterCheckedConversion)
    {
        for (cstring mode : {"OPAQUE", "MASK", "BLEND", "LEGACY"})
        {
            const auto            text   = ChangedMaterial("BLEND", mode);
            MaterialAssetDocument output = {};
            ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(text), &output).Succeeded());
            EXPECT_STREQ(Rendering::Materials::AlphaModeName(output.AlphaMode), mode);
        }
        for (cstring mode : {"blend", "UNKNOWN", ""})
            ExpectBadMaterial(ChangedMaterial("BLEND", mode));
        for (cstring cutoff : {"-0.5", "1", "1e999"})
            ExpectBadMaterial(ChangedMaterial("\"alpha_cutoff\":0.5", std::string("\"alpha_cutoff\":") + cutoff));
        const auto            text   = ChangedMaterial("\"alpha_cutoff\":0.5", "\"alpha_cutoff\":0.5000000000001");
        MaterialAssetDocument output = {};
        EXPECT_TRUE(ReadMaterialAssetDocument(MaterialBytes(text), &output).Succeeded());
    }

    TEST(MaterialAssetDocumentTest, ProvenanceIsBoundedOwnedDataAndNeverRequiresANativeSource)
    {
        const auto            text   = ChangedMaterial("\"name\":\"Material\"", R"("name":"Material","source_material":{"path":"C:\\missing\\source.glb","importer":"GltfImporter","index":4294967295})");
        MaterialAssetDocument output = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(text), &output).Succeeded());
        EXPECT_TRUE(output.HasSource);
        EXPECT_STREQ(output.Source.Path, "C:\\missing\\source.glb");
        EXPECT_STREQ(output.Source.Importer, "GltfImporter");
        EXPECT_EQ(output.Source.Index, UINT32_MAX);
        for (cstring source : {R"({"path":"a","importer":"I"})", R"({"path":"a","importer":"","index":0})", R"({"path":"a","importer":"I","index":4294967296})", R"({"path":"a","importer":"I","index":0.0})", R"({"path":"a","importer":"I","index":0,"other":0})", R"({"path":"a","path":"b","importer":"I","index":0})"})
            ExpectBadMaterial(ChangedMaterial("\"name\":\"Material\"", std::string("\"name\":\"Material\",\"source_material\":") + source));
    }

    TEST(MaterialAssetDocumentTest, UTF8AndDecodedFieldByteBoundsAreStrictAndNeverTruncate)
    {
        const auto            valid  = ChangedMaterial("Material\"", std::string(4096, 'a') + '"');
        MaterialAssetDocument output = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(valid), &output).Succeeded());
        EXPECT_EQ(Helpers::secure_strlen(output.Name), 4096u);
        ExpectBadMaterial(ChangedMaterial("Material\"", std::string(4097, 'a') + '"'), VFS::VFSError::SizeLimitExceeded);
        ExpectBadMaterial(ChangedMaterial("/Assets/tex.png", std::string(256, 'a')), VFS::VFSError::SizeLimitExceeded);
        ExpectBadMaterial(ChangedMaterial("Material\"", "bad\\u0000name\""));
        ExpectBadMaterial(ChangedMaterial("Material\"", std::string("\xc0\x80") + '"'));
        const auto unicode = ChangedMaterial("Material\"", R"(M\u00e9tal\uD834\uDD1E")");
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(unicode), &output).Succeeded());
        EXPECT_STREQ(output.Name, "M\xc3\xa9tal\xf0\x9d\x84\x9e");
    }

    TEST(MaterialAssetDocumentTest, InvalidArgumentsAndConfiguredFileLimitPreserveCallerState)
    {
        MaterialAssetDocument output = {};
        output.Factors[0]            = 123.f;
        EXPECT_EQ(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), nullptr).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ReadMaterialAssetDocument({nullptr, 1}, &output).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &output, {}, 1).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(output.Factors[0], 123.f);
    }

    TEST(MaterialAssetDocumentTest, CanonicalEmissionMatchesExactGoldenAndRewritesOnlyResolvedTexturePaths)
    {
        MaterialAssetDocument document = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &document).Succeeded());
        const auto   paths = MaterialObjectPaths();
        MaterialSink sink  = {};
        ASSERT_TRUE(WriteCanonicalMaterialAsset(document, {paths.data(), paths.size()}, MaterialSink::Write, &sink).Succeeded());
        constexpr cstring golden =
            R"({"albedo_color":[1.00000000e0,1.00000000e0,1.00000000e0,1.00000000e0],"alpha_cutoff":5.00000000e-1,"alpha_mode":"BLEND","ambient_color":[0,0,0,0],"emissive_color":[0,0,0,1.00000000e0],"factors":[0,0,5.00000000e-1,0],"material_version":1,"name":"Material","roughness_color":[1.00000000e0,5.00000000e-1,0,0],"specular_color":[0,0,0,0],"textures":{"albedo":{"path":"objects/1111111111111111111111111111111111111111111111111111111111111111.png","uuid":"00000000-0000-0000-0000-000000000002"},"emissive":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"normal":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"opacity":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"},"specular":{"path":"","uuid":"00000000-0000-0000-0000-000000000000"}},"uuid":"00000000-0000-0000-0000-000000000001"})"
            "\n";
        EXPECT_TRUE(sink.Text() == golden) << sink.Bytes;
        EXPECT_STREQ(document.Textures[0].Path, "/Assets/tex.png");
        MaterialAssetDocument decoded = {};
        ASSERT_TRUE(ReadMaterialAssetDocument({reinterpret_cast<const uint8_t*>(sink.Bytes), sink.Count}, &decoded, document.UUID).Succeeded());
        EXPECT_EQ(Core::Containers::StringView(decoded.Textures[0].Path), paths[0].View());
        EXPECT_FALSE(decoded.HasSource);
        EXPECT_EQ(decoded.Factors, document.Factors);
    }

    TEST(MaterialAssetDocumentTest, SemanticProjectionIgnoresSourceProvenanceAndEveryTextureLocationButRetainsUUIDs)
    {
        MaterialAssetDocument document = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &document).Succeeded());
        MaterialSink original = {}, moved = {}, changed = {};
        ASSERT_TRUE(WriteCanonicalMaterialSemanticProjection(document, MaterialSink::Write, &original).Succeeded());
        ASSERT_EQ(Helpers::secure_strcpy(document.Textures[0].Path, sizeof(document.Textures[0].Path), "/Other/texture.png"), Helpers::MEMORY_OP_SUCCESS);
        document.HasSource    = true;
        document.Source.Index = 99;
        ASSERT_EQ(Helpers::secure_strcpy(document.Source.Path, sizeof(document.Source.Path), "/missing/native-source.glb"), Helpers::MEMORY_OP_SUCCESS);
        ASSERT_TRUE(WriteCanonicalMaterialSemanticProjection(document, MaterialSink::Write, &moved).Succeeded());
        EXPECT_TRUE(original.Text() == moved.Text());
        EXPECT_EQ(std::string_view(original.Bytes, original.Count).find("/Other/"), std::string_view::npos);
        EXPECT_EQ(std::string_view(original.Bytes, original.Count).find("source_material"), std::string_view::npos);
        document.Textures[0].UUID = uuids::uuid::from_string("00000000-0000-0000-0000-000000000003").value();
        ASSERT_TRUE(WriteCanonicalMaterialSemanticProjection(document, MaterialSink::Write, &changed).Succeeded());
        EXPECT_FALSE(original.Text() == changed.Text());
        // The projection intentionally has a nonnil UUID with an empty path;
        // it is for hashing, never a loadable material or packaged object.
        MaterialAssetDocument not_loadable = {};
        EXPECT_EQ(ReadMaterialAssetDocument({reinterpret_cast<const uint8_t*>(original.Bytes), original.Count}, &not_loadable).Error(), VFS::VFSError::Corrupted);
    }

    TEST(MaterialAssetDocumentTest, RuntimeEmissionRequiresOneResolvedPathForEachNonnilSlotAndNoExtras)
    {
        MaterialAssetDocument document = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &document).Succeeded());
        auto         paths = MaterialObjectPaths();
        MaterialSink sink  = {};
        EXPECT_EQ(WriteCanonicalMaterialAsset(document, {}, MaterialSink::Write, &sink).Error(), VFS::VFSError::InvalidPath);
        EXPECT_EQ(WriteCanonicalMaterialAsset(document, {paths.data(), 4}, MaterialSink::Write, &sink).Error(), VFS::VFSError::InvalidPath);
        paths[1] = paths[0];
        EXPECT_EQ(WriteCanonicalMaterialAsset(document, {paths.data(), paths.size()}, MaterialSink::Write, &sink).Error(), VFS::VFSError::Corrupted);
        paths[1] = {};
        paths[0] = {};
        EXPECT_EQ(WriteCanonicalMaterialAsset(document, {paths.data(), paths.size()}, MaterialSink::Write, &sink).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(sink.Calls, 0u);
    }

    TEST(MaterialAssetDocumentTest, EmissionRejectsForgedNonfiniteOrUnterminatedDocumentsBeforeTheFirstWrite)
    {
        MaterialAssetDocument document = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &document).Succeeded());
        for (uint32_t invalid = 0; invalid < 7; ++invalid)
        {
            auto         candidate = document;
            MaterialSink sink      = {};
            if (invalid == 0)
                candidate.UUID = {};
            if (invalid == 1)
                candidate.AlphaMode = static_cast<Rendering::Materials::AlphaMode>(255);
            if (invalid == 2)
                candidate.Factors[2] = -1.f;
            if (invalid == 3)
                candidate.AmbientColor[0] = std::numeric_limits<float>::quiet_NaN();
            if (invalid == 4)
                Helpers::secure_memset(candidate.Name, 'x', sizeof(candidate.Name), sizeof(candidate.Name));
            if (invalid == 5)
                Helpers::secure_memset(candidate.Textures[0].Path, 'x', sizeof(candidate.Textures[0].Path), sizeof(candidate.Textures[0].Path));
            if (invalid == 6)
                candidate.Textures[0].UUID = {};
            EXPECT_EQ(WriteCanonicalMaterialSemanticProjection(candidate, MaterialSink::Write, &sink).Error(), VFS::VFSError::Corrupted);
            EXPECT_EQ(sink.Calls, 0u);
        }
    }

    TEST(MaterialAssetDocumentTest, EveryCanonicalEmissionWriteFailureAbortsWithTheUnderlyingError)
    {
        MaterialAssetDocument document = {};
        ASSERT_TRUE(ReadMaterialAssetDocument(MaterialBytes(MaterialFixture), &document).Succeeded());
        const auto   paths      = MaterialObjectPaths();
        MaterialSink successful = {};
        ASSERT_TRUE(WriteCanonicalMaterialAsset(document, {paths.data(), paths.size()}, MaterialSink::Write, &successful).Succeeded());
        for (uint32_t fail_at = 1; fail_at <= successful.Calls; ++fail_at)
        {
            MaterialSink failing = {};
            failing.FailAt       = fail_at;
            EXPECT_EQ(WriteCanonicalMaterialAsset(document, {paths.data(), paths.size()}, MaterialSink::Write, &failing).Error(), VFS::VFSError::PermissionDenied);
            EXPECT_EQ(failing.Calls, fail_at);
        }
    }
} // namespace ZEngine::Importers::AssetCodec
