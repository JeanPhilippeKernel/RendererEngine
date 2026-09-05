#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Memory;

namespace
{
    // The registry is a process-wide singleton, so initialize and register exactly once
    // for the whole suite. Register() is a no-op on duplicate TypeIDs anyway.
    struct ReflectionEnv
    {
        MemoryManager Manager;

        ReflectionEnv()
        {
            Manager.Initialize(ZMega(8), {});
            ComponentReflectionRegistry::Get().Initialize(&Manager.MainArena);

            RegisterBuiltInComponentReflection();
        }
    };

    const ComponentReflectionRegistry& Registry()
    {
        static ReflectionEnv s_env;
        return ComponentReflectionRegistry::Get();
    }

    const FieldDescriptor* FindField(const ComponentMeta& meta, cstring name)
    {
        for (uint32_t i = 0; i < meta.FieldCount; ++i)
        {
            if (std::strcmp(meta.Fields[i].Name, name) == 0)
                return &meta.Fields[i];
        }
        return nullptr;
    }
} // namespace

TEST(ComponentReflection, AllEightBuiltInsAreRegistered)
{
    EXPECT_EQ(Registry().Count(), 8u);
}

TEST(ComponentReflection, LookupByTypeIDMatchesLookupByName)
{
    const ComponentMeta* by_id   = Registry().Lookup(ComponentTypeOf<TransformComponent>());
    const ComponentMeta* by_name = Registry().LookupByName("TransformComponent");

    ASSERT_NE(by_id, nullptr);
    ASSERT_NE(by_name, nullptr);
    EXPECT_EQ(by_id, by_name);
    EXPECT_EQ(by_id->Size, sizeof(TransformComponent));
    EXPECT_EQ(by_id->Align, alignof(TransformComponent));
}

TEST(ComponentReflection, LookupMissesReturnNull)
{
    EXPECT_EQ(Registry().Lookup(9999u), nullptr);
    EXPECT_EQ(Registry().LookupByName("NoSuchComponent"), nullptr);
    EXPECT_EQ(Registry().LookupByName(nullptr), nullptr);
}

TEST(ComponentReflection, ForEachVisitsAllEightInRegistrationOrder)
{
    cstring expected[] = {
        "TransformComponent",
        "MeshComponent",
        "CameraComponent",
        "LightComponent",
        "MaterialComponent",
        "NameComponent",
        "RigidBodyComponent",
        "UUIDComponent",
    };

    uint32_t visited = 0;
    Registry().ForEach([&](const ComponentMeta& meta) {
        ASSERT_LT(visited, 8u);
        EXPECT_STREQ(meta.TypeName, expected[visited]);
        ++visited;
    });
    EXPECT_EQ(visited, 8u);
}

TEST(ComponentReflection, NameComponentValueIsEditableStringWithCap128)
{
    const ComponentMeta* meta = Registry().LookupByName("NameComponent");
    ASSERT_NE(meta, nullptr);

    const FieldDescriptor* value = FindField(*meta, "Value");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->Type, FieldType::String);
    EXPECT_EQ(value->StringCap, 128u);
    EXPECT_FALSE(value->ReadOnly);
    EXPECT_FALSE(value->Hidden);
}

TEST(ComponentReflection, UUIDComponentValueIsReadOnly)
{
    const ComponentMeta* meta = Registry().LookupByName("UUIDComponent");
    ASSERT_NE(meta, nullptr);

    const FieldDescriptor* value = FindField(*meta, "Value");
    ASSERT_NE(value, nullptr);
    EXPECT_EQ(value->Type, FieldType::AssetUUID);
    EXPECT_TRUE(value->ReadOnly);
}

TEST(ComponentReflection, HiddenAndReadOnlyFlagsMatchSpec)
{
    const ComponentMeta* transform = Registry().LookupByName("TransformComponent");
    ASSERT_NE(transform, nullptr);
    EXPECT_TRUE(FindField(*transform, "PreviousPosition")->Hidden);
    EXPECT_FALSE(FindField(*transform, "Position")->Hidden);

    const ComponentMeta* mesh = Registry().LookupByName("MeshComponent");
    ASSERT_NE(mesh, nullptr);
    EXPECT_TRUE(FindField(*mesh, "MeshUUID")->ReadOnly);
    EXPECT_TRUE(FindField(*mesh, "RenderInstanceId")->Hidden);

    const ComponentMeta* body = Registry().LookupByName("RigidBodyComponent");
    ASSERT_NE(body, nullptr);
    EXPECT_TRUE(FindField(*body, "BodyID")->ReadOnly);
    EXPECT_TRUE(FindField(*body, "BodyID")->Hidden);
}

TEST(ComponentReflection, EnumFieldsCarryTheirValueTables)
{
    const FieldDescriptor* light = FindField(*Registry().LookupByName("LightComponent"), "LightType");
    ASSERT_NE(light, nullptr);
    EXPECT_EQ(light->Type, FieldType::Enum);
    ASSERT_EQ(light->EnumCount, 3u);
    EXPECT_STREQ(light->EnumValues[0].Name, "Directional");
    EXPECT_EQ(light->EnumValues[2].Value, static_cast<int64_t>(LightComponent::Type::Spot));

    const FieldDescriptor* body = FindField(*Registry().LookupByName("RigidBodyComponent"), "MotionKind");
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->Type, FieldType::Enum);
    ASSERT_EQ(body->EnumCount, 3u);
    EXPECT_STREQ(body->EnumValues[2].Name, "Dynamic");
    EXPECT_EQ(body->EnumValues[2].Value, static_cast<int64_t>(RigidBodyComponent::MotionType::Dynamic));
}

TEST(ComponentReflection, EveryFieldFitsWithinItsComponent)
{
    Registry().ForEach([](const ComponentMeta& meta) {
        for (uint32_t i = 0; i < meta.FieldCount; ++i)
        {
            const FieldDescriptor& f = meta.Fields[i];
            EXPECT_NE(f.Name, nullptr) << meta.TypeName << " field " << i;
            EXPECT_GT(f.Size, 0u) << meta.TypeName << "." << f.Name;
            EXPECT_LE(f.Offset + f.Size, meta.Size) << meta.TypeName << "." << f.Name << " runs past the end of the component";
        }
    });
}

class ReflectionSceneFixture : public ::testing::Test
{
protected:
    MemoryManager m_manager;
    Scene         m_scene;

    void          SetUp() override
    {
        Registry();
        m_manager.Initialize(ZMega(64), {});
        m_scene.Initialize(&m_manager.MainArena);
    }

    void TearDown() override
    {
        m_scene.Shutdown();
    }
};

TEST_F(ReflectionSceneFixture, EveryBuiltInHasAnAddFactory)
{
    Registry().ForEach([](const ComponentMeta& meta) { EXPECT_NE(meta.Add, nullptr) << meta.TypeName; });
}

TEST_F(ReflectionSceneFixture, AddComponentRawCreatesEveryBuiltInType)
{
    EntityID id = m_scene.CreateEntity();

    Registry().ForEach([&](const ComponentMeta& meta) {
        EXPECT_EQ(m_scene.GetComponentRaw(id, meta.TypeID), nullptr) << meta.TypeName;
        m_scene.AddComponentRaw(id, meta.TypeID);
        EXPECT_NE(m_scene.GetComponentRaw(id, meta.TypeID), nullptr) << meta.TypeName;
        EXPECT_TRUE(MaskHas(m_scene.GetMask(id), meta.TypeID)) << meta.TypeName;
    });
}

TEST_F(ReflectionSceneFixture, AddComponentRawAppliesDefaultMemberInitializers)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponentRaw(id, ComponentTypeOf<TransformComponent>());

    auto* tc = m_scene.GetComponent<TransformComponent>(id);
    ASSERT_NE(tc, nullptr);
    // NOT memset-to-zero: a zeroed Scale would make every added actor invisible.
    EXPECT_FLOAT_EQ(tc->Scale.x, 1.f);
    EXPECT_FLOAT_EQ(tc->Scale.y, 1.f);
    EXPECT_FLOAT_EQ(tc->Scale.z, 1.f);
    EXPECT_FLOAT_EQ(tc->Position.x, 0.f);
}

TEST_F(ReflectionSceneFixture, AddComponentRawIsANoOpOnDuplicate)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponentRaw(id, ComponentTypeOf<CameraComponent>());

    void* first = m_scene.GetComponentRaw(id, ComponentTypeOf<CameraComponent>());
    ASSERT_NE(first, nullptr);

    m_scene.AddComponentRaw(id, ComponentTypeOf<CameraComponent>());
    EXPECT_EQ(m_scene.GetComponentRaw(id, ComponentTypeOf<CameraComponent>()), first);
}

TEST_F(ReflectionSceneFixture, AddComponentRawIgnoresUnknownTypesAndDeadEntities)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponentRaw(id, 9999u);
    EXPECT_EQ(m_scene.GetComponentRaw(id, 9999u), nullptr);

    m_scene.DestroyEntity(id);
    m_scene.AddComponentRaw(id, ComponentTypeOf<NameComponent>()); // dead entity
    EXPECT_EQ(m_scene.GetComponentRaw(id, ComponentTypeOf<NameComponent>()), nullptr);
}

TEST_F(ReflectionSceneFixture, AddComponentRawUsesExistingStorageForLaterEntities)
{
    EntityID first = m_scene.CreateEntity();
    m_scene.AddComponentRaw(first, ComponentTypeOf<LightComponent>());

    EntityID second = m_scene.CreateEntity();
    m_scene.AddComponentRaw(second, ComponentTypeOf<LightComponent>());

    auto* lc = m_scene.GetComponent<LightComponent>(second);
    ASSERT_NE(lc, nullptr);
    EXPECT_TRUE(MaskHas(m_scene.GetMask(second), ComponentTypeOf<LightComponent>()));
    EXPECT_FLOAT_EQ(lc->Intensity, 1.f);
    EXPECT_FLOAT_EQ(lc->Color[0], 1.f);
}

TEST_F(ReflectionSceneFixture, AddComponentRawPreservesInactiveSentinels)
{
    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponentRaw(id, ComponentTypeOf<MeshComponent>());
    m_scene.AddComponentRaw(id, ComponentTypeOf<RigidBodyComponent>());

    auto* mc = m_scene.GetComponent<MeshComponent>(id);
    auto* rb = m_scene.GetComponent<RigidBodyComponent>(id);
    ASSERT_NE(mc, nullptr);
    ASSERT_NE(rb, nullptr);

    EXPECT_EQ(mc->RenderInstanceId, UINT32_MAX);
    EXPECT_EQ(rb->BodyID, UINT32_MAX);
    EXPECT_FLOAT_EQ(rb->Mass, 1.f);
}

TEST_F(ReflectionSceneFixture, AddComponentRawAcceptsMatchingSizeAndAlign)
{
    const ComponentMeta* meta = Registry().Lookup(ComponentTypeOf<CameraComponent>());
    ASSERT_NE(meta, nullptr);

    EntityID id = m_scene.CreateEntity();
    m_scene.AddComponentRaw(id, meta->TypeID, meta->Size, meta->Align);
    EXPECT_NE(m_scene.GetComponentRaw(id, meta->TypeID), nullptr);
}
