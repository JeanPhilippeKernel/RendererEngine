#include <Tetragrama/EditorScene.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <gtest/gtest.h>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

class EditorActionsTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager memory = {};
    Scene                                ecs    = {};
    ActorManager                         actors = {};
    Tetragrama::EditorScene              scene  = {};

    void                                 SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        ecs.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, ecs);
    }
    void TearDown() override
    {
        actors.Shutdown();
        ecs.Shutdown();
    }
};

TEST_F(EditorActionsTest, CreationSelectsActorAndMarksDirtyWithDistinctIdentity)
{
    const auto first = scene.CreateActor(actors, "Actor");
    ASSERT_TRUE(first.Valid());
    EXPECT_TRUE(scene.IsDirty());
    EXPECT_EQ(scene.SelectedActorHandle.Index, first.Index);
    const auto identity = actors.Access(first)->GetComponent<UUIDComponent>()->Value;
    EXPECT_FALSE(identity.is_nil());
    EXPECT_STREQ(actors.Access(first)->GetComponent<NameComponent>()->Value, "Actor");
    EXPECT_NE(actors.Access(first)->GetComponent<TransformComponent>(), nullptr);
    scene.MarkDirty(false);
    const auto second = scene.CreateActor(actors, "Collection");
    EXPECT_TRUE(scene.IsDirty());
    EXPECT_NE(actors.Access(second)->GetComponent<UUIDComponent>()->Value, identity);
}

TEST_F(EditorActionsTest, InvalidCreationAndNoOpResetLeaveSavedSceneClean)
{
    EXPECT_FALSE(scene.CreateActor(actors, nullptr).Valid());
    EXPECT_FALSE(scene.IsDirty());
    EXPECT_FALSE(scene.ResetActorTransform(actors, {}));
    const auto handle = scene.CreateActor(actors, "Actor");
    scene.MarkDirty(false);
    EXPECT_FALSE(scene.ResetActorTransform(actors, handle));
    EXPECT_FALSE(scene.IsDirty());
    actors.Destroy(handle);
    EXPECT_FALSE(scene.ResetActorTransform(actors, handle));
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(EditorActionsTest, ResetChangesLocalTRSWithoutOverwritingComputedWorldTransform)
{
    const auto handle               = scene.CreateActor(actors, "Actor");
    auto*      transform            = actors.Access(handle)->GetComponent<TransformComponent>();
    transform->Position             = {1.f, 2.f, 3.f};
    transform->Rotation             = {0.f, 1.f, 0.f};
    transform->Scale                = {2.f, 3.f, 4.f};
    transform->WorldTransform(0, 3) = 100.f;
    scene.MarkDirty(false);
    EXPECT_TRUE(scene.ResetActorTransform(actors, handle));
    EXPECT_TRUE(scene.IsDirty());
    EXPECT_FLOAT_EQ(transform->Position.x, 0.f);
    EXPECT_FLOAT_EQ(transform->Rotation.y, 0.f);
    EXPECT_FLOAT_EQ(transform->Scale.z, 1.f);
    EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 100.f);
}

TEST_F(EditorActionsTest, FocusReadsWorldPositionForParentedActors)
{
    const auto parent                                                     = scene.CreateActor(actors, "Parent");
    const auto child                                                      = scene.CreateActor(actors, "Child");
    actors.Access(parent)->GetComponent<TransformComponent>()->Position.x = 100.f;
    actors.Access(child)->GetComponent<TransformComponent>()->Position.x  = 1.f;
    actors.Access(child)->AddComponent<ParentComponent>({actors.Access(parent)->GetEntityID()});
    Systems::SyncHierarchy(ecs);
    ZEngine::Core::Maths::Vec3f position = {};
    ASSERT_TRUE(Tetragrama::EditorScene::TryGetActorWorldPosition(actors, child, position));
    EXPECT_FLOAT_EQ(position.x, 101.f);
    EXPECT_FALSE(Tetragrama::EditorScene::TryGetActorWorldPosition(actors, {}, position));
}

TEST_F(EditorActionsTest, FocusIncludesParentRotationAndNonuniformScale)
{
    const auto parent                                                  = scene.CreateActor(actors, "Parent");
    const auto child                                                   = scene.CreateActor(actors, "Child");
    auto*      transform                                               = actors.Access(parent)->GetComponent<TransformComponent>();
    transform->Position                                                = {100.f, 2.f, 0.f};
    transform->Rotation.z                                              = 1.5707963268f;
    transform->Scale                                                   = {2.f, 3.f, 4.f};
    actors.Access(child)->GetComponent<TransformComponent>()->Position = {1.f, 2.f, 3.f};
    actors.Access(child)->AddComponent<ParentComponent>({actors.Access(parent)->GetEntityID()});
    Systems::SyncHierarchy(ecs);
    ZEngine::Core::Maths::Vec3f position = {};
    ASSERT_TRUE(Tetragrama::EditorScene::TryGetActorWorldPosition(actors, child, position));
    // The engine's positive Z rotation maps (x, y) to (y, -x).
    EXPECT_NEAR(position.x, 106.f, 0.001f);
    EXPECT_NEAR(position.y, 0.f, 0.001f);
    EXPECT_NEAR(position.z, 12.f, 0.001f);
}
