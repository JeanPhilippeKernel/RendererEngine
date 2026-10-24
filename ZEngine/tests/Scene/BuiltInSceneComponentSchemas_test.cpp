#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/AuthoredComponentValues.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
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
    EXPECT_EQ(schemas.Count(), 6u);
    ASSERT_TRUE(RegisterBuiltInSceneComponentSchemas(schemas, &diagnostics));
    EXPECT_EQ(schemas.Count(), 6u);
    const auto& schema = Schema<TransformComponent>();
    EXPECT_STREQ(schema.Key, "TransformComponent");
    EXPECT_EQ(schema.Version, 1u);
    EXPECT_EQ(schema.FieldCount, 5u);
    EXPECT_EQ(schema.Fields[3].Class, SceneFieldClass::RuntimeDerived);
    EXPECT_EQ(schema.Fields[4].Class, SceneFieldClass::RuntimeDerived);
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

TEST_F(BuiltInSceneCodecTest, RepeatedCaptureAndBinaryConversionReuseReservedMemory)
{
    scene.AddComponent<TransformComponent>(source, {});
    const auto                offset = memory.MainArena.m_current_offset;
    const auto&               codec  = Schema<TransformComponent>().Codecs;
    std::array<std::byte, 36> bytes  = {};
    for (int i = 0; i < 1000; ++i)
    {
        writer.Reset();
        ASSERT_TRUE(Capture<TransformComponent>());
        SceneBinaryWriter binary(bytes);
        ASSERT_TRUE(codec.EncodeBinary(codec.Context, writer.Value(), &binary, &diagnostics));
        writer.Reset();
        SceneBinaryReader reader(bytes);
        ASSERT_TRUE(codec.DecodeBinary(codec.Context, 1, &reader, &writer, &diagnostics));
    }
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
}
