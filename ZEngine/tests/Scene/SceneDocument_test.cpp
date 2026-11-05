#include <ZEngine/Core/Containers/HeapSort.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Serialization/YAMLAllocation.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <ZEngine/ECS/SceneDocumentInternal.h>
#include <ZEngine/ECS/SceneSettingsYAML.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <future>
#include <limits>
#include <vector>
#include "../TemporaryDirectory.h"
#include "SceneWorkingMemoryTestSupport.h"

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::VFS;

namespace
{
    std::string EmitTestYAML(const SceneYAML::Node& node)
    {
        SceneYAML::Emitter emitter(SceneDocumentLimits{}.MaxFileBytes);
        emitter << node;
        std::string output;
        EXPECT_TRUE(emitter.Output(&output));
        return output;
    }

    const auto SceneUUID  = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto FirstUUID  = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    const auto SecondUUID = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    const auto AssetUUID  = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");

    struct UnsupportedSceneComponent
    {
        uint32_t Value = 0;
    };

    struct FieldScratchTrace
    {
        SceneComponentSchemaRegistry* Registry    = nullptr;
        SceneDiagnostics*             Diagnostics = nullptr;
        SceneComponentCodecFns        Original    = {};
        const SceneValueField*        First       = nullptr;
        size_t                        Calls       = 0;
        bool                          Registered  = true;

        bool                          Observe(const SceneValue& value)
        {
            if (value.Fields().empty())
                return false;
            ++Calls;
            if (!First)
                First = value.Fields().data();
            return First == value.Fields().data();
        }
        static void Register(void* opaque, const SceneComponentSchema& schema)
        {
            auto&                    trace = *static_cast<FieldScratchTrace*>(opaque);
            SceneComponentSchemaDesc desc  = {.Key = schema.Key, .Version = schema.Version, .RuntimeType = schema.RuntimeType, .Fields = schema.Fields, .FieldCount = schema.FieldCount, .Codecs = schema.Codecs, .References = schema.References};
            if (std::string_view(schema.Key) == "NameComponent")
            {
                trace.Original = schema.Codecs;
                // The remaining built-in Name callbacks have unused contexts.
                EXPECT_EQ(trace.Original.Context, nullptr);
                desc.Codecs.Context = &trace;
                desc.Codecs.Capture = [](void* context, EntityID entity, const Scene& scene, SceneValueWriter* value, SceneDiagnostics* diagnostics) {
                    auto& state = *static_cast<FieldScratchTrace*>(context);
                    return state.Original.Capture(state.Original.Context, entity, scene, value, diagnostics) && state.Observe(value->Value());
                };
                desc.Codecs.DecodeYAML = [](void* context, uint32_t version, const SceneYAML::Node& source, SceneValueWriter* value, SceneDiagnostics* diagnostics) {
                    auto& state = *static_cast<FieldScratchTrace*>(context);
                    return state.Original.DecodeYAML(state.Original.Context, version, source, value, diagnostics) && state.Observe(value->Value());
                };
                desc.Codecs.DecodeBinary = [](void* context, uint32_t version, SceneBinaryReader* source, SceneValueWriter* value, SceneDiagnostics* diagnostics) {
                    auto& state = *static_cast<FieldScratchTrace*>(context);
                    return state.Original.DecodeBinary(state.Original.Context, version, source, value, diagnostics) && state.Observe(value->Value());
                };
                desc.Codecs.EncodeBinary = [](void* context, const SceneValue& value, SceneBinaryWriter* output, SceneDiagnostics* diagnostics) {
                    auto& state = *static_cast<FieldScratchTrace*>(context);
                    return state.Observe(value) && state.Original.EncodeBinary(state.Original.Context, value, output, diagnostics);
                };
            }
            trace.Registered = trace.Registry->Register(desc, trace.Diagnostics) && trace.Registered;
        }
    };

    struct SceneFaultVFS : VFSContext
    {
        bool            FailRename = false;
        VFSResult<void> Rename(const VFSPath& from, const VFSPath& to) override
        {
            return FailRename ? VFSResult<void>::Fail(VFSError::IOError) : VFSContext::Rename(from, to);
        }
    };
} // namespace

class SceneDocumentTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager Memory      = {};
    Scene                                Source      = {};
    Scene                                Candidate   = {};
    SceneComponentSchemaRegistry         Schemas     = {};
    SceneDiagnostics                     Diagnostics = {};
    SceneDocumentSettings                Settings    = {.UUID = SceneUUID, .Name = "Main Scene"};
    SceneDocument                        Document    = {};
    SceneYAML::Storage                   YAMLStorage = {};

    void                                 SetUp() override
    {
        Memory.Initialize(ZMega(128), {});
        Source.Initialize(&Memory.MainArena);
        Candidate.Initialize(&Memory.MainArena);
        Schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(YAMLStorage.Initialize());
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
    }
    void TearDown() override
    {
        Candidate.Shutdown();
        Source.Shutdown();
    }
    EntityID Add(const uuids::uuid& uuid, cstring name = "Entity")
    {
        const auto entity = Source.CreateEntity();
        Source.AddComponent<UUIDComponent>(entity, {uuid});
        Source.AddComponent<TransformComponent>(entity, {});
        NameComponent label  = {};
        const auto    length = ZEngine::Helpers::secure_strlen(name);
        EXPECT_EQ(ZEngine::Helpers::secure_strncpy(label.Value, sizeof(label.Value), name, length), ZEngine::Helpers::MEMORY_OP_SUCCESS);
        Source.AddComponent<NameComponent>(entity, label);
        return entity;
    }
    bool Capture(const SceneDocumentLimits& limits = {})
    {
        return SceneDocument::Capture(Source, Settings, Schemas, &Document, &Diagnostics, limits);
    }
    std::string Emit()
    {
        std::string result;
        EXPECT_TRUE(Document.Emit(&result, &Diagnostics));
        return result;
    }
    bool Parse(const std::string& source, const SceneDocumentLimits& limits = {})
    {
        return SceneDocument::Parse(source.data(), source.size(), Schemas, &Document, &Diagnostics, limits);
    }
    EntityID Find(const Scene& scene, const uuids::uuid& uuid)
    {
        EntityID result = {};
        scene.ForEachEntity([&](EntityID entity) {
            const auto* identity = scene.GetComponent<UUIDComponent>(entity);
            if (identity && identity->Value == uuid)
                result = entity;
        });
        return result;
    }
    SceneYAML::Node ReadTestYAML(std::string_view source)
    {
        auto node = YAMLStorage.Destination();
        EXPECT_TRUE(ReadSceneYAML(source, {}, &node, &Diagnostics));
        return node;
    }
};

TEST_F(SceneDocumentTest, EmptySnapshotRejectsSaveAndReportsNoSettings)
{
    EXPECT_EQ(Document.Settings(), nullptr);
    EXPECT_EQ(Document.EntityCount(), 0u);
    EXPECT_TRUE(Document.ReadOnly());
    std::string output = "unchanged";
    EXPECT_FALSE(Document.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, "unchanged");
}

TEST_F(SceneDocumentTest, EmptySceneIsAValidOwnedDocument)
{
    ASSERT_TRUE(Capture());
    EXPECT_FALSE(Document.ReadOnly());
    ASSERT_NE(Document.Settings(), nullptr);
    EXPECT_EQ(Document.Settings()->UUID, SceneUUID);
    const auto text = Emit();
    ASSERT_TRUE(Parse(text));
    EXPECT_EQ(Document.EntityCount(), 0u);
    EXPECT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
}

TEST_F(SceneDocumentTest, FreshSceneResolvesParentUUIDAfterCreatingEveryEntityAndRebuildsDerivedValues)
{
    // Child is created first, opposite both hierarchy and canonical UUID order.
    const auto child                                          = Add(SecondUUID, "Child");
    const auto parent                                         = Add(FirstUUID, "Parent");
    Source.GetComponent<TransformComponent>(parent)->Position = {5.f, 0.f, 0.f};
    Source.GetComponent<TransformComponent>(child)->Position  = {2.f, 3.f, 4.f};
    Source.AddComponent<ParentComponent>(child, {parent});
    Source.AddComponent<MeshComponent>(child, {AssetUUID, 12345});
    Source.SynchronizeHierarchy();
    ASSERT_TRUE(Capture());
    const auto text = Emit();
    EXPECT_EQ(text.find("render_instance_id"), std::string::npos);
    EXPECT_EQ(text.find("world_transform"), std::string::npos);
    EXPECT_EQ(text.find("previous_position"), std::string::npos);
    const auto retired = Candidate.CreateEntity();
    Candidate.DestroyEntity(retired); // Runtime generation is deliberately different.
    ASSERT_TRUE(Parse(text));
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto loaded_child  = Find(Candidate, SecondUUID);
    const auto loaded_parent = Find(Candidate, FirstUUID);
    ASSERT_TRUE(loaded_child.IsValid());
    ASSERT_TRUE(loaded_parent.IsValid());
    EXPECT_NE(loaded_parent, parent);
    EXPECT_EQ(Candidate.GetComponent<ParentComponent>(loaded_child)->Parent, loaded_parent);
    EXPECT_EQ(Candidate.GetComponent<MeshComponent>(loaded_child)->MeshUUID, AssetUUID);
    EXPECT_EQ(Candidate.GetComponent<MeshComponent>(loaded_child)->RenderInstanceId, UINT32_MAX);
    EXPECT_STREQ(Candidate.GetComponent<NameComponent>(loaded_child)->Value, "Child");
    const auto* transform = Candidate.GetComponent<TransformComponent>(loaded_child);
    EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 7.f);
    EXPECT_FLOAT_EQ(transform->PreviousPosition.y, 3.f);
    EXPECT_EQ(Emit(), text);
}

TEST_F(SceneDocumentTest, CaptureOwnsNamesAndTransformsAfterSourceMutationAndDeletion)
{
    const auto entity                                           = Add(FirstUUID, "Captured");
    Source.GetComponent<TransformComponent>(entity)->Position.x = 15.f;
    ASSERT_TRUE(Capture());
    const auto before                                           = Emit();
    Source.GetComponent<NameComponent>(entity)->Value[0]        = 'X';
    Source.GetComponent<TransformComponent>(entity)->Position.x = -20.f;
    Source.DestroyEntity(entity);
    EXPECT_EQ(Emit(), before);
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto loaded = Find(Candidate, FirstUUID);
    EXPECT_STREQ(Candidate.GetComponent<NameComponent>(loaded)->Value, "Captured");
    EXPECT_FLOAT_EQ(Candidate.GetComponent<TransformComponent>(loaded)->Position.x, 15.f);
}

TEST_F(SceneDocumentTest, CanonicalOrderDoesNotDependOnEntityCreationOrComponentMapOrder)
{
    Add(SecondUUID, "Second");
    Add(FirstUUID, "First");
    ASSERT_TRUE(Capture());
    const auto canonical = Emit();
    const auto node      = ReadTestYAML(canonical);
    EXPECT_EQ(node["scene"]["entities"][0]["uuid"].Scalar(), uuids::to_string(FirstUUID));
    EXPECT_EQ(node["scene"]["entities"][0]["components"].Mapping().front().Key, "NameComponent");
    auto reversed = node;
    auto records  = YAMLStorage.Make(SceneYAML::NodeType::Sequence);
    ASSERT_TRUE(YAMLStorage.Append(records, reversed["scene"]["entities"][1]));
    ASSERT_TRUE(YAMLStorage.Append(records, reversed["scene"]["entities"][0]));
    ASSERT_TRUE(YAMLStorage.Set(reversed["scene"], "entities", records));
    ASSERT_TRUE(Parse(EmitTestYAML(reversed)));
    EXPECT_EQ(Emit(), canonical);
}

TEST_F(SceneDocumentTest, FloatsUseShortestExactText)
{
    const auto entity                                         = Add(FirstUUID);
    Source.GetComponent<TransformComponent>(entity)->Position = {1.f, 1.5f, -2.25f};
    Source.GetComponent<TransformComponent>(entity)->Scale.x  = .1f;
    ASSERT_TRUE(Capture());
    const auto text = Emit();
    EXPECT_NE(text.find("\"position\": [1, 1.5, -2.25]"), std::string::npos);
    EXPECT_NE(text.find("\"scale\": [0.1, 1, 1]"), std::string::npos);
    EXPECT_EQ(text.find("0.100000001"), std::string::npos);
}

TEST_F(SceneDocumentTest, AmbiguousStringsAreQuotedAndRemainStrings)
{
    Settings.Name = "null";
    Add(FirstUUID, "true");
    ASSERT_TRUE(Capture());
    const auto text = Emit();
    EXPECT_NE(text.find("\"name\": \"null\""), std::string::npos);
    EXPECT_NE(text.find("\"value\": \"true\""), std::string::npos);
    ASSERT_TRUE(Parse(text));
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    EXPECT_EQ(Document.Settings()->Name, "null");
    EXPECT_STREQ(Candidate.GetComponent<NameComponent>(Find(Candidate, FirstUUID))->Value, "true");
}

TEST_F(SceneDocumentTest, SortingShuffledComponentRecordsCannotAliasOrReplaceAnotherPayload)
{
    const auto entity                                         = Add(FirstUUID, "Not a transform");
    Source.GetComponent<TransformComponent>(entity)->Position = {12.f, 13.f, 14.f};
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 31});
    ASSERT_TRUE(Capture());
    const auto canonical = Emit();
    auto       root      = ReadTestYAML(canonical);
    auto       old       = root["scene"]["entities"][0]["components"];
    auto       shuffled  = YAMLStorage.Make(SceneYAML::NodeType::Map);
    for (const auto key : {"TransformComponent", "NameComponent", "MeshComponent"})
        ASSERT_TRUE(YAMLStorage.Set(shuffled, key, old[key]));
    ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][0], "components", shuffled));
    ASSERT_TRUE(Parse(EmitTestYAML(root)));
    EXPECT_EQ(Emit(), canonical);
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto loaded = Find(Candidate, FirstUUID);
    EXPECT_STREQ(Candidate.GetComponent<NameComponent>(loaded)->Value, "Not a transform");
    EXPECT_FLOAT_EQ(Candidate.GetComponent<TransformComponent>(loaded)->Position.z, 14.f);
    EXPECT_EQ(Candidate.GetComponent<MeshComponent>(loaded)->MeshUUID, AssetUUID);
}

TEST_F(SceneDocumentTest, EntityAndComponentSortRefusalsPreserveThePriorSourceAndBinaryDocument)
{
    Add(SecondUUID, "Second");
    Add(FirstUUID, "First");
    ASSERT_TRUE(Capture());
    const auto                                canonical = Emit();
    const auto*                               original  = Document.Settings();
    ZEngine::Core::Containers::Array<uint8_t> binary    = {};
    ASSERT_TRUE(BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &binary, &Diagnostics));
    constexpr size_t component_bytes = sizeof(ZEngine::Core::Containers::HeapSortState<SceneDocumentDetail::ComponentRecord>) + sizeof(ZEngine::Core::Memory::WorkingMemoryCharge);
    constexpr size_t entity_bytes    = sizeof(ZEngine::Core::Containers::HeapSortState<SceneDocumentDetail::EntityRecord>) + sizeof(ZEngine::Core::Memory::WorkingMemoryCharge);
    static_assert(component_bytes != entity_bytes);
    for (const bool from_binary : {false, true})
        for (const size_t refused : {component_bytes, entity_bytes})
        {
            SCOPED_TRACE(from_binary);
            SCOPED_TRACE(refused);
            ZEngine::Tests::SceneTestAllocationRoute route = {};
            route.Limit                                    = SIZE_MAX;
            route.RefuseBytes                              = refused;
            Diagnostics.Clear();
            EXPECT_FALSE(from_binary ? BinarySceneCodec::Decode({binary.data(), binary.size()}, Schemas, &Document, &Diagnostics, {}, &route) : SceneDocument::Parse(canonical.data(), canonical.size(), Schemas, &Document, &Diagnostics, {}, nullptr, &route));
            EXPECT_EQ(route.Refusals, 1u);
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(route.Blocks, 0u);
            EXPECT_TRUE(Diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
            EXPECT_EQ(Document.Settings(), original);
            EXPECT_EQ(Emit(), canonical);
            // The same route succeeds once the explicit sorting refusal is lifted.
            route.RefuseBytes = 0;
            {
                SceneDocument accepted = {};
                ASSERT_TRUE(from_binary ? BinarySceneCodec::Decode({binary.data(), binary.size()}, Schemas, &accepted, &Diagnostics, {}, &route) : SceneDocument::Parse(canonical.data(), canonical.size(), Schemas, &accepted, &Diagnostics, {}, nullptr, &route));
                std::string output = {};
                ASSERT_TRUE(accepted.Emit(&output, &Diagnostics));
                EXPECT_EQ(output, canonical);
                EXPECT_EQ(route.Live, 0u);
            }
            EXPECT_EQ(route.Blocks, 0u);
        }
}

TEST(SceneTreeWalkTest, ExactDepthAndNodeLimitsIncludeKeysButNotLeafFrames)
{
    SceneYAML::Storage storage = {};
    ASSERT_TRUE(storage.Initialize(ZMega(2)));
    auto value = storage.Scalar("leaf");
    for (uint32_t level = 0; level < 32; ++level)
    {
        const auto parent = storage.Make(level % 2 ? SceneYAML::NodeType::Map : SceneYAML::NodeType::Sequence);
        ASSERT_TRUE(level % 2 ? storage.AppendField(parent, storage.Scalar("key"), value) : storage.Append(parent, value));
        value = parent;
    }
    SceneDocumentLimits limits  = {};
    limits.MaxNodes             = 49; // 32 collections, 16 keys and one scalar leaf.
    uint32_t            count   = 0;
    SceneDiagnosticCode failure = SceneDiagnosticCode::None;
    ASSERT_TRUE(SceneDocumentDetail::CountCapturedNodes(value, limits, count, 1, &failure, true));
    EXPECT_EQ(count, 49u);
    EXPECT_EQ(failure, SceneDiagnosticCode::None);
    for (const bool depth_limit : {false, true})
    {
        count        = 0;
        auto smaller = limits;
        if (depth_limit)
            --smaller.MaxDepth;
        else
            --smaller.MaxNodes;
        EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(value, smaller, count, 1, &failure));
        EXPECT_EQ(failure, SceneDiagnosticCode::LimitExceeded);
    }
    // Capture starts some payloads at depth four, rather than at the root.
    count = 0;
    EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(value, limits, count, 4, &failure));
    count = 0;
    EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(value, limits, count, UINT32_MAX, &failure));
    count = UINT32_MAX;
    EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(value, limits, count, 1, &failure));
    EXPECT_EQ(count, UINT32_MAX);
}

TEST(SceneTreeWalkTest, WideForwardTraversalHasOneReservationAndNoAllocation)
{
    ZEngine::Tests::SceneTestAllocationRoute route = {};
    route.Limit                                    = SIZE_MAX;
    route.WatchBytes                               = SceneDocumentDetail::SceneTreeWalkWorkingBytes();
    {
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(16), 1048576, 65536, 4096, &route));
        const auto sequence = storage.Make(SceneYAML::NodeType::Sequence);
        const auto scalar   = storage.Scalar("shared leaf");
        for (uint32_t i = 0; i < 65536; ++i)
            ASSERT_TRUE(storage.Append(sequence, scalar));
        const auto          attempts   = route.Attempts;
        const auto          arena_used = storage.Allocator()->m_current_offset;
        uint32_t            count      = 0;
        SceneDocumentLimits limits     = {};
        limits.MaxNodes                = 65537;
        ASSERT_TRUE(SceneDocumentDetail::CountCapturedNodes(sequence, limits, count, 1, nullptr, true));
        EXPECT_EQ(count, 65537u);
        EXPECT_EQ(route.WatchCalls, 1u);
        EXPECT_EQ(route.WatchReleases, 1u);
        EXPECT_EQ(route.Attempts, attempts);
        EXPECT_EQ(route.Live, 0u);
        EXPECT_EQ(storage.Allocator()->m_current_offset, arena_used);
        --limits.MaxCollectionItems;
        count                       = 0;
        SceneDiagnosticCode failure = SceneDiagnosticCode::None;
        EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(sequence, limits, count, 1, &failure));
        EXPECT_EQ(failure, SceneDiagnosticCode::LimitExceeded);
    }
    EXPECT_EQ(route.Blocks, 0u);
}

TEST(SceneTreeWalkTest, ExactBudgetUsesActualOwnerAndRefusesBeforeChangingCount)
{
    ZEngine::Tests::SceneTestAllocationRoute route = {}, unrelated = {};
    unrelated.Limit  = 0;
    route.WatchBytes = SceneDocumentDetail::SceneTreeWalkWorkingBytes();
    {
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(2), 1048576, 65536, 4096, &route));
        const auto root = storage.Make(SceneYAML::NodeType::Map);
        ASSERT_TRUE(storage.Set(root, "key", storage.Scalar("value")));
        const auto attempts = route.Attempts;
        const auto used     = storage.Allocator()->m_current_offset;
        for (const bool exact : {false, true})
        {
            route.Limit                                               = route.WatchBytes - (exact ? 0 : 1);
            uint32_t                                          count   = 7;
            SceneDiagnosticCode                               failure = SceneDiagnosticCode::None;
            ZEngine::Core::Serialization::YAMLAllocationScope scope(unrelated);
            EXPECT_EQ(SceneDocumentDetail::CountCapturedNodes(root, {}, count, 1, &failure, true), exact);
            EXPECT_EQ(count, exact ? 10u : 7u);
            EXPECT_EQ(failure, exact ? SceneDiagnosticCode::None : SceneDiagnosticCode::LimitExceeded);
            EXPECT_TRUE(storage.Good());
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(unrelated.Required, 0u);
            EXPECT_EQ(route.Attempts, attempts);
            EXPECT_EQ(storage.Allocator()->m_current_offset, used);
        }
        EXPECT_EQ(route.WatchCalls, 2u);
        EXPECT_EQ(route.WatchReleases, 1u);
        EXPECT_EQ(route.Peak, route.WatchBytes);
    }
    EXPECT_EQ(route.Blocks, 0u);
}

TEST(SceneTreeWalkTest, NestedDuplicatesAndInvalidScalarsKeepFailureClassification)
{
    for (uint32_t invalid = 0; invalid < 5; ++invalid)
    {
        SCOPED_TRACE(invalid);
        SceneYAML::Storage storage = {};
        ASSERT_TRUE(storage.Initialize(ZMega(2)));
        const auto root = storage.Make(SceneYAML::NodeType::Sequence);
        const auto map  = storage.Make(SceneYAML::NodeType::Map);
        ASSERT_TRUE(storage.Append(root, map));
        const auto key    = storage.Scalar("key");
        const auto scalar = storage.Scalar("value");
        ASSERT_TRUE(storage.AppendField(map, key, scalar));
        // Normal construction already rejects NUL. Corrupt owned text here
        // specifically to exercise the walk's defensive validation.
        if (invalid == 1 || invalid == 2)
            const_cast<char*>((invalid == 1 ? key : scalar).Scalar().data())[1] = '\0';
        if (invalid == 0)
            ASSERT_TRUE(storage.AppendField(map, storage.Scalar("key"), storage.Make(SceneYAML::NodeType::Null)));
        SceneDocumentLimits limits = {};
        if (invalid >= 3)
            limits.MaxScalarBytes = invalid == 3 ? 2 : 4;
        uint32_t            count   = 0;
        SceneDiagnosticCode failure = SceneDiagnosticCode::None;
        EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes(root, limits, count, 1, &failure, true));
        EXPECT_EQ(failure, invalid < 3 ? SceneDiagnosticCode::InvalidData : SceneDiagnosticCode::LimitExceeded);
    }
    uint32_t            count   = 0;
    SceneDiagnosticCode failure = SceneDiagnosticCode::None;
    EXPECT_FALSE(SceneDocumentDetail::CountCapturedNodes({}, {}, count, 1, &failure));
    EXPECT_EQ(failure, SceneDiagnosticCode::InvalidData);
}

TEST_F(SceneDocumentTest, CaptureAndSourceBinaryCodecPhasesShareOneChargedFieldBuffer)
{
    Add(SecondUUID, "Second");
    Add(FirstUUID, "First");
    ASSERT_TRUE(Capture());
    const auto                                canonical = Emit();
    ZEngine::Core::Containers::Array<uint8_t> binary    = {};
    ASSERT_TRUE(BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &binary, &Diagnostics));
    SceneComponentSchemaRegistry traced = {};
    traced.Initialize(&Memory.MainArena);
    FieldScratchTrace trace = {.Registry = &traced, .Diagnostics = &Diagnostics};
    Schemas.ForEachCanonical(FieldScratchTrace::Register, &trace);
    ASSERT_TRUE(trace.Registered);
    for (uint32_t operation = 0; operation < 5; ++operation)
    {
        SCOPED_TRACE(operation);
        ZEngine::Tests::SceneTestAllocationRoute route = {};
        route.Limit                                    = SIZE_MAX;
        route.WatchBytes                               = SceneDocumentDetail::SceneFieldScratchWorkingBytes();
        trace.First                                    = nullptr;
        trace.Calls                                    = 0;
        {
            ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
            SceneDocument                                     accepted = {};
            switch (operation)
            {
                case 0:
                    ASSERT_TRUE(SceneDocument::Capture(Source, Settings, traced, &accepted, &Diagnostics));
                    break;
                case 1:
                    ASSERT_TRUE(SceneDocument::Parse(canonical.data(), canonical.size(), traced, &accepted, &Diagnostics, {}, nullptr, &route));
                    break;
                case 2:
                    ASSERT_TRUE(BinarySceneCodec::Decode({binary.data(), binary.size()}, traced, &accepted, &Diagnostics, {}, &route));
                    break;
                case 3:
                {
                    size_t measured = 0;
                    ASSERT_TRUE(BinarySceneCodec::Measure(Document, traced, &measured, &Diagnostics));
                    EXPECT_EQ(measured, binary.size());
                    break;
                }
                case 4:
                {
                    ZEngine::Core::Containers::Array<uint8_t> encoded = {};
                    ASSERT_TRUE(BinarySceneCodec::Encode(Document, traced, &Memory.MainArena, &encoded, &Diagnostics));
                    EXPECT_EQ(encoded.size(), binary.size());
                    EXPECT_TRUE(std::equal(encoded.begin(), encoded.end(), binary.begin()));
                    break;
                }
            }
            EXPECT_EQ(route.Live, 0u);
            if (operation < 3)
            {
                std::string text = {};
                ASSERT_TRUE(accepted.Emit(&text, &Diagnostics));
                EXPECT_EQ(text, canonical);
                EXPECT_EQ(route.WatchCalls, 1u);
                EXPECT_EQ(route.WatchReleases, 1u);
            }
        }
        EXPECT_EQ(route.Blocks, 0u);
        EXPECT_EQ(trace.Calls, operation == 0 || operation == 2 ? 6u : operation == 4 ? 8u : 4u);
    }
}

TEST_F(SceneDocumentTest, FieldAndGraphPrincipalRefusalsPreserveSourceAndBinaryDocuments)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto                                canonical = Emit();
    const auto*                               original  = Document.Settings();
    ZEngine::Core::Containers::Array<uint8_t> binary    = {};
    ASSERT_TRUE(BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &binary, &Diagnostics));
    ZEngine::Tests::SceneTestAllocationRoute probe = {};
    probe.Limit                                    = SIZE_MAX;
    {
        SceneDocument accepted = {};
        ASSERT_TRUE(BinarySceneCodec::Decode({binary.data(), binary.size()}, Schemas, &accepted, &Diagnostics, {}, &probe));
    }
    const auto graph_bytes = probe.Required; // Graph validation is the final reservation.
    ASSERT_NE(graph_bytes, SceneDocumentDetail::SceneFieldScratchWorkingBytes());
    EXPECT_EQ(probe.Blocks, 0u);
    for (const bool from_binary : {false, true})
        for (const size_t refused : {SceneDocumentDetail::SceneFieldScratchWorkingBytes(), graph_bytes})
        {
            SCOPED_TRACE(from_binary);
            SCOPED_TRACE(refused);
            ZEngine::Tests::SceneTestAllocationRoute route = {};
            route.Limit                                    = SIZE_MAX;
            route.RefuseBytes                              = refused;
            Diagnostics.Clear();
            EXPECT_FALSE(from_binary ? BinarySceneCodec::Decode({binary.data(), binary.size()}, Schemas, &Document, &Diagnostics, {}, &route) : SceneDocument::Parse(canonical.data(), canonical.size(), Schemas, &Document, &Diagnostics, {}, nullptr, &route));
            EXPECT_EQ(route.Refusals, 1u);
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(route.Blocks, 0u);
            if (from_binary && refused == SceneDocumentDetail::SceneFieldScratchWorkingBytes())
                EXPECT_EQ(route.Attempts, 0u);
            EXPECT_TRUE(Diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
            EXPECT_EQ(Document.Settings(), original);
            EXPECT_EQ(Emit(), canonical);
        }
}

TEST_F(SceneDocumentTest, BinaryFieldAndLayoutExactBudgetPreservesMeasuredSizeOutputAndArenaOnRefusal)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    size_t required = 0;
    for (const bool encoding : {false, true})
        for (uint32_t attempt = 0; attempt < 3; ++attempt)
        {
            ZEngine::Tests::SceneTestAllocationRoute route     = {};
            route.Limit                                        = attempt == 0 ? 0 : required - (attempt == 1 ? 1 : 0);
            size_t                                    measured = SIZE_MAX;
            ZEngine::Core::Containers::Array<uint8_t> encoded  = {};
            const auto                                used     = Memory.MainArena.m_current_offset;
            Diagnostics.Clear();
            {
                ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
                EXPECT_EQ(encoding ? BinarySceneCodec::Encode(Document, Schemas, &Memory.MainArena, &encoded, &Diagnostics) : BinarySceneCodec::Measure(Document, Schemas, &measured, &Diagnostics), attempt == 2);
            }
            if (attempt == 0)
                required = route.Required;
            EXPECT_GT(required, SceneDocumentDetail::SceneFieldScratchWorkingBytes());
            EXPECT_EQ(route.Required, required);
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(route.Attempts, 0u);
            if (attempt < 2)
            {
                EXPECT_TRUE(Diagnostics.HasError(SceneDiagnosticCode::LimitExceeded));
                EXPECT_EQ(measured, SIZE_MAX);
                EXPECT_TRUE(encoded.empty());
                EXPECT_EQ(Memory.MainArena.m_current_offset, used);
            }
            else
                EXPECT_EQ(route.Peak, required);
        }
}

TEST_F(SceneDocumentTest, ReferenceFieldBudgetRefusesBeforeVisitorsAndDrainsAfterRetry)
{
    const auto entity = Add(FirstUUID);
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 31});
    ASSERT_TRUE(Capture());
    for (const bool all : {false, true})
        for (const bool refused : {true, false})
        {
            ZEngine::Tests::SceneTestAllocationRoute route = {};
            route.Limit                                    = refused ? 0 : SIZE_MAX;
            size_t calls                                   = 0;
            Diagnostics.Clear();
            {
                ZEngine::Core::Serialization::YAMLAllocationScope scope(route);
                EXPECT_EQ(
                    all ? Document.EnumerateReferences(
                              Schemas,
                              [](void* context, const SceneReferenceOccurrence&) {
                                  ++*static_cast<size_t*>(context);
                                  return true;
                              },
                              &calls,
                              &Diagnostics)
                        : Document.EnumerateComponentReferences(
                              ComponentTypeOf<MeshComponent>(),
                              Schemas,
                              [](void* context, const SceneReference&) {
                                  ++*static_cast<size_t*>(context);
                                  return true;
                              },
                              &calls,
                              &Diagnostics),
                    !refused);
            }
            EXPECT_EQ(calls, refused ? 0u : 1u);
            EXPECT_EQ(route.Live, 0u);
            EXPECT_EQ(route.Attempts, 0u);
            EXPECT_EQ(Diagnostics.HasError(SceneDiagnosticCode::LimitExceeded), refused);
        }
}

TEST_F(SceneDocumentTest, CaptureHonorsNodeDepthScalarAndCollectionBudgetsBeforeRetainingWholeScene)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    auto       limits   = SceneDocumentLimits{};
    limits.MaxNodes     = 10;
    EXPECT_FALSE(Capture(limits));
    limits          = {};
    limits.MaxDepth = 3;
    EXPECT_FALSE(Capture(limits));
    limits                = {};
    limits.MaxScalarBytes = 10;
    EXPECT_FALSE(Capture(limits));
    limits                    = {};
    limits.MaxCollectionItems = 2;
    EXPECT_FALSE(Capture(limits));
    EXPECT_EQ(Emit(), original);
}

TEST_F(SceneDocumentTest, DecodedMemoryBudgetRejectsCaptureAndParseWithoutReplacingTheSnapshot)
{
    Add(FirstUUID, "Retained");
    ASSERT_TRUE(Capture());
    const auto original    = Emit();
    auto       limits      = SceneDocumentLimits{};
    limits.MaxDecodedBytes = 4096;
    EXPECT_FALSE(Capture(limits));
    EXPECT_FALSE(Parse(original, limits));
    EXPECT_EQ(Emit(), original);
    limits.MaxDecodedBytes = 0;
    EXPECT_FALSE(Capture(limits));
    EXPECT_FALSE(Parse(original, limits));
    limits.MaxDecodedBytes = ZMega(256) + 1;
    EXPECT_FALSE(Capture(limits));
    EXPECT_FALSE(Parse(original, limits));
    EXPECT_EQ(Emit(), original);
}

TEST_F(SceneDocumentTest, MovingSnapshotKeepsBorrowedComponentViewsAndPayloadOwnersStable)
{
    Add(FirstUUID, "Moved");
    ASSERT_TRUE(Capture());
    const auto    original = Emit();
    SceneDocument moved(std::move(Document));
    EXPECT_EQ(Document.EntityCount(), 0u);
    Document = std::move(moved);
    EXPECT_EQ(Emit(), original);
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto entity = Find(Candidate, FirstUUID);
    ASSERT_TRUE(entity.IsValid());
    EXPECT_STREQ(Candidate.GetComponent<NameComponent>(entity)->Value, "Moved");
}

TEST_F(SceneDocumentTest, FiniteLocalValuesThatOverflowAcrossParentFramesCannotBeSaved)
{
    const auto parent                                      = Add(FirstUUID);
    const auto child                                       = Add(SecondUUID);
    Source.GetComponent<TransformComponent>(parent)->Scale = {1.e30f, 1.e30f, 1.e30f};
    Source.GetComponent<TransformComponent>(child)->Scale  = {1.e30f, 1.e30f, 1.e30f};
    Source.AddComponent<ParentComponent>(child, {parent});
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Document.Settings(), nullptr);
}

TEST_F(SceneDocumentTest, MissingNilDuplicateAndUnterminatedIdentityOrNamesRejectCaptureWithoutReplacingSnapshot)
{
    const auto first = Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    const auto second   = Add(FirstUUID);
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Emit(), original);
    Source.GetComponent<UUIDComponent>(second)->Value = {};
    EXPECT_FALSE(Capture());
    Source.RemoveComponent<UUIDComponent>(second);
    EXPECT_FALSE(Capture());
    Source.DestroyEntity(second);
    auto* name = Source.GetComponent<NameComponent>(first);
    std::fill(std::begin(name->Value), std::end(name->Value), 'x');
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Emit(), original);
}

TEST_F(SceneDocumentTest, UnknownRuntimeComponentsCannotBeSilentlyDroppedOnSave)
{
    const auto entity = Add(FirstUUID);
    Source.AddComponent<UnsupportedSceneComponent>(entity, {});
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Document.Settings(), nullptr);
    EXPECT_TRUE(Diagnostics.HasErrors());
}

TEST_F(SceneDocumentTest, MaterialAndRigidBodyRoundTripWithStableReferencesAndNoRuntimeBodyIdentity)
{
    const auto entity = Add(FirstUUID);
    Source.AddComponent<MaterialComponent>(entity, {AssetUUID});
    Source.AddComponent<RigidBodyComponent>(entity, {.MotionKind = RigidBodyComponent::MotionType::Kinematic, .Mass = 7.5f, .Friction = 0.25f, .Restitution = 0.75f, .BodyID = 99});
    ASSERT_TRUE(Capture());
    const auto text = Emit();
    EXPECT_NE(text.find("MaterialComponent"), std::string::npos);
    EXPECT_NE(text.find("RigidBodyComponent"), std::string::npos);
    EXPECT_EQ(text.find("body_id"), std::string::npos);
    EXPECT_EQ(text.find("BodyID"), std::string::npos);
    ASSERT_TRUE(Parse(text));
    EXPECT_FALSE(Document.ReadOnly());
    SceneReference reference = {};
    ASSERT_TRUE(Document.EnumerateComponentReferences(
        ComponentTypeOf<MaterialComponent>(),
        Schemas,
        [](void* context, const SceneReference& item) {
            *static_cast<SceneReference*>(context) = item;
            return true;
        },
        &reference,
        &Diagnostics));
    EXPECT_EQ(reference.Kind, SceneReferenceKind::Asset);
    EXPECT_EQ(reference.UUID, AssetUUID);
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto loaded = Find(Candidate, FirstUUID);
    ASSERT_TRUE(loaded.IsValid());
    const auto* material = Candidate.GetComponent<MaterialComponent>(loaded);
    const auto* body     = Candidate.GetComponent<RigidBodyComponent>(loaded);
    ASSERT_NE(material, nullptr);
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(material->MaterialUUID, AssetUUID);
    EXPECT_EQ(body->MotionKind, RigidBodyComponent::MotionType::Kinematic);
    EXPECT_FLOAT_EQ(body->Mass, 7.5f);
    EXPECT_FLOAT_EQ(body->Friction, 0.25f);
    EXPECT_FLOAT_EQ(body->Restitution, 0.75f);
    EXPECT_EQ(body->BodyID, UINT32_MAX);
    EXPECT_EQ(Emit(), text);
}

TEST_F(SceneDocumentTest, InvalidMaterialOrRigidBodyCannotReplaceAValidCapturedDocument)
{
    const auto entity = Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    Source.AddComponent<MaterialComponent>(entity, {});
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Emit(), original);
    Source.RemoveComponent<MaterialComponent>(entity);
    Source.AddComponent<RigidBodyComponent>(entity, {.Mass = 0.f});
    EXPECT_FALSE(Capture());
    EXPECT_EQ(Emit(), original);
}

TEST_F(SceneDocumentTest, NonFiniteFieldsAndInvalidOrStaleHierarchyRejectCapture)
{
    const auto first  = Add(FirstUUID);
    const auto second = Add(SecondUUID);
    Source.AddComponent<ParentComponent>(first, {second});
    Source.AddComponent<ParentComponent>(second, {first});
    EXPECT_FALSE(Capture());
    Source.RemoveComponent<ParentComponent>(second);
    Source.DestroyEntity(second);
    EXPECT_FALSE(Capture());
    Source.RemoveComponent<ParentComponent>(first);
    Source.GetComponent<TransformComponent>(first)->Scale.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Capture());
}

TEST_F(SceneDocumentTest, ParseFailureRetainsPreviousSnapshotAndDoesNotTouchCurrentScene)
{
    const auto entity = Add(FirstUUID, "Original");
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    EXPECT_FALSE(Parse("scene: [unterminated"));
    EXPECT_EQ(Emit(), original);
    EXPECT_STREQ(Source.GetComponent<NameComponent>(entity)->Value, "Original");
    EXPECT_EQ(Source.AliveCount(), 1u);
    EXPECT_EQ(Candidate.AliveCount(), 0u);
}

TEST_F(SceneDocumentTest, DuplicateYamlKeysAliasesAnchorsCustomTagsAndMultipleDocumentsAreRejected)
{
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    for (const auto* malformed : {"scene: {}\nscene: {}", "scene: &scene {}", "scene: *missing", "scene: !custom {}", "scene: {}\n---\nscene: {}", "? [not, scalar]\n: {}"})
    {
        SCOPED_TRACE(malformed);
        EXPECT_FALSE(Parse(malformed));
        EXPECT_EQ(Emit(), original);
    }
}

TEST_F(SceneDocumentTest, ParserBoundsFileNodesDepthCollectionsScalarsEntitiesAndComponentCounts)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto source   = Emit();
    auto       limits   = SceneDocumentLimits{};
    limits.MaxFileBytes = source.size() - 1;
    EXPECT_FALSE(Parse(source, limits));
    limits          = {};
    limits.MaxNodes = 5;
    EXPECT_FALSE(Parse(source, limits));
    limits          = {};
    limits.MaxDepth = 3;
    EXPECT_FALSE(Parse(source, limits));
    limits                    = {};
    limits.MaxCollectionItems = 2;
    EXPECT_FALSE(Parse(source, limits));
    limits                = {};
    limits.MaxScalarBytes = 8;
    EXPECT_FALSE(Parse(source, limits));
    limits             = {};
    limits.MaxEntities = 0;
    EXPECT_FALSE(Parse(source, limits));
    limits                        = {};
    limits.MaxComponentsPerEntity = 1;
    EXPECT_FALSE(Parse(source, limits));
    limits.MaxDepth = 100000;
    EXPECT_FALSE(Parse(source, limits));
}

TEST_F(SceneDocumentTest, DuplicateUUIDsMalformedFieldsAndUnresolvedParentsAreRejected)
{
    Add(FirstUUID);
    Add(SecondUUID);
    ASSERT_TRUE(Capture());
    const auto source = Emit();
    auto       root   = ReadTestYAML(source);
    ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][1], "uuid", YAMLStorage.UUID(FirstUUID)));
    EXPECT_FALSE(Parse(EmitTestYAML(root)));
    root = ReadTestYAML(source);
    ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][0]["components"]["TransformComponent"]["fields"], "position", YAMLStorage.Scalar("not a vector")));
    EXPECT_FALSE(Parse(EmitTestYAML(root)));
    root        = ReadTestYAML(source);
    auto parent = YAMLStorage.Make(SceneYAML::NodeType::Map);
    auto fields = YAMLStorage.Make(SceneYAML::NodeType::Map);
    ASSERT_TRUE(YAMLStorage.Set(parent, "schema_version", YAMLStorage.UInt(1)));
    ASSERT_TRUE(YAMLStorage.Set(fields, "parent_uuid", YAMLStorage.UUID(AssetUUID)));
    ASSERT_TRUE(YAMLStorage.Set(parent, "fields", fields));
    ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][0]["components"], "ParentComponent", parent));
    EXPECT_FALSE(Parse(EmitTestYAML(root)));
}

TEST_F(SceneDocumentTest, UnknownComponentsAndVersionsAreReadOnlyAndCannotOverwriteSavedSource)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto source = Emit();
    for (const bool unknown_key : {true, false})
    {
        SCOPED_TRACE(unknown_key);
        auto       root      = ReadTestYAML(source);
        const auto key       = unknown_key ? "FutureComponent" : "TransformComponent";
        auto       component = YAMLStorage.Make(SceneYAML::NodeType::Map);
        auto       fields    = YAMLStorage.Make(SceneYAML::NodeType::Map);
        ASSERT_TRUE(YAMLStorage.Set(component, "schema_version", YAMLStorage.UInt(999)));
        ASSERT_TRUE(YAMLStorage.Set(fields, "future", YAMLStorage.Scalar("preserved only for inspection")));
        ASSERT_TRUE(YAMLStorage.Set(component, "fields", fields));
        ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][0]["components"], key, component));
        ASSERT_TRUE(Parse(EmitTestYAML(root))) << (Diagnostics.Count ? Diagnostics.Entries[Diagnostics.Count - 1].Message : "no diagnostic");
        EXPECT_TRUE(Document.ReadOnly());
        std::string output = "old file";
        EXPECT_FALSE(Document.Emit(&output, &Diagnostics));
        EXPECT_EQ(output, "old file");
    }
}

TEST_F(SceneDocumentTest, CandidateMustBeUnpublishedAndEmpty)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto existing = Candidate.CreateEntity();
    EXPECT_FALSE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    EXPECT_EQ(Candidate.AliveCount(), 1u);
    EXPECT_TRUE(Candidate.IsAlive(existing));
}

TEST_F(SceneDocumentTest, InsufficientCandidateArenaRejectsBeforeCreatingEntitiesOrComponents)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    const auto available = Memory.MainArena.m_total_size - Memory.MainArena.m_current_offset;
    ASSERT_GT(available, 32768u);
    ASSERT_NE(Memory.MainArena.AllocateNoZero(available - 32768), nullptr);
    const auto position = Memory.MainArena.m_current_offset;
    EXPECT_FALSE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    EXPECT_EQ(Candidate.AliveCount(), 0u);
    EXPECT_EQ(Memory.MainArena.m_current_offset, position);
    EXPECT_EQ(Source.AliveCount(), 1u);
}

TEST_F(SceneDocumentTest, GridRoundTripNormalizesOrientationAndPreservesStyleAndSky)
{
    Settings.Grid.Origin                    = {4.f, 5.f, 6.f};
    Settings.Grid.Orientation               = {0.f, 0.f, -2.f, -2.f};
    Settings.Grid.BaseSpacing               = .125f;
    Settings.Grid.MajorLineEvery            = 8;
    Settings.Grid.Labels                    = SceneGridLabels::UV;
    Settings.Grid.UColor                    = {.1f, .2f, .3f, .4f};
    Settings.Sky.EnvironmentMap             = AssetUUID;
    Settings.Sky.EnvironmentIntensity       = 2.5f;
    Settings.Sky.Atmosphere.GroundAlbedo[1] = .42f;
    Settings.Sky.Sphere.ShowSunDisc         = false;
    ASSERT_TRUE(Capture());
    const auto source = Emit();
    ASSERT_TRUE(Parse(source));
    const auto& value = *Document.Settings();
    EXPECT_FLOAT_EQ(value.Grid.Origin.y, 5.f);
    EXPECT_FLOAT_EQ(value.Grid.Orientation.z, std::sqrt(.5f));
    EXPECT_GT(value.Grid.Orientation.w, 0.f);
    EXPECT_FLOAT_EQ(value.Grid.BaseSpacing, .125f);
    EXPECT_EQ(value.Grid.MajorLineEvery, 8u);
    EXPECT_EQ(value.Grid.UColor, Settings.Grid.UColor);
    EXPECT_EQ(value.Grid.Labels, SceneGridLabels::UV);
    EXPECT_EQ(value.Sky.EnvironmentMap, AssetUUID);
    EXPECT_FLOAT_EQ(value.Sky.Atmosphere.GroundAlbedo[1], .42f);
    EXPECT_FALSE(value.Sky.Sphere.ShowSunDisc);
    EXPECT_EQ(Emit(), source);
}

TEST_F(SceneDocumentTest, InvalidSettingsRejectRatherThanSanitizeAuthoredData)
{
    Settings.Grid.Orientation = {0.f, 0.f, 0.f, 0.f};
    EXPECT_FALSE(Capture());
    Settings.Grid             = {};
    Settings.Grid.BaseSpacing = 0.f;
    EXPECT_FALSE(Capture());
    Settings.Grid        = {};
    Settings.Grid.Labels = static_cast<SceneGridLabels>(99);
    EXPECT_FALSE(Capture());
    Settings.Grid                                    = {};
    Settings.Sky.Atmosphere.MieScaleHeightKilometers = -1.f;
    EXPECT_FALSE(Capture());
}

TEST_F(SceneDocumentTest, SaveEmissionStopsAtFileBudgetWithoutReplacingCallerOutput)
{
    Add(FirstUUID);
    auto limits         = SceneDocumentLimits{};
    limits.MaxFileBytes = 128;
    ASSERT_TRUE(Capture(limits));
    std::string output = "previous saved source";
    EXPECT_FALSE(Document.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, "previous saved source");
}

TEST_F(SceneDocumentTest, RepeatedConcurrentEmissionReadsOnlyImmutableOwnedPayloads)
{
    const auto entity = Add(FirstUUID, "Stable");
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 52});
    ASSERT_TRUE(Capture());
    const auto expected       = Emit();
    const auto arena_position = Memory.MainArena.m_current_offset;
    const auto emit_many      = [&] {
        SceneDiagnostics diagnostics = {};
        for (uint32_t i = 0; i < 100; ++i)
        {
            std::string output;
            if (!Document.Emit(&output, &diagnostics) || output != expected)
                return false;
        }
        return true;
    };
    auto first  = std::async(std::launch::async, emit_many);
    auto second = std::async(std::launch::async, emit_many);
    EXPECT_TRUE(first.get());
    EXPECT_TRUE(second.get());
    EXPECT_EQ(Memory.MainArena.m_current_offset, arena_position);
    EXPECT_EQ(Emit(), expected);
}

TEST_F(SceneDocumentTest, ReverseCreatedDeepHierarchyRoundTripsWithoutRecursiveParentWalks)
{
    constexpr uint32_t    count = 4096;
    std::vector<EntityID> entities;
    entities.reserve(count);
    for (uint32_t i = count; i > 0; --i)
    {
        std::array<uint8_t, 16> bytes                               = {};
        bytes[0]                                                    = 1;
        bytes[14]                                                   = static_cast<uint8_t>(i >> 8);
        bytes[15]                                                   = static_cast<uint8_t>(i);
        const auto entity                                           = Add(uuids::uuid(bytes), "Deep");
        Source.GetComponent<TransformComponent>(entity)->Position.x = 1.f;
        entities.push_back(entity);
    }
    for (uint32_t i = 0; i + 1 < count; ++i)
        Source.AddComponent<ParentComponent>(entities[i], {entities[i + 1]});
    ASSERT_TRUE(Capture());
    const auto source = Emit();
    ASSERT_TRUE(Parse(source));
    ASSERT_TRUE(Document.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    EXPECT_EQ(Candidate.AliveCount(), count);
    const auto uuid = Source.GetComponent<UUIDComponent>(entities[0])->Value;
    const auto leaf = Find(Candidate, uuid);
    ASSERT_TRUE(leaf.IsValid());
    EXPECT_FLOAT_EQ(Candidate.GetComponent<TransformComponent>(leaf)->WorldTransform(0, 3), static_cast<float>(count));
}

TEST_F(SceneDocumentTest, GridCanonicalizationHandlesHalfTurnsNegativeZeroAndExtremeFiniteMagnitudes)
{
    SceneGridSettings value = {};
    value.Orientation       = {-2.f, -0.f, 0.f, 0.f};
    ASSERT_TRUE(CanonicalizeSceneGridSettings(value));
    EXPECT_FLOAT_EQ(value.Orientation.x, 1.f);
    EXPECT_FALSE(std::signbit(value.Orientation.y));
    value.Orientation = {std::numeric_limits<float>::max(), 0.f, 0.f, 0.f};
    ASSERT_TRUE(CanonicalizeSceneGridSettings(value));
    EXPECT_FLOAT_EQ(value.Orientation.x, 1.f);
    value.Orientation = {0.f, 0.f, 0.f, 0.f};
    EXPECT_FALSE(CanonicalizeSceneGridSettings(value));
    EXPECT_FLOAT_EQ(value.Orientation.w, 0.f);
}

TEST(SceneSettingsComparisonTest, GridComparesEveryAuthoredFieldExactlyButIgnoresUnusedVectorLane)
{
    const SceneGridSettings original = {};
    const auto              check    = [&](auto change) {
        auto other = original;
        change(other);
        EXPECT_FALSE(EqualSceneGridSettings(original, other));
        EXPECT_FALSE(EqualSceneGridSettings(other, original));
    };
    EXPECT_TRUE(EqualSceneGridSettings(original, {}));
    check([](auto& v) { v.Enabled = !v.Enabled; });
    for (uint32_t i = 0; i < 3; ++i)
        check([&](auto& v) { v.Origin[i] += 1.f; });
    check([](auto& v) { v.Orientation.x += .1f; });
    check([](auto& v) { v.Orientation.y += .1f; });
    check([](auto& v) { v.Orientation.z += .1f; });
    check([](auto& v) { v.Orientation.w += .1f; });
    check([](auto& v) { v.BaseSpacing = std::nextafter(v.BaseSpacing, 1.f); });
    check([](auto& v) { ++v.MajorLineEvery; });
    for (uint32_t i = 0; i < 4; ++i)
    {
        check([&](auto& v) { v.MinorColor[i] += .1f; });
        check([&](auto& v) { v.MajorColor[i] += .1f; });
        check([&](auto& v) { v.UColor[i] += .1f; });
        check([&](auto& v) { v.VColor[i] += .1f; });
    }
    check([](auto& v) { v.LineWidth += 1.f; });
    check([](auto& v) { v.FadeRadius += 1.f; });
    check([](auto& v) { v.FadeStrength += 1.f; });
    check([](auto& v) { ++v.MaxLOD; });
    check([](auto& v) { v.Labels = SceneGridLabels::UV; });
    auto other     = original;
    other.Origin.w = std::numeric_limits<float>::quiet_NaN();
    EXPECT_TRUE(EqualSceneGridSettings(original, other));
}

TEST(SceneGridBinaryTest, RoundTripsEveryAuthoredFieldWithoutNativePaddingOrUnusedLanes)
{
    SceneGridSettings value = {};
    value.Enabled           = false;
    value.Origin            = {1.f, -2.f, 3.f};
    value.Origin.w          = std::numeric_limits<float>::quiet_NaN();
    value.Orientation       = {-.2f, .3f, -.4f, -.5f};
    value.BaseSpacing       = .125f;
    value.MajorLineEvery    = 0x12345678;
    value.MinorColor        = {.1f, .2f, .3f, .4f};
    value.MajorColor        = {.5f, .6f, .7f, .8f};
    value.UColor            = {1.f, .9f, .8f, .7f};
    value.VColor            = {.6f, .5f, .4f, .3f};
    value.LineWidth         = 2.5f;
    value.FadeRadius        = 1024.f;
    value.FadeStrength      = 1.25f;
    value.MaxLOD            = 16;
    value.Labels            = SceneGridLabels::UV;
    SceneGridBinary bytes   = {};
    ASSERT_TRUE(EncodeSceneGridBinary(value, bytes));
    ASSERT_EQ(bytes.size(), 128u);
    EXPECT_EQ(bytes[0], std::byte{1});     // schema version
    EXPECT_EQ(bytes[4], std::byte{0});     // enabled encoded as uint32
    EXPECT_EQ(bytes[10], std::byte{0x80}); // IEEE little-endian 1.0f
    EXPECT_EQ(bytes[11], std::byte{0x3f});
    EXPECT_EQ(bytes[40], std::byte{0x78});
    EXPECT_EQ(bytes[41], std::byte{0x56});
    EXPECT_EQ(bytes[42], std::byte{0x34});
    EXPECT_EQ(bytes[43], std::byte{0x12});
    SceneGridSettings output = {};
    ASSERT_TRUE(DecodeSceneGridBinary(bytes, output));
    ASSERT_TRUE(CanonicalizeSceneGridSettings(value));
    EXPECT_TRUE(EqualSceneGridSettings(value, output));
    EXPECT_FLOAT_EQ(output.Origin.w, 0.f);
    SceneYAML::Storage storage = {};
    ASSERT_TRUE(storage.Initialize());
    auto yaml = storage.Make(SceneYAML::NodeType::Null);
    ASSERT_TRUE(EncodeSceneGridYAML(output, &yaml, nullptr));
    SceneGridSettings loaded = {};
    ASSERT_TRUE(DecodeSceneGridYAML(yaml, &loaded, nullptr));
    EXPECT_TRUE(EqualSceneGridSettings(output, loaded));
}

TEST(SceneGridBinaryTest, EncoderRejectsInvalidValuesAndStorageWithoutWritingAnyByte)
{
    ZEngine::Core::Containers::FixedArray<std::byte, SceneGridBinaryBytes + 1> storage = {};
    for (auto& byte : storage)
        byte = std::byte{0x5a};
    const auto sentinel = storage;
    EXPECT_FALSE(EncodeSceneGridBinary({}, {nullptr, SceneGridBinaryBytes}));
    for (size_t size = 0; size <= storage.size(); ++size)
        if (size != SceneGridBinaryBytes)
            EXPECT_FALSE(EncodeSceneGridBinary({}, {storage.data(), size}));
    for (uint32_t fault = 0; fault < 8; ++fault)
    {
        SceneGridSettings value = {};
        if (fault == 0)
            value.Origin.x = std::numeric_limits<float>::infinity();
        if (fault == 1)
            value.Orientation = {0.f, 0.f, 0.f, 0.f};
        if (fault == 2)
            value.BaseSpacing = 0.f;
        if (fault == 3)
            value.MajorLineEvery = 0;
        if (fault == 4)
            value.MaxLOD = 17;
        if (fault == 5)
            value.Labels = SceneGridLabels(2);
        if (fault == 6)
            value.MinorColor[2] = -1.f;
        if (fault == 7)
            value.FadeRadius = std::numeric_limits<float>::quiet_NaN();
        EXPECT_FALSE(EncodeSceneGridBinary(value, {storage.data(), SceneGridBinaryBytes}));
        EXPECT_TRUE(storage == sentinel);
    }
}

TEST(SceneGridBinaryTest, DecoderRejectsEveryTruncationAndMalformedScalarWithoutChangingOutput)
{
    SceneGridBinary valid = {};
    ASSERT_TRUE(EncodeSceneGridBinary({}, valid));
    SceneGridSettings output = {};
    output.Origin            = {7.f, 8.f, 9.f};
    output.Origin.w          = 11.f;
    const auto sentinel      = output;
    const auto preserved     = [&] {
        EXPECT_TRUE(EqualSceneGridSettings(output, sentinel));
        EXPECT_FLOAT_EQ(output.Origin.w, 11.f);
    };
    for (size_t size = 0; size < valid.size(); ++size)
    {
        EXPECT_FALSE(DecodeSceneGridBinary({valid.data(), size}, output));
        preserved();
    }
    EXPECT_FALSE(DecodeSceneGridBinary({nullptr, valid.size()}, output));
    EXPECT_FALSE(DecodeSceneGridBinary({valid.data(), valid.size() + 1}, output));
    for (uint32_t word = 0; word < 32; ++word)
    {
        auto     bytes   = valid;
        uint32_t invalid = 0x7fc00000u; // non-finite float, or invalid enum/flag
        if (word == 0 || word == 1 || word == 31)
            invalid = 2;
        if (word == 10)
            invalid = 0; // major cadence
        if (word == 30)
            invalid = 17; // maximum LOD
        for (uint32_t i = 0; i < 4; ++i)
            bytes[word * 4 + i] = std::byte((invalid >> (8 * i)) & 0xffu);
        EXPECT_FALSE(DecodeSceneGridBinary(bytes, output)) << word;
        preserved();
    }
    auto bytes = valid;
    for (uint32_t i = 20; i < 36; ++i)
        bytes[i] = std::byte{};
    EXPECT_FALSE(DecodeSceneGridBinary(bytes, output));
    preserved();
}

TEST(SceneGridBinaryTest, CanonicalBytesRemainStableAcrossRepeatedQuaternionRoundTrips)
{
    uint32_t   state = 0x764ad381;
    const auto next  = [&] {
        state = state * 1664525u + 1013904223u;
        return float(int32_t(state)) / 65536.f;
    };
    for (uint32_t i = 0; i < 100000; ++i)
    {
        SceneGridSettings value = {};
        value.Orientation       = {next(), next(), next(), next()};
        value.Origin.x          = -0.f;
        value.Origin.w          = float(i); // not authored
        SceneGridBinary first = {}, second = {};
        ASSERT_TRUE(EncodeSceneGridBinary(value, first));
        for (uint32_t round = 0; round < 3; ++round)
        {
            ASSERT_TRUE(DecodeSceneGridBinary(first, value));
            ASSERT_TRUE(EncodeSceneGridBinary(value, second));
            ASSERT_TRUE(first == second) << i << "," << round;
            EXPECT_FALSE(std::signbit(value.Origin.x));
        }
    }
    SceneGridSettings value = {};
    value.BaseSpacing       = std::numeric_limits<float>::denorm_min();
    value.FadeRadius        = std::numeric_limits<float>::max();
    value.Orientation       = {std::numeric_limits<float>::max(), 0.f, 0.f, 0.f};
    SceneGridBinary first = {}, second = {};
    ASSERT_TRUE(EncodeSceneGridBinary(value, first));
    ASSERT_TRUE(DecodeSceneGridBinary(first, value));
    ASSERT_TRUE(EncodeSceneGridBinary(value, second));
    EXPECT_TRUE(first == second);
}

TEST_F(SceneDocumentTest, EnumeratesOwnedMeshReferencesWithoutCreatingOrMutatingAnECSWorld)
{
    const auto first  = Add(FirstUUID);
    const auto second = Add(SecondUUID);
    Source.AddComponent<MeshComponent>(first, {AssetUUID, 123});
    Source.AddComponent<MeshComponent>(second, {AssetUUID, 456});
    ASSERT_TRUE(Capture());
    Source.DestroyEntity(first);
    Source.DestroyEntity(second);
    struct References
    {
        uuids::uuid UUID  = {};
        uint32_t    Count = 0;
    } references = {};
    ASSERT_TRUE(Document.EnumerateComponentReferences(
        ComponentTypeOf<MeshComponent>(),
        Schemas,
        [](void* context, const SceneReference& reference) {
            auto& result = *static_cast<References*>(context);
            if (reference.Kind != SceneReferenceKind::Asset)
                return false;
            result.UUID = reference.UUID;
            ++result.Count;
            return true;
        },
        &references,
        &Diagnostics));
    EXPECT_EQ(references.UUID, AssetUUID);
    EXPECT_EQ(references.Count, 2u);
    EXPECT_EQ(Candidate.AliveCount(), 0u);
    EXPECT_EQ(Source.AliveCount(), 0u);
}

TEST_F(SceneDocumentTest, ReferenceEnumerationHandlesEmptyKindsAndPropagatesVisitorFailure)
{
    const auto entity = Add(FirstUUID);
    ASSERT_TRUE(Capture());
    uint32_t   visits = 0;
    const auto visit  = [](void* context, const SceneReference&) {
        ++*static_cast<uint32_t*>(context);
        return false;
    };
    EXPECT_TRUE(Document.EnumerateComponentReferences(ComponentTypeOf<MeshComponent>(), Schemas, visit, &visits, &Diagnostics));
    EXPECT_EQ(visits, 0u);
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 9});
    ASSERT_TRUE(Capture());
    EXPECT_FALSE(Document.EnumerateComponentReferences(ComponentTypeOf<MeshComponent>(), Schemas, visit, &visits, &Diagnostics));
    EXPECT_EQ(visits, 1u);
    EXPECT_FALSE(Document.EnumerateComponentReferences(ComponentTypeOf<MeshComponent>(), Schemas, nullptr, nullptr, &Diagnostics));
    EXPECT_FALSE(Document.EnumerateComponentReferences(ComponentTypeOf<NameComponent>(), Schemas, visit, &visits, &Diagnostics));
    const SceneDocument empty = {};
    EXPECT_FALSE(empty.EnumerateComponentReferences(ComponentTypeOf<MeshComponent>(), Schemas, visit, &visits, &Diagnostics));
}

TEST_F(SceneDocumentTest, DocumentReferencesPreserveTypesAndPerEntityProvenanceWithoutAnECSWorld)
{
    const auto first  = Add(FirstUUID);
    const auto second = Add(SecondUUID);
    Source.AddComponent<MeshComponent>(first, {AssetUUID, 1});
    Source.AddComponent<MaterialComponent>(first, {AssetUUID});
    Source.AddComponent<MeshComponent>(second, {AssetUUID, 2});
    Source.AddComponent<ParentComponent>(second, {first});
    ASSERT_TRUE(Capture());
    Source.DestroyEntity(second);
    Source.DestroyEntity(first);

    size_t visits = 0;
    ASSERT_TRUE(Document.EnumerateReferences(
        Schemas,
        [](void* opaque, const SceneReferenceOccurrence& occurrence) {
            const auto index = (*static_cast<size_t*>(opaque))++;
            if (index > 3)
                return false;
            EXPECT_EQ(occurrence.EntityUUID, index < 2 ? FirstUUID : SecondUUID);
            const auto& reference = occurrence.Reference;
            EXPECT_EQ(reference.Kind, index == 3 ? SceneReferenceKind::Entity : SceneReferenceKind::Asset);
            EXPECT_EQ(reference.UUID, index == 3 ? FirstUUID : AssetUUID);
            if (index == 0)
            {
                EXPECT_STREQ(occurrence.SchemaKey, "MaterialComponent");
                EXPECT_STREQ(reference.FieldKey, "material_uuid");
                EXPECT_STREQ(reference.AssetKindKey, "material");
            }
            else if (index < 3)
            {
                EXPECT_STREQ(occurrence.SchemaKey, "MeshComponent");
                EXPECT_STREQ(reference.FieldKey, "mesh_uuid");
                EXPECT_STREQ(reference.AssetKindKey, "mesh");
            }
            else
            {
                EXPECT_STREQ(occurrence.SchemaKey, "ParentComponent");
                EXPECT_STREQ(reference.FieldKey, "parent_uuid");
                EXPECT_EQ(reference.AssetKindKey, nullptr);
            }
            return true;
        },
        &visits,
        &Diagnostics));
    EXPECT_EQ(visits, 4u);
    EXPECT_EQ(Source.AliveCount(), 0u);
    EXPECT_EQ(Candidate.AliveCount(), 0u);
}

TEST_F(SceneDocumentTest, DocumentReferencesIncludeRetainedInactiveSkyEnvironmentAndSkipNil)
{
    Settings.Sky.EnvironmentMap = AssetUUID;
    Settings.Sky.Mode           = ZEngine::Rendering::Scenes::SkyMode::Atmosphere;
    ASSERT_TRUE(Capture());
    struct Reference
    {
        uint32_t    Count = 0;
        uuids::uuid UUID  = {};
    } result         = {};
    const auto visit = [](void* opaque, const SceneReferenceOccurrence& occurrence) {
        auto& output = *static_cast<Reference*>(opaque);
        ++output.Count;
        output.UUID = occurrence.Reference.UUID;
        EXPECT_TRUE(occurrence.EntityUUID.is_nil());
        EXPECT_STREQ(occurrence.SchemaKey, "SceneSettings");
        EXPECT_EQ(occurrence.Reference.Kind, SceneReferenceKind::Asset);
        EXPECT_STREQ(occurrence.Reference.FieldKey, "sky.environment");
        EXPECT_STREQ(occurrence.Reference.AssetKindKey, "environment-map");
        return true;
    };
    ASSERT_TRUE(Document.EnumerateReferences(Schemas, visit, &result, &Diagnostics));
    EXPECT_EQ(result.Count, 1u);
    EXPECT_EQ(result.UUID, AssetUUID);
    Settings.Sky.EnvironmentMap = {};
    ASSERT_TRUE(Capture());
    ASSERT_TRUE(Document.EnumerateReferences(Schemas, visit, &result, &Diagnostics));
    EXPECT_EQ(result.Count, 1u);
}

TEST_F(SceneDocumentTest, DocumentReferenceVisitorFailureStopsBeforeLaterComponentsAndSettings)
{
    const auto entity = Add(FirstUUID);
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 0});
    Source.AddComponent<MaterialComponent>(entity, {AssetUUID});
    Settings.Sky.EnvironmentMap = AssetUUID;
    ASSERT_TRUE(Capture());
    uint32_t   visits = 0;
    const auto stop   = [](void* opaque, const SceneReferenceOccurrence&) {
        ++*static_cast<uint32_t*>(opaque);
        return false;
    };
    EXPECT_FALSE(Document.EnumerateReferences(Schemas, stop, &visits, &Diagnostics));
    EXPECT_EQ(visits, 1u);
    Source.DestroyEntity(entity);
    ASSERT_TRUE(Capture());
    EXPECT_FALSE(Document.EnumerateReferences(Schemas, stop, &visits, &Diagnostics));
    EXPECT_EQ(visits, 2u);
}

TEST_F(SceneDocumentTest, DocumentReferencesRejectMissingInputsAndChangedSchemas)
{
    uint32_t   visits = 0;
    const auto visit  = [](void* opaque, const SceneReferenceOccurrence&) {
        ++*static_cast<uint32_t*>(opaque);
        return true;
    };
    EXPECT_FALSE(Document.EnumerateReferences(Schemas, visit, &visits, &Diagnostics));
    ASSERT_TRUE(Capture());
    EXPECT_TRUE(Document.EnumerateReferences(Schemas, visit, &visits, &Diagnostics));
    EXPECT_FALSE(Document.EnumerateReferences(Schemas, nullptr, nullptr, &Diagnostics));
    SceneComponentSchemaRegistry missing = {};
    EXPECT_FALSE(Document.EnumerateReferences(missing, visit, &visits, &Diagnostics));
    const auto entity = Add(FirstUUID);
    Source.AddComponent<MeshComponent>(entity, {AssetUUID, 0});
    ASSERT_TRUE(Capture());
    missing.Initialize(&Memory.MainArena);
    EXPECT_FALSE(Document.EnumerateReferences(missing, visit, &visits, &Diagnostics));
    EXPECT_EQ(visits, 0u);
}

TEST_F(SceneDocumentTest, DocumentReferencesRejectInspectOnlyUnknownComponents)
{
    Add(FirstUUID);
    ASSERT_TRUE(Capture());
    auto root      = ReadTestYAML(Emit());
    auto component = YAMLStorage.Make(SceneYAML::NodeType::Map);
    ASSERT_TRUE(YAMLStorage.Set(component, "schema_version", YAMLStorage.UInt(999)));
    ASSERT_TRUE(YAMLStorage.Set(component, "fields", YAMLStorage.Make(SceneYAML::NodeType::Map)));
    ASSERT_TRUE(YAMLStorage.Set(root["scene"]["entities"][0]["components"], "FutureComponent", component));
    ASSERT_TRUE(Parse(EmitTestYAML(root)));
    ASSERT_TRUE(Document.ReadOnly());
    uint32_t visits = 0;
    EXPECT_FALSE(Document.EnumerateReferences(
        Schemas,
        [](void* opaque, const SceneReferenceOccurrence&) {
            ++*static_cast<uint32_t*>(opaque);
            return true;
        },
        &visits,
        &Diagnostics));
    EXPECT_EQ(visits, 0u);
}

TEST(SceneSettingsComparisonTest, SkyIncludesHiddenModesAllScalarsColorAlphaAndStableReferences)
{
    using namespace ZEngine::Rendering::Scenes;
    const SkyConfig original = {};
    const auto      check    = [&](auto change) {
        auto other = original;
        change(other);
        EXPECT_NE(original, other);
        EXPECT_NE(other, original);
    };
    EXPECT_EQ(original, SkyConfig{});
    check([](auto& v) { v.Mode = SkyMode::HDRI; });
    const auto uuid = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    check([&](auto& v) { v.EnvironmentMap = uuid; });
    check([&](auto& v) { v.PrimaryCelestialLight = uuid; });
    check([](auto& v) { v.EnvironmentIntensity = std::nextafter(v.EnvironmentIntensity, 2.f); });
    check([](auto& v) { v.EnvironmentYawRadians += 1.f; });
    for (uint32_t i = 0; i < 4; ++i)
    {
        check([&](auto& v) { v.EnvironmentTint[i] += .1f; });
        check([&](auto& v) { v.Sphere.HorizonColor[i] += .1f; });
        check([&](auto& v) { v.Sphere.ZenithColor[i] += .1f; });
        check([&](auto& v) { v.Sphere.GroundColor[i] += .1f; });
    }
    for (uint32_t i = 0; i < 3; ++i)
    {
        check([&](auto& v) { v.Atmosphere.PlanetCenterWorld[i] += 1.f; });
        check([&](auto& v) { v.Atmosphere.RayleighScatteringPerKilometer[i] += .1f; });
        check([&](auto& v) { v.Atmosphere.OzoneAbsorptionPerKilometer[i] += .1f; });
        check([&](auto& v) { v.Atmosphere.GroundAlbedo[i] += .1f; });
    }
    check([](auto& v) { v.Atmosphere.WorldUnitsPerMeter += .1f; });
    check([](auto& v) { v.Atmosphere.PlanetRadiusKilometers += 1.f; });
    check([](auto& v) { v.Atmosphere.AtmosphereRadiusKilometers += 1.f; });
    check([](auto& v) { v.Atmosphere.RayleighScaleHeightKilometers += .1f; });
    check([](auto& v) { v.Atmosphere.MieScatteringPerKilometer += .1f; });
    check([](auto& v) { v.Atmosphere.MieAbsorptionPerKilometer += .1f; });
    check([](auto& v) { v.Atmosphere.MieScaleHeightKilometers += .1f; });
    check([](auto& v) { v.Atmosphere.MieAnisotropy += .1f; });
    check([](auto& v) { v.Atmosphere.OzoneCenterKilometers += .1f; });
    check([](auto& v) { v.Atmosphere.OzoneThicknessKilometers += .1f; });
    check([](auto& v) { v.Atmosphere.SunAngularRadiusRadians += .1f; });
    check([](auto& v) { v.Atmosphere.SunIlluminanceLux += 1.f; });
    check([](auto& v) { v.Atmosphere.GroundAmbientIrradiance += .1f; });
    check([](auto& v) { v.Sphere.SunDiscAngularRadiusRadians += .1f; });
    check([](auto& v) { v.Sphere.SunDiscIntensity += 1.f; });
    check([](auto& v) { v.Sphere.HorizonSharpness += .1f; });
    check([](auto& v) { v.Sphere.ShowSunDisc = !v.Sphere.ShowSunDisc; });
}

TEST_F(SceneDocumentTest, PrimaryCelestialReferenceMustResolveToATransformedDirectionalLight)
{
    Settings.Sky.PrimaryCelestialLight = FirstUUID;
    EXPECT_FALSE(Capture());
    const auto light = Add(FirstUUID);
    Source.AddComponent<LightComponent>(light, {});
    Source.GetComponent<LightComponent>(light)->LightType = LightComponent::Type::Point;
    EXPECT_FALSE(Capture());
    Source.GetComponent<LightComponent>(light)->LightType = LightComponent::Type::Directional;
    ASSERT_TRUE(Capture());
    EXPECT_EQ(Document.Settings()->Sky.PrimaryCelestialLight, FirstUUID);
}

TEST_F(SceneDocumentTest, AtomicVFSSaveReadAndFailedReplacementPreservePreviousFileAndSnapshot)
{
    VFSMemoryBackend backend;
    backend.Initialize(&Memory.MainArena);
    SceneFaultVFS vfs;
    vfs.Initialize(&Memory.MainArena);
    ASSERT_TRUE(vfs.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    const auto path   = VFSPath::Parse("/main.zescene").Value();
    const auto entity = Add(FirstUUID, "Saved");
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    ASSERT_TRUE(Document.Save(vfs, path, &Diagnostics).Succeeded());
    Source.GetComponent<TransformComponent>(entity)->Position.x = 25.f;
    ASSERT_TRUE(Capture());
    vfs.FailRename = true;
    EXPECT_TRUE(Document.Save(vfs, path, &Diagnostics).Failed());
    SceneDocument loaded;
    ASSERT_TRUE(SceneDocument::Read(vfs, path, Schemas, &loaded, &Diagnostics));
    std::string output;
    ASSERT_TRUE(loaded.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, original);
    vfs.FailRename = false;
    ASSERT_TRUE(Document.Save(vfs, path, &Diagnostics).Succeeded());
    ASSERT_TRUE(SceneDocument::Read(vfs, path, Schemas, &loaded, &Diagnostics));
    ASSERT_TRUE(loaded.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, Emit());
    vfs.Shutdown();
}

TEST_F(SceneDocumentTest, AtomicDiskSaveAndReadRoundTripThroughUtf8RootsAndSceneNames)
{
    ZEngine::Tests::TemporaryDirectory root  = {};
    std::error_code                    error = {};
    ASSERT_TRUE(root.Create(std::filesystem::path(u8"zengine_scene_document_é_東京_🚀"), error));
    ASSERT_FALSE(error);
    const auto     utf8_root = root.Path().generic_u8string();
    VFSDiskBackend backend   = {};
    backend.Initialize(reinterpret_cast<const char*>(utf8_root.c_str()), VFSBackendCaps::Read | VFSBackendCaps::Write, &Memory.MainArena, 4);
    SceneFaultVFS vfs = {};
    vfs.Initialize(&Memory.MainArena);
    ASSERT_TRUE(vfs.Mount(&backend, VFSPath::Root(), 0).Succeeded());
    const auto path        = VFSPath::Parse(reinterpret_cast<const char*>(u8"/scène_資料_🚀.zescene")).Value();
    const auto native_file = root.Path() / std::filesystem::path(u8"scène_資料_🚀.zescene");
    const auto entity      = Add(FirstUUID, "Shared gizmo history");
    Settings.Grid.Origin   = {2.f, 3.f, 4.f};
    ASSERT_TRUE(Capture());
    const auto original = Emit();
    ASSERT_TRUE(Document.Save(vfs, path, &Diagnostics).Succeeded());
    ASSERT_TRUE(std::filesystem::is_regular_file(native_file, error));
    ASSERT_FALSE(error);
    {
        std::ifstream input(native_file, std::ios::binary);
        ASSERT_TRUE(input.is_open());
        const std::string bytes((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        EXPECT_EQ(bytes, original);
    }
    Source.GetComponent<TransformComponent>(entity)->Position.x = 25.f;
    ASSERT_TRUE(Capture());
    vfs.FailRename = true;
    EXPECT_TRUE(Document.Save(vfs, path, &Diagnostics).Failed());
    SceneDocument loaded = {};
    ASSERT_TRUE(SceneDocument::Read(vfs, path, Schemas, &loaded, &Diagnostics));
    std::string output = {};
    ASSERT_TRUE(loaded.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, original);
    vfs.FailRename = false;
    ASSERT_TRUE(Document.Save(vfs, path, &Diagnostics).Succeeded());
    ASSERT_TRUE(SceneDocument::Read(vfs, path, Schemas, &loaded, &Diagnostics));
    ASSERT_TRUE(loaded.Emit(&output, &Diagnostics));
    EXPECT_EQ(output, Emit());
    ASSERT_TRUE(loaded.PopulateCandidate(Candidate, Schemas, &Diagnostics));
    const auto* transform = Candidate.GetComponent<TransformComponent>(Find(Candidate, FirstUUID));
    ASSERT_NE(transform, nullptr);
    EXPECT_EQ(transform->Position.x, 25.f);
    EXPECT_EQ(loaded.Settings()->Grid.Origin, Settings.Grid.Origin);
    auto listed = backend.List(&Memory.MainArena, VFSPath::Root());
    ASSERT_TRUE(listed.Succeeded());
    ASSERT_EQ(listed.Value().size(), 1u); // Failed saves leave no temporary sibling.
    EXPECT_EQ(listed.Value()[0].Path, path);
    vfs.Shutdown();
}
