#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/SceneBinaryValue.h>
#include <ZEngine/ECS/SceneValue.h>
#include <ZEngine/ECS/SceneYAMLReader.h>
#include <ZEngine/ECS/SceneYamlValue.h>
#include <gtest/gtest.h>
#include <array>
#include <limits>
#include <utility>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class BuiltInSceneCodecTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory       = {};
    Scene                                scene        = {};
    SceneComponentSchemaRegistry         schemas      = {};
    SceneDiagnostics                     diagnostics  = {};
    SceneYAML::Storage                   yaml_storage = {};
    std::array<SceneValueField, 8>       fields       = {};
    SceneValueWriter                     writer{fields};
    EntityID                             source    = {};
    EntityID                             candidate = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        scene.Initialize(&memory.MainArena);
        schemas.Initialize(&memory.MainArena);
        ASSERT_TRUE(yaml_storage.Initialize());
        ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &diagnostics));
        source    = scene.CreateEntity();
        candidate = scene.CreateEntity();
    }

    void TearDown() override
    {
        scene.Shutdown();
    }

    template <typename T>
    const SceneComponentSchema& Schema()
    {
        return *schemas.FindByRuntimeType(ComponentTypeOf<T>());
    }

    template <typename T>
    bool Capture()
    {
        const auto& schema = Schema<T>();
        return schema.Codecs.Capture(schema.Codecs.Context, source, scene, &writer, &diagnostics);
    }

    template <typename T>
    bool Populate(const SceneValue& value)
    {
        const auto& schema = Schema<T>();
        return schema.Codecs.PopulateCandidate(schema.Codecs.Context, candidate, scene, value, &diagnostics);
    }
};

TEST_F(BuiltInSceneCodecTest, StartupRegistersRealIdempotentCallbacksAndDerivedFieldPolicy)
{
    EXPECT_EQ(schemas.Count(), 8u);
    ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &diagnostics));
    EXPECT_EQ(schemas.Count(), 8u);
    const auto& schema = Schema<TransformComponent>();
    EXPECT_STREQ(schema.Key, "TransformComponent");
    EXPECT_EQ(schema.Version, 1u);
    EXPECT_EQ(schema.FieldCount, 5u);
    EXPECT_EQ(schema.Fields[3].Class, SceneFieldClass::RuntimeDerived);
    EXPECT_EQ(schema.Fields[4].Class, SceneFieldClass::RuntimeDerived);
    EXPECT_STREQ(Schema<MaterialComponent>().Key, "MaterialComponent");
    const auto& body = Schema<RigidBodyComponent>();
    EXPECT_STREQ(body.Key, "RigidBodyComponent");
    ASSERT_EQ(body.FieldCount, 5u);
    EXPECT_STREQ(body.Fields[4].Key, "body_id");
    EXPECT_EQ(body.Fields[4].Class, SceneFieldClass::RuntimeDerived);
    EXPECT_FALSE(diagnostics.HasErrors());
}

TEST_F(BuiltInSceneCodecTest, YAMLUsesSharedValidationAndRejectsUnknownVersionsAndDuplicateOrDerivedFields)
{
    scene.AddComponent<TransformComponent>(source, {});
    ASSERT_TRUE(Capture<TransformComponent>());
    const auto& codec = Schema<TransformComponent>().Codecs;
    ASSERT_NE(codec.EncodeYAML, nullptr);
    ASSERT_NE(codec.DecodeYAML, nullptr);
    auto payload = yaml_storage.Destination();
    ASSERT_TRUE(codec.EncodeYAML(codec.Context, writer.Value(), &payload, &diagnostics));
    writer.Reset();
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 999, payload, &writer, &diagnostics));
    EXPECT_TRUE(writer.Empty());
    ASSERT_TRUE(codec.DecodeYAML(codec.Context, 1, payload, &writer, &diagnostics));
    ASSERT_TRUE(Populate<TransformComponent>(writer.Value()));
    writer.Reset();
    auto duplicate = yaml_storage.Destination();
    EXPECT_FALSE(ReadSceneYAML("position: [0, 0, 0]\nposition: [0, 0, 0]\nscale: [1, 1, 1]", {}, &duplicate, &diagnostics));
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, duplicate, &writer, &diagnostics));
    EXPECT_TRUE(writer.Empty());
    ASSERT_TRUE(yaml_storage.Set(payload, "world_transform", yaml_storage.Scalar("forbidden")));
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, payload, &writer, &diagnostics));
    EXPECT_TRUE(writer.Empty());
}

TEST_F(BuiltInSceneCodecTest, NameBinaryAndYAMLRoundTripsUseBoundedStringsRatherThanNativeStructs)
{
    NameComponent name = {};
    ASSERT_EQ(ZEngine::Helpers::secure_strncpy(name.Value, sizeof(name.Value), "Editor name", 11), ZEngine::Helpers::MEMORY_OP_SUCCESS);
    scene.AddComponent<NameComponent>(source, name);
    ASSERT_TRUE(Capture<NameComponent>());
    const auto&               codec = Schema<NameComponent>().Codecs;
    std::array<std::byte, 15> bytes = {};
    SceneBinaryWriter         binary(bytes);
    ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(bytes[0], std::byte{11});
    writer.Reset();
    SceneBinaryReader reader(bytes);
    ASSERT_TRUE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
    EXPECT_EQ(reader.Remaining(), 0u);
    auto yaml = yaml_storage.Destination();
    ASSERT_TRUE(codec.EncodeYAML(codec.Context, writer.Value(), &yaml, &diagnostics));
    EXPECT_EQ(yaml["value"].Scalar(), "Editor name");
    writer.Reset();
    ASSERT_TRUE(codec.DecodeYAML(codec.Context, 1, yaml, &writer, &diagnostics));
    ASSERT_TRUE(Populate<NameComponent>(writer.Value()));
    EXPECT_STREQ(scene.GetComponent<NameComponent>(candidate)->Value, "Editor name");
    writer.Reset();
    ASSERT_TRUE(yaml_storage.Set(yaml, "value", yaml_storage.Scalar(std::string(128, 'x'))));
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, yaml, &writer, &diagnostics));
    EXPECT_TRUE(writer.Empty());
}

TEST_F(BuiltInSceneCodecTest, MeshBinaryOmitsRuntimeHandleAndEnumeratesStableAssetReference)
{
    const auto uuid = *uuids::uuid::from_string("e823e956-d1a9-48d9-bba3-724791072320");
    scene.AddComponent<MeshComponent>(source, {uuid, 99});
    ASSERT_TRUE(Capture<MeshComponent>());
    const auto&               schema = Schema<MeshComponent>();
    std::array<std::byte, 16> bytes  = {};
    SceneBinaryWriter         binary(bytes);
    ASSERT_TRUE(schema.Codecs.EncodeBinary(nullptr, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(binary.Position(), 16u);
    writer.Reset();
    SceneBinaryReader reader(bytes);
    ASSERT_TRUE(schema.Codecs.DecodeBinary(nullptr, 1, &reader, &writer, &diagnostics));
    ASSERT_TRUE(Populate<MeshComponent>(writer.Value()));
    EXPECT_EQ(scene.GetComponent<MeshComponent>(candidate)->MeshUUID, uuid);
    EXPECT_EQ(scene.GetComponent<MeshComponent>(candidate)->RenderInstanceId, UINT32_MAX);
    SceneReference reference = {};
    ASSERT_TRUE(schema.References.Enumerate(
        nullptr,
        writer.Value(),
        [](void* context, const SceneReference& item) {
            *static_cast<SceneReference*>(context) = item;
            return true;
        },
        &reference,
        &diagnostics));
    EXPECT_EQ(reference.Kind, SceneReferenceKind::Asset);
    EXPECT_EQ(reference.UUID, uuid);
}

TEST_F(BuiltInSceneCodecTest, ParentNullRootIsPreservedAndMissingReferencesFailBeforeComponentWrite)
{
    const auto& codec = Schema<ParentComponent>().Codecs;
    auto        yaml  = yaml_storage.Destination();
    ASSERT_TRUE(ReadSceneYAML("parent_uuid: null", {}, &yaml, &diagnostics));
    ASSERT_TRUE(codec.DecodeYAML(nullptr, 1, yaml, &writer, &diagnostics));
    ASSERT_TRUE(Populate<ParentComponent>(writer.Value()));
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(candidate));
    ASSERT_TRUE(codec.ResolveCandidateReferences(nullptr, candidate, scene, writer.Value(), {}, &diagnostics));
    EXPECT_EQ(scene.GetComponent<ParentComponent>(candidate)->Parent, INVALID_ENTITY);
    scene.RemoveComponent<ParentComponent>(candidate);
    writer.Reset();
    ASSERT_TRUE(yaml_storage.Set(yaml, "parent_uuid", yaml_storage.Scalar("e823e956-d1a9-48d9-bba3-724791072320")));
    ASSERT_TRUE(codec.DecodeYAML(nullptr, 1, yaml, &writer, &diagnostics));
    EXPECT_FALSE(codec.ResolveCandidateReferences(nullptr, candidate, scene, writer.Value(), {}, &diagnostics));
    EXPECT_FALSE(scene.HasComponent<ParentComponent>(candidate));
}

TEST_F(BuiltInSceneCodecTest, TransformRoundTripCapturesOnlyAuthoredValues)
{
    TransformComponent transform   = {};
    transform.Position             = {1.f, 2.f, 3.f};
    transform.Rotation             = {.1f, .2f, .3f};
    transform.Scale                = {-1.f, 0.f, 3.f};
    transform.PreviousPosition     = {99.f, 98.f, 97.f};
    transform.WorldTransform(0, 3) = 42.f;
    scene.AddComponent<TransformComponent>(source, transform);
    ASSERT_TRUE(Capture<TransformComponent>());
    EXPECT_EQ(writer.Value().Fields().size(), 3u);
    EXPECT_EQ(writer.Value().Find<ZEngine::Core::Maths::Vec3f>("previous_position"), nullptr);
    ASSERT_TRUE(Populate<TransformComponent>(writer.Value()));
    const auto* loaded = scene.GetComponent<TransformComponent>(candidate);
    ASSERT_NE(loaded, nullptr);
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(transform), CaptureLocalTransform(*loaded)));
    EXPECT_FLOAT_EQ(loaded->PreviousPosition.x, 1.f);
    EXPECT_FLOAT_EQ(loaded->WorldTransform(0, 3), 0.f);
    EXPECT_FALSE(Populate<TransformComponent>(writer.Value()));
}

TEST_F(BuiltInSceneCodecTest, LightAndCameraUseTheSameAuthoredValidatorsAsFieldTransactions)
{
    LightComponent light = {};
    light.LightType      = LightComponent::Type::Spot;
    light.Range          = 42.f;
    light.SpotAngle      = 35.f;
    light.Color[1]       = .25f;
    scene.AddComponent<LightComponent>(source, light);
    ASSERT_TRUE(Capture<LightComponent>());
    ASSERT_TRUE(Populate<LightComponent>(writer.Value()));
    const auto* loaded_light = scene.GetComponent<LightComponent>(candidate);
    EXPECT_EQ(loaded_light->LightType, LightComponent::Type::Spot);
    EXPECT_FLOAT_EQ(loaded_light->Range, 42.f);
    EXPECT_FLOAT_EQ(loaded_light->SpotAngle, 35.f);
    EXPECT_FLOAT_EQ(loaded_light->Color[1], .25f);
    writer.Reset();
    CameraComponent camera = {};
    camera.FovY            = 90.f;
    camera.IsMain          = true;
    scene.AddComponent<CameraComponent>(source, camera);
    ASSERT_TRUE(Capture<CameraComponent>());
    ASSERT_TRUE(Populate<CameraComponent>(writer.Value()));
    EXPECT_FLOAT_EQ(scene.GetComponent<CameraComponent>(candidate)->FovY, 90.f);
    EXPECT_TRUE(scene.GetComponent<CameraComponent>(candidate)->IsMain);
}

TEST_F(BuiltInSceneCodecTest, MalformedOrUnknownFieldsNeverPartiallyPopulateTheCandidate)
{
    scene.AddComponent<TransformComponent>(source, {});
    ASSERT_TRUE(Capture<TransformComponent>());
    fields[0].Value     = std::numeric_limits<float>::infinity();
    const auto revision = scene.StructuralRevision();
    EXPECT_FALSE(Populate<TransformComponent>(writer.Value()));
    EXPECT_FALSE(scene.HasComponent<TransformComponent>(candidate));
    EXPECT_EQ(scene.StructuralRevision(), revision);
    fields[0].Value = ZEngine::Core::Maths::Vec3f{};
    fields[0].Key   = "world_transform";
    EXPECT_FALSE(Populate<TransformComponent>(writer.Value()));
    EXPECT_FALSE(scene.HasComponent<TransformComponent>(candidate));
    EXPECT_TRUE(diagnostics.HasErrors());
}

TEST_F(BuiltInSceneCodecTest, DuplicateFieldKeysAreRejected)
{
    const std::array duplicate = {
        SceneValueField{"position",              ZEngine::Core::Maths::Vec3f{}},
        SceneValueField{"position",              ZEngine::Core::Maths::Vec3f{}},
        SceneValueField{   "scale", ZEngine::Core::Maths::Vec3f{1.f, 1.f, 1.f}},
    };
    EXPECT_FALSE(Populate<TransformComponent>(SceneValue(duplicate)));
    EXPECT_FALSE(scene.HasComponent<TransformComponent>(candidate));
}

TEST_F(BuiltInSceneCodecTest, CapturePreflightsCompleteFieldCapacity)
{
    scene.AddComponent<TransformComponent>(source, {});
    std::array<SceneValueField, 2> small = {};
    SceneValueWriter               limited(small);
    const auto&                    codec = Schema<TransformComponent>().Codecs;
    EXPECT_FALSE(codec.Capture(codec.Context, source, scene, &limited, &diagnostics));
    EXPECT_TRUE(limited.Empty());
}

TEST_F(BuiltInSceneCodecTest, BinaryTransformUsesCanonicalScalarsNotNativeVectorStorage)
{
    TransformComponent transform = {};
    transform.Position           = {1.f, -2.f, 3.f};
    scene.AddComponent<TransformComponent>(source, transform);
    ASSERT_TRUE(Capture<TransformComponent>());
    std::array<std::byte, 36> bytes = {};
    SceneBinaryWriter         binary(bytes);
    const auto&               codec = Schema<TransformComponent>().Codecs;
    ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(binary.Position(), 36u);
    EXPECT_EQ(bytes[0], std::byte{0x00});
    EXPECT_EQ(bytes[1], std::byte{0x00});
    EXPECT_EQ(bytes[2], std::byte{0x80});
    EXPECT_EQ(bytes[3], std::byte{0x3f});
    writer.Reset();
    SceneBinaryReader reader(bytes);
    ASSERT_TRUE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
    EXPECT_EQ(reader.Remaining(), 0u);
    ASSERT_TRUE(Populate<TransformComponent>(writer.Value()));
    EXPECT_TRUE(SameLocalTransform(CaptureLocalTransform(transform), CaptureLocalTransform(*scene.GetComponent<TransformComponent>(candidate))));
}

TEST_F(BuiltInSceneCodecTest, BinaryDecodeRejectsVersionsTruncationAndNonFiniteDataWithoutConsumingInput)
{
    const auto&               codec = Schema<TransformComponent>().Codecs;
    std::array<std::byte, 36> bytes = {};
    SceneBinaryReader         versioned(bytes);
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 2, &versioned, &writer, &diagnostics));
    EXPECT_EQ(versioned.Position(), 0u);
    SceneBinaryReader truncated(std::span<const std::byte>(bytes).first(35));
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &truncated, &writer, &diagnostics));
    EXPECT_EQ(truncated.Position(), 0u);
    bytes[2] = std::byte{0x80};
    bytes[3] = std::byte{0x7f};
    SceneBinaryReader nonfinite(bytes);
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &nonfinite, &writer, &diagnostics));
    EXPECT_EQ(nonfinite.Position(), 0u);
    EXPECT_TRUE(writer.Empty());
}

TEST_F(BuiltInSceneCodecTest, BinaryEncodePreflightsByteCapacity)
{
    scene.AddComponent<TransformComponent>(source, {});
    ASSERT_TRUE(Capture<TransformComponent>());
    std::array<std::byte, 35> bytes = {};
    SceneBinaryWriter         binary(bytes);
    const auto&               codec = Schema<TransformComponent>().Codecs;
    EXPECT_FALSE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(binary.Position(), 0u);
}

TEST_F(BuiltInSceneCodecTest, BinaryLightAndCameraRoundTripsRejectIllegalEnumsAndFlags)
{
    scene.AddComponent<LightComponent>(source, {});
    ASSERT_TRUE(Capture<LightComponent>());
    std::array<std::byte, 28> light_bytes = {};
    SceneBinaryWriter         light_binary(light_bytes);
    const auto&               light_codec = Schema<LightComponent>().Codecs;
    ASSERT_TRUE(light_codec.EncodeBinary(light_codec.Context, writer.Value(), &light_binary, &diagnostics));
    writer.Reset();
    SceneBinaryReader light_reader(light_bytes);
    ASSERT_TRUE(light_codec.DecodeBinary(light_codec.Context, 1, &light_reader, &writer, &diagnostics));
    ASSERT_TRUE(Populate<LightComponent>(writer.Value()));
    writer.Reset();
    light_bytes[0] = std::byte{0xff};
    SceneBinaryReader bad_light(light_bytes);
    EXPECT_FALSE(light_codec.DecodeBinary(light_codec.Context, 1, &bad_light, &writer, &diagnostics));
    EXPECT_EQ(bad_light.Position(), 0u);
    scene.AddComponent<CameraComponent>(source, {});
    ASSERT_TRUE(Capture<CameraComponent>());
    std::array<std::byte, 20> camera_bytes = {};
    SceneBinaryWriter         camera_binary(camera_bytes);
    const auto&               camera_codec = Schema<CameraComponent>().Codecs;
    ASSERT_TRUE(camera_codec.EncodeBinary(camera_codec.Context, writer.Value(), &camera_binary, &diagnostics));
    writer.Reset();
    SceneBinaryReader camera_reader(camera_bytes);
    ASSERT_TRUE(camera_codec.DecodeBinary(camera_codec.Context, 1, &camera_reader, &writer, &diagnostics));
    ASSERT_TRUE(Populate<CameraComponent>(writer.Value()));
    writer.Reset();
    camera_bytes[16] = std::byte{2};
    SceneBinaryReader bad_camera(camera_bytes);
    EXPECT_FALSE(camera_codec.DecodeBinary(camera_codec.Context, 1, &bad_camera, &writer, &diagnostics));
    EXPECT_EQ(bad_camera.Position(), 0u);
}

TEST_F(BuiltInSceneCodecTest, RigidBodyYAMLAndBinaryRoundTripsPreserveAllMotionTypesWithoutRuntimeState)
{
    RigidBodyComponent body = {.Mass = 12.5f, .Friction = 0.75f, .Restitution = 0.25f, .BodyID = 12345};
    scene.AddComponent<RigidBodyComponent>(source, body);
    const auto& codec = Schema<RigidBodyComponent>().Codecs;
    for (const auto motion : {RigidBodyComponent::MotionType::Static, RigidBodyComponent::MotionType::Kinematic, RigidBodyComponent::MotionType::Dynamic})
    {
        SCOPED_TRACE(static_cast<uint32_t>(motion));
        scene.GetComponent<RigidBodyComponent>(source)->MotionKind = motion;
        writer.Reset();
        ASSERT_TRUE(Capture<RigidBodyComponent>());
        EXPECT_EQ(writer.Value().Fields().size(), 4u);
        EXPECT_EQ(writer.Value().Find<uint32_t>("body_id"), nullptr);
        std::array<std::byte, 16> bytes = {};
        SceneBinaryWriter         binary(bytes);
        ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
        EXPECT_EQ(binary.Position(), bytes.size());
        EXPECT_EQ(bytes[0], std::byte(static_cast<uint32_t>(motion)));
        EXPECT_EQ(bytes[1], std::byte{0});
        EXPECT_EQ(bytes[2], std::byte{0});
        EXPECT_EQ(bytes[3], std::byte{0});
        writer.Reset();
        SceneBinaryReader reader(bytes);
        ASSERT_TRUE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
        EXPECT_EQ(reader.Remaining(), 0u);
        auto yaml = yaml_storage.Destination();
        ASSERT_TRUE(codec.EncodeYAML(codec.Context, writer.Value(), &yaml, &diagnostics));
        EXPECT_FALSE(yaml["body_id"].IsDefined());
        writer.Reset();
        ASSERT_TRUE(codec.DecodeYAML(codec.Context, 1, yaml, &writer, &diagnostics));
        ASSERT_TRUE(Populate<RigidBodyComponent>(writer.Value()));
        const auto* loaded = scene.GetComponent<RigidBodyComponent>(candidate);
        ASSERT_NE(loaded, nullptr);
        EXPECT_EQ(loaded->MotionKind, motion);
        EXPECT_FLOAT_EQ(loaded->Mass, body.Mass);
        EXPECT_FLOAT_EQ(loaded->Friction, body.Friction);
        EXPECT_FLOAT_EQ(loaded->Restitution, body.Restitution);
        EXPECT_EQ(loaded->BodyID, UINT32_MAX);
        EXPECT_FALSE(Populate<RigidBodyComponent>(writer.Value()));
        scene.RemoveComponent<RigidBodyComponent>(candidate);
    }
}

TEST_F(BuiltInSceneCodecTest, RigidBodyYAMLRejectsInvalidFieldsVersionsAndCapacityWithoutPublishingValues)
{
    const auto&                       codec   = Schema<RigidBodyComponent>().Codecs;
    constexpr std::array<cstring, 15> invalid = {
        "motion_type: 3\nmass: 1\nfriction: 0.5\nrestitution: 0",
        "motion_type: 256\nmass: 1\nfriction: 0.5\nrestitution: 0",
        "motion_type: -1\nmass: 1\nfriction: 0.5\nrestitution: 0",
        "motion_type: 2\nmass: 0\nfriction: 0.5\nrestitution: 0",
        "motion_type: 2\nmass: -1\nfriction: 0.5\nrestitution: 0",
        "motion_type: 2\nmass: .nan\nfriction: 0.5\nrestitution: 0",
        "motion_type: 2\nmass: 1\nfriction: -0.1\nrestitution: 0",
        "motion_type: 2\nmass: 1\nfriction: .inf\nrestitution: 0",
        "motion_type: 2\nmass: 1\nfriction: 0.5\nrestitution: -0.1",
        "motion_type: 2\nmass: 1\nfriction: 0.5\nrestitution: 1.1",
        "motion_type: 2\nmass: 1\nfriction: 0.5\nrestitution: .nan",
        "motion_type: 2\nmass: 1\nfriction: 0.5",
        "body_id: 2\nmass: 1\nfriction: 0.5\nrestitution: 0",
        "motion_type: 2\nmass: 1\nfriction: 0.5\nrestitution: 0\nbody_id: 9",
        "motion_type: true\nmass: 1\nfriction: 0.5\nrestitution: 0",
    };
    for (const auto source_yaml : invalid)
    {
        SCOPED_TRACE(source_yaml);
        auto yaml = yaml_storage.Destination();
        ASSERT_TRUE(ReadSceneYAML(source_yaml, {}, &yaml, &diagnostics));
        EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, yaml, &writer, &diagnostics));
        EXPECT_TRUE(writer.Empty());
        EXPECT_FALSE(scene.HasComponent<RigidBodyComponent>(candidate));
    }
    auto yaml = yaml_storage.Destination();
    ASSERT_TRUE(ReadSceneYAML("motion_type: 2\nmass: 1\nfriction: 0.5\nrestitution: 0", {}, &yaml, &diagnostics));
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 2, yaml, &writer, &diagnostics));
    EXPECT_TRUE(writer.Empty());
    std::array<SceneValueField, 3> small = {};
    SceneValueWriter               limited(small);
    EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, yaml, &limited, &diagnostics));
    EXPECT_TRUE(limited.Empty());
    scene.AddComponent<RigidBodyComponent>(source, {});
    EXPECT_FALSE(codec.Capture(codec.Context, source, scene, &limited, &diagnostics));
    EXPECT_TRUE(limited.Empty());
}

TEST_F(BuiltInSceneCodecTest, RigidBodyBinaryRejectsTruncationTrailingBytesAndInvalidScalarsWithoutConsumingInput)
{
    scene.AddComponent<RigidBodyComponent>(source, {});
    ASSERT_TRUE(Capture<RigidBodyComponent>());
    const auto&               codec = Schema<RigidBodyComponent>().Codecs;
    std::array<std::byte, 17> bytes = {};
    SceneBinaryWriter         binary(std::span<std::byte>(bytes).first(16));
    ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    writer.Reset();
    for (size_t size = 0; size <= bytes.size(); ++size)
    {
        if (size == 16)
        {
            continue;
        }
        SCOPED_TRACE(size);
        SceneBinaryReader reader(std::span<const std::byte>(bytes).first(size));
        EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
        EXPECT_EQ(reader.Position(), 0u);
        EXPECT_TRUE(writer.Empty());
    }
    SceneBinaryReader versioned(std::span<const std::byte>(bytes).first(16));
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 2, &versioned, &writer, &diagnostics));
    EXPECT_EQ(versioned.Position(), 0u);
    const auto           valid   = bytes;
    constexpr std::array invalid = {
        std::pair<size_t, uint32_t>{ 0,           3},
        std::pair<size_t, uint32_t>{ 0,         256},
        std::pair<size_t, uint32_t>{ 4,           0},
        std::pair<size_t, uint32_t>{ 4, 0xbf800000u},
        std::pair<size_t, uint32_t>{ 4, 0x7f800000u},
        std::pair<size_t, uint32_t>{ 8, 0xbf800000u},
        std::pair<size_t, uint32_t>{ 8, 0x7fc00000u},
        std::pair<size_t, uint32_t>{12, 0xbf800000u},
        std::pair<size_t, uint32_t>{12, 0x40000000u},
    };
    for (const auto& [offset, bits] : invalid)
    {
        bytes = valid;
        SceneBinaryWriter corrupt(std::span<std::byte>(bytes).subspan(offset, 4));
        ASSERT_TRUE(corrupt.UInt32(bits));
        SceneBinaryReader reader(std::span<const std::byte>(bytes).first(16));
        EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
        EXPECT_EQ(reader.Position(), 0u);
        EXPECT_TRUE(writer.Empty());
    }
}

TEST_F(BuiltInSceneCodecTest, InvalidRigidBodyCapturePopulateAndEncodePreserveCallerState)
{
    scene.AddComponent<RigidBodyComponent>(source, {});
    ASSERT_TRUE(Capture<RigidBodyComponent>());
    const auto& codec   = Schema<RigidBodyComponent>().Codecs;
    fields[0].Value     = uint32_t{256};
    const auto revision = scene.StructuralRevision();
    EXPECT_FALSE(Populate<RigidBodyComponent>(writer.Value()));
    EXPECT_EQ(scene.StructuralRevision(), revision);
    std::array<std::byte, 16> bytes = {};
    bytes.fill(std::byte{0xa5});
    const auto        original = bytes;
    SceneBinaryWriter binary(bytes);
    EXPECT_FALSE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(binary.Position(), 0u);
    EXPECT_EQ(bytes, original);
    writer.Reset();
    scene.GetComponent<RigidBodyComponent>(source)->Mass = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(Capture<RigidBodyComponent>());
    EXPECT_TRUE(writer.Empty());
    scene.GetComponent<RigidBodyComponent>(source)->Mass = 1.f;
    ASSERT_TRUE(Capture<RigidBodyComponent>());
    SceneBinaryWriter small(std::span<std::byte>(bytes).first(15));
    EXPECT_FALSE(codec.EncodeBinary(codec.Context, writer.Value(), &small, &diagnostics));
    EXPECT_EQ(small.Position(), 0u);
    EXPECT_EQ(bytes, original);
}

TEST_F(BuiltInSceneCodecTest, MaterialYAMLAndBinaryRoundTripPreserveAndRemapAssetReferences)
{
    const auto uuid        = *uuids::uuid::from_string("e823e956-d1a9-48d9-bba3-724791072320");
    const auto replacement = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    scene.AddComponent<MaterialComponent>(source, {uuid});
    ASSERT_TRUE(Capture<MaterialComponent>());
    const auto&               schema = Schema<MaterialComponent>();
    std::array<std::byte, 16> bytes  = {};
    SceneBinaryWriter         binary(bytes);
    ASSERT_TRUE(schema.Codecs.EncodeBinary(schema.Codecs.Context, writer.Value(), &binary, &diagnostics));
    EXPECT_EQ(binary.Position(), bytes.size());
    const auto uuid_bytes = uuid.as_bytes();
    for (size_t i = 0; i < bytes.size(); ++i)
    {
        EXPECT_EQ(bytes[i], std::byte(uuid_bytes[i]));
    }
    writer.Reset();
    SceneBinaryReader reader(bytes);
    ASSERT_TRUE(schema.Codecs.DecodeBinary(schema.Codecs.Context, 1, &reader, &writer, &diagnostics));
    EXPECT_EQ(reader.Remaining(), 0u);
    auto yaml = yaml_storage.Destination();
    ASSERT_TRUE(schema.Codecs.EncodeYAML(schema.Codecs.Context, writer.Value(), &yaml, &diagnostics));
    EXPECT_EQ(yaml["material_uuid"].Scalar(), uuids::to_string(uuid));
    writer.Reset();
    ASSERT_TRUE(schema.Codecs.DecodeYAML(schema.Codecs.Context, 1, yaml, &writer, &diagnostics));
    ASSERT_TRUE(Populate<MaterialComponent>(writer.Value()));
    EXPECT_EQ(scene.GetComponent<MaterialComponent>(candidate)->MaterialUUID, uuid);
    SceneReference reference = {};
    ASSERT_TRUE(schema.References.Enumerate(
        schema.References.Context,
        writer.Value(),
        [](void* context, const SceneReference& value) {
            *static_cast<SceneReference*>(context) = value;
            return true;
        },
        &reference,
        &diagnostics));
    EXPECT_EQ(reference.Kind, SceneReferenceKind::Asset);
    EXPECT_EQ(reference.UUID, uuid);
    EXPECT_FALSE(schema.References.Enumerate(schema.References.Context, writer.Value(), [](void*, const SceneReference&) { return false; }, nullptr, &diagnostics));
    struct RemapContext
    {
        uuids::uuid From = {};
        uuids::uuid To   = {};
    } context                                      = {uuid, replacement};
    const SceneUUIDRemap           remap           = {&context, [](void* input, const uuids::uuid& from, uuids::uuid* out) {
                                      const auto& mapping = *static_cast<RemapContext*>(input);
                                      if (from != mapping.From)
                                      {
                                          return false;
                                      }
                                      *out = mapping.To;
                                      return true;
                                                      }};
    std::array<SceneValueField, 1> remapped_fields = {};
    SceneValueWriter               remapped(remapped_fields);
    ASSERT_TRUE(schema.References.Remap(schema.References.Context, writer.Value(), remap, &remapped, &diagnostics));
    ASSERT_NE(remapped.Value().Find<uuids::uuid>("material_uuid"), nullptr);
    EXPECT_EQ(*remapped.Value().Find<uuids::uuid>("material_uuid"), replacement);
    EXPECT_EQ(*writer.Value().Find<uuids::uuid>("material_uuid"), uuid);
    remapped.Reset();
    EXPECT_FALSE(schema.References.Remap(schema.References.Context, writer.Value(), {}, &remapped, &diagnostics));
    EXPECT_TRUE(remapped.Empty());
    context.To = {};
    EXPECT_FALSE(schema.References.Remap(schema.References.Context, writer.Value(), remap, &remapped, &diagnostics));
    EXPECT_TRUE(remapped.Empty());
}

TEST_F(BuiltInSceneCodecTest, MaterialRejectsMalformedOrNilUUIDsAndUnsupportedFields)
{
    const auto&                      codec   = Schema<MaterialComponent>().Codecs;
    constexpr std::array<cstring, 6> invalid = {
        "material_uuid: null",
        "material_uuid: 00000000-0000-0000-0000-000000000000",
        "material_uuid: invalid",
        "material_uuid: [1, 2, 3]",
        "mesh_uuid: e823e956-d1a9-48d9-bba3-724791072320",
        "material_uuid: e823e956-d1a9-48d9-bba3-724791072320\nslot: 1",
    };
    for (const auto source_yaml : invalid)
    {
        SCOPED_TRACE(source_yaml);
        auto yaml = yaml_storage.Destination();
        ASSERT_TRUE(ReadSceneYAML(source_yaml, {}, &yaml, &diagnostics));
        EXPECT_FALSE(codec.DecodeYAML(codec.Context, 1, yaml, &writer, &diagnostics));
        EXPECT_TRUE(writer.Empty());
    }
    scene.AddComponent<MaterialComponent>(source, {});
    EXPECT_FALSE(Capture<MaterialComponent>());
    EXPECT_TRUE(writer.Empty());
    const std::array fields_with_nil = {
        SceneValueField{"material_uuid", uuids::uuid{}}
    };
    EXPECT_FALSE(Populate<MaterialComponent>(SceneValue(fields_with_nil)));
    EXPECT_FALSE(scene.HasComponent<MaterialComponent>(candidate));
}

TEST_F(BuiltInSceneCodecTest, MaterialBinaryChecksVersionLengthUUIDAndOutputCapacity)
{
    const auto uuid = *uuids::uuid::from_string("e823e956-d1a9-48d9-bba3-724791072320");
    scene.AddComponent<MaterialComponent>(source, {uuid});
    ASSERT_TRUE(Capture<MaterialComponent>());
    const auto&               codec = Schema<MaterialComponent>().Codecs;
    std::array<std::byte, 17> bytes = {};
    bytes.fill(std::byte{0xa5});
    const auto        untouched = bytes;
    SceneBinaryWriter small(std::span<std::byte>(bytes).first(15));
    EXPECT_FALSE(codec.EncodeBinary(codec.Context, writer.Value(), &small, &diagnostics));
    EXPECT_EQ(small.Position(), 0u);
    EXPECT_EQ(bytes, untouched);
    SceneBinaryWriter binary(std::span<std::byte>(bytes).first(16));
    ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
    writer.Reset();
    for (size_t size = 0; size <= bytes.size(); ++size)
    {
        if (size == 16)
        {
            continue;
        }
        SceneBinaryReader reader(std::span<const std::byte>(bytes).first(size));
        EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
        EXPECT_EQ(reader.Position(), 0u);
        EXPECT_TRUE(writer.Empty());
    }
    SceneBinaryReader versioned(std::span<const std::byte>(bytes).first(16));
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 2, &versioned, &writer, &diagnostics));
    EXPECT_EQ(versioned.Position(), 0u);
    SceneValueWriter full(ZEngine::Core::Containers::ArrayView<SceneValueField>{});
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &versioned, &full, &diagnostics));
    EXPECT_EQ(versioned.Position(), 0u);
    bytes.fill(std::byte{0});
    SceneBinaryReader nil(std::span<const std::byte>(bytes).first(16));
    EXPECT_FALSE(codec.DecodeBinary(codec.Context, 1, &nil, &writer, &diagnostics));
    EXPECT_EQ(nil.Position(), 0u);
    EXPECT_TRUE(writer.Empty());
}

TEST_F(BuiltInSceneCodecTest, RepeatedCaptureAndBinaryConversionReuseReservedMemory)
{
    scene.AddComponent<TransformComponent>(source, {});
    scene.AddComponent<RigidBodyComponent>(source, {});
    const auto material = *uuids::uuid::from_string("e823e956-d1a9-48d9-bba3-724791072320");
    scene.AddComponent<MaterialComponent>(source, {material});
    const auto                offset = memory.MainArena.m_current_offset;
    const std::array          codecs = {&Schema<TransformComponent>().Codecs, &Schema<RigidBodyComponent>().Codecs, &Schema<MaterialComponent>().Codecs};
    std::array<std::byte, 36> bytes  = {};
    for (int i = 0; i < 1000; ++i)
    {
        for (const auto* codec : codecs)
        {
            writer.Reset();
            ASSERT_TRUE(codec->Capture(codec->Context, source, scene, &writer, &diagnostics));
            SceneBinaryWriter binary(bytes);
            ASSERT_TRUE(codec->EncodeBinary(codec->Context, writer.Value(), &binary, &diagnostics));
            writer.Reset();
            SceneBinaryReader reader(std::span<const std::byte>(bytes).first(binary.Position()));
            ASSERT_TRUE(codec->DecodeBinary(codec->Context, 1, &reader, &writer, &diagnostics));
        }
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}
