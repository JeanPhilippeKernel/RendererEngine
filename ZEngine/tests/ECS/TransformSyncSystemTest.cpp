#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Systems/TransformSyncSystem.h>
#include <gtest/gtest.h>
#include <algorithm>

using ZEngine::Core::Memory::MemoryManager;
using ZEngine::ECS::Scene;
using ZEngine::ECS::Components::MeshComponent;
using ZEngine::ECS::Components::TransformComponent;
using ZEngine::Rendering::Scenes::RenderScene;

class TransformSyncSystemTest : public testing::Test
{
protected:
    MemoryManager Memory = {};
    Scene         World  = {};
    RenderScene   Render = {};
    void          SetUp() override
    {
        Memory.Initialize(ZMega(64), {});
        World.Initialize(&Memory.MainArena);
        Render.Instances.init(&Memory.MainArena, 8193);
    }
    void TearDown() override
    {
        World.Shutdown();
        Memory.Shutdown();
    }
    ZEngine::ECS::EntityID Add(uint32_t id, float x)
    {
        const auto entity = World.CreateEntity();
        auto       matrix = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        matrix(0, 3)      = x;
        World.AddComponent<TransformComponent>(entity, {.WorldTransform = matrix});
        World.AddComponent<MeshComponent>(entity, {{}, id});
        Render.Instances.push({.Id = id});
        return entity;
    }
    void Sync()
    {
        ZEngine::ECS::Systems::SyncECSToRenderScene(World, 1.f, Render);
    }
};

TEST_F(TransformSyncSystemTest, ExactCapJoinsReorderedSparseIdentitiesWithoutAllocating)
{
    constexpr auto cap = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    for (uint32_t index = 0; index < cap; ++index)
    {
        Add(index * 4099 + 17, float(index));
    }
    std::reverse(Render.Instances.begin(), Render.Instances.end());
    const auto before = Memory.MainArena.m_current_offset;
    Sync();
    EXPECT_EQ(Memory.MainArena.m_current_offset, before);
    for (const auto& instance : Render.Instances)
    {
        const auto* mesh      = World.GetComponent<MeshComponent>(instance.Entity);
        const auto* transform = World.GetComponent<TransformComponent>(instance.Entity);
        ASSERT_NE(mesh, nullptr);
        ASSERT_NE(transform, nullptr);
        EXPECT_EQ(mesh->RenderInstanceId, instance.Id);
        EXPECT_FLOAT_EQ(instance.Transform(0, 3), float((instance.Id - 17) / 4099));
    }
}

TEST_F(TransformSyncSystemTest, MissingStaleAndUnboundIdentitiesCannotChangeAnotherInstance)
{
    const auto retained = Add(70, 7.f);
    const auto missing  = Add(90, 9.f);
    Render.Instances.erase(1);
    const auto unbound = Add(UINT32_MAX, 11.f);
    Sync();
    EXPECT_EQ(Render.Instances[0].Entity, retained);
    EXPECT_FLOAT_EQ(Render.Instances[0].Transform(0, 3), 7.f);
    EXPECT_FALSE(Render.Instances[1].Entity.IsValid());
    EXPECT_NE(Render.Instances[0].Entity, missing);
    EXPECT_NE(Render.Instances[0].Entity, unbound);
    EXPECT_FALSE(Render.SetInstanceStateAtIndex(2, {}, missing));
}

TEST_F(TransformSyncSystemTest, RemovalAndIdentityEditsRebuildTheJoinAndDuplicateIdsKeepFirstMatch)
{
    const auto entity = Add(12, 3.f);
    Render.Instances.push({.Id = 12});
    Sync();
    EXPECT_EQ(Render.Instances[0].Entity, entity);
    EXPECT_FALSE(Render.Instances[1].Entity.IsValid());
    Render.Instances.erase(0);
    Render.Instances[0].Id                                      = 1234;
    World.GetComponent<MeshComponent>(entity)->RenderInstanceId = 1234;
    Sync();
    EXPECT_EQ(Render.Instances[0].Entity, entity);
    EXPECT_FLOAT_EQ(Render.Instances[0].Transform(0, 3), 3.f);
    for (const auto& dirty : Render.InstancesDirty)
    {
        EXPECT_TRUE(dirty.value.load());
    }
}

TEST_F(TransformSyncSystemTest, ReusedEntitySlotsBindTheNewGenerationNotTheStaleSnapshot)
{
    const auto old_entity = Add(91, 1.f);
    Sync();
    World.DestroyEntity(old_entity);
    const auto current = Add(91, 2.f);
    ASSERT_NE(current, old_entity);
    Sync();
    EXPECT_EQ(Render.Instances[0].Entity, current);
    EXPECT_FLOAT_EQ(Render.Instances[0].Transform(0, 3), 2.f);
}

TEST_F(TransformSyncSystemTest, LegacyOverCapArraysAreNotSilentlyTruncated)
{
    const auto     entity = Add(1, 4.f);
    constexpr auto cap    = ZEngine::Rendering::Scenes::SceneData::MAX_DRAW_COMMANDS;
    for (uint32_t index = 1; index <= cap; ++index)
    {
        Render.Instances.push({.Id = index + 1});
    }
    World.GetComponent<MeshComponent>(entity)->RenderInstanceId = cap + 1;
    Sync();
    EXPECT_EQ(Render.Instances[cap].Entity, entity);
    EXPECT_FLOAT_EQ(Render.Instances[cap].Transform(0, 3), 4.f);
}
