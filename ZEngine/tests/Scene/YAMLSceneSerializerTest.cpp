#ifdef ZENGINE_EDITOR
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/ECS/ComponentSerializerRegistry.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/YAMLSceneSerializer.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <cstring>
#include <filesystem>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using ZEngine::Core::Containers::String;
using ZEngine::Helpers::secure_strcmp;

namespace
{
    VFSPath P(const char* s)
    {
        return VFSPath::Parse(s).Value();
    }

    void SerializeTransform(void*, EntityID id, const Scene& scene, YAML::Node& node)
    {
        const auto* t = scene.GetComponent<TransformComponent>(id);
        if (!t)
            return;
        YAML::Node position(YAML::NodeType::Sequence);
        position.push_back(t->Position.x);
        position.push_back(t->Position.y);
        position.push_back(t->Position.z);
        node["position"] = position;

        YAML::Node scale(YAML::NodeType::Sequence);
        scale.push_back(t->Scale.x);
        scale.push_back(t->Scale.y);
        scale.push_back(t->Scale.z);
        node["scale"] = scale;
    }

    void DeserializeTransform(void*, EntityID id, Scene& scene, const YAML::Node& node)
    {
        auto* t = scene.GetComponent<TransformComponent>(id);
        if (!t)
            return;
        const YAML::Node p = node["position"];
        t->Position        = {p[0].as<float>(), p[1].as<float>(), p[2].as<float>()};
        const YAML::Node s = node["scale"];
        t->Scale           = {s[0].as<float>(), s[1].as<float>(), s[2].as<float>()};
    }

    // Registries are process-wide singletons: set up once for the suite.
    void EnsureRegistries()
    {
        static MemoryManager s_manager;
        static bool          s_once = [] {
            s_manager.Initialize(ZMega(8), {});
            ComponentReflectionRegistry::Get().Initialize(&s_manager.MainArena);
            RegisterBuiltInComponentReflection();
            ComponentSerializerRegistry::Get().Initialize(&s_manager.MainArena);
            ComponentSerializerRegistry::Get().Register(
                ComponentTypeOf<TransformComponent>(),
                {
                         .SerializeYAML   = SerializeTransform,
                         .DeserializeYAML = DeserializeTransform,
                });
            return true;
        }();
        (void) s_once;
    }
} // namespace

class YAMLSceneSerializerTest : public ::testing::Test
{
protected:
    MemoryManager       m_manager;
    VFSContext          m_ctx;
    VFSDiskBackend      m_backend;
    Scene               m_scene;
    YAMLSceneSerializer m_serializer;
    String              m_root;

    void                SetUp() override
    {
        EnsureRegistries();
        m_manager.Initialize(ZMega(32), {});

        m_root.init(&m_manager.MainArena, (std::filesystem::temp_directory_path() / "zengine_yaml_scene_tests").string().c_str());
        std::error_code ec;
        std::filesystem::remove_all(m_root.c_str(), ec);
        std::filesystem::create_directories(m_root.c_str(), ec);

        m_backend.Initialize(m_root.c_str(), VFSBackendCaps::Read | VFSBackendCaps::Write, &m_manager.MainArena);
        m_ctx.Initialize(&m_manager.MainArena);
        ASSERT_TRUE(m_ctx.Mount(&m_backend, P("/scenes"), 0).Succeeded());

        m_scene.Initialize(&m_manager.MainArena);
        m_serializer.Initialize(&m_scene, &m_manager.MainArena);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
        std::error_code ec;
        std::filesystem::remove_all(m_root.c_str(), ec);
    }

    void FullPath(const char* rel, char (&out)[512]) const
    {
        snprintf(out, sizeof(out), "%s/%s", m_root.c_str(), rel);
    }

    // out must be large enough for the file plus a terminator.
    void ReadRaw(const char* rel, char* out, size_t out_size) const
    {
        char path[512];
        FullPath(rel, path);
        out[0]       = '\0';
        std::FILE* f = std::fopen(path, "rb");
        ASSERT_NE(f, nullptr);
        const size_t n = std::fread(out, 1, out_size - 1, f);
        out[n]         = '\0';
        std::fclose(f);
    }

    void WriteRaw(const char* rel, const char* text) const
    {
        char path[512];
        FullPath(rel, path);
        std::FILE* f = std::fopen(path, "wb");
        ASSERT_NE(f, nullptr);
        std::fwrite(text, 1, std::strlen(text), f);
        std::fclose(f);
    }
};

TEST_F(YAMLSceneSerializerTest, SerializeWritesReadableYAML)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    m_scene.GetComponent<TransformComponent>(id)->Position = {1.f, 2.f, 3.f};
    m_scene.AddComponent<NameComponent>(id, {});
    snprintf(m_scene.GetComponent<NameComponent>(id)->Value, 128, "%s", "PlayerMesh");

    SceneSnapshot snap{};
    snap.Name.init(&m_manager.MainArena, "MainLevel");
    snap.Entities.init(&m_manager.MainArena, 8);
    snap.Entities.push(id);

    ASSERT_TRUE(m_serializer.Serialize(m_ctx, P("/scenes/a.scene.yaml"), snap).Succeeded());

    char text[4096];
    ReadRaw("a.scene.yaml", text, sizeof(text));
    EXPECT_NE(std::strstr(text, "scene:"), nullptr);
    EXPECT_NE(std::strstr(text, "MainLevel"), nullptr);
    EXPECT_NE(std::strstr(text, "PlayerMesh"), nullptr);
    EXPECT_NE(std::strstr(text, "TransformComponent"), nullptr);
    EXPECT_EQ(std::strchr(text, '{'), nullptr); // block YAML, not JSON flow style
}

TEST_F(YAMLSceneSerializerTest, RoundTripRestoresComponentValues)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    auto* t     = m_scene.GetComponent<TransformComponent>(id);
    t->Position = {4.f, 5.f, 6.f};
    t->Scale    = {2.f, 2.f, 2.f};

    SceneSnapshot snap{};
    snap.Name.init(&m_manager.MainArena, "RT");
    snap.Entities.init(&m_manager.MainArena, 8);
    snap.Entities.push(id);
    ASSERT_TRUE(m_serializer.Serialize(m_ctx, P("/scenes/rt.scene.yaml"), snap).Succeeded());

    SceneSnapshot loaded{};
    ASSERT_TRUE(m_serializer.Deserialize(m_ctx, P("/scenes/rt.scene.yaml"), loaded).Succeeded());

    ASSERT_EQ(loaded.Entities.size(), 1u);
    auto* rt = m_scene.GetComponent<TransformComponent>(loaded.Entities[0]);
    ASSERT_NE(rt, nullptr);
    EXPECT_FLOAT_EQ(rt->Position.x, 4.f);
    EXPECT_FLOAT_EQ(rt->Position.z, 6.f);
    EXPECT_FLOAT_EQ(rt->Scale.y, 2.f);
}

TEST_F(YAMLSceneSerializerTest, HandEditedYAMLLoads)
{
    WriteRaw("hand.scene.yaml", R"(
# edited by a human
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: MainLevel
  entities:
    - id: 1
      components:
        TransformComponent:
          position: [9.0, 8.0, 7.0]
          scale:    [1.0, 1.0, 1.0]
)");

    SceneSnapshot loaded{};
    ASSERT_TRUE(m_serializer.Deserialize(m_ctx, P("/scenes/hand.scene.yaml"), loaded).Succeeded());

    ASSERT_EQ(loaded.Entities.size(), 1u);
    auto* t = m_scene.GetComponent<TransformComponent>(loaded.Entities[0]);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->Position.x, 9.f);
    char uuid_text[37] = {};
    uuids::to_string<char>(loaded.SceneUUID, uuid_text);
    EXPECT_EQ(secure_strcmp(uuid_text, "550e8400-e29b-41d4-a716-446655440000"), 0);
}

TEST_F(YAMLSceneSerializerTest, RejectsFilePathAsAssetRef)
{
    WriteRaw("bad.scene.yaml", R"(
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: Bad
  entities:
    - id: 1
      components:
        MeshComponent:
          asset_uuid: "Assets/Meshes/cube.glb"
)");

    SceneSnapshot loaded{};
    auto          r = m_serializer.Deserialize(m_ctx, P("/scenes/bad.scene.yaml"), loaded);
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.Error(), VFSError::Corrupted);
}

TEST_F(YAMLSceneSerializerTest, UnknownComponentKeyIsSkippedNotFailed)
{
    WriteRaw("unknown.scene.yaml", R"(
scene:
  uuid: "550e8400-e29b-41d4-a716-446655440000"
  name: Fwd
  entities:
    - id: 1
      components:
        FutureComponent:
          whatever: 1
        TransformComponent:
          position: [1.0, 1.0, 1.0]
          scale:    [1.0, 1.0, 1.0]
)");

    SceneSnapshot loaded{};
    ASSERT_TRUE(m_serializer.Deserialize(m_ctx, P("/scenes/unknown.scene.yaml"), loaded).Succeeded());
    ASSERT_EQ(loaded.Entities.size(), 1u);
    EXPECT_NE(m_scene.GetComponent<TransformComponent>(loaded.Entities[0]), nullptr);
}

TEST_F(YAMLSceneSerializerTest, MalformedYAMLReturnsCorruptedNotThrow)
{
    WriteRaw("broken.scene.yaml", "scene:\n  entities: [ {unclosed\n");

    SceneSnapshot loaded{};
    auto          r = m_serializer.Deserialize(m_ctx, P("/scenes/broken.scene.yaml"), loaded);
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.Error(), VFSError::Corrupted);
}

TEST_F(YAMLSceneSerializerTest, MissingTopLevelSceneKeyIsCorrupted)
{
    WriteRaw("nokey.scene.yaml", "something_else: 1\n");

    SceneSnapshot loaded{};
    auto          r = m_serializer.Deserialize(m_ctx, P("/scenes/nokey.scene.yaml"), loaded);
    EXPECT_FALSE(r.Succeeded());
    EXPECT_EQ(r.Error(), VFSError::Corrupted);
}

TEST_F(YAMLSceneSerializerTest, MissingFileReturnsError)
{
    SceneSnapshot loaded{};
    EXPECT_FALSE(m_serializer.Deserialize(m_ctx, P("/scenes/nope.scene.yaml"), loaded).Succeeded());
}

TEST_F(YAMLSceneSerializerTest, ValidUUIDAssetRefPasses)
{
    YAML::Node node = YAML::Load(R"(
MeshComponent:
  asset_uuid: "a1b2c3d4-e5f6-7890-abcd-ef1234567890"
)");
    EXPECT_TRUE(YAMLSceneSerializer::ValidateAssetRefs(node).Succeeded());
}
#endif // ZENGINE_EDITOR
