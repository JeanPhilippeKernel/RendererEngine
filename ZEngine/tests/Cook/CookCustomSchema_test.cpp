#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <limits>
#include "Process/SceneCookCustomSchema.h"

namespace ZEngine::Cook::Tests
{
    namespace
    {
        const auto                                          TextureUUID = *uuids::uuid::from_string("50000000-0000-0000-0000-000000000005");
        // Reviewed independently of the callback: UUID network-order bytes,
        // then .375f (0x3ec00000) in little-endian. No native component padding.
        constexpr Core::Containers::FixedArray<uint8_t, 20> Wire        = {
            {
             0x50, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0xc0, 0x3e,
             }
        };
    } // namespace

    class CookCustomSchemaTest : public testing::Test
    {
    protected:
        Core::Memory::ArenaAllocator                          Arena       = {};
        ECS::SceneComponentSchemaRegistry                     Schemas     = {};
        ECS::SceneDiagnostics                                 Diagnostics = {};
        const ECS::SceneComponentSchema*                      Schema      = nullptr;
        Core::Containers::FixedArray<ECS::SceneValueField, 2> Fields      = {
            {{"texture_uuid", TextureUUID}, {"weight", .375f}}
        };

        void SetUp() override
        {
            Arena.Initialize(ZMega(1), 0, "CookCustomSchemaTest");
            Schemas.Initialize(&Arena);
            ASSERT_TRUE(RegisterCookCustomSchema(Schemas, 1, &Diagnostics));
            Schema = Schemas.FindByKey(CookCustomSchemaKey);
            ASSERT_NE(Schema, nullptr);
        }

        ECS::SceneValue Value() const
        {
            return ECS::SceneValue({Fields.data(), Fields.size()});
        }
    };

    TEST_F(CookCustomSchemaTest, LiteralWireMatchesBothVersionsWithoutNativePaddingOrAllocation)
    {
        const auto before = Arena.m_current_offset;
        for (uint32_t version : {1u, 2u})
        {
            ECS::SceneComponentSchemaRegistry current = {};
            current.Initialize(&Arena);
            ASSERT_TRUE(RegisterCookCustomSchema(current, version, &Diagnostics));
            const auto&                               codec     = current.FindByKey(CookCustomSchemaKey)->Codecs;
            const auto                                allocated = Arena.m_current_offset;
            Core::Containers::FixedArray<uint8_t, 20> bytes     = {};
            ECS::SceneBinaryWriter                    writer(std::as_writable_bytes(std::span(bytes.data(), bytes.size())));
            ASSERT_TRUE(codec.EncodeBinary(nullptr, Value(), &writer, &Diagnostics));
            EXPECT_EQ(writer.Position(), Wire.size());
            EXPECT_EQ(bytes, Wire);
            auto measure = ECS::SceneBinaryWriter::Measure(Wire.size());
            ASSERT_TRUE(codec.EncodeBinary(nullptr, Value(), &measure, &Diagnostics));
            EXPECT_EQ(measure.Position(), Wire.size());
            Core::Containers::FixedArray<ECS::SceneValueField, 2> decoded = {};
            ECS::SceneValueWriter                                 output({decoded.data(), decoded.size()});
            ECS::SceneBinaryReader                                reader(std::as_bytes(std::span(Wire.data(), Wire.size())));
            ASSERT_TRUE(codec.DecodeBinary(nullptr, version, &reader, &output, &Diagnostics));
            EXPECT_EQ(reader.Remaining(), 0u);
            ASSERT_NE(output.Value().Find<uuids::uuid>("texture_uuid"), nullptr);
            ASSERT_NE(output.Value().Find<float>("weight"), nullptr);
            EXPECT_EQ(*output.Value().Find<uuids::uuid>("texture_uuid"), TextureUUID);
            EXPECT_EQ(*output.Value().Find<float>("weight"), .375f);
            EXPECT_EQ(Arena.m_current_offset, allocated);
        }
        EXPECT_GT(Arena.m_current_offset, before); // Only schema registration allocates.
    }

    TEST_F(CookCustomSchemaTest, BinaryDecodeRejectsEveryTruncationWrongVersionAndTrailingBytesTransactionally)
    {
        Core::Containers::FixedArray<uint8_t, 21> bytes = {};
        std::copy(Wire.begin(), Wire.end(), bytes.begin());
        for (size_t length = 0; length <= bytes.size(); ++length)
        {
            for (uint32_t version : {1u, 2u})
            {
                if (length == Wire.size() && version == 1)
                {
                    continue;
                }
                SCOPED_TRACE(length);
                SCOPED_TRACE(version);
                Core::Containers::FixedArray<ECS::SceneValueField, 2> decoded = {};
                ECS::SceneValueWriter                                 output({decoded.data(), decoded.size()});
                ECS::SceneBinaryReader                                reader(std::as_bytes(std::span(bytes.data(), length)));
                EXPECT_FALSE(Schema->Codecs.DecodeBinary(nullptr, version, &reader, &output, &Diagnostics));
                EXPECT_EQ(reader.Position(), 0u);
                EXPECT_TRUE(output.Empty());
            }
        }
    }

    TEST_F(CookCustomSchemaTest, InvalidAuthoredValuesAndShortOutputsDoNotPublishBytes)
    {
        for (uint32_t mode = 0; mode < 6; ++mode)
        {
            SCOPED_TRACE(mode);
            auto fields = Fields;
            if (mode == 0)
            {
                fields[0].Value = uuids::uuid{};
            }
            else if (mode == 1)
            {
                fields[1].Value = -.01f;
            }
            else if (mode == 2)
            {
                fields[1].Value = 1.01f;
            }
            else if (mode == 3)
            {
                fields[1].Value = std::numeric_limits<float>::quiet_NaN();
            }
            else if (mode == 4)
            {
                fields[1].Key = "runtime_cache";
            }
            Core::Containers::FixedArray<uint8_t, 20> bytes = {};
            std::fill(bytes.begin(), bytes.end(), 0xa5);
            const auto             before = bytes;
            ECS::SceneBinaryWriter writer(std::as_writable_bytes(std::span(bytes.data(), mode == 5 ? 19 : 20)));
            EXPECT_FALSE(Schema->Codecs.EncodeBinary(nullptr, ECS::SceneValue({fields.data(), fields.size()}), &writer, &Diagnostics));
            EXPECT_EQ(writer.Position(), 0u);
            EXPECT_EQ(bytes, before);
            if (mode == 5)
            {
                EXPECT_TRUE(writer.LimitExceeded());
            }
        }
    }

    TEST_F(CookCustomSchemaTest, BinarySemanticsAndInsufficientFieldStorageRejectWithoutConsumingInput)
    {
        for (uint32_t mode = 0; mode < 4; ++mode)
        {
            auto bytes = Wire;
            if (mode == 0)
            {
                std::fill(bytes.begin(), bytes.begin() + 16, 0);
            }
            else if (mode == 1 || mode == 2)
            {
                // -1.f / +infinity: both are forbidden semantic values.
                bytes[18] = 0x80;
                bytes[19] = mode == 1 ? 0xbf : 0x7f;
            }
            Core::Containers::FixedArray<ECS::SceneValueField, 2> decoded = {};
            ECS::SceneValueWriter                                 output({decoded.data(), mode == 3 ? 1u : 2u});
            ECS::SceneBinaryReader                                reader(std::as_bytes(std::span(bytes.data(), bytes.size())));
            EXPECT_FALSE(Schema->Codecs.DecodeBinary(nullptr, 1, &reader, &output, &Diagnostics));
            EXPECT_EQ(reader.Position(), 0u);
            EXPECT_TRUE(output.Empty());
        }
    }

    TEST_F(CookCustomSchemaTest, TypedReferencesAndUUIDRemappingPreserveAuthoredWeight)
    {
        ECS::SceneReference reference = {};
        ASSERT_TRUE(Schema->References.Enumerate(
            nullptr,
            Value(),
            [](void* context, const ECS::SceneReference& item) {
                *static_cast<ECS::SceneReference*>(context) = item;
                return true;
            },
            &reference,
            &Diagnostics));
        EXPECT_EQ(reference.Kind, ECS::SceneReferenceKind::Asset);
        EXPECT_EQ(reference.UUID, TextureUUID);
        EXPECT_STREQ(reference.FieldKey, "texture_uuid");
        EXPECT_STREQ(reference.AssetKindKey, "texture");
        EXPECT_FALSE(Schema->References.Enumerate(nullptr, Value(), [](void*, const ECS::SceneReference&) { return false; }, nullptr, &Diagnostics));
        const auto                                            replacement = *uuids::uuid::from_string("70000000-0000-0000-0000-000000000007");
        auto                                                  target      = replacement;
        ECS::SceneUUIDRemap                                   remap       = {.Context = &target, .Find = [](void* context, const uuids::uuid& source, uuids::uuid* output) {
                                         EXPECT_EQ(source, TextureUUID);
                                         *output = *static_cast<uuids::uuid*>(context);
                                         return true;
                                                                             }};
        Core::Containers::FixedArray<ECS::SceneValueField, 2> decoded     = {};
        ECS::SceneValueWriter                                 output({decoded.data(), decoded.size()});
        ASSERT_TRUE(Schema->References.Remap(nullptr, Value(), remap, &output, &Diagnostics));
        EXPECT_EQ(*output.Value().Find<uuids::uuid>("texture_uuid"), replacement);
        EXPECT_EQ(*output.Value().Find<float>("weight"), .375f);
        EXPECT_EQ(*Value().Find<uuids::uuid>("texture_uuid"), TextureUUID);
    }

    TEST_F(CookCustomSchemaTest, SourceMigrationOwnsItsCloneAndRejectsDerivedOrUnknownFields)
    {
        alignas(std::max_align_t) Core::Containers::FixedArray<uint8_t, ZKilo(16)> source_bytes = {}, migrated_bytes = {};
        ECS::SceneYAML::Storage                                                    source_storage = {}, migrated_storage = {};
        ASSERT_TRUE(source_storage.InitializeBorrowed(source_bytes.data(), source_bytes.size()));
        ASSERT_TRUE(migrated_storage.InitializeBorrowed(migrated_bytes.data(), migrated_bytes.size()));
        auto source = source_storage.Destination();
        ASSERT_TRUE(Schema->Codecs.EncodeYAML(nullptr, Value(), &source, &Diagnostics));
        auto migrated = migrated_storage.Destination();
        ASSERT_TRUE(MigrateCookCustomSchema(nullptr, source, &migrated, &Diagnostics));
        EXPECT_EQ(migrated.Owner(), &migrated_storage);
        ASSERT_TRUE(source_storage.Set(source, "weight", source_storage.Float(.75f)));
        Core::Containers::FixedArray<ECS::SceneValueField, 2> decoded = {};
        ECS::SceneValueWriter                                 output({decoded.data(), decoded.size()});
        ASSERT_TRUE(Schema->Codecs.DecodeYAML(nullptr, 1, migrated, &output, &Diagnostics));
        EXPECT_EQ(*output.Value().Find<float>("weight"), .375f);
        for (cstring field : {"runtime_cache", "unknown_field"})
        {
            auto invalid = source_storage.Clone(source);
            ASSERT_TRUE(source_storage.Set(invalid, field, source_storage.UInt(99)));
            output.Reset();
            EXPECT_FALSE(Schema->Codecs.DecodeYAML(nullptr, 1, invalid, &output, &Diagnostics));
            EXPECT_TRUE(output.Empty());
        }
        output.Reset();
        EXPECT_FALSE(Schema->Codecs.DecodeYAML(nullptr, 2, migrated, &output, &Diagnostics));
        EXPECT_TRUE(output.Empty());
    }

    TEST_F(CookCustomSchemaTest, CandidatePopulationResetsRuntimeCacheAndCaptureOmitsIt)
    {
        Core::Memory::MemoryManager memory = {};
        ECS::Scene                  scene  = {};
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        const auto source    = scene.CreateEntity();
        const auto candidate = scene.CreateEntity();
        scene.AddComponent<CookCustomComponent>(source, {.Texture = TextureUUID, .Weight = .375f, .RuntimeCache = 12345});
        Core::Containers::FixedArray<ECS::SceneValueField, 2> captured = {};
        ECS::SceneValueWriter                                 output({captured.data(), captured.size()});
        ASSERT_TRUE(Schema->Codecs.Capture(nullptr, source, scene, &output, &Diagnostics));
        EXPECT_EQ(output.Value().Fields().size(), 2u);
        EXPECT_EQ(output.Value().Find<uint32_t>("runtime_cache"), nullptr);
        ASSERT_TRUE(Schema->Codecs.PopulateCandidate(nullptr, candidate, scene, output.Value(), &Diagnostics));
        const auto* component = scene.GetComponent<CookCustomComponent>(candidate);
        ASSERT_NE(component, nullptr);
        EXPECT_EQ(component->Texture, TextureUUID);
        EXPECT_EQ(component->Weight, .375f);
        EXPECT_EQ(component->RuntimeCache, UINT32_MAX);
        EXPECT_EQ(Schema->Fields[2].Class, ECS::SceneFieldClass::RuntimeDerived);
        scene.Shutdown();
    }
} // namespace ZEngine::Cook::Tests
