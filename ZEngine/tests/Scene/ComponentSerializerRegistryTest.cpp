#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/ComponentSerializerRegistry.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneSnapshot.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <gtest/gtest.h>
#include <cstring>
#include "SceneTestRegistries.h"

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;
using ZEngine::Helpers::secure_strcmp;

namespace
{
    ComponentSerializerRegistry& Registry()
    {
        SceneTests::EnsureRegistries();
        return ComponentSerializerRegistry::Get();
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
        SceneTests::EnsureRegistries();
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

    int        hits_before = SceneTests::YAMLContextHits();
    YAML::Node node;
    fns->SerializeYAML(fns->Context, id, m_scene, node);
    EXPECT_EQ(SceneTests::YAMLContextHits(), hits_before + 1); // Context was forwarded

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
    cstring text = emitter.c_str();

    EXPECT_NE(std::strstr(text, "position"), nullptr);
    EXPECT_EQ(std::strchr(text, '{'), nullptr); // block YAML, not JSON-style flow

    t->Position         = {0.f, 0.f, 0.f};
    YAML::Node reparsed = YAML::Load(text);
    fns->DeserializeYAML(fns->Context, id, m_scene, reparsed);

    EXPECT_FLOAT_EQ(t->Position.x, 10.f);
    EXPECT_FLOAT_EQ(t->Position.z, 30.f);
}

TEST_F(SerializerRegistryFixture, SecondInitializeDoesNotDiscardRegistrations)
{
    ASSERT_TRUE(Registry().IsInitialized());
    const uint32_t before = Registry().Count();
    ASSERT_GT(before, 0u);

    MemoryManager other;
    other.Initialize(ZMega(1), {});
    ComponentSerializerRegistry::Get().Initialize(&other.MainArena);

    EXPECT_EQ(Registry().Count(), before);
    EXPECT_NE(Registry().Lookup(ComponentTypeOf<TransformComponent>()), nullptr);
}

TEST_F(SerializerRegistryFixture, ReflectionRegistrySurvivesSecondInitializeToo)
{
    const uint32_t before = ComponentReflectionRegistry::Get().Count();
    ASSERT_GT(before, 0u);

    MemoryManager other;
    other.Initialize(ZMega(1), {});
    ComponentReflectionRegistry::Get().Initialize(&other.MainArena);

    EXPECT_EQ(ComponentReflectionRegistry::Get().Count(), before);
}

TEST_F(SerializerRegistryFixture, SnapshotCreateInitializesItsContainers)
{
    SceneSnapshot snap = SceneSnapshot::Create(&m_manager.MainArena, "Level", 4);
    EXPECT_EQ(snap.Entities.size(), 0u);
    EXPECT_GE(snap.Entities.capacity(), 4u);

    snap.Entities.push(EntityID{1, 1}); // would assert on an uninitialized Array
    EXPECT_EQ(snap.Entities.size(), 1u);
    EXPECT_EQ(secure_strcmp(snap.Name.c_str(), "Level"), 0);
}
