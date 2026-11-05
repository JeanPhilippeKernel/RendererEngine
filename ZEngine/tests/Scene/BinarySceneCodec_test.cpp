#include <ZEngine/Core/Memory/AllocationRoute.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneSettingsBinary.h>
#include <ZEngine/ECS/SceneValue.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <vector>

namespace
{
    namespace ECS         = ZEngine::ECS;
    namespace Components  = ZEngine::ECS::Components;
    namespace Format      = ZEngine::ECS::SceneBinaryFormat;
    const auto SceneUUID  = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto ParentUUID = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    const auto ChildUUID  = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");

    uint32_t   Word(std::span<const uint8_t> bytes, size_t offset)
    {
        EXPECT_LE(offset + 4, bytes.size());
        if (offset > bytes.size() || bytes.size() - offset < 4)
        {
            return 0;
        }
        ECS::SceneBinaryReader reader(std::as_bytes(bytes.subspan(offset, 4)));
        uint32_t               value = 0;
        EXPECT_TRUE(reader.UInt32(value));
        return value;
    }
    void SetWord(std::span<uint8_t> bytes, size_t offset, uint32_t value)
    {
        ASSERT_LE(offset + 4, bytes.size());
        ECS::SceneBinaryWriter writer(std::as_writable_bytes(bytes.subspan(offset, 4)));
        ASSERT_TRUE(writer.UInt32(value));
    }
    void FixChecksum(std::span<uint8_t> bytes)
    {
        SetWord(bytes, Format::ChecksumOffset, Format::Checksum(bytes));
    }

    struct RefusingRoute final : ZEngine::Core::Memory::IAllocationRoute
    {
        size_t Calls = 0;

        void*  Allocate(size_t) noexcept override
        {
            ++Calls;
            return nullptr;
        }
        void* Reallocate(void*, size_t, size_t) noexcept override
        {
            ++Calls;
            return nullptr;
        }
        void Free(void*) noexcept override
        {
            ++Calls;
        }
    };

    class BinarySceneCodecTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager      Memory      = {};
        ZEngine::Core::Memory::ArenaAllocator     Output      = {};
        ECS::Scene                                Source      = {};
        ECS::Scene                                Candidate   = {};
        ECS::SceneComponentSchemaRegistry         Schemas     = {};
        ECS::SceneDiagnostics                     Diagnostics = {};
        ECS::SceneDocument                        Document    = {};
        ECS::SceneDocumentSettings                Settings    = {.UUID = SceneUUID, .Name = "Binary Scene"};
        ZEngine::Core::Containers::Array<uint8_t> Bytes       = {};

        void                                      SetUp() override
        {
            Memory.Initialize(ZMega(128), {});
            Output.Initialize(ZMega(8), 0, "BinarySceneTest/Output");
            Source.Initialize(&Memory.MainArena);
            Candidate.Initialize(&Memory.MainArena);
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
            const auto child  = Source.CreateEntity();
            const auto parent = Source.CreateEntity();
            Source.AddComponent<Components::UUIDComponent>(child, {ChildUUID});
            Source.AddComponent<Components::UUIDComponent>(parent, {ParentUUID});
            Source.AddComponent<Components::TransformComponent>(
                child,
                {
                .Position = {2.f, 3.f, 4.f}
            });
            Source.AddComponent<Components::TransformComponent>(
                parent,
                {
                .Position = {5.f, 0.f, 0.f}
            });
            Source.AddComponent<Components::ParentComponent>(child, {parent});
            Components::NameComponent name = {};
            ASSERT_EQ(ZEngine::Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Child", 5), ZEngine::Helpers::MEMORY_OP_SUCCESS);
            Source.AddComponent<Components::NameComponent>(child, name);
            Settings.Sky.SkyLuminanceFactor = 2.5f;
            Settings.Grid.BaseSpacing       = .125f;
            ASSERT_TRUE(ECS::SceneDocument::Capture(Source, Settings, Schemas, &Document, &Diagnostics));
            ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Output, &Bytes, &Diagnostics));
        }
        void TearDown() override
        {
            Candidate.Shutdown();
            Source.Shutdown();
        }
        std::vector<uint8_t> Copy()
        {
            return {Bytes.begin(), Bytes.end()};
        }
        size_t Entities()
        {
            return Format::HeaderBytes + Word({Bytes.data(), Bytes.size()}, 48) + ECS::SceneGridBinaryBytes + ECS::SceneSkyBinaryBytes;
        }
        size_t ComponentsOffset()
        {
            return Entities() + Word({Bytes.data(), Bytes.size()}, 52) * Format::EntityBytes;
        }
        size_t Entry(std::string_view key, size_t occurrence = 0)
        {
            const auto bytes = std::span(Bytes.data(), Bytes.size());
            for (uint32_t i = 0; i < Word(bytes, 56); ++i)
            {
                const size_t           row = ComponentsOffset() + i * Format::ComponentBytes;
                const std::string_view actual(reinterpret_cast<const char*>(bytes.data() + Word(bytes, row)), Word(bytes, row + 4));
                if (actual == key && occurrence-- == 0)
                {
                    return row;
                }
            }
            ADD_FAILURE() << "Missing fixture component " << key;
            return 0;
        }
        bool AlternateSchemas(ECS::SceneComponentSchemaRegistry* out, const ECS::SceneComponentCodecFns& name_codecs, uint32_t name_version = 1)
        {
            out->Initialize(&Memory.MainArena);
            for (const auto* key : {"NameComponent", "ParentComponent", "TransformComponent"})
            {
                const auto* schema = Schemas.FindByKey(key);
                if (!schema || !out->Register({.Key = schema->Key, .Version = std::string_view(key) == "NameComponent" ? name_version : schema->Version, .RuntimeType = schema->RuntimeType, .Fields = schema->Fields, .FieldCount = schema->FieldCount, .Codecs = std::string_view(key) == "NameComponent" ? name_codecs : schema->Codecs, .References = schema->References}, &Diagnostics))
                {
                    return false;
                }
            }
            return true;
        }
        void Rejects(std::span<const uint8_t> bytes, const ECS::SceneDocumentLimits& limits = {}, const ECS::SceneComponentSchemaRegistry* schemas = nullptr, ZEngine::Core::Memory::IAllocationRoute* route = nullptr)
        {
            std::string before = {}, after = {};
            ASSERT_TRUE(Document.Emit(&before, &Diagnostics));
            Diagnostics.Clear();
            EXPECT_FALSE(ECS::BinarySceneCodec::Decode(bytes, schemas ? *schemas : Schemas, &Document, &Diagnostics, limits, route));
            EXPECT_TRUE(Diagnostics.HasErrors());
            ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
            EXPECT_EQ(before, after);
            EXPECT_EQ(Candidate.AliveCount(), 0u);
        }
    };

    TEST_F(BinarySceneCodecTest, PortableHeaderAndOwnedRoundTripReconstructUUIDHierarchy)
    {
        EXPECT_EQ(std::string_view(reinterpret_cast<const char*>(Bytes.data()), 4), "ZESC");
        EXPECT_EQ(Word({Bytes.data(), Bytes.size()}, 4), 1u);
        EXPECT_EQ(Word({Bytes.data(), Bytes.size()}, 12), 0x01020304u);
        EXPECT_EQ(Word({Bytes.data(), Bytes.size()}, 24), Bytes.size());
        EXPECT_EQ(Word({Bytes.data(), Bytes.size()}, 28), Format::Checksum({Bytes.data(), Bytes.size()}));
        std::string original = {}, decoded = {};
        ASSERT_TRUE(Document.Emit(&original, &Diagnostics));
        auto               input  = Copy();
        ECS::SceneDocument loaded = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Decode(input, Schemas, &loaded, &Diagnostics));
        std::fill(input.begin(), input.end(), 0xff);
        ASSERT_TRUE(loaded.Emit(&decoded, &Diagnostics));
        EXPECT_EQ(original, decoded);
        ASSERT_TRUE(loaded.PopulateCandidate(Candidate, Schemas, &Diagnostics));
        ECS::EntityID child = {}, parent = {};
        Candidate.ForEachEntity([&](ECS::EntityID entity) {
            const auto id                      = Candidate.GetComponent<Components::UUIDComponent>(entity)->Value;
            (id == ChildUUID ? child : parent) = entity;
        });
        ASSERT_TRUE(child.IsValid());
        ASSERT_TRUE(parent.IsValid());
        EXPECT_EQ(Candidate.GetComponent<Components::ParentComponent>(child)->Parent, parent);
        EXPECT_FLOAT_EQ(Candidate.GetComponent<Components::TransformComponent>(child)->WorldTransform(0, 3), 7.f);
        ZEngine::Core::Containers::Array<uint8_t> reencoded = {};
        ASSERT_TRUE(ECS::BinarySceneCodec::Encode(loaded, Schemas, &Output, &reencoded, &Diagnostics));
        EXPECT_EQ(Copy(), std::vector<uint8_t>(reencoded.begin(), reencoded.end()));
    }

    TEST_F(BinarySceneCodecTest, EmptyAndIdentityOnlyEntitiesHaveCanonicalArtifacts)
    {
        ECS::Scene empty = {};
        empty.Initialize(&Memory.MainArena);
        for (uint32_t count = 0; count < 2; ++count)
        {
            ECS::SceneDocument                        document = {}, loaded = {};
            ZEngine::Core::Containers::Array<uint8_t> bytes = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(empty, Settings, Schemas, &document, &Diagnostics));
            ASSERT_TRUE(ECS::BinarySceneCodec::Encode(document, Schemas, &Output, &bytes, &Diagnostics));
            ASSERT_TRUE(ECS::BinarySceneCodec::Decode({bytes.data(), bytes.size()}, Schemas, &loaded, &Diagnostics));
            EXPECT_EQ(loaded.EntityCount(), count);
            if (!count)
            {
                empty.AddComponent<Components::UUIDComponent>(empty.CreateEntity(), {ChildUUID});
            }
        }
        empty.Shutdown();
    }

    TEST_F(BinarySceneCodecTest, EveryTruncationPreservesThePreviousDocument)
    {
        for (size_t length = 0; length < Bytes.size(); ++length)
        {
            SCOPED_TRACE(length);
            Rejects({Bytes.data(), length});
        }
    }

    TEST_F(BinarySceneCodecTest, EverySingleBitCorruptionIsDetectedByIntegrity)
    {
        auto bytes = Copy();
        for (size_t i = 0; i < bytes.size(); ++i)
        {
            SCOPED_TRACE(i);
            bytes[i] ^= 1;
            Rejects(bytes);
            bytes[i] ^= 1;
        }
    }

    TEST_F(BinarySceneCodecTest, IncompatibleHeadersAndOverflowingCountsFailEvenWithValidChecksum)
    {
        for (const size_t offset : {0u, 4u, 8u, 12u, 16u, 20u, 24u, 48u, 52u, 56u, 60u})
        {
            SCOPED_TRACE(offset);
            auto bytes = Copy();
            SetWord(bytes, offset, UINT32_MAX);
            FixChecksum(bytes);
            Rejects(bytes);
        }
        auto bytes = Copy();
        std::fill(bytes.begin() + 32, bytes.begin() + 48, 0);
        FixChecksum(bytes);
        Rejects(bytes);
    }

    TEST_F(BinarySceneCodecTest, EntityTablesRejectDuplicateUUIDsUnorderedRowsAndInvalidRanges)
    {
        for (const auto offset : {16u, 20u})
        {
            auto bytes = Copy();
            SetWord(bytes, Entities() + offset, UINT32_MAX);
            FixChecksum(bytes);
            Rejects(bytes);
        }
        auto bytes = Copy();
        std::copy_n(bytes.begin() + Entities(), 16, bytes.begin() + Entities() + Format::EntityBytes);
        FixChecksum(bytes);
        Rejects(bytes);
        bytes = Copy();
        std::swap_ranges(bytes.begin() + Entities(), bytes.begin() + Entities() + 16, bytes.begin() + Entities() + Format::EntityBytes);
        FixChecksum(bytes);
        Rejects(bytes);
    }

    TEST_F(BinarySceneCodecTest, ComponentTablesRejectInvalidBoundsVersionsAndUnknownKeys)
    {
        for (const auto offset : {0u, 4u, 8u, 12u, 16u})
        {
            auto bytes = Copy();
            SetWord(bytes, ComponentsOffset() + offset, UINT32_MAX);
            FixChecksum(bytes);
            Rejects(bytes);
        }
        auto bytes                             = Copy();
        bytes[Word(bytes, ComponentsOffset())] = '?';
        FixChecksum(bytes);
        Rejects(bytes);
        bytes = Copy();
        SetWord(bytes, ComponentsOffset() + 12, Word(bytes, ComponentsOffset()));
        FixChecksum(bytes);
        Rejects(bytes);
    }

    TEST_F(BinarySceneCodecTest, PayloadReadersCannotCrossIntoFollowingComponents)
    {
        const auto row   = Entry("TransformComponent");
        auto       bytes = Copy();
        SetWord(bytes, row + 16, 4);
        FixChecksum(bytes);
        Rejects(bytes);
        bytes = Copy();
        bytes.push_back(0);
        SetWord(bytes, 24, static_cast<uint32_t>(bytes.size()));
        FixChecksum(bytes);
        Rejects(bytes);
    }

    TEST_F(BinarySceneCodecTest, ExactPayloadBoundsRejectTruncatedAndExtendedFinalPayloads)
    {
        const auto row = ComponentsOffset() + (Word({Bytes.data(), Bytes.size()}, 56) - 1) * Format::ComponentBytes;
        for (const bool extend : {false, true})
        {
            auto       bytes  = Copy();
            const auto length = Word(bytes, row + 16);
            if (extend)
            {
                bytes.push_back(0);
            }
            else
            {
                bytes.pop_back();
            }
            SetWord(bytes, row + 16, extend ? length + 1 : length - 1);
            SetWord(bytes, 24, static_cast<uint32_t>(bytes.size()));
            FixChecksum(bytes);
            Rejects(bytes);
        }
    }

    TEST_F(BinarySceneCodecTest, ContainerRejectsCodecSuccessWithUnconsumedPayload)
    {
        struct Probe
        {
            ECS::SceneComponentCodecFns Original = {};
            uint32_t                    Calls    = 0;
        } probe             = {.Original = Schemas.FindByKey("NameComponent")->Codecs};
        auto codecs         = probe.Original;
        codecs.Context      = &probe;
        codecs.DecodeBinary = [](void* context, uint32_t version, ECS::SceneBinaryReader* reader, ECS::SceneValueWriter* value, ECS::SceneDiagnostics* diagnostics) {
            auto& state = *static_cast<Probe*>(context);
            ++state.Calls;
            auto copy = *reader;
            return state.Original.DecodeBinary(state.Original.Context, version, &copy, value, diagnostics);
        };
        ECS::SceneComponentSchemaRegistry alternate = {};
        ASSERT_TRUE(AlternateSchemas(&alternate, codecs));
        Rejects(Copy(), {}, &alternate);
        EXPECT_EQ(probe.Calls, 1u);
    }

    TEST_F(BinarySceneCodecTest, CodecFailureAfterMeasurementRollsBackAllocatedOutput)
    {
        struct Probe
        {
            ECS::SceneComponentCodecFns Original = {};
            uint32_t                    Calls    = 0;
        } probe             = {.Original = Schemas.FindByKey("NameComponent")->Codecs};
        auto codecs         = probe.Original;
        codecs.Context      = &probe;
        codecs.EncodeBinary = [](void* context, const ECS::SceneValue& value, ECS::SceneBinaryWriter* writer, ECS::SceneDiagnostics* diagnostics) {
            auto& state = *static_cast<Probe*>(context);
            ++state.Calls;
            const bool written = state.Original.EncodeBinary(state.Original.Context, value, writer, diagnostics);
            return written && state.Calls == 1;
        };
        ECS::SceneComponentSchemaRegistry alternate = {};
        ASSERT_TRUE(AlternateSchemas(&alternate, codecs));
        const auto before   = Copy();
        const auto offset   = Output.m_current_offset;
        const auto previous = Output.m_previous_offset;
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, alternate, &Output, &Bytes, &Diagnostics));
        EXPECT_EQ(probe.Calls, 2u);
        EXPECT_EQ(Output.m_current_offset, offset);
        EXPECT_EQ(Output.m_previous_offset, previous);
        EXPECT_EQ(Copy(), before);
    }

    TEST_F(BinarySceneCodecTest, MissingAndNonCurrentRegisteredSchemasFailBeforePublication)
    {
        ECS::SceneComponentSchemaRegistry missing = {}, incompatible = {};
        missing.Initialize(&Memory.MainArena);
        ASSERT_TRUE(AlternateSchemas(&incompatible, Schemas.FindByKey("NameComponent")->Codecs, 2));
        const auto before = Copy();
        for (const auto* registry : {&missing, &incompatible})
        {
            RefusingRoute route = {};
            Rejects(before, {}, registry, &route);
            EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion));
            EXPECT_EQ(route.Calls, 0u);
            const auto offset = Output.m_current_offset;
            EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, *registry, &Output, &Bytes, &Diagnostics));
            EXPECT_EQ(Output.m_current_offset, offset);
            EXPECT_EQ(Copy(), before);
        }
    }

    TEST_F(BinarySceneCodecTest, WellFormedUnknownKeysAndNewerVersionsReportUnsupportedWithoutAllocation)
    {
        const auto row = Entry("NameComponent");
        for (const bool unknown_key : {false, true})
        {
            auto bytes = Copy();
            if (unknown_key)
            {
                bytes[Word(bytes, row)] = 'O'; // Valid key, still before ParentComponent.
            }
            else
            {
                SetWord(bytes, row + 8, 2);
            }
            FixChecksum(bytes);
            RefusingRoute route = {};
            Rejects(bytes, {}, nullptr, &route);
            EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion));
            EXPECT_EQ(route.Calls, 0u);
        }
    }

    TEST_F(BinarySceneCodecTest, MalformedTablesTakePrecedenceOverMissingSchemasWithoutAllocation)
    {
        ECS::SceneComponentSchemaRegistry missing = {};
        missing.Initialize(&Memory.MainArena);
        const auto last = ComponentsOffset() + (Word({Bytes.data(), Bytes.size()}, 56) - 1) * Format::ComponentBytes;
        for (uint32_t mode = 0; mode < 5; ++mode)
        {
            SCOPED_TRACE(mode);
            auto bytes = Copy();
            switch (mode)
            {
                case 0:
                    bytes[Word(bytes, last)] = ' ';
                    break;
                case 1:
                    SetWord(bytes, last + 12, Word(bytes, last + 12) - 1);
                    break;
                case 2:
                    SetWord(bytes, last + 16, UINT32_MAX);
                    break;
                case 3:
                    bytes.push_back(0);
                    SetWord(bytes, 24, static_cast<uint32_t>(bytes.size()));
                    break;
                case 4:
                {
                    // NameComponent and UUIDComponent have the same key length;
                    // leave only this row so canonical-order rejection cannot
                    // mask the explicit reserved-identity-key rule.
                    Source.ForEachEntity([&](ECS::EntityID entity) {
                        Source.RemoveComponent<Components::ParentComponent>(entity);
                        Source.RemoveComponent<Components::TransformComponent>(entity);
                    });
                    ASSERT_TRUE(ECS::SceneDocument::Capture(Source, Settings, Schemas, &Document, &Diagnostics));
                    ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Output, &Bytes, &Diagnostics));
                    bytes = Copy();
                    ASSERT_EQ(Word(bytes, 56), 1u);
                    const auto key = Word(bytes, Entry("NameComponent"));
                    ASSERT_EQ(ZEngine::Helpers::secure_memcpy(bytes.data() + key, bytes.size() - key, "UUIDComponent", 13), ZEngine::Helpers::MEMORY_OP_SUCCESS);
                    break;
                }
            }
            FixChecksum(bytes);
            RefusingRoute route = {};
            Rejects(bytes, {}, &missing, &route);
            EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::InvalidData));
            EXPECT_FALSE(Diagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion));
            EXPECT_EQ(route.Calls, 0u);
        }
    }

    TEST_F(BinarySceneCodecTest, InvalidSemanticFloatsNamesAndSettingsFailWithValidIntegrity)
    {
        const auto row   = Entry("TransformComponent");
        auto       bytes = Copy();
        SetWord(bytes, Word(bytes, row + 12), std::bit_cast<uint32_t>(std::numeric_limits<float>::infinity()));
        FixChecksum(bytes);
        Rejects(bytes);
        for (uint8_t invalid : {uint8_t(0), uint8_t(0xff), uint8_t(0xc0)})
        {
            bytes                      = Copy();
            bytes[Format::HeaderBytes] = invalid;
            FixChecksum(bytes);
            Rejects(bytes);
        }
        bytes           = Copy();
        const auto grid = Format::HeaderBytes + Word(bytes, 48);
        SetWord(bytes, grid + 36, 0); // base spacing must be positive
        FixChecksum(bytes);
        Rejects(bytes);
        bytes = Copy();
        SetWord(bytes, grid + ECS::SceneGridBinaryBytes + 4, 256); // enum checked before narrowing
        FixChecksum(bytes);
        Rejects(bytes);
    }

    TEST_F(BinarySceneCodecTest, SharedGraphValidationRejectsDanglingReferencesAndCycles)
    {
        const auto row = Entry("ParentComponent");
        for (const auto id : {ChildUUID, SceneUUID})
        {
            auto       bytes = Copy();
            const auto uuid  = id.as_bytes();
            std::transform(uuid.begin(), uuid.end(), bytes.begin() + Word(bytes, row + 12), [](std::byte byte) { return std::to_integer<uint8_t>(byte); });
            FixChecksum(bytes);
            Rejects(bytes);
        }
    }

    TEST_F(BinarySceneCodecTest, TightResourceLimitsFailWithoutPartialOutput)
    {
        const auto input = Copy();
        for (uint32_t mode = 0; mode < 8; ++mode)
        {
            ECS::SceneDocumentLimits limits = {};
            switch (mode)
            {
                case 0:
                    limits.MaxFileBytes = Bytes.size() - 1;
                    break;
                case 1:
                    limits.MaxEntities = 1;
                    break;
                case 2:
                    limits.MaxComponentsPerEntity = 1;
                    break;
                case 3:
                    limits.MaxDecodedBytes = 64;
                    break;
                case 4:
                    limits.MaxNodes = 1;
                    break;
                case 5:
                    limits.MaxDepth = 1;
                    break;
                case 6:
                    limits.MaxCollectionItems = 1;
                    break;
                case 7:
                    limits.MaxScalarBytes = 1;
                    break;
            }
            SCOPED_TRACE(mode);
            Rejects(input, limits);
        }
        const auto               offset = Output.m_current_offset;
        ECS::SceneDocumentLimits limits = {};
        limits.MaxFileBytes             = Bytes.size() - 1;
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Output, &Bytes, &Diagnostics, limits));
        EXPECT_EQ(Output.m_current_offset, offset);
        EXPECT_EQ(Copy(), input);
        std::array<std::byte, 32>             tiny_storage = {};
        ZEngine::Core::Memory::ArenaAllocator tiny         = {};
        ASSERT_TRUE(tiny.InitializeBorrowed(tiny_storage.data(), tiny_storage.size()));
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, Schemas, &tiny, &Bytes, &Diagnostics));
        EXPECT_EQ(tiny.m_current_offset, 0u);
        EXPECT_EQ(Copy(), input);
    }

    TEST_F(BinarySceneCodecTest, EmptyUninitializedAndReadOnlyDocumentsNeverProduceCookedOutput)
    {
        const auto                        before        = Copy();
        ECS::SceneDocument                empty         = {};
        ECS::SceneComponentSchemaRegistry uninitialized = {};
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(empty, Schemas, &Output, &Bytes, &Diagnostics));
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, uninitialized, &Output, &Bytes, &Diagnostics));
        EXPECT_FALSE(ECS::BinarySceneCodec::Decode(before, uninitialized, &Document, &Diagnostics));
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, Schemas, nullptr, &Bytes, &Diagnostics));
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Output, nullptr, &Diagnostics));
        EXPECT_FALSE(ECS::BinarySceneCodec::Decode(before, Schemas, nullptr, &Diagnostics));
        std::string source = {};
        ASSERT_TRUE(Document.Emit(&source, &Diagnostics));
        const auto key = source.find("NameComponent");
        ASSERT_NE(key, std::string::npos);
        source.replace(key, 13, "UnknownComponent");
        ECS::SceneDocument unknown = {};
        ASSERT_TRUE(ECS::SceneDocument::Parse(source.data(), source.size(), Schemas, &unknown, &Diagnostics));
        ASSERT_TRUE(unknown.ReadOnly());
        EXPECT_FALSE(ECS::BinarySceneCodec::Encode(unknown, Schemas, &Output, &Bytes, &Diagnostics));
        EXPECT_EQ(Copy(), before);
    }

    TEST(SceneBinaryWriterTest, MeasurementUsesTheSameBoundsAndFiniteValueRulesWithoutStorage)
    {
        auto writer = ECS::SceneBinaryWriter::Measure(16);
        EXPECT_TRUE(writer.UInt32(9));
        EXPECT_TRUE(writer.Vector({1.f, 2.f, 3.f}));
        EXPECT_EQ(writer.Position(), 16u);
        EXPECT_EQ(writer.Remaining(), 0u);
        EXPECT_FALSE(writer.UInt32(0));
        auto finite = ECS::SceneBinaryWriter::Measure(4);
        EXPECT_FALSE(finite.Float(std::numeric_limits<float>::quiet_NaN()));
        EXPECT_EQ(finite.Position(), 0u);
    }

    TEST(SceneSkyBinaryTest, AllModesRoundTripAndFailuresPreserveOutput)
    {
        std::array<std::byte, ECS::SceneSkyBinaryBytes> bytes = {};
        for (auto mode : {ZEngine::Rendering::Scenes::SkyMode::Atmosphere, ZEngine::Rendering::Scenes::SkyMode::HDRI, ZEngine::Rendering::Scenes::SkyMode::SkySphere})
        {
            ZEngine::Rendering::Scenes::SkyConfig sky = {};
            sky.Mode                                  = mode;
            sky.SkyLuminanceFactor                    = 2.5f;
            sky.SkyLowerHemisphereColor[1]            = .25f;
            sky.Atmosphere.MieAnisotropy              = .5f;
            sky.Sphere.ShowSunDisc                    = false;
            ASSERT_TRUE(ECS::EncodeSceneSkyBinary(sky, {bytes.data(), bytes.size()}));
            ZEngine::Rendering::Scenes::SkyConfig decoded = {};
            ASSERT_TRUE(ECS::DecodeSceneSkyBinary({bytes.data(), bytes.size()}, decoded));
            EXPECT_EQ(sky, decoded);
            for (size_t length = 0; length < bytes.size(); ++length)
            {
                EXPECT_FALSE(ECS::DecodeSceneSkyBinary({bytes.data(), length}, decoded));
                EXPECT_EQ(sky, decoded);
            }
            auto                   invalid = bytes;
            ECS::SceneBinaryWriter version{std::span(invalid).first(4)};
            ASSERT_TRUE(version.UInt32(2));
            EXPECT_FALSE(ECS::DecodeSceneSkyBinary({invalid.data(), invalid.size()}, decoded));
            EXPECT_EQ(sky, decoded);
            const auto before        = bytes;
            sky.EnvironmentIntensity = -1.f;
            EXPECT_FALSE(ECS::EncodeSceneSkyBinary(sky, {bytes.data(), bytes.size()}));
            EXPECT_EQ(before, bytes);
        }
    }
} // namespace
