#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ComponentSerializerRegistry.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;

namespace
{
    ComponentSerializerRegistry& Registry()
    {
        static MemoryManager s_manager = [] {
            MemoryManager m;
            m.Initialize(ZMega(8), {});
            ComponentSerializerRegistry::Get().Initialize(&m.MainArena);
            return m;
        }();
        (void) s_manager;
        return ComponentSerializerRegistry::Get();
    }

    void SerializeTransformYAML(void* ctx, EntityID id, const Scene& scene, YAML::Node& node)
    {
        if (ctx)
            *static_cast<int*>(ctx) += 1; // proves Context reaches the callback
        const auto* t = scene.GetComponent<TransformComponent>(id);
        if (!t)
            return;
        node["position"] = std::vector<float>{t->Position.x, t->Position.y, t->Position.z};
        node["scale"]    = std::vector<float>{t->Scale.x, t->Scale.y, t->Scale.z};
    }

    void DeserializeTransformYAML(void*, EntityID id, Scene& scene, const YAML::Node& node)
    {
        auto* t = scene.GetComponent<TransformComponent>(id);
        if (!t)
            return;
        const auto p = node["position"].as<std::vector<float>>();
        t->Position  = {p[0], p[1], p[2]};
        const auto s = node["scale"].as<std::vector<float>>();
        t->Scale     = {s[0], s[1], s[2]};
    }

    void SerializeTransformBinary(void*, EntityID id, const Scene& scene, Core::Containers::Array<uint8_t>& out)
    {
        const auto*    t   = scene.GetComponent<TransformComponent>(id);
        const uint8_t* raw = reinterpret_cast<const uint8_t*>(t);
        for (uint32_t i = 0; i < sizeof(TransformComponent); ++i)
            out.push(raw[i]);
    }

    void DeserializeTransformBinary(void*, EntityID id, Scene& scene, const uint8_t* data, uint32_t size)
    {
        auto* t = scene.GetComponent<TransformComponent>(id);
        if (!t || size != sizeof(TransformComponent))
            return;
        *t = *reinterpret_cast<const TransformComponent*>(data);
    }

    int  s_yaml_ctx_hits = 0;

    void RegisterTestSerializers()
    {
        static bool s_once = [] {
            Registry().Register(
                ComponentTypeOf<TransformComponent>(),
                {
                .SerializeYAML     = SerializeTransformYAML,
                .DeserializeYAML   = DeserializeTransformYAML,
                .SerializeBinary   = SerializeTransformBinary,
                .DeserializeBinary = DeserializeTransformBinary,
                .Context           = &s_yaml_ctx_hits,
                });
            return true;
        }();
        (void) s_once;
    }

    struct CountCtx
    {
        uint32_t        Visited = 0;
        ComponentTypeID Last    = 0;
    };

    void CountVisitor(void* ctx, ComponentTypeID type_id, const ComponentSerializeFns&)
    {
        auto* c = static_cast<CountCtx*>(ctx);
        c->Visited++;
        c->Last = type_id;
    }
} // namespace

class SerializerRegistryFixture : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    Scene         m_scene;

    void          SetUp() override
    {
        RegisterTestSerializers();
        m_manager.Initialize(ZMega(32), {});
        m_scene.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }
};

TEST_F(SerializerRegistryFixture, LookupReturnsRegisteredFunctions)
{
    const auto* fns = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);
    EXPECT_NE(fns->SerializeYAML, nullptr);
    EXPECT_NE(fns->DeserializeYAML, nullptr);
    EXPECT_NE(fns->SerializeBinary, nullptr);
    EXPECT_NE(fns->DeserializeBinary, nullptr);
}

TEST_F(SerializerRegistryFixture, LookupOfUnregisteredTypeReturnsNull)
{
    EXPECT_EQ(Registry().Lookup(9999u), nullptr);
}

TEST_F(SerializerRegistryFixture, RegisterIgnoresDuplicateTypeID)
{
    uint32_t before = Registry().Count();
    Registry().Register(ComponentTypeOf<TransformComponent>(), {});
    EXPECT_EQ(Registry().Count(), before);

    // The original functions survive the ignored re-registration.
    const auto* fns = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);
    EXPECT_NE(fns->SerializeYAML, nullptr);
}

TEST_F(SerializerRegistryFixture, ForEachVisitsEveryRegisteredType)
{
    CountCtx c{};
    Registry().ForEach(CountVisitor, &c);
    EXPECT_EQ(c.Visited, Registry().Count());
    EXPECT_GT(c.Visited, 0u);
}

TEST_F(SerializerRegistryFixture, ForEachWithNullFunctionIsANoOp)
{
    Registry().ForEach(nullptr, nullptr); // must not crash
    SUCCEED();
}

TEST_F(SerializerRegistryFixture, YAMLRoundTripThroughDispatch)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    auto* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    t->Position     = {1.f, 2.f, 3.f};
    t->Scale        = {4.f, 5.f, 6.f};

    const auto* fns = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);

    int        hits_before = s_yaml_ctx_hits;
    YAML::Node node;
    fns->SerializeYAML(fns->Context, id, m_scene, node);
    EXPECT_EQ(s_yaml_ctx_hits, hits_before + 1); // Context was forwarded

    t->Position = {0.f, 0.f, 0.f};
    t->Scale    = {0.f, 0.f, 0.f};
    fns->DeserializeYAML(fns->Context, id, m_scene, node);

    EXPECT_FLOAT_EQ(t->Position.x, 1.f);
    EXPECT_FLOAT_EQ(t->Position.z, 3.f);
    EXPECT_FLOAT_EQ(t->Scale.y, 5.f);
}

TEST_F(SerializerRegistryFixture, BinaryRoundTripThroughDispatch)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    auto* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    t->Position     = {7.f, 8.f, 9.f};

    const auto* fns = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);

    Core::Containers::Array<uint8_t> blob;
    blob.init(&m_manager.MainArena, 128);
    fns->SerializeBinary(fns->Context, id, m_scene, blob);
    EXPECT_EQ(blob.size(), sizeof(TransformComponent));

    t->Position = {0.f, 0.f, 0.f};
    fns->DeserializeBinary(fns->Context, id, m_scene, blob.data(), static_cast<uint32_t>(blob.size()));

    EXPECT_FLOAT_EQ(t->Position.x, 7.f);
    EXPECT_FLOAT_EQ(t->Position.z, 9.f);
}

TEST_F(SerializerRegistryFixture, HandWrittenYAMLWithCommentsParsesBack)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});

    const char* edited_by_hand = R"(
# tweaked in a text editor
position: [1.5, 2.5, 3.5]
scale:    [2.0, 2.0, 2.0]   # made it bigger
)";

    YAML::Node  node           = YAML::Load(edited_by_hand);
    const auto* fns            = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);
    fns->DeserializeYAML(fns->Context, id, m_scene, node);

    auto* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    EXPECT_FLOAT_EQ(t->Position.x, 1.5f);
    EXPECT_FLOAT_EQ(t->Position.z, 3.5f);
    EXPECT_FLOAT_EQ(t->Scale.y, 2.0f);
}

TEST_F(SerializerRegistryFixture, EmittedYAMLTextRoundTrips)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponent<TransformComponent>(id, {});
    auto* t = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(t, nullptr);
    t->Position     = {10.f, 20.f, 30.f};

    const auto* fns = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    ASSERT_NE(fns, nullptr);

    YAML::Node out;
    fns->SerializeYAML(fns->Context, id, m_scene, out);

    YAML::Emitter emitter;
    emitter << out;
    const std::string text = emitter.c_str();

    EXPECT_NE(text.find("position"), std::string::npos);
    EXPECT_EQ(text.find('{'), std::string::npos); // block YAML, not JSON-style flow

    t->Position         = {0.f, 0.f, 0.f};
    YAML::Node reparsed = YAML::Load(text);
    fns->DeserializeYAML(fns->Context, id, m_scene, reparsed);

    EXPECT_FLOAT_EQ(t->Position.x, 10.f);
    EXPECT_FLOAT_EQ(t->Position.z, 30.f);
}
