#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/SceneRuntimeCamera.h>
#include <gtest/gtest.h>
#include <limits>

using ZEngine::ECS::CaptureSceneRuntimeCamera;
using ZEngine::ECS::Components::CameraComponent;
using ZEngine::ECS::Components::TransformComponent;
using ZEngine::ECS::Components::UUIDComponent;

namespace ZEngine::ECS
{
    class SceneRuntimeCameraTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager         Memory      = {};
        Scene                               World       = {};
        SceneDiagnostics                    Diagnostics = {};
        Rendering::Cameras::CameraFrameData Frame       = {};
        const uuids::uuid                   First       = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        const uuids::uuid                   Second      = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

        void                                SetUp() override
        {
            Memory.Initialize(ZMega(32), {});
            World.Initialize(&Memory.MainArena);
        }
        void TearDown() override
        {
            World.Shutdown();
        }
        EntityID AddCamera(const uuids::uuid& id, bool main)
        {
            const auto entity = World.CreateEntity();
            World.AddComponent<UUIDComponent>(entity, {id});
            World.AddComponent<TransformComponent>(entity, {});
            CameraComponent camera = {};
            camera.IsMain          = main;
            World.AddComponent<CameraComponent>(entity, camera);
            return entity;
        }
        bool Capture(const uuids::uuid& id = {}, uint32_t width = 1600, uint32_t height = 900)
        {
            return CaptureSceneRuntimeCamera(World, id, width, height, &Frame, &Diagnostics);
        }
    };

    TEST_F(SceneRuntimeCameraTest, RequiresExactlyOneMainUnlessAUuidIsSpecified)
    {
        const auto first                                         = AddCamera(First, true);
        const auto second                                        = AddCamera(Second, true);
        World.GetComponent<TransformComponent>(second)->Position = {1.f, 2.f, 3.f};
        World.SynchronizeHierarchy();
        EXPECT_FALSE(Capture());
        ASSERT_TRUE(Capture(Second));
        EXPECT_FLOAT_EQ(Frame.Position.x, 1.f);
        EXPECT_FLOAT_EQ(Frame.Position.y, 2.f);
        EXPECT_FLOAT_EQ(Frame.Position.z, 3.f);
        EXPECT_FLOAT_EQ(Frame.View(0, 3), -1.f);
        EXPECT_FLOAT_EQ(Frame.View(1, 3), -2.f);
        EXPECT_FLOAT_EQ(Frame.View(2, 3), -3.f);
        EXPECT_LT(Frame.Projection(1, 1), 0.f);
        EXPECT_FLOAT_EQ(Frame.Projection(1, 1) / Frame.Projection(0, 0), -1600.f / 900.f);
        World.GetComponent<CameraComponent>(second)->IsMain = false;
        ASSERT_TRUE(Capture());
        EXPECT_FLOAT_EQ(Frame.Position.x, 0.f);
        World.GetComponent<CameraComponent>(first)->IsMain = false;
        EXPECT_FALSE(Capture());
        EXPECT_FALSE(Capture(*uuids::uuid::from_string("33333333-3333-4333-8333-333333333333")));
    }

    TEST_F(SceneRuntimeCameraTest, UsesSynchronizedParentTransformAndRejectsInvalidInputs)
    {
        const auto         parent           = World.CreateEntity();
        TransformComponent parent_transform = {};
        parent_transform.Position           = {4.f, 0.f, 0.f};
        World.AddComponent<TransformComponent>(parent, parent_transform);
        const auto camera                                        = AddCamera(First, true);
        World.GetComponent<TransformComponent>(camera)->Position = {1.f, 0.f, 0.f};
        ASSERT_TRUE(World.SetParent(camera, parent));
        EXPECT_FALSE(Capture()); // hierarchy has not been synchronized
        World.SynchronizeHierarchy();
        ASSERT_TRUE(Capture());
        EXPECT_FLOAT_EQ(Frame.Position.x, 5.f);
        EXPECT_FALSE(Capture({}, 0, 900));
        auto* projection = World.GetComponent<CameraComponent>(camera);
        projection->Near = projection->Far;
        EXPECT_FALSE(Capture());
        projection->Near = 0.1f;
        projection->FovY = std::numeric_limits<float>::infinity();
        EXPECT_FALSE(Capture());
        projection->FovY                                      = 60.f;
        World.GetComponent<TransformComponent>(camera)->Scale = {0.f, 1.f, 1.f};
        World.SynchronizeHierarchy();
        EXPECT_FALSE(Capture());
    }

    TEST_F(SceneRuntimeCameraTest, AmbiguousOrIncompleteCameraDoesNotOverwriteThePreviousFrame)
    {
        const auto first                                        = AddCamera(First, true);
        World.GetComponent<TransformComponent>(first)->Position = {2.f, 3.f, 4.f};
        World.SynchronizeHierarchy();
        ASSERT_TRUE(Capture(First));
        const auto original  = Frame;

        const auto duplicate = AddCamera(First, false);
        World.SynchronizeHierarchy();
        EXPECT_FALSE(Capture(First));
        EXPECT_FLOAT_EQ(Frame.Position.x, original.Position.x);
        EXPECT_FLOAT_EQ(Frame.Position.y, original.Position.y);
        EXPECT_FLOAT_EQ(Frame.View(0, 3), original.View(0, 3));
        World.DestroyEntity(duplicate);
        World.RemoveComponent<TransformComponent>(first);
        World.SynchronizeHierarchy();
        EXPECT_FALSE(Capture(First));
        EXPECT_FLOAT_EQ(Frame.Position.x, original.Position.x);
        EXPECT_FLOAT_EQ(Frame.Projection(0, 0), original.Projection(0, 0));
        EXPECT_FALSE(CaptureSceneRuntimeCamera(World, First, 1600, 900, nullptr, &Diagnostics));
    }
} // namespace ZEngine::ECS
