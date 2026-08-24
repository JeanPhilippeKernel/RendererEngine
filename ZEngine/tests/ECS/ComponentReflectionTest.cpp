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
