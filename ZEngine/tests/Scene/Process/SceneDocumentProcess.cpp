#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/RigidBodyComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneDocument.h>
#include <gtest/gtest.h>
#include <array>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    namespace ECS        = ZEngine::ECS;
    namespace Components = ZEngine::ECS::Components;
    namespace VFS        = ZEngine::Core::VFS;

    struct ProcessOptions
    {
        bool                  Write   = false;
        bool                  Reverse = false;
        bool                  Binary  = false;
        std::filesystem::path Root    = {};
    };

    struct RuntimeOnlyComponent
    {
        uint32_t Value = 0;
    };

    ProcessOptions             Options         = {};
    const auto                 SceneUUID       = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto                 ParentUUID      = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    const auto                 ChildUUID       = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    const auto                 MeshUUID        = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    const auto                 MaterialUUID    = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    const auto                 EnvironmentUUID = *uuids::uuid::from_string("66666666-6666-4666-8666-666666666666");

    ECS::SceneDocumentSettings ExpectedSettings()
    {
        ECS::SceneDocumentSettings settings                = {.UUID = SceneUUID, .Name = "Process round trip"};
        settings.Grid.Origin                               = {1.f, -2.f, 3.f};
        settings.Grid.Orientation                          = {0.f, 0.f, 1.f, 0.f};
        settings.Grid.BaseSpacing                          = .125f;
        settings.Grid.MajorLineEvery                       = 8;
        settings.Grid.MinorColor                           = {.1f, .2f, .3f, .4f};
        settings.Grid.MajorColor                           = {.5f, .6f, .7f, .8f};
        settings.Grid.UColor                               = {.8f, .7f, .6f, .5f};
        settings.Grid.VColor                               = {.4f, .3f, .2f, .1f};
        settings.Grid.LineWidth                            = 2.5f;
        settings.Grid.FadeRadius                           = 128.f;
        settings.Grid.FadeStrength                         = .75f;
        settings.Grid.MaxLOD                               = 4;
        settings.Grid.Labels                               = ECS::SceneGridLabels::UV;
        settings.Sky.Mode                                  = ZEngine::Rendering::Scenes::SkyMode::HDRI;
        settings.Sky.EnvironmentMap                        = EnvironmentUUID;
        settings.Sky.PrimaryCelestialLight                 = ParentUUID;
        settings.Sky.EnvironmentIntensity                  = 2.5f;
        settings.Sky.EnvironmentTint[2]                    = .5f;
        settings.Sky.EnvironmentYawRadians                 = .75f;
        settings.Sky.SkyLuminanceFactor                    = 1.5f;
        settings.Sky.SkyDiffuseIntensity                   = .5f;
        settings.Sky.SkySpecularIntensity                  = 2.f;
        settings.Sky.SkyLowerHemisphereColor[1]            = .25f;
        settings.Sky.AerialPerspectiveStartDepthKilometers = .125f;
        settings.Sky.AerialPerspectiveDistanceScale        = 2.f;
        settings.Sky.Atmosphere.GroundAmbientIrradiance    = .75f;
        settings.Sky.Sphere.ShowSunDisc                    = false;
        return settings;
    }

    class SceneDocumentProcessTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager Memory      = {};
        ECS::Scene                           Scene       = {};
        ECS::SceneComponentSchemaRegistry    Schemas     = {};
        ECS::SceneDiagnostics                Diagnostics = {};
        VFS::VFSDiskBackend                  Backend     = {};
        VFS::VFSContext                      Files       = {};

        void                                 SetUp() override
        {
            // Allocate real process-local IDs before production startup; never reset
            // the global counter. Reverse the built-ins' allocation order, and
            // seed an unrelated type so even the middle built-in changes ID.
            if (Options.Reverse)
                ASSERT_EQ(ECS::ComponentTypeOf<RuntimeOnlyComponent>(), 0u);
            const std::array<ECS::ComponentTypeID (*)(), 9> types = {&ECS::ComponentTypeOf<Components::UUIDComponent>, &ECS::ComponentTypeOf<Components::NameComponent>, &ECS::ComponentTypeOf<Components::TransformComponent>, &ECS::ComponentTypeOf<Components::ParentComponent>, &ECS::ComponentTypeOf<Components::MeshComponent>, &ECS::ComponentTypeOf<Components::MaterialComponent>, &ECS::ComponentTypeOf<Components::CameraComponent>, &ECS::ComponentTypeOf<Components::LightComponent>, &ECS::ComponentTypeOf<Components::RigidBodyComponent>};
            for (size_t i = 0; i < types.size(); ++i)
                ASSERT_EQ(types[Options.Reverse ? types.size() - 1 - i : i](), i + (Options.Reverse ? 1u : 0u));

            Memory.Initialize(ZMega(128), {});
            Scene.Initialize(&Memory.MainArena);
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas, &Diagnostics));
            ASSERT_EQ(Schemas.Count(), 8u);
            const auto root = Options.Root.generic_u8string();
            Backend.Initialize(reinterpret_cast<const char*>(root.c_str()), VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write, &Memory.MainArena, 4);
            Files.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Files.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
        }

        void TearDown() override
        {
            if (HasFailure())
                for (uint32_t i = 0; i < Diagnostics.Count; ++i)
                    std::cerr << Diagnostics.Entries[i].Message << '\n';
            Files.Shutdown();
            Scene.Shutdown();
        }

        void AddNamedEntity(ECS::EntityID entity, const uuids::uuid& uuid, const char* name)
        {
            Scene.AddComponent<Components::UUIDComponent>(entity, {uuid});
            Components::NameComponent label = {};
            ASSERT_EQ(ZEngine::Helpers::secure_strncpy(label.Value, sizeof(label.Value), name, ZEngine::Helpers::secure_strlen(name)), ZEngine::Helpers::MEMORY_OP_SUCCESS);
            Scene.AddComponent<Components::NameComponent>(entity, label);
        }

        void BuildSource()
        {
            // Opposite canonical UUID and hierarchy order.
            const auto child  = Scene.CreateEntity();
            const auto parent = Scene.CreateEntity();
            AddNamedEntity(child, ChildUUID, reinterpret_cast<const char*>(u8"Child \u00e9 \u6771\u4eac \U0001f680"));
            AddNamedEntity(parent, ParentUUID, "Sun");
            ASSERT_FALSE(HasFatalFailure());
            Scene.AddComponent<Components::TransformComponent>(
                parent,
                {
                .Position = {5.f, 0.f, 0.f}
            });
            Scene.AddComponent<Components::LightComponent>(
                parent,
                {
                .Intensity = 12.f, .Range = 25.f, .SpotAngle = 30.f, .Color = {.25f, .5f, .75f}
            });
            Scene.AddComponent<Components::TransformComponent>(
                child,
                {
                .Position = { 2.f,  3.f,  4.f},
                  .Rotation = { .1f,  .2f,  .3f},
                  .Scale = { 2.f,  3.f,  4.f},
                  .PreviousPosition = {99.f, 98.f, 97.f}
            });
            Scene.AddComponent<Components::ParentComponent>(child, {parent});
            Scene.AddComponent<Components::MeshComponent>(child, {MeshUUID, 12345});
            Scene.AddComponent<Components::MaterialComponent>(child, {MaterialUUID});
            Scene.AddComponent<Components::CameraComponent>(child, {.FovY = 75.f, .Near = .25f, .Far = 512.f, .AspectRatio = 1.5f, .IsMain = true});
            Scene.AddComponent<Components::RigidBodyComponent>(child, {.MotionKind = Components::RigidBodyComponent::MotionType::Kinematic, .Mass = 7.5f, .Friction = .25f, .Restitution = .75f, .BodyID = 6789});
        }

        ECS::EntityID Find(const uuids::uuid& uuid)
        {
            ECS::EntityID found = {};
            Scene.ForEachEntity([&](ECS::EntityID entity) {
                const auto* identity = Scene.GetComponent<Components::UUIDComponent>(entity);
                if (identity && identity->Value == uuid)
                    found = entity;
            });
            return found;
        }

        void CheckLoaded(const ECS::SceneDocument& document)
        {
            ASSERT_EQ(Scene.AliveCount(), 2u);
            ASSERT_NE(document.Settings(), nullptr);
            const auto expected = ExpectedSettings();
            EXPECT_EQ(document.Settings()->UUID, expected.UUID);
            EXPECT_EQ(document.Settings()->Name, expected.Name);
            EXPECT_TRUE(ECS::EqualSceneGridSettings(document.Settings()->Grid, expected.Grid));
            EXPECT_EQ(document.Settings()->Sky, expected.Sky);
            const auto child  = Find(ChildUUID);
            const auto parent = Find(ParentUUID);
            ASSERT_TRUE(child.IsValid());
            ASSERT_TRUE(parent.IsValid());
            // Writer's IDs have generation 2. Retired reader slots must not leak
            // into references, and original entity IDs must never be reused.
            EXPECT_GT(child.Generation, 2u);
            EXPECT_GT(parent.Generation, 2u);
            ASSERT_NE(Scene.GetComponent<Components::ParentComponent>(child), nullptr);
            EXPECT_EQ(Scene.GetComponent<Components::ParentComponent>(child)->Parent, parent);
            ASSERT_NE(Scene.GetComponent<Components::NameComponent>(child), nullptr);
            ASSERT_NE(Scene.GetComponent<Components::NameComponent>(parent), nullptr);
            EXPECT_STREQ(Scene.GetComponent<Components::NameComponent>(child)->Value, reinterpret_cast<const char*>(u8"Child \u00e9 \u6771\u4eac \U0001f680"));
            EXPECT_STREQ(Scene.GetComponent<Components::NameComponent>(parent)->Value, "Sun");
            const auto* transform = Scene.GetComponent<Components::TransformComponent>(child);
            ASSERT_NE(transform, nullptr);
            EXPECT_FLOAT_EQ(transform->Position.x, 2.f);
            EXPECT_FLOAT_EQ(transform->Position.y, 3.f);
            EXPECT_FLOAT_EQ(transform->Position.z, 4.f);
            EXPECT_FLOAT_EQ(transform->Rotation.x, .1f);
            EXPECT_FLOAT_EQ(transform->Rotation.y, .2f);
            EXPECT_FLOAT_EQ(transform->Rotation.z, .3f);
            EXPECT_FLOAT_EQ(transform->Scale.x, 2.f);
            EXPECT_FLOAT_EQ(transform->Scale.y, 3.f);
            EXPECT_FLOAT_EQ(transform->Scale.z, 4.f);
            EXPECT_FLOAT_EQ(transform->PreviousPosition.x, 2.f);
            EXPECT_FLOAT_EQ(transform->PreviousPosition.y, 3.f);
            EXPECT_FLOAT_EQ(transform->PreviousPosition.z, 4.f);
            EXPECT_FLOAT_EQ(transform->WorldTransform(0, 3), 7.f);
            EXPECT_FLOAT_EQ(transform->WorldTransform(1, 3), 3.f);
            EXPECT_FLOAT_EQ(transform->WorldTransform(2, 3), 4.f);
            const auto* mesh = Scene.GetComponent<Components::MeshComponent>(child);
            ASSERT_NE(mesh, nullptr);
            EXPECT_EQ(mesh->MeshUUID, MeshUUID);
            EXPECT_EQ(mesh->RenderInstanceId, UINT32_MAX);
            const auto* material = Scene.GetComponent<Components::MaterialComponent>(child);
            ASSERT_NE(material, nullptr);
            EXPECT_EQ(material->MaterialUUID, MaterialUUID);
            const auto* camera = Scene.GetComponent<Components::CameraComponent>(child);
            ASSERT_NE(camera, nullptr);
            EXPECT_FLOAT_EQ(camera->FovY, 75.f);
            EXPECT_FLOAT_EQ(camera->Near, .25f);
            EXPECT_FLOAT_EQ(camera->Far, 512.f);
            EXPECT_FLOAT_EQ(camera->AspectRatio, 1.5f);
            EXPECT_TRUE(camera->IsMain);
            const auto* light = Scene.GetComponent<Components::LightComponent>(parent);
            ASSERT_NE(light, nullptr);
            EXPECT_EQ(light->LightType, Components::LightComponent::Type::Directional);
            EXPECT_FLOAT_EQ(light->Intensity, 12.f);
            EXPECT_FLOAT_EQ(light->Range, 25.f);
            EXPECT_FLOAT_EQ(light->SpotAngle, 30.f);
            EXPECT_FLOAT_EQ(light->Color[0], .25f);
            EXPECT_FLOAT_EQ(light->Color[1], .5f);
            EXPECT_FLOAT_EQ(light->Color[2], .75f);
            const auto* body = Scene.GetComponent<Components::RigidBodyComponent>(child);
            ASSERT_NE(body, nullptr);
            EXPECT_EQ(body->MotionKind, Components::RigidBodyComponent::MotionType::Kinematic);
            EXPECT_FLOAT_EQ(body->Mass, 7.5f);
            EXPECT_FLOAT_EQ(body->Friction, .25f);
            EXPECT_FLOAT_EQ(body->Restitution, .75f);
            EXPECT_EQ(body->BodyID, UINT32_MAX);
        }
    };

    TEST_F(SceneDocumentProcessTest, SaveOrReopenUsingProductionDiskVFS)
    {
        const auto path     = VFS::VFSPath::Parse(Options.Binary ? "/scene.zescenebin" : reinterpret_cast<const char*>(u8"/sc\u00e8ne_\u8cc7\u6599_\U0001f680.zescene"));
        const auto reopened = VFS::VFSPath::Parse(Options.Binary ? "/reopened.zescenebin" : "/reopened.zescene");
        ASSERT_TRUE(path.Succeeded());
        ASSERT_TRUE(reopened.Succeeded());
        ECS::SceneDocument document = {};
        if (Options.Write)
        {
            BuildSource();
            ASSERT_FALSE(HasFatalFailure());
            ASSERT_TRUE(ECS::SceneDocument::Capture(Scene, ExpectedSettings(), Schemas, &document, &Diagnostics));
            if (Options.Binary)
            {
                ZEngine::Core::Containers::Array<uint8_t> bytes = {};
                ASSERT_TRUE(ECS::BinarySceneCodec::Encode(document, Schemas, &Memory.MainArena, &bytes, &Diagnostics));
                ASSERT_TRUE(VFS::WriteFileAtomically(Files, path.Value(), {bytes.data(), bytes.size()}).Succeeded());
            }
            else
                ASSERT_TRUE(document.Save(Files, path.Value(), &Diagnostics).Succeeded());
        }
        else
        {
            const std::array<ECS::EntityID, 2> retired = {Scene.CreateEntity(), Scene.CreateEntity()};
            for (const auto entity : retired)
                Scene.DestroyEntity(entity);
            if (Options.Binary)
            {
                auto opened = Files.Open(path.Value(), VFS::VFSOpenFlags::Read);
                ASSERT_TRUE(opened.Succeeded());
                struct FileLease
                {
                    VFS::VFSContext& Context;
                    VFS::IVFSFile*   File = nullptr;
                    ~FileLease()
                    {
                        Context.Close(File);
                    }
                } lease{Files, opened.Value()};
                const auto size = lease.File->Size();
                ASSERT_TRUE(size.Succeeded());
                ASSERT_LE(size.Value(), ECS::SceneDocumentLimits{}.MaxFileBytes);
                std::vector<uint8_t> bytes(static_cast<size_t>(size.Value()));
                const auto           read = lease.File->ReadAll({bytes.data(), bytes.size()});
                ASSERT_TRUE(read.Succeeded());
                ASSERT_EQ(read.Value(), bytes.size());
                ASSERT_TRUE(ECS::BinarySceneCodec::Decode(bytes, Schemas, &document, &Diagnostics));
                // Input is destroyed before candidate reconstruction: documents
                // must own component strings and settings, not borrowed spans.
            }
            else
                ASSERT_TRUE(ECS::SceneDocument::Read(Files, path.Value(), Schemas, &document, &Diagnostics));
            ASSERT_FALSE(document.ReadOnly());
            ASSERT_EQ(document.EntityCount(), 2u);
            ASSERT_TRUE(document.PopulateCandidate(Scene, Schemas, &Diagnostics));
            CheckLoaded(document);
            ASSERT_FALSE(HasFatalFailure());
            ECS::SceneDocument recaptured = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(Scene, *document.Settings(), Schemas, &recaptured, &Diagnostics));
            if (Options.Binary)
            {
                ZEngine::Core::Containers::Array<uint8_t> bytes = {};
                ASSERT_TRUE(ECS::BinarySceneCodec::Encode(recaptured, Schemas, &Memory.MainArena, &bytes, &Diagnostics));
                ASSERT_TRUE(VFS::WriteFileAtomically(Files, reopened.Value(), {bytes.data(), bytes.size()}).Succeeded());
            }
            else
                ASSERT_TRUE(recaptured.Save(Files, reopened.Value(), &Diagnostics).Succeeded());
        }
        EXPECT_FALSE(Diagnostics.HasErrors());
    }

    template <typename Char>
    int Run(int argc, Char** argv)
    {
        if (argc != 4)
        {
            std::cerr << "Usage: SceneDocumentProcess <write|read|write-binary|read-binary> <forward|reverse> <native-root>\n";
            return 2;
        }
        const std::filesystem::path operation(argv[1]);
        const std::filesystem::path order(argv[2]);
        if ((operation != "write" && operation != "read" && operation != "write-binary" && operation != "read-binary") || (order != "forward" && order != "reverse"))
            return 2;
        Options           = {.Write = operation == "write" || operation == "write-binary", .Reverse = order == "reverse", .Binary = operation == "write-binary" || operation == "read-binary", .Root = std::filesystem::path(argv[3])};
        // Native argv (wchar_t on Windows) belongs to this helper, not GTest.
        int   test_argc   = 1;
        char  name[]      = "SceneDocumentProcess";
        char* test_argv[] = {name, nullptr};
        testing::InitGoogleTest(&test_argc, test_argv);
        return RUN_ALL_TESTS();
    }
} // namespace

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    return Run(argc, argv);
}
