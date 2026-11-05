#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneCompatibility.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <future>
#include <vector>
#include "SceneWorkingMemoryTestSupport.h"

namespace
{
    namespace ECS                         = ZEngine::ECS;
    namespace Components                  = ZEngine::ECS::Components;
    namespace VFS                         = ZEngine::Core::VFS;
    const auto                 SceneUUID  = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto                 EntityUUID = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

    // Synthetic versioned fixtures exercise the extension point. They do not
    // claim to describe shipped historical scene/component formats.
    constexpr std::string_view NameV1     = "label: \"Migrated \\u00e9 \\u6771\\u4eac\"";
    constexpr std::string_view NameV2     = "title: \"Migrated \\u00e9 \\u6771\\u4eac\"";
    constexpr std::string_view NameV3     = "value: \"Migrated \\u00e9 \\u6771\\u4eac\"";

    struct RenameStep
    {
        cstring From = nullptr;
        cstring To   = nullptr;
    };
    bool RenameField(void* context, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*)
    {
        const auto& step = *static_cast<const RenameStep*>(context);
        if (!source.IsMap() || source.size() != 1 || !source[step.From].IsScalar())
        {
            return false;
        }
        auto&      storage = *out->Owner();
        const auto result  = storage.Make(ECS::SceneYAML::NodeType::Map);
        if (!storage.Set(result, step.To, storage.Clone(source[step.From])))
        {
            return false;
        }
        *out = result;
        return true;
    }
    bool UpgradeDocument(void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*)
    {
        auto&      storage = *out->Owner();
        const auto result  = storage.Clone(source);
        if (!storage.Set(result["scene"], "schema_version", storage.UInt(ECS::SceneDocument::SchemaVersion)))
        {
            return false;
        }
        *out = result;
        return true;
    }

    class SceneCompatibilityTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager Memory = {};
        ECS::Scene                           Source = {}, Candidate = {};
        ECS::SceneComponentSchemaRegistry    BuiltIns = {}, Schemas = {};
        ECS::SceneMigrationRegistry          Migrations   = {};
        ECS::SceneComponentCodecFns          OriginalName = {};
        ECS::SceneDiagnostics                Diagnostics  = {};
        ECS::SceneDocument                   Document     = {};
        std::string                          Canonical    = {};
        RenameStep                           First = {"label", "title"}, Second = {"title", "value"};

        void                                 SetUp() override
        {
            Memory.Initialize(ZMega(128), {});
            Source.Initialize(&Memory.MainArena);
            Candidate.Initialize(&Memory.MainArena);
            BuiltIns.Initialize(&Memory.MainArena);
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(BuiltIns, &Diagnostics));
            OriginalName = BuiltIns.FindByKey("NameComponent")->Codecs;
            // Same semantic codec, registered as a test-only version 3 schema.
            for (const auto* key : {"NameComponent", "TransformComponent", "ParentComponent"})
            {
                const auto* schema  = BuiltIns.FindByKey(key);
                auto        codecs  = schema->Codecs;
                uint32_t    version = schema->Version;
                if (std::string_view(key) == "NameComponent")
                {
                    version           = 3;
                    codecs.Context    = &OriginalName;
                    codecs.DecodeYAML = [](void* context, uint32_t encoded, const ECS::SceneYAML::Node& value, ECS::SceneValueWriter* out, ECS::SceneDiagnostics* diagnostics) {
                        const auto& original = *static_cast<const ECS::SceneComponentCodecFns*>(context);
                        return encoded == 3 && original.DecodeYAML(original.Context, 1, value, out, diagnostics);
                    };
                    codecs.DecodeBinary = [](void* context, uint32_t encoded, ECS::SceneBinaryReader* reader, ECS::SceneValueWriter* out, ECS::SceneDiagnostics* diagnostics) {
                        const auto& original = *static_cast<const ECS::SceneComponentCodecFns*>(context);
                        return encoded == 3 && original.DecodeBinary(original.Context, 1, reader, out, diagnostics);
                    };
                }
                ASSERT_TRUE(Schemas.Register({.Key = schema->Key, .Version = version, .RuntimeType = schema->RuntimeType, .Fields = schema->Fields, .FieldCount = schema->FieldCount, .Codecs = codecs, .References = schema->References}, &Diagnostics));
            }
            ASSERT_TRUE(Migrations.Initialize(&Memory.MainArena, 64));
            const auto entity = Source.CreateEntity();
            Source.AddComponent<Components::UUIDComponent>(entity, {EntityUUID});
            Source.AddComponent<Components::TransformComponent>(
                entity,
                {
                .Position = {1.f, 2.f, 3.f}
            });
            Components::NameComponent name = {};
            ASSERT_EQ(ZEngine::Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Current", 7), ZEngine::Helpers::MEMORY_OP_SUCCESS);
            Source.AddComponent<Components::NameComponent>(entity, name);
            ASSERT_TRUE(ECS::SceneDocument::Capture(Source, {.UUID = SceneUUID, .Name = "Compatibility"}, Schemas, &Document, &Diagnostics));
            ASSERT_TRUE(Document.Emit(&Canonical, &Diagnostics));
        }
        void TearDown() override
        {
            Candidate.Shutdown();
            Source.Shutdown();
        }
        std::string Fixture(uint32_t component_version = 1, uint32_t document_version = ECS::SceneDocument::SchemaVersion, std::string_view fields = NameV1, cstring key = "NameComponent")
        {
            ECS::SceneYAML::Storage storage = {};
            EXPECT_TRUE(storage.Initialize(ZMega(2)));
            auto root = storage.Destination();
            EXPECT_TRUE(ECS::ReadSceneYAML(Canonical, {}, &root, &Diagnostics));
            const auto scene      = root["scene"];
            const auto components = scene["entities"][size_t{0}]["components"];
            const auto envelope   = components["NameComponent"];
            auto       payload    = storage.Destination();
            EXPECT_TRUE(ECS::ReadSceneYAML(fields, {}, &payload, &Diagnostics));
            EXPECT_TRUE(storage.Set(envelope, "schema_version", storage.UInt(component_version)));
            EXPECT_TRUE(storage.Set(envelope, "fields", payload));
            EXPECT_TRUE(storage.Set(scene, "schema_version", storage.UInt(document_version)));
            if (std::string_view(key) != "NameComponent")
            {
                const auto replacement = storage.Make(ECS::SceneYAML::NodeType::Map);
                EXPECT_TRUE(storage.Set(replacement, key, envelope));
                EXPECT_TRUE(storage.Set(replacement, "TransformComponent", components["TransformComponent"]));
                EXPECT_TRUE(storage.Set(scene["entities"][size_t{0}], "components", replacement));
            }
            ECS::SceneYAML::Emitter emitter(ECS::SceneDocumentLimits{}.MaxFileBytes);
            emitter << root;
            std::string result = {};
            EXPECT_TRUE(emitter.Output(&result));
            return result;
        }
        bool Parse(std::string_view source, const ECS::SceneDocumentLimits& limits = {}, const ECS::SceneMigrationRegistry* registry = nullptr)
        {
            return ECS::SceneDocument::Parse(source.data(), source.size(), Schemas, &Document, &Diagnostics, limits, registry ? registry : &Migrations);
        }
        void RegisterSteps()
        {
            ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &First, RenameField, &Diagnostics));
            ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 2, 3, &Second, RenameField, &Diagnostics));
        }
        void Rejects(std::string_view source, const ECS::SceneDocumentLimits& limits = {})
        {
            std::string before = {}, after = {};
            ASSERT_TRUE(Document.Emit(&before, &Diagnostics));
            Diagnostics.Clear();
            EXPECT_FALSE(Parse(source, limits));
            EXPECT_TRUE(Diagnostics.HasErrors());
            ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
            EXPECT_EQ(before, after);
            EXPECT_EQ(Candidate.AliveCount(), 0u);
        }
    };

    TEST(SceneDiagnosticsTest, RetentionReusesCallerStorageAndPreservesAddMessages)
    {
        ECS::SceneDiagnostics diagnostics = {};
        auto*                 entry       = diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Warning);
        ASSERT_EQ(entry, &diagnostics.Entries[0]);
        EXPECT_EQ(entry->Severity, ECS::SceneDiagnosticSeverity::Warning);
        EXPECT_EQ(entry->Code, ECS::SceneDiagnosticCode::None);
        EXPECT_TRUE(std::all_of(std::begin(entry->Message), std::end(entry->Message), [](char value) { return value == 0; }));
        ASSERT_EQ(ZEngine::Helpers::secure_strncpy(entry->Message, sizeof(entry->Message), "retained message", 16), ZEngine::Helpers::MEMORY_OP_SUCCESS);
        diagnostics.Add(ECS::SceneDiagnosticSeverity::Info, entry->Message);
        EXPECT_STREQ(diagnostics.Entries[1].Message, "retained message");
        diagnostics.Error(nullptr);
        EXPECT_STREQ(diagnostics.Entries[2].Message, "");
        EXPECT_TRUE(diagnostics.HasError(ECS::SceneDiagnosticCode::InvalidData));
        EXPECT_EQ(diagnostics.ErrorCount, 1u);

        diagnostics.Clear();
        EXPECT_EQ(diagnostics.Count, 0u);
        EXPECT_EQ(diagnostics.Dropped, 0u);
        EXPECT_EQ(diagnostics.ErrorCount, 0u);
        EXPECT_EQ(diagnostics.ErrorCodes, 0u);
        // Clear does not erase retained entries. Add must not clear its target
        // before copying from a different retained slot supplied by the caller.
        diagnostics.Add(ECS::SceneDiagnosticSeverity::Info, diagnostics.Entries[1].Message);
        EXPECT_STREQ(diagnostics.Entries[0].Message, "retained message");
        diagnostics.Clear();
        entry = diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Error, ECS::SceneDiagnosticCode::AllocationFailed);
        ASSERT_EQ(entry, &diagnostics.Entries[0]);
        EXPECT_TRUE(std::all_of(std::begin(entry->Message), std::end(entry->Message), [](char value) { return value == 0; }));
        EXPECT_EQ(entry->Code, ECS::SceneDiagnosticCode::AllocationFailed);
        EXPECT_TRUE(diagnostics.HasError(ECS::SceneDiagnosticCode::AllocationFailed));
        EXPECT_FALSE(diagnostics.HasError(ECS::SceneDiagnosticCode::InvalidData));

        const std::string long_message(ECS::SceneDiagnostics::MAX_MESSAGE_LEN * 2, 'm');
        diagnostics.Error(long_message.c_str());
        EXPECT_EQ(ZEngine::Helpers::secure_strlen(diagnostics.Entries[1].Message), ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 1);
        EXPECT_EQ(std::string_view(diagnostics.Entries[1].Message), std::string_view(long_message).substr(0, ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 1));

        diagnostics.Clear();
        entry = diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Info, ECS::SceneDiagnosticCode::None, diagnostics.Entries[1].Message + 1);
        ASSERT_EQ(entry, &diagnostics.Entries[0]);
        EXPECT_EQ(ZEngine::Helpers::secure_strlen(entry->Message), ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 2);
        diagnostics.Clear();
        entry = diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Info, ECS::SceneDiagnosticCode::None, diagnostics.Entries[0].Message + 1);
        ASSERT_EQ(entry, &diagnostics.Entries[0]);
        EXPECT_EQ(ZEngine::Helpers::secure_strlen(entry->Message), ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 3);
        EXPECT_EQ(entry->Message[ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 2], '\0');
    }

    TEST(SceneDiagnosticsTest, FullBuffersRetainErrorCategoriesAndDoNotOverwriteEntries)
    {
        ECS::SceneDiagnostics diagnostics = {};
        for (uint32_t index = 0; index < ECS::SceneDiagnostics::MAX_ENTRIES; ++index)
        {
            diagnostics.Add(ECS::SceneDiagnosticSeverity::Warning, "unchanged");
        }
        EXPECT_EQ(diagnostics.Count, ECS::SceneDiagnostics::MAX_ENTRIES);
        EXPECT_FALSE(diagnostics.HasErrors());
        EXPECT_EQ(diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Error, ECS::SceneDiagnosticCode::LimitExceeded), nullptr);
        diagnostics.Error("not retained", ECS::SceneDiagnosticCode::AllocationFailed);
        EXPECT_EQ(diagnostics.Emplace(ECS::SceneDiagnosticSeverity::Info), nullptr);
        EXPECT_EQ(diagnostics.Count, ECS::SceneDiagnostics::MAX_ENTRIES);
        EXPECT_EQ(diagnostics.ErrorCount, 2u);
        EXPECT_EQ(diagnostics.Dropped, 3u);
        EXPECT_TRUE(diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded));
        EXPECT_TRUE(diagnostics.HasError(ECS::SceneDiagnosticCode::AllocationFailed));
        for (const auto& entry : diagnostics.Entries)
        {
            EXPECT_EQ(entry.Severity, ECS::SceneDiagnosticSeverity::Warning);
            EXPECT_EQ(entry.Code, ECS::SceneDiagnosticCode::None);
            EXPECT_STREQ(entry.Message, "unchanged");
        }
    }

    TEST_F(SceneCompatibilityTest, MigrationDiagnosticsFormatAndTruncateInRetainedStorage)
    {
        Diagnostics.Clear();
        const auto offset = Memory.MainArena.m_current_offset;
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(0, 2, nullptr, UpgradeDocument, &Diagnostics));
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_STREQ(Diagnostics.Entries[0].Message, "Migration scene 0 -> 2: invalid, duplicate/branching or full registration");
        EXPECT_EQ(Diagnostics.Entries[0].Severity, ECS::SceneDiagnosticSeverity::Error);
        EXPECT_EQ(Diagnostics.Entries[0].Code, ECS::SceneDiagnosticCode::InvalidData);

        const std::string long_key(127, 'k');
        EXPECT_FALSE(Migrations.RegisterComponentMigration(long_key.c_str(), UINT32_MAX - 1, UINT32_MAX - 1, nullptr, RenameField, &Diagnostics));
        std::string expected = "Migration " + long_key + " 4294967294 -> 4294967294: invalid, duplicate/branching or full registration";
        expected.resize(ECS::SceneDiagnostics::MAX_MESSAGE_LEN - 1);
        ASSERT_EQ(Diagnostics.Count, 2u);
        EXPECT_EQ(std::string_view(Diagnostics.Entries[1].Message), expected);
        EXPECT_EQ(Diagnostics.ErrorCount, 2u);
        EXPECT_EQ(Diagnostics.Dropped, 0u);
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(0, 2, nullptr, UpgradeDocument, nullptr));
        EXPECT_EQ(Diagnostics.Count, 2u);
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
        EXPECT_EQ(Migrations.Count(), 0u);

        Diagnostics.Clear();
        Diagnostics.Add(ECS::SceneDiagnosticSeverity::Info, "NameComponent");
        Diagnostics.Clear();
        EXPECT_FALSE(Migrations.RegisterComponentMigration(Diagnostics.Entries[0].Message, 0, 2, nullptr, RenameField, &Diagnostics));
        ASSERT_EQ(Diagnostics.Count, 1u);
        EXPECT_STREQ(Diagnostics.Entries[0].Message, "Migration NameComponent 0 -> 2: invalid, duplicate/branching or full registration");
        EXPECT_EQ(Memory.MainArena.m_current_offset, offset);
    }

    TEST_F(SceneCompatibilityTest, MissingMigrationWithFullDiagnosticsPreservesDocumentAndReportsVersionCategory)
    {
        const auto source = Fixture();
        Diagnostics.Clear();
        for (uint32_t index = 0; index < ECS::SceneDiagnostics::MAX_ENTRIES; ++index)
        {
            Diagnostics.Add(ECS::SceneDiagnosticSeverity::Info, "unchanged");
        }
        EXPECT_FALSE(Parse(source));
        EXPECT_TRUE(Diagnostics.HasErrors());
        EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion));
        EXPECT_GE(Diagnostics.Dropped, 1u);
        EXPECT_EQ(Diagnostics.Count, ECS::SceneDiagnostics::MAX_ENTRIES);
        for (const auto& entry : Diagnostics.Entries)
        {
            EXPECT_STREQ(entry.Message, "unchanged");
        }
        std::string after = {};
        ASSERT_TRUE(Document.Emit(&after, nullptr));
        EXPECT_EQ(after, Canonical);
        EXPECT_EQ(Candidate.AliveCount(), 0u);
    }

    TEST_F(SceneCompatibilityTest, RegistrationRejectsInvalidAmbiguousAndExhaustedRoutes)
    {
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(0, 2, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(2, 1, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(1, 1, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(1, 2, nullptr, nullptr, &Diagnostics));
        for (const auto* key : {"", "bad/key", "bad\\key", "bad\"key", "bad key", "\xc3\xa9", "UUIDComponent"})
        {
            EXPECT_FALSE(Migrations.RegisterComponentMigration(key, 1, 2, &First, RenameField, &Diagnostics));
        }
        EXPECT_FALSE(Migrations.RegisterComponentMigration(nullptr, 1, 2, &First, RenameField, &Diagnostics));
        const std::string long_key(128, 'k');
        EXPECT_FALSE(Migrations.RegisterComponentMigration(long_key.c_str(), 1, 2, &First, RenameField, &Diagnostics));
        ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &First, RenameField, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &First, RenameField, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterComponentMigration("NameComponent", 1, 3, &First, RenameField, &Diagnostics));
        // Routes with different starts may converge; they are not ambiguous.
        EXPECT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 2, 3, &Second, RenameField, &Diagnostics));
        EXPECT_TRUE(Migrations.RegisterComponentMigration("OtherComponent", 1, 3, &First, RenameField, &Diagnostics));
        EXPECT_TRUE(Migrations.RegisterDocumentMigration(1, 2, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(Migrations.RegisterDocumentMigration(1, 3, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_EQ(Migrations.Count(), 4u);
        ECS::SceneMigrationRegistry tiny = {};
        ASSERT_TRUE(tiny.Initialize(&Memory.MainArena, 1));
        ASSERT_TRUE(tiny.RegisterDocumentMigration(1, 2, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(tiny.RegisterDocumentMigration(2, 3, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_EQ(tiny.Count(), 1u);
    }

    TEST_F(SceneCompatibilityTest, RegistryInitializationIsCheckedAndFailureDoesNotPublishRegistration)
    {
        ECS::SceneMigrationRegistry empty = {};
        EXPECT_FALSE(empty.RegisterDocumentMigration(1, 2, nullptr, UpgradeDocument, &Diagnostics));
        EXPECT_FALSE(empty.Initialize(nullptr));
        EXPECT_FALSE(empty.Initialize(&Memory.MainArena, 0));
        EXPECT_FALSE(empty.Initialize(&Memory.MainArena, ECS::SceneMigrationRegistry::MaxMigrations + 1));
        EXPECT_FALSE(empty.IsInitialized());
        std::array<std::byte, 64>             bytes = {};
        ZEngine::Core::Memory::ArenaAllocator tiny  = {};
        ASSERT_TRUE(tiny.InitializeBorrowed(bytes.data(), bytes.size()));
        EXPECT_FALSE(empty.Initialize(&tiny));
        EXPECT_EQ(tiny.m_current_offset, 0u);
        EXPECT_FALSE(empty.IsInitialized());
        EXPECT_TRUE(empty.Initialize(&Memory.MainArena));
        EXPECT_FALSE(empty.Initialize(&Memory.MainArena));
    }

    TEST_F(SceneCompatibilityTest, DocumentAndComponentChainsProduceOwnedCurrentValuesBeforeCandidateConstruction)
    {
        RegisterSteps();
        ASSERT_TRUE(Migrations.RegisterDocumentMigration(1, 2, nullptr, UpgradeDocument, &Diagnostics));
        auto source = Fixture(1, 1);
        ASSERT_TRUE(Parse(source));
        std::fill(source.begin(), source.end(), '?');
        EXPECT_EQ(Candidate.AliveCount(), 0u);
        ASSERT_FALSE(Document.ReadOnly());
        ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
        Candidate.ForEachEntity([&](ECS::EntityID entity) {
            EXPECT_EQ(Candidate.GetComponent<Components::UUIDComponent>(entity)->Value, EntityUUID);
            EXPECT_STREQ(Candidate.GetComponent<Components::NameComponent>(entity)->Value, reinterpret_cast<const char*>(u8"Migrated \u00e9 \u6771\u4eac"));
            EXPECT_FLOAT_EQ(Candidate.GetComponent<Components::TransformComponent>(entity)->WorldTransform(1, 3), 2.f);
        });
        std::string upgraded = {};
        ASSERT_TRUE(Document.Emit(&upgraded, &Diagnostics));
        EXPECT_NE(upgraded.find("value"), std::string::npos);
        EXPECT_EQ(upgraded.find("\"label\":"), std::string::npos);
        EXPECT_EQ(upgraded.find("\"title\":"), std::string::npos);
        // The new canonical source no longer requires its migration registry.
        ECS::SceneDocument reloaded = {};
        ASSERT_TRUE(ECS::SceneDocument::Parse(upgraded.data(), upgraded.size(), Schemas, &reloaded, &Diagnostics));
    }

    TEST_F(SceneCompatibilityTest, EveryVersionedFixtureConvergesToDeterministicCurrentSourceAndBinary)
    {
        RegisterSteps();
        std::string          expected        = {};
        std::vector<uint8_t> expected_binary = {};
        for (uint32_t version = 1; version <= 3; ++version)
        {
            SCOPED_TRACE(version);
            ASSERT_TRUE(Parse(Fixture(version, 2, version == 1 ? NameV1 : version == 2 ? NameV2 : NameV3)));
            std::string actual = {};
            ASSERT_TRUE(Document.Emit(&actual, &Diagnostics));
            ZEngine::Core::Containers::Array<uint8_t> bytes = {};
            ASSERT_TRUE(ECS::SceneCompatibilityService::PrepareForSave(Document, ECS::SceneCompatibilityUse::CookedRuntime, &Diagnostics));
            ASSERT_TRUE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &bytes, &Diagnostics));
            const std::vector<uint8_t> binary(bytes.begin(), bytes.end());
            if (version == 1)
            {
                expected        = actual;
                expected_binary = binary;
            }
            EXPECT_EQ(expected, actual);
            EXPECT_EQ(expected_binary, binary);
            ECS::SceneDocument decoded = {};
            ASSERT_TRUE(ECS::BinarySceneCodec::Decode(binary, Schemas, &decoded, &Diagnostics));
            std::string reopened = {};
            ASSERT_TRUE(decoded.Emit(&reopened, &Diagnostics));
            EXPECT_EQ(reopened, expected);
        }
    }

    TEST_F(SceneCompatibilityTest, MissingIncompleteOvershootingAndUninitializedRoutesPreservePreviousDocument)
    {
        const auto source = Fixture();
        Rejects(source);
        ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &First, RenameField, &Diagnostics));
        Rejects(source);
        ECS::SceneMigrationRegistry overshoot = {}, uninitialized = {};
        ASSERT_TRUE(overshoot.Initialize(&Memory.MainArena));
        ASSERT_TRUE(overshoot.RegisterComponentMigration("NameComponent", 1, 4, &First, RenameField, &Diagnostics));
        EXPECT_FALSE(Parse(source, {}, &overshoot));
        EXPECT_FALSE(Parse(source, {}, &uninitialized));
        Rejects(Fixture(3, 1, NameV3));
        Rejects(Fixture(3, 99, NameV3));
        EXPECT_EQ(Candidate.AliveCount(), 0u);
        std::string after = {};
        ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
        EXPECT_EQ(after, Canonical);
    }

    TEST_F(SceneCompatibilityTest, MigrationPrincipalAndNestedScratchBudgetsRefuseBeforeCallbacksAndPreserveOutputs)
    {
        struct Trace
        {
            RenameStep* Step  = nullptr;
            uint32_t    Calls = 0;
        } trace            = {&First};
        const auto migrate = [](void* context, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics* diagnostics) {
            auto& state = *static_cast<Trace*>(context);
            ++state.Calls;
            return RenameField(state.Step, source, out, diagnostics);
        };
        ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &trace, migrate, &Diagnostics));
        ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 2, 3, &Second, RenameField, &Diagnostics));
        const auto text      = Fixture();
        const auto principal = ECS::SceneDocumentDetail::MigrationScratchWorkingBytes();
        size_t     peak      = 0;
        for (uint32_t attempt = 0; attempt < 4; ++attempt)
        {
            ZEngine::Tests::SceneTestAllocationRoute route = {}, unrelated = {};
            route.Limit = SIZE_MAX;
            {
                ECS::SceneYAML::Storage storage = {};
                ASSERT_TRUE(storage.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
                auto root = storage.Destination();
                ASSERT_TRUE(ECS::ReadSceneYAML(text, {}, &root, &Diagnostics));
                const auto original    = root["scene"]["entities"][size_t{0}]["components"]["NameComponent"]["fields"]["label"].Scalar();
                const auto used        = storage.BytesUsed();
                const auto allocations = route.Attempts;
                const auto blocks      = route.Blocks;
                route.WatchBytes       = principal;
                route.Peak             = 0;
                route.Limit            = attempt < 2 ? SIZE_MAX : peak - (attempt == 2 ? 1 : 0);
                route.RefuseBytes      = attempt == 1 ? principal : 0;
                trace.Calls            = 0;
                Diagnostics.Clear();
                ZEngine::Core::Serialization::YAMLAllocationScope scope(unrelated);
                const bool                                        accepted = attempt == 0 || attempt == 3;
                EXPECT_EQ(ECS::SceneCompatibilityService::PrepareForLoad(&root, Schemas, &Migrations, &Diagnostics, {}, &route), accepted);
                if (attempt == 0)
                {
                    peak = route.Peak;
                }
                EXPECT_GT(peak, principal); // Snapshot state stays charged during cloning.
                EXPECT_EQ(trace.Calls, accepted ? 1u : 0u);
                EXPECT_EQ(route.Live, 0u);
                EXPECT_EQ(route.WatchCalls, 1u);
                EXPECT_EQ(route.WatchReleases, attempt == 1 ? 0u : 1u);
                EXPECT_EQ(unrelated.Required, 0u);
                EXPECT_TRUE(storage.Good());
                const auto fields = root["scene"]["entities"][size_t{0}]["components"]["NameComponent"]["fields"];
                if (accepted)
                {
                    EXPECT_EQ(fields["value"].Scalar(), original);
                }
                else
                {
                    EXPECT_EQ(fields["label"].Scalar(), original);
                    EXPECT_EQ(storage.BytesUsed(), used);
                    EXPECT_EQ(route.Blocks, blocks); // Snapshot owners have drained.
                    EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded));
                    if (attempt == 1)
                    {
                        EXPECT_EQ(route.Attempts, allocations); // Before first snapshot allocation.
                    }
                }
            }
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(route.Blocks, 0u);
        }
        // Check the published document's stronger candidate boundary as well.
        ZEngine::Tests::SceneTestAllocationRoute route    = {};
        const auto*                              previous = Document.Settings();
        route.Limit                                       = SIZE_MAX;
        route.RefuseBytes                                 = principal;
        trace.Calls                                       = 0;
        Diagnostics.Clear();
        EXPECT_FALSE(ECS::SceneDocument::Parse(text.data(), text.size(), Schemas, &Document, &Diagnostics, {}, &Migrations, &route));
        EXPECT_TRUE(Diagnostics.HasError(ECS::SceneDiagnosticCode::LimitExceeded));
        EXPECT_EQ(trace.Calls, 0u);
        EXPECT_EQ(Document.Settings(), previous);
        EXPECT_EQ(route.Live, 0u);
        EXPECT_EQ(route.Blocks, 0u);
        std::string after = {};
        ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
        EXPECT_EQ(after, Canonical);
    }

    TEST_F(SceneCompatibilityTest, EntireRouteIsCheckedBeforeAnyCallbackRuns)
    {
        uint32_t   calls = 0;
        const auto count = [](void* context, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node*, ECS::SceneDiagnostics*) {
            ++*static_cast<uint32_t*>(context);
            return false;
        };
        ASSERT_TRUE(Migrations.RegisterComponentMigration("NameComponent", 1, 2, &calls, count, &Diagnostics));
        Rejects(Fixture());
        EXPECT_EQ(calls, 0u);
        ECS::SceneMigrationRegistry deep = {};
        ASSERT_TRUE(deep.Initialize(&Memory.MainArena, 64));
        // Current document version is only 2, so component migration routes are
        // tested against a separate current schema with a larger version.
        ECS::SceneComponentSchemaRegistry schemas = {};
        schemas.Initialize(&Memory.MainArena);
        const auto* original = BuiltIns.FindByKey("NameComponent");
        ASSERT_TRUE(schemas.Register({.Key = original->Key, .Version = 35, .RuntimeType = original->RuntimeType, .Fields = original->Fields, .FieldCount = original->FieldCount, .Codecs = original->Codecs}, &Diagnostics));
        for (uint32_t version = 1; version < 35; ++version)
        {
            ASSERT_TRUE(deep.RegisterComponentMigration("NameComponent", version, version + 1, &calls, count, &Diagnostics));
        }
        const auto source = Fixture();
        EXPECT_FALSE(ECS::SceneDocument::Parse(source.data(), source.size(), schemas, &Document, &Diagnostics, {}, &deep));
        EXPECT_EQ(calls, 0u);
    }

    TEST_F(SceneCompatibilityTest, UnknownAndFutureComponentsStayReadOnlyAndCannotReachAnySavePath)
    {
        for (const bool future : {false, true})
        {
            const auto source = Fixture(future ? 99 : 1, 2, "nested: {list: [one, null, {x: two}]}", future ? "NameComponent" : "PluginComponent");
            ASSERT_TRUE(Parse(source));
            EXPECT_TRUE(Document.ReadOnly());
            std::string output = "previous source";
            EXPECT_FALSE(Document.Emit(&output, &Diagnostics));
            EXPECT_EQ(output, "previous source");
            EXPECT_FALSE(ECS::SceneCompatibilityService::PrepareForSave(Document, ECS::SceneCompatibilityUse::EditorSource, &Diagnostics));
            EXPECT_FALSE(ECS::SceneCompatibilityService::PrepareForSave(Document, ECS::SceneCompatibilityUse::CookedRuntime, &Diagnostics));
            ZEngine::Core::Containers::Array<uint8_t> binary = {};
            const auto                                mark   = Memory.MainArena.m_current_offset;
            EXPECT_FALSE(ECS::BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &binary, &Diagnostics));
            EXPECT_EQ(Memory.MainArena.m_current_offset, mark);
            EXPECT_TRUE(binary.empty());
            EXPECT_EQ(Candidate.AliveCount(), 0u);
        }
    }

    TEST_F(SceneCompatibilityTest, CallbackFailureBorrowedOutputAndInputMutationAreRejected)
    {
        const std::array<ECS::SceneMigrationFn, 4> failures = {
            [](void*, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
                *out = out->Owner()->Make(ECS::SceneYAML::NodeType::Map);
                return false;
            },
            [](void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
                *out = source;
                return true;
            },
            [](void*, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node*, ECS::SceneDiagnostics*) { return true; },
            [](void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
                const bool mutated = source.Owner()->Set(source, "label", source.Owner()->Scalar("mutated"));
                EXPECT_FALSE(mutated);
                *out = out->Owner()->Clone(source);
                return true;
            }};
        for (const auto callback : failures)
        {
            ECS::SceneMigrationRegistry registry = {};
            ASSERT_TRUE(registry.Initialize(&Memory.MainArena));
            ASSERT_TRUE(registry.RegisterComponentMigration("NameComponent", 1, 3, nullptr, callback, &Diagnostics));
            const auto source = Fixture();
            EXPECT_FALSE(Parse(source, {}, &registry));
            std::string after = {};
            ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
            EXPECT_EQ(after, Canonical);
        }
    }

    TEST_F(SceneCompatibilityTest, InvalidDuplicateAndOversizedMigratedPayloadsFailSharedValidation)
    {
        for (uint32_t mode = 0; mode < 5; ++mode)
        {
            ECS::SceneMigrationRegistry registry = {};
            ASSERT_TRUE(registry.Initialize(&Memory.MainArena));
            const auto invalid = [](void* context, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
                const auto mode    = *static_cast<const uint32_t*>(context);
                auto&      storage = *out->Owner();
                auto       result  = storage.Make(ECS::SceneYAML::NodeType::Map);
                if (mode == 0)
                {
                    result = storage.Scalar("not a component map");
                }
                else if (mode == 1)
                {
                    if (!storage.AppendField(result, storage.Scalar("value"), storage.Scalar("one")) || !storage.AppendField(result, storage.Scalar("value"), storage.Scalar("two")))
                    {
                        return false;
                    }
                }
                else if (mode == 2)
                {
                    if (!storage.Set(result, "value", storage.Scalar(std::string(4097, 'x'))))
                    {
                        return false;
                    }
                }
                else if (mode == 3)
                {
                    if (!storage.Set(result, "value", storage.Scalar(std::string(128, 'x'))))
                    {
                        return false; // valid tree, invalid NameComponent semantics
                    }
                }
                else if (!storage.Set(result, "unexpected", storage.Scalar("not current fields")))
                {
                    return false;
                }
                *out = result;
                return true;
            };
            ASSERT_TRUE(registry.RegisterComponentMigration("NameComponent", 1, 3, &mode, invalid, &Diagnostics));
            const auto source = Fixture();
            EXPECT_FALSE(Parse(source, {}, &registry));
            std::string after = {};
            ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
            EXPECT_EQ(after, Canonical);
        }
    }

    TEST_F(SceneCompatibilityTest, DocumentCallbacksMustEmitTheirDeclaredTargetVersion)
    {
        const auto no_upgrade = [](void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
            *out = out->Owner()->Clone(source);
            return true;
        };
        ASSERT_TRUE(Migrations.RegisterDocumentMigration(1, 2, nullptr, no_upgrade, &Diagnostics));
        Rejects(Fixture(3, 1, NameV3));
    }

    TEST_F(SceneCompatibilityTest, MigrationOutputObeysNodeDepthCollectionScalarAndMemoryBudgets)
    {
        for (uint32_t mode = 0; mode < 5; ++mode)
        {
            ECS::SceneMigrationRegistry registry = {};
            ASSERT_TRUE(registry.Initialize(&Memory.MainArena));
            const auto grow = [](void* context, const ECS::SceneYAML::Node&, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
                const auto mode    = *static_cast<const uint32_t*>(context);
                auto&      storage = *out->Owner();
                auto       result  = storage.Make(ECS::SceneYAML::NodeType::Map);
                if (mode == 0 || mode == 2 || mode == 4)
                {
                    const uint32_t count = mode == 0 ? 65 : mode == 2 ? 512 : 4000;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (!storage.Set(result, std::to_string(i), storage.Scalar("value")))
                        {
                            return false;
                        }
                    }
                }
                else if (mode == 1)
                {
                    if (!storage.Set(result, "value", storage.Scalar(std::string(513, 'x'))))
                    {
                        return false;
                    }
                }
                else
                {
                    for (uint32_t i = 0; i < 10; ++i)
                    {
                        const auto parent = storage.Make(ECS::SceneYAML::NodeType::Map);
                        if (!storage.Set(parent, "nested", result))
                        {
                            return false;
                        }
                        result = parent;
                    }
                }
                *out = result;
                return true;
            };
            ASSERT_TRUE(registry.RegisterComponentMigration("NameComponent", 1, 3, &mode, grow, &Diagnostics));
            ECS::SceneDocumentLimits limits = {};
            if (mode == 0)
            {
                limits.MaxCollectionItems = 64;
            }
            if (mode == 1)
            {
                limits.MaxScalarBytes = 512;
            }
            if (mode == 2)
            {
                limits.MaxNodes = 512;
            }
            if (mode == 3)
            {
                limits.MaxDepth = 8;
            }
            if (mode == 4)
            {
                limits.MaxDecodedBytes = ZKilo(64);
            }
            const auto source = Fixture();
            EXPECT_FALSE(Parse(source, limits, &registry));
            std::string after = {};
            ASSERT_TRUE(Document.Emit(&after, &Diagnostics));
            EXPECT_EQ(after, Canonical);
            EXPECT_EQ(Candidate.AliveCount(), 0u);
        }
    }

    TEST_F(SceneCompatibilityTest, MigratedDocumentsStillRunSharedHierarchyValidation)
    {
        const auto cycle = [](void*, const ECS::SceneYAML::Node& source, ECS::SceneYAML::Node* out, ECS::SceneDiagnostics*) {
            auto&      storage  = *out->Owner();
            const auto result   = storage.Clone(source);
            const auto envelope = storage.Make(ECS::SceneYAML::NodeType::Map);
            const auto fields   = storage.Make(ECS::SceneYAML::NodeType::Map);
            if (!storage.Set(fields, "parent_uuid", storage.UUID(EntityUUID)) || !storage.Set(envelope, "schema_version", storage.UInt(1)) || !storage.Set(envelope, "fields", fields) || !storage.Set(result["scene"]["entities"][size_t{0}]["components"], "ParentComponent", envelope) || !storage.Set(result["scene"], "schema_version", storage.UInt(2)))
            {
                return false;
            }
            *out = result;
            return true;
        };
        ASSERT_TRUE(Migrations.RegisterDocumentMigration(1, 2, nullptr, cycle, &Diagnostics));
        Rejects(Fixture(3, 1, NameV3));
    }

    TEST_F(SceneCompatibilityTest, VFSReadUsesTheSameCompatibilityBoundaryAsMemoryParsing)
    {
        RegisterSteps();
        VFS::VFSMemoryBackend backend = {};
        VFS::VFSContext       files   = {};
        backend.Initialize(&Memory.MainArena);
        files.Initialize(&Memory.MainArena);
        ASSERT_TRUE(files.Mount(&backend, VFS::VFSPath::Root(), 0).Succeeded());
        const auto path   = VFS::VFSPath::Parse("/legacy.zescene").Value();
        const auto source = Fixture();
        ASSERT_TRUE(backend.WriteFile(path, {reinterpret_cast<const uint8_t*>(source.data()), source.size()}).Succeeded());
        EXPECT_TRUE(ECS::SceneDocument::Read(files, path, Schemas, &Document, &Diagnostics, {}, &Migrations));
        EXPECT_FALSE(Document.ReadOnly());
        std::string upgraded = {};
        EXPECT_TRUE(Document.Emit(&upgraded, &Diagnostics));
        EXPECT_EQ(upgraded.find("\"label\":"), std::string::npos);
        files.Shutdown();
    }

    TEST_F(SceneCompatibilityTest, CurrentSourceNeedsNoMigrationAndSaveGateRejectsInvalidUse)
    {
        ASSERT_TRUE(Parse(Canonical));
        EXPECT_FALSE(Document.ReadOnly());
        EXPECT_TRUE(ECS::SceneCompatibilityService::PrepareForSave(Document, ECS::SceneCompatibilityUse::EditorSource, &Diagnostics));
        EXPECT_TRUE(ECS::SceneCompatibilityService::PrepareForSave(Document, ECS::SceneCompatibilityUse::CookedRuntime, &Diagnostics));
        EXPECT_FALSE(ECS::SceneCompatibilityService::PrepareForSave(Document, static_cast<ECS::SceneCompatibilityUse>(255), &Diagnostics));
        ECS::SceneDocument empty = {};
        EXPECT_FALSE(ECS::SceneCompatibilityService::PrepareForSave(empty, ECS::SceneCompatibilityUse::EditorSource, &Diagnostics));
    }

    TEST_F(SceneCompatibilityTest, ConcurrentLoadsUseImmutableRegistriesAndIndependentScratch)
    {
        RegisterSteps();
        const auto source = Fixture();
        const auto load   = [&] {
            ECS::SceneDocument    document    = {};
            ECS::SceneDiagnostics diagnostics = {};
            std::string           output      = {};
            return ECS::SceneDocument::Parse(source.data(), source.size(), Schemas, &document, &diagnostics, {}, &Migrations) && document.Emit(&output, &diagnostics) ? output : std::string{};
        };
        auto       first    = std::async(std::launch::async, load);
        auto       second   = std::async(std::launch::async, load);
        const auto expected = first.get();
        EXPECT_FALSE(expected.empty());
        EXPECT_EQ(expected, second.get());
    }
} // namespace
