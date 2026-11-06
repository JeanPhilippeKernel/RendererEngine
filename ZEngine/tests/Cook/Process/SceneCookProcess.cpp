#include <ZEngine/Cook/CookBudgetAllocator.h>
#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Cook/CookFrozenInputSource.h>
#include <ZEngine/Cook/CookManifest.h>
#include <ZEngine/Cook/CookObjectReader.h>
#include <ZEngine/Cook/CookScratchAllocator.h>
#include <ZEngine/Cook/SceneCooker.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Core/VFS/VFSFileIO.h>
#include <ZEngine/ECS/BinarySceneCodec.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/ECS/Components/MaterialComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/Importers/AssetCodec.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Importers/ImageCodec.h>
#include <ZEngine/Importers/MaterialAssetDocument.h>
#include <gtest/gtest.h>
#include <rapidhash.h>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <thread>
#include "../CookPublicationTestSupport.h"
#include "../CookTestAllocator.h"
#include "CookResourceTestSupport.h"
#include "SceneCookCustomSchema.h"
#include "SceneCookProjectSmoke.h"
#if defined(__linux__)
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/statfs.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <cerrno>
#endif

namespace ZEngine::Cook::Tests
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;

    namespace
    {
        struct ProcessOptions
        {
            std::filesystem::path Mode          = {};
            std::filesystem::path Root          = {};
            bool                  Custom        = false;
            uint32_t              CustomVersion = 1;
        };

        ProcessOptions Options = {};

        bool           DiskFullMode()
        {
            return Options.Mode == "disk-full" || Options.Mode == "disk-full-manifest";
        }

        uuids::uuid Identity(cstring text)
        {
            return uuids::uuid::from_string(text).value();
        }

        const auto   SceneUUID     = Identity("10000000-0000-0000-0000-000000000001");
        const auto   EntityUUID    = Identity("20000000-0000-0000-0000-000000000002");
        const auto   MeshUUID      = Identity("30000000-0000-0000-0000-000000000003");
        const auto   MaterialUUID  = Identity("40000000-0000-0000-0000-000000000004");
        const auto   TextureUUID   = Identity("50000000-0000-0000-0000-000000000005");
        const auto   SkyUUID       = Identity("60000000-0000-0000-0000-000000000006");
        const auto   HierarchyUUID = Identity("70000000-0000-0000-0000-000000000007");

        VFS::VFSPath Path(cstring text)
        {
            return VFS::VFSPath::Parse(text).Value();
        }

        VFS::VFSRelativePath Relative(cstring text)
        {
            return VFS::VFSRelativePath::Parse(text).Value();
        }

        constexpr cstring SceneFile        = "/scenes/scène_東京.scene";
        constexpr cstring ActorName        = "Actor é 東京";
        constexpr cstring SceneName        = "Cook process é 東京";
        constexpr cstring SnapshotReady    = "ZE_COOK_SNAPSHOT_READY";
        constexpr cstring SnapshotComplete = ".ze-test-commit.done";

        // Exact 1x1, 24-bit, uncompressed BMP; padding is zero. The changed
        // fixture modifies one pixel byte without changing identity/metadata.
        constexpr uint8_t Pixel[]          = {
            0x42, 0x4d, 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 20, 30, 0,
        };
        constexpr cstring SkySource = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 2\n\x80\x80\x80\x81\x80\x80\x80\x81";

        struct FixtureOnlyComponent
        {
            uint32_t Value = 0;
        };

        struct MaterialSink
        {
            Core::Containers::FixedArray<uint8_t, ZKilo(1)> Bytes = {};
            size_t                                          Size  = 0;
            static VFS::VFSResult<void>                     Write(void* state, Core::Containers::ArrayView<const uint8_t> bytes)
            {
                auto& sink = *static_cast<MaterialSink*>(state);
                if (bytes.size() > sink.Bytes.size() - sink.Size)
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
                }
                if (!bytes.empty())
                {
                    (void) Helpers::secure_memcpy(sink.Bytes.data() + sink.Size, sink.Bytes.size() - sink.Size, bytes.data(), bytes.size());
                }
                sink.Size += bytes.size();
                return VFS::VFSResult<void>::Ok();
            }
        };

        struct BorrowedBytes
        {
            Core::Containers::ArrayView<const uint8_t> Bytes = {};
            Codec::AssetByteSource                     Source()
            {
                return {
                    this,
                    [](void* state, Core::Containers::ArrayView<uint8_t> output, uint64_t offset) {
                        const auto& bytes = static_cast<BorrowedBytes*>(state)->Bytes;
                        if (offset > bytes.size())
                        {
                            return VFS::VFSResult<size_t>::Fail(VFS::VFSError::Corrupted);
                        }
                        const size_t count = std::min(output.size(), bytes.size() - static_cast<size_t>(offset));
                        if (count)
                        {
                            (void) Helpers::secure_memcpy(output.data(), output.size(), bytes.data() + offset, count);
                        }
                        return VFS::VFSResult<size_t>::Ok(count);
                    },
                    Bytes.size()};
            }
        };

#if defined(__linux__)
        // Only the explicitly selected test phase can use this stimulus. The
        // opened file must belong to a <=16 MiB tmpfs, never the host disk.
        struct NativeDiskExhaustion
        {
            NativeDiskExhaustion()                                       = default;
            NativeDiskExhaustion(const NativeDiskExhaustion&)            = delete;
            NativeDiskExhaustion& operator=(const NativeDiskExhaustion&) = delete;
            std::filesystem::path Path                                   = {};
            int                   File                                   = -1;
            uint64_t              Bytes                                  = 0;
            bool                  Exhausted                              = false;

            ~NativeDiskExhaustion()
            {
                Reset();
            }
            bool Initialize(const std::filesystem::path& root)
            {
                if (File >= 0)
                {
                    return false;
                }
                Path = root / ".ze-test-disk-full";
                File = ::open(Path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
                if (File < 0)
                {
                    return false;
                }
                struct statfs  filesystem = {};
                struct statvfs capacity   = {};
                return ::fstatfs(File, &filesystem) == 0 && filesystem.f_type == TMPFS_MAGIC && ::fstatvfs(File, &capacity) == 0 && capacity.f_frsize && capacity.f_blocks <= ZMega(16) / capacity.f_frsize;
            }
            static void Fill(void* context)
            {
                auto&                                           self  = *static_cast<NativeDiskExhaustion*>(context);
                Core::Containers::FixedArray<uint8_t, ZKilo(4)> bytes = {};
                std::fill(bytes.begin(), bytes.end(), uint8_t{0x5a});
                while (self.Bytes <= ZMega(16))
                {
                    const auto written = ::write(self.File, bytes.data(), bytes.size());
                    if (written < 0 && errno == EINTR)
                    {
                        continue;
                    }
                    if (written <= 0)
                    {
                        self.Exhausted = written < 0 && errno == ENOSPC;
                        return;
                    }
                    self.Bytes += static_cast<uint64_t>(written);
                }
            }
            void Reset()
            {
                if (File < 0)
                {
                    return;
                }
                EXPECT_EQ(::close(File), 0);
                File = -1;
                EXPECT_EQ(::unlink(Path.c_str()), 0);
            }
        };
#endif
    } // namespace

    // Test-only producer/consumer executable, not a shipping cooker CLI or
    // runtime/Play integration. Consumer mounts ONLY the published package, with
    // read-only authority, and never constructs an input provider or reads YAML.
    class SceneCookProcessTest : public testing::Test
    {
    protected:
        Core::Memory::MemoryManager                      Memory           = {};
        VFS::VFSDiskBackend                              Disk             = {};
        PublicationFaults                                Faults           = {};
        PublicationBackend                               Backend          = {};
        VFS::VFSContext                                  Files            = {};
        FixedAllocator<ZMega(8), 1>                      Parent           = {};
        CookScratchAllocator                             Allocator        = {};
        ECS::SceneComponentSchemaRegistry                Schemas          = {};
        ECS::SceneMigrationRegistry                      Migrations       = {};
        CookLimits                                       Limits           = {.MaxWorkingBytes = ZMega(16), .SceneLimits = {.MaxDecodedBytes = ZKilo(128)}};
        CookDiagnostics                                  Diagnostics      = {};
        ECS::SceneDiagnostics                            SceneDiagnostics = {};
        Core::Containers::FixedArray<uint8_t, ZKilo(64)> FileBytes        = {};
        Core::Containers::FixedArray<uint8_t, ZKilo(4)>  Stream           = {};
        size_t                                           FileSize         = 0;
        bool                                             Reading          = false;

        void                                             SetUp() override
        {
            Reading            = Options.Mode == "read" || Options.Mode == "read-changed" || Options.Mode == "reject-missing" || Options.Mode == "reject-corrupt" || Options.Mode == "snapshot-read" || Options.Mode == "reject-schema" || Options.Mode == "reject-version";
            // Allocate different real runtime type IDs in the reader and second
            // producer, without resetting global IDs or changing schema keys.
            const bool reverse = Reading || Options.Mode == "write-reverse";
            if (reverse)
            {
                EXPECT_EQ(ECS::ComponentTypeOf<FixtureOnlyComponent>(), 0u);
                if (Options.Custom)
                {
                    EXPECT_EQ(ECS::ComponentTypeOf<CookCustomComponent>(), 1u);
                }
            }
            const Core::Containers::FixedArray<ECS::ComponentTypeID (*)(), 5> types = {
                &ECS::ComponentTypeOf<ECS::Components::UUIDComponent>,
                &ECS::ComponentTypeOf<ECS::Components::NameComponent>,
                &ECS::ComponentTypeOf<ECS::Components::TransformComponent>,
                &ECS::ComponentTypeOf<ECS::Components::MeshComponent>,
                &ECS::ComponentTypeOf<ECS::Components::MaterialComponent>,
            };
            for (size_t i = 0; i < types.size(); ++i)
            {
                EXPECT_EQ(types[reverse ? types.size() - 1 - i : i](), i + (reverse ? (Options.Custom ? 2u : 1u) : 0u));
            }
            Memory.Initialize(ZMega(128), {});
            Schemas.Initialize(&Memory.MainArena);
            const bool register_custom = Options.Custom && Options.Mode != "reject-schema";
            if (register_custom && reverse)
            {
                ASSERT_TRUE(RegisterCookCustomSchema(Schemas, Options.CustomVersion, &SceneDiagnostics));
            }
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas, &SceneDiagnostics));
            if (register_custom && !reverse)
            {
                ASSERT_TRUE(RegisterCookCustomSchema(Schemas, Options.CustomVersion, &SceneDiagnostics));
            }
            if (Options.Custom && Options.CustomVersion == 2 && !Reading && Options.Mode != "reject-migration")
            {
                ASSERT_TRUE(Migrations.Initialize(&Memory.MainArena));
                ASSERT_TRUE(Migrations.RegisterComponentMigration(CookCustomSchemaKey, 1, 2, nullptr, MigrateCookCustomSchema, &SceneDiagnostics));
            }
            ASSERT_TRUE(Parent.Ledger.Initialize(Limits.MaxWorkingBytes).Succeeded());
            ASSERT_TRUE(Allocator.Initialize(Parent, ZMega(8), CookMemoryCategory::Codecs).Succeeded());
            const auto root = Options.Root.generic_u8string();
            Disk.Initialize(reinterpret_cast<cstring>(root.c_str()), Reading ? VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::List : VFS::VFSBackendCaps::Read | VFS::VFSBackendCaps::Write | VFS::VFSBackendCaps::List, &Memory.MainArena);
            Files.Initialize(&Memory.MainArena);
            Faults.MemoryBounds      = VFS::VFSError::OK;
            Backend.Inner            = &Disk;
            Backend.Provider         = Helpers::Ref<PublicationProvider>(new PublicationProvider());
            Backend.Provider->Inner  = Disk.RootProvider();
            Backend.Provider->Faults = &Faults;
            ASSERT_TRUE(Files.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
        }

        void TearDown() override
        {
            if (HasFailure())
            {
                for (uint32_t i = 0; i < Diagnostics.Count; ++i)
                {
                    std::cerr << Diagnostics.Entries[i].Message << " (VFS " << static_cast<uint32_t>(Diagnostics.Entries[i].Underlying) << ", " << Diagnostics.Entries[i].Source.CStr() << ")\n";
                }
                for (uint32_t i = 0; i < SceneDiagnostics.Count; ++i)
                {
                    std::cerr << SceneDiagnostics.Entries[i].Message << '\n';
                }
            }
            EXPECT_EQ(Allocator.OutstandingAllocations(), 0u);
            Allocator.Release();
            EXPECT_EQ(Parent.Statistics().LiveBytes, 0u);
            Files.Shutdown();
        }

        void Write(const VFS::VFSPath& path, Core::Containers::ArrayView<const uint8_t> bytes)
        {
            ASSERT_TRUE(VFS::WriteFileAtomically(Files, path, bytes).Succeeded());
        }

        void WriteText(cstring path, Core::Containers::StringView text)
        {
            Write(Path(path), {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
        }

        void SavePixel(bool changed = false)
        {
            Core::Containers::FixedArray<uint8_t, sizeof(Pixel)> bytes = {};
            (void) Helpers::secure_memcpy(bytes.data(), bytes.size(), Pixel, sizeof(Pixel));
            if (changed)
            {
                bytes[54] = 90;
            }
            Write(Path("/assets/pixel.bmp"), bytes);
        }

        void BuildFixture()
        {
            for (cstring path : {"/scenes", "/assets", "/_cache/envmaps", "/scratch", "/output"})
            {
                ASSERT_TRUE(Files.CreateDir(Path(path)).Succeeded());
            }
            ECS::Scene scene = {};
            scene.Initialize(&Memory.MainArena);
            const auto entity = scene.CreateEntity();
            scene.AddComponent<ECS::Components::UUIDComponent>(entity, {EntityUUID});
            ECS::Components::NameComponent name = {};
            ASSERT_EQ(Helpers::secure_strcpy(name.Value, sizeof(name.Value), ActorName), Helpers::MEMORY_OP_SUCCESS);
            scene.AddComponent<ECS::Components::NameComponent>(entity, name);
            scene.AddComponent<ECS::Components::TransformComponent>(
                entity,
                {
                .Position = {1, 2, 3}
            });
            scene.AddComponent<ECS::Components::MeshComponent>(entity, {MeshUUID});
            scene.AddComponent<ECS::Components::MaterialComponent>(entity, {MaterialUUID});
            if (Options.Custom)
            {
                scene.AddComponent<CookCustomComponent>(entity, {.Texture = TextureUUID, .Weight = .375f, .RuntimeCache = 12345});
            }
            ECS::SceneDocumentSettingsView settings = {.UUID = SceneUUID, .Name = SceneName};
            settings.Sky.Mode                       = Rendering::Scenes::SkyMode::HDRI;
            settings.Sky.EnvironmentMap             = SkyUUID;
            settings.Sky.EnvironmentIntensity       = 2.5f;
            ECS::SceneDocument document             = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(scene, settings, Schemas, &document, &SceneDiagnostics, Limits.SceneLimits));
            ASSERT_TRUE(document.Save(Files, Path(SceneFile), &SceneDiagnostics).Succeeded());
            scene.Shutdown();

            Importers::AssetMesh mesh = {};
            mesh.MeshUUID             = MeshUUID;
            mesh.Vertices.init(&Memory.MainArena, 24, 24);
            mesh.Vertices[8]  = 1;
            mesh.Vertices[17] = 1;
            mesh.Indices.init(&Memory.MainArena, 3);
            mesh.Indices.push(0);
            mesh.Indices.push(1);
            mesh.Indices.push(2);
            mesh.SubMeshes.init(&Memory.MainArena, 1);
            mesh.SubMeshes.push({.MaterialUUID = MaterialUUID, .VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
            Importers::AssetNodeHierarchy hierarchy = {.NodeHierarchyUUID = HierarchyUUID, .MeshUUID = MeshUUID};
            hierarchy.Hierarchies.init(&Memory.MainArena, 1);
            hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
            hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
            hierarchy.Names.init(&Memory.MainArena, 1);
            hierarchy.MaterialNames.init(&Memory.MainArena, 0);
            hierarchy.NodeNames.init(&Memory.MainArena, 16);
            hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
            hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
            ASSERT_EQ(Importers::AddNode(hierarchy, -1, 0), 0);
            hierarchy.Names.push_use({}).init(&Memory.MainArena, "Fixture root");
            hierarchy.NodeNames.insert(0, 0);
            hierarchy.NodeMeshes.insert(0, 0);
            Codec::ImportConfiguration configuration = {.VFS = &Files};
            configuration.OutputAssetsPath.init(&Memory.MainArena, "/assets");
            configuration.OutputAssetFile.init(&Memory.MainArena, "triangle.zemesh");
            ASSERT_TRUE(Codec::SerializeMeshAssetFile(&Memory.MainArena, mesh, hierarchy, configuration).Succeeded());
            WriteText("/assets/triangle.zemesh.meta", R"({"uuid":"30000000-0000-0000-0000-000000000003","importer":"MeshImporter","settings":[]})");

            Codec::MaterialAssetDocument material = {.UUID = MaterialUUID};
            ASSERT_EQ(Helpers::secure_strcpy(material.Name, sizeof(material.Name), "Fixture material"), Helpers::MEMORY_OP_SUCCESS);
            material.Textures[0].UUID = TextureUUID;
            ASSERT_EQ(Helpers::secure_strcpy(material.Textures[0].Path, sizeof(material.Textures[0].Path), "assets/pixel.bmp"), Helpers::MEMORY_OP_SUCCESS);
            material.AlbedoColor                                        = {1, .5f, .25f, 1};
            material.Factors[2]                                         = .5f;
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {Relative("assets/pixel.bmp")};
            MaterialSink                                          sink  = {};
            ASSERT_TRUE(Codec::WriteCanonicalMaterialAsset(material, paths, MaterialSink::Write, &sink).Succeeded());
            Write(Path("/assets/material.zematerial"), {sink.Bytes.data(), sink.Size});
            WriteText("/assets/material.zematerial.meta", R"({"uuid":"40000000-0000-0000-0000-000000000004","importer":"MaterialImporter","settings":[]})");
            SavePixel();
            WriteText("/assets/pixel.bmp.meta", R"({"uuid":"50000000-0000-0000-0000-000000000005","importer":"TextureImporter","settings":[]})");

            const size_t source_bytes = Helpers::secure_strlen(SkySource);
            Write(Path("/assets/sky.hdr"), {reinterpret_cast<const uint8_t*>(SkySource), source_bytes});
            const uint64_t                                             source_hash = rapidhash_withSeed(SkySource, source_bytes, 0);
            Codec::EnvironmentMapFileHeader                            header      = {.MagicNumber = ZENVMAP_MAGIC, .Version = 2, .HeaderByteSize = sizeof(header), .ImporterVersion = 1, .SourceHash = source_hash, .FaceWidth = 1, .FaceHeight = 1, .Channel = 4, .LayerCount = 6, .MipCount = 1, .ColorSpace = 1, .Orientation = 1, .MipPolicy = 1, .Exposure = 1, .BufferByteSize = 96};
            Core::Containers::FixedArray<uint8_t, sizeof(header) + 96> artifact    = {};
            (void) Helpers::secure_memcpy(artifact.data(), artifact.size(), &header, sizeof(header));
            for (size_t i = 0; i < 24; ++i)
            {
                const float value = i % 4 == 0 || i % 4 == 3 ? 1.f : 0.f;
                (void) Helpers::secure_memcpy(artifact.data() + sizeof(header) + i * sizeof(float), sizeof(float), &value, sizeof(float));
            }
            Write(Path("/_cache/envmaps/sky.zenvmap"), artifact);
            ASSERT_EQ(source_hash, 1131020857819731355ull);
            WriteText("/assets/sky.hdr.meta", R"({"uuid":"60000000-0000-0000-0000-000000000006","importer":"EnvironmentMapImporter","source_hash":1131020857819731355,"artifact_path":"envmaps/sky.zenvmap","settings":[{"key":"artifact_version","value":"2"},{"key":"pixel_format","value":"rgba32f"},{"key":"color_space","value":"linear_scene"},{"key":"orientation","value":"renderer_canonical_v1"},{"key":"mip_policy","value":"generate_on_gpu"},{"key":"exposure","value":"1.0"}]})");
        }

        // Fixture preparation only. Stream bounded generated periods to disk;
        // never retain a large source/asset buffer alongside the real cook.
        void StoreGenerated(cstring path, const ICookCapturedFile& capture)
        {
            const auto opened = Files.Open(Path(path), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Create | VFS::VFSOpenFlags::Truncate);
            ASSERT_TRUE(opened.Succeeded());
            const auto copy = [&]() {
                for (uint64_t offset = 0; offset < capture.Size();)
                {
                    const auto read = capture.Read(offset, FileBytes);
                    ASSERT_TRUE(read.Succeeded());
                    ASSERT_GT(read.Value(), 0u);
                    const auto written = opened.Value()->Write({FileBytes.data(), read.Value()}, offset);
                    ASSERT_TRUE(written.Succeeded());
                    ASSERT_EQ(written.Value(), read.Value());
                    offset += read.Value();
                }
            };
            copy();
            EXPECT_TRUE(opened.Value()->Flush().Succeeded());
            EXPECT_TRUE(Files.CloseChecked(opened.Value()).Succeeded());
        }

        void BuildStressFixture()
        {
            BuildFixture();
            ASSERT_FALSE(HasFatalFailure());
            // A real 4096-square, 32-bit BMP (64 MiB of encoded pixels).
            GeneratedResourceCapture                  texture = {};
            Core::Containers::FixedArray<uint8_t, 54> header  = {};
            const auto                                word    = [&](size_t offset, uint32_t value, size_t bytes = 4) {
                for (size_t i = 0; i < bytes; ++i)
                {
                    header[offset + i] = static_cast<uint8_t>(value >> (8 * i));
                }
            };
            header[0] = 'B';
            header[1] = 'M';
            word(2, 54 + ZMega(64));
            word(10, 54);
            word(14, 40);
            word(18, 4096);
            word(22, 4096);
            word(26, 1, 2);
            word(28, 32, 2);
            word(34, ZMega(64));
            constexpr uint8_t pixel[] = {11, 42, 93, 255};
            texture.Append(header, header.size());
            texture.Append(pixel, ZMega(64));
            StoreGenerated("/assets/pixel.bmp", texture);

            // 27 MiB of real vertex/index streams and a valid material edge.
            GeneratedResourceCapture mesh = {};
            ConfigureGeneratedMesh(mesh, 786432, 786432, MeshUUID, HierarchyUUID, MaterialUUID);
            ASSERT_FALSE(HasFatalFailure());
            StoreGenerated("/assets/triangle.zemesh", mesh);

            constexpr uint32_t                    face               = 512;
            const Codec::EnvironmentMapFileHeader environment_header = {.MagicNumber = ZENVMAP_MAGIC, .Version = 2, .HeaderByteSize = sizeof(environment_header), .ImporterVersion = 1, .SourceHash = 1131020857819731355ull, .FaceWidth = face, .FaceHeight = face, .Channel = 4, .LayerCount = 6, .MipCount = Codec::GetEnvironmentMapFullMipCount(face), .ColorSpace = 1, .Orientation = 1, .MipPolicy = 1, .Exposure = 1, .BufferByteSize = uint64_t(face) * face * 96};
            GeneratedResourceCapture              environment        = {};
            environment.Append({reinterpret_cast<const uint8_t*>(&environment_header), sizeof(environment_header)}, sizeof(environment_header));
            constexpr float sky_pixel[] = {.25f, .5f, .75f, 1.f};
            environment.Append({reinterpret_cast<const uint8_t*>(sky_pixel), sizeof(sky_pixel)}, environment_header.BufferByteSize);
            StoreGenerated("/_cache/envmaps/sky.zenvmap", environment);
            ASSERT_FALSE(HasFatalFailure());

            ECS::Scene empty = {};
            empty.Initialize(&Memory.MainArena);
            ECS::SceneDocumentSettingsView settings = {.UUID = SceneUUID, .Name = "Dense publication stress"};
            settings.Sky.Mode                       = Rendering::Scenes::SkyMode::HDRI;
            settings.Sky.EnvironmentMap             = SkyUUID;
            ECS::SceneDocument document             = {};
            ASSERT_TRUE(ECS::SceneDocument::Capture(empty, settings, Schemas, &document, &SceneDiagnostics));
            std::string yaml = {};
            ASSERT_TRUE(document.Emit(&yaml, &SceneDiagnostics));
            empty.Shutdown();
            ASSERT_LT(yaml.size(), ZKilo(4));
            const auto end = yaml.rfind("[]");
            ASSERT_NE(end, std::string::npos);
            ASSERT_EQ(yaml.find_first_not_of(" \r\n", end + 2), std::string::npos);
            const auto opened = Files.Open(Path(SceneFile), VFS::VFSOpenFlags::Write | VFS::VFSOpenFlags::Truncate);
            ASSERT_TRUE(opened.Succeeded());
            const auto write_scene = [&]() {
                yaml.resize(end);
                yaml         += '\n';
                auto written  = opened.Value()->Write({reinterpret_cast<const uint8_t*>(yaml.data()), yaml.size()}, 0);
                ASSERT_TRUE(written.Succeeded());
                ASSERT_EQ(written.Value(), yaml.size());
                uint64_t offset = yaml.size();
                size_t   used   = 0;
                for (uint32_t id = 65536; id > 0; --id)
                {
                    char      row[512] = {};
                    const int length   = std::snprintf(row, sizeof(row), "  - uuid: \"20000000-0000-0000-0000-%012x\"\n    components:%s", id, id == 65536 ? "\n      MaterialComponent:\n        schema_version: 1\n        fields: {material_uuid: \"40000000-0000-0000-0000-000000000004\"}\n      MeshComponent:\n        schema_version: 1\n        fields: {mesh_uuid: \"30000000-0000-0000-0000-000000000003\"}\n" : " {}\n");
                    ASSERT_GT(length, 0);
                    ASSERT_LT(static_cast<size_t>(length), sizeof(row));
                    if (static_cast<size_t>(length) > FileBytes.size() - used)
                    {
                        written = opened.Value()->Write({FileBytes.data(), used}, offset);
                        ASSERT_TRUE(written.Succeeded());
                        ASSERT_EQ(written.Value(), used);
                        offset += used;
                        used    = 0;
                    }
                    ASSERT_EQ(Helpers::secure_memcpy(FileBytes.data() + used, FileBytes.size() - used, row, static_cast<size_t>(length)), Helpers::MEMORY_OP_SUCCESS);
                    used += static_cast<size_t>(length);
                }
                written = opened.Value()->Write({FileBytes.data(), used}, offset);
                ASSERT_TRUE(written.Succeeded());
                ASSERT_EQ(written.Value(), used);
            };
            write_scene();
            EXPECT_TRUE(opened.Value()->Flush().Succeeded());
            EXPECT_TRUE(Files.CloseChecked(opened.Value()).Succeeded());
            WriteText("/project.json", R"({"workingSpace":".","sceneDir":"scenes","assetDirs":{"all":"assets"},"sceneList":[{"name":"scène_東京.scene"}]})");
        }

        void CookFixture(bool force, bool published, bool busy = false)
        {
            CookFrozenLibraryOwner      owner   = {};
            const CookFrozenRootRequest roots[] = {
                { "scenes", &Files,         Path("/scenes")},
                { "assets", &Files,         Path("/assets")},
                {"envmaps", &Files, Path("/_cache/envmaps")},
            };
            const auto initialized = owner.Initialize(Allocator, roots);
            if (initialized.Failed())
            {
                // Keep native CI failures actionable: distinguish acquisition
                // from ancestry rejection before the owner folds both to VFS.
                VFS::VFSReadRootLease leases[std::size(roots)] = {};
                for (size_t i = 0; i < std::size(roots); ++i)
                {
                    const auto acquired = Files.AcquireReadRoot(roots[i].Path, &leases[i]);
                    ASSERT_TRUE(acquired.Succeeded()) << "Input root acquisition failed: " << roots[i].Path.CStr() << " (VFS " << static_cast<uint32_t>(acquired.Error()) << ").";
                    for (size_t j = 0; j < i; ++j)
                    {
                        const auto overlap = leases[i].Overlaps(leases[j]);
                        ASSERT_TRUE(overlap.Succeeded()) << "Input root ancestry query failed: " << roots[i].Path.CStr() << " / " << roots[j].Path.CStr() << " (VFS " << static_cast<uint32_t>(overlap.Error()) << ").";
                        EXPECT_FALSE(overlap.Value()) << "Input roots unexpectedly overlap: " << roots[i].Path.CStr() << " / " << roots[j].Path.CStr();
                    }
                }
            }
            ASSERT_TRUE(initialized.Succeeded()) << "Frozen input root initialization failed (VFS " << static_cast<uint32_t>(initialized.Error()) << ").";
            CookFrozenInputSource source(owner);
            CookAdapterRegistry   adapters    = {};
            const CookAdapterFns  functions[] = {CookEnvironmentAdapter(), CookMaterialAdapter(), CookMeshAdapter(), CookTextureAdapter()};
            ASSERT_TRUE(adapters.Initialize(functions).Succeeded());
            const VFS::VFSPath     scenes[]        = {Path(SceneFile)};
            const SceneCookRequest request         = {scenes, Path("/output"), CookTarget::DesktopVulkanLE64, force};
            const SceneCookContext context         = {&source, &Files, &Files, Path("/scratch"), &Schemas, Migrations.IsInitialized() ? &Migrations : nullptr, &adapters, &Allocator, Limits};
            const Digest256        previous_digest = {.Bytes = {{0xa5, 0x5a}}};
            SceneCookResult        result          = {};
            if (Options.Mode == "reject-migration" || DiskFullMode())
            {
                result = {.ManifestPath = Path("/previous-manifest"), .ManifestDigest = previous_digest, .SceneCount = 77, .AssetCount = 83, .ObjectCount = 91, .ReferencedObjectBytes = 1001, .PeakWorkingBytes = 123, .Published = true};
                for (auto& peak : result.PeakWorkingBytesByCategory)
                {
                    peak = 123;
                }
            }
            const auto cooked = SceneCooker::Cook(request, context, &result, &Diagnostics);
            if (Options.Mode == "reject-migration" || DiskFullMode())
            {
                const bool disk_full = DiskFullMode();
                EXPECT_EQ(cooked.Error(), disk_full ? VFS::VFSError::IOError : VFS::VFSError::Unsupported);
                EXPECT_EQ(result.ManifestPath, Path("/previous-manifest"));
                EXPECT_EQ(result.ManifestDigest, previous_digest);
                EXPECT_EQ(result.SceneCount, 77u);
                EXPECT_EQ(result.AssetCount, 83u);
                EXPECT_EQ(result.ObjectCount, 91u);
                EXPECT_EQ(result.ReferencedObjectBytes, 1001u);
                EXPECT_EQ(result.PeakWorkingBytes, 123u);
                for (uint64_t peak : result.PeakWorkingBytesByCategory)
                {
                    EXPECT_EQ(peak, 123u);
                }
                EXPECT_TRUE(result.Published);
                ASSERT_NE(Diagnostics.Count, 0u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, disk_full ? CookDiagnosticCode::IOFailure : CookDiagnosticCode::UnsupportedVersion);
                if (disk_full)
                {
                    if (Options.Mode == "disk-full-manifest")
                    {
                        EXPECT_GT(Faults.ObjectRenames, 0u);
                    }
                    else
                    {
                        EXPECT_EQ(Faults.ObjectRenames, 0u);
                    }
                    EXPECT_EQ(Faults.ManifestRenames, 0u);
                    EXPECT_EQ(Faults.LiveFiles, 0u);
                }
                return;
            }
            if (busy)
            {
                EXPECT_EQ(cooked.Error(), VFS::VFSError::AlreadyExists);
                EXPECT_EQ(result.SceneCount, 0u);
                EXPECT_EQ(result.AssetCount, 0u);
                EXPECT_EQ(result.ObjectCount, 0u);
                EXPECT_FALSE(result.Published);
                ASSERT_NE(Diagnostics.Count, 0u);
                EXPECT_EQ(Diagnostics.Entries[0].Code, CookDiagnosticCode::OutputBusy);
                return;
            }
            ASSERT_TRUE(cooked.Succeeded()) << "Cook failed (VFS " << static_cast<uint32_t>(cooked.Error()) << "; writes " << Faults.Writes << ", reads " << Faults.Reads << ", object renames " << Faults.ObjectRenames << ", manifest renames " << Faults.ManifestRenames << ").";
            EXPECT_EQ(result.SceneCount, 1u);
            EXPECT_EQ(result.AssetCount, 4u);
            EXPECT_EQ(result.ObjectCount, 5u);
            EXPECT_EQ(result.Published, published);
            EXPECT_LE(result.PeakWorkingBytes, Limits.MaxWorkingBytes);
            EXPECT_FALSE(Diagnostics.HasErrors());
            std::cout << "Cook peak working bytes: " << result.PeakWorkingBytes << '\n';
        }

        VFS::VFSResult<void> ReadOpenFile(VFS::IVFSFile& file, const CookObjectDescriptor* expected = nullptr)
        {
            const auto size = file.Size();
            if (size.Failed())
            {
                return VFS::VFSResult<void>::Fail(size.Error());
            }
            if (size.Value() > FileBytes.size())
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::SizeLimitExceeded);
            }
            if (expected)
            {
                const auto verified = VerifyCookObjectFile(file, *expected, Stream, Allocator);
                if (verified.Failed())
                {
                    return verified;
                }
            }
            FileSize        = static_cast<size_t>(size.Value());
            const auto read = file.ReadAll({FileBytes.data(), FileSize});
            return read.Failed() || read.Value() != FileSize ? VFS::VFSResult<void>::Fail(read.Failed() ? read.Error() : VFS::VFSError::Corrupted) : VFS::VFSResult<void>::Ok();
        }

        VFS::VFSResult<void> ReadFile(VFS::VFSReadRootLease& root, const VFS::VFSRelativePath& path, const CookObjectDescriptor* expected = nullptr)
        {
            const auto opened = root.Open(path);
            if (opened.Failed())
            {
                return VFS::VFSResult<void>::Fail(opened.Error());
            }
            const auto perform = ReadOpenFile(*opened.Value(), expected);
            const auto closed  = root.CloseChecked(opened.Value());
            return perform.Failed() ? perform : closed;
        }

        Core::Containers::ArrayView<const uint8_t> Bytes() const
        {
            return {FileBytes.data(), FileSize};
        }

        void CorruptTexture()
        {
            VFS::VFSReadRootLease root = {};
            ASSERT_TRUE(Files.AcquireReadRoot(VFS::VFSPath::Root(), &root).Succeeded());
            ASSERT_TRUE(ReadFile(root, Relative("manifest.json")).Succeeded());
            CookManifest manifest = {};
            const auto   parsed   = manifest.Read(Bytes(), CookTarget::DesktopVulkanLE64, Limits, Allocator, &Diagnostics);
            ASSERT_TRUE(parsed.Succeeded());
            ASSERT_EQ(parsed.Value(), CookManifestReadStatus::Valid);
            const auto* texture = manifest.FindAsset({CookAssetKind::Texture, TextureUUID});
            ASSERT_NE(texture, nullptr);
            ASSERT_TRUE(ReadFile(root, texture->Result.Object.Path, &texture->Result.Object).Succeeded());
            ASSERT_EQ(FileSize, sizeof(Pixel));
            const uint8_t damaged = FileBytes[54] ^ 1;
            const auto    path    = VFS::VFSPath::Root().Append(texture->Result.Object.Path.CStr());
            ASSERT_TRUE(path.Succeeded());
            const auto opened = Files.Open(path.Value(), VFS::VFSOpenFlags::Write);
            ASSERT_TRUE(opened.Succeeded());
            const auto written = opened.Value()->Write({&damaged, 1}, 54);
            EXPECT_TRUE(written.Succeeded());
            if (written.Succeeded())
            {
                EXPECT_EQ(written.Value(), 1u);
            }
            EXPECT_EQ(opened.Value()->Size().Value(), texture->Result.Object.Size);
            EXPECT_TRUE(opened.Value()->Flush().Succeeded());
            EXPECT_TRUE(Files.CloseChecked(opened.Value()).Succeeded());
        }

        void CheckScene(const CookManifest& manifest, const CookManifestScene& entry)
        {
            CookCodecAllocationRoute route(Allocator);
            ECS::SceneDocument       document = {};
            if (Options.Mode == "reject-schema" || Options.Mode == "reject-version")
            {
                EXPECT_FALSE(ECS::BinarySceneCodec::Decode(Bytes(), Schemas, &document, &SceneDiagnostics, Limits.SceneLimits, &route));
                EXPECT_TRUE(SceneDiagnostics.HasError(ECS::SceneDiagnosticCode::UnsupportedVersion));
                EXPECT_EQ(document.Settings(), nullptr);
                EXPECT_EQ(document.EntityCount(), 0u);
                return;
            }
            ASSERT_TRUE(ECS::BinarySceneCodec::Decode(Bytes(), Schemas, &document, &SceneDiagnostics, Limits.SceneLimits, &route));
            ASSERT_NE(document.Settings(), nullptr);
            EXPECT_EQ(document.Settings()->UUID, entry.UUID);
            EXPECT_EQ(entry.UUID, SceneUUID);
            EXPECT_EQ(document.Settings()->Name, SceneName);
            EXPECT_EQ(document.Settings()->Sky.Mode, Rendering::Scenes::SkyMode::HDRI);
            EXPECT_EQ(document.Settings()->Sky.EnvironmentMap, SkyUUID);
            EXPECT_EQ(document.Settings()->Sky.EnvironmentIntensity, 2.5f);
            EXPECT_EQ(document.EntityCount(), 1u);
            ECS::Scene candidate = {};
            candidate.Initialize(&Memory.MainArena);
            ASSERT_TRUE(document.PopulateCandidate(candidate, Schemas, &SceneDiagnostics));
            uint32_t entities = 0;
            candidate.ForEachEntity([&](ECS::EntityID entity) {
                ++entities;
                ASSERT_NE(candidate.GetComponent<ECS::Components::UUIDComponent>(entity), nullptr);
                EXPECT_EQ(candidate.GetComponent<ECS::Components::UUIDComponent>(entity)->Value, EntityUUID);
                ASSERT_NE(candidate.GetComponent<ECS::Components::NameComponent>(entity), nullptr);
                EXPECT_STREQ(candidate.GetComponent<ECS::Components::NameComponent>(entity)->Value, ActorName);
                ASSERT_NE(candidate.GetComponent<ECS::Components::MeshComponent>(entity), nullptr);
                EXPECT_EQ(candidate.GetComponent<ECS::Components::MeshComponent>(entity)->MeshUUID, MeshUUID);
                ASSERT_NE(candidate.GetComponent<ECS::Components::MaterialComponent>(entity), nullptr);
                EXPECT_EQ(candidate.GetComponent<ECS::Components::MaterialComponent>(entity)->MaterialUUID, MaterialUUID);
                ASSERT_NE(candidate.GetComponent<ECS::Components::TransformComponent>(entity), nullptr);
                const auto& position = candidate.GetComponent<ECS::Components::TransformComponent>(entity)->Position;
                EXPECT_EQ(position.x, 1.f);
                EXPECT_EQ(position.y, 2.f);
                EXPECT_EQ(position.z, 3.f);
                if (Options.Custom)
                {
                    const auto* component = candidate.GetComponent<CookCustomComponent>(entity);
                    ASSERT_NE(component, nullptr);
                    EXPECT_EQ(component->Texture, TextureUUID);
                    EXPECT_EQ(component->Weight, .375f);
                    EXPECT_EQ(component->RuntimeCache, UINT32_MAX);
                }
            });
            EXPECT_EQ(entities, 1u);
            candidate.Shutdown();
            const auto roots = manifest.Roots(0);
            ASSERT_EQ(roots.size(), Options.Custom ? 4u : 3u);
            EXPECT_EQ(roots[0], (CookAssetIdentity{CookAssetKind::EnvironmentMap, SkyUUID}));
            EXPECT_EQ(roots[1], (CookAssetIdentity{CookAssetKind::Material, MaterialUUID}));
            EXPECT_EQ(roots[2], (CookAssetIdentity{CookAssetKind::Mesh, MeshUUID}));
            if (Options.Custom)
            {
                EXPECT_EQ(roots[3], (CookAssetIdentity{CookAssetKind::Texture, TextureUUID}));
            }
        }

        void CheckAsset(const CookManifest& manifest, const CookManifestAsset& entry, bool changed)
        {
            BorrowedBytes bytes        = {Bytes()};
            const auto    dependencies = manifest.Dependencies(static_cast<uint32_t>(&entry - manifest.Assets().data()));
            switch (entry.Identity.Kind)
            {
                case CookAssetKind::Mesh:
                {
                    EXPECT_EQ(entry.Identity.UUID, MeshUUID);
                    size_t capacity = 0;
                    ASSERT_TRUE(Codec::MeasureMeshAssetReadWorkspace(bytes.Source(), &capacity, MeshUUID).Succeeded());
                    CookWorkspace workspace = {};
                    ASSERT_TRUE(workspace.Initialize(Allocator, capacity, CookMemoryCategory::Codecs).Succeeded());
                    Importers::AssetMesh          mesh      = {};
                    Importers::AssetNodeHierarchy hierarchy = {};
                    ASSERT_TRUE(Codec::ReadMeshAsset(bytes.Source(), workspace.Arena(), &mesh, &hierarchy, MeshUUID).Succeeded());
                    EXPECT_EQ(mesh.Vertices.size(), 24u);
                    EXPECT_EQ(mesh.Indices.size(), 3u);
                    for (size_t i = 0; i < mesh.Vertices.size(); ++i)
                    {
                        EXPECT_EQ(mesh.Vertices[i], i == 8 || i == 17 ? 1.f : 0.f);
                    }
                    for (size_t i = 0; i < mesh.Indices.size(); ++i)
                    {
                        EXPECT_EQ(mesh.Indices[i], i);
                    }
                    ASSERT_EQ(mesh.SubMeshes.size(), 1u);
                    EXPECT_EQ(mesh.SubMeshes[0].MaterialUUID, MaterialUUID);
                    EXPECT_EQ(hierarchy.NodeHierarchyUUID, HierarchyUUID);
                    EXPECT_EQ(hierarchy.MeshUUID, MeshUUID);
                    ASSERT_EQ(hierarchy.Names.size(), 1u);
                    EXPECT_STREQ(hierarchy.Names[0].c_str(), "Fixture root");
                    ASSERT_EQ(dependencies.size(), 1u);
                    EXPECT_EQ(dependencies[0].Identity, (CookAssetIdentity{CookAssetKind::Material, MaterialUUID}));
                    EXPECT_EQ(dependencies[0].Slot, Core::Containers::StringView("submeshes/0/material"));
                    break;
                }
                case CookAssetKind::Material:
                {
                    EXPECT_EQ(entry.Identity.UUID, MaterialUUID);
                    Codec::MaterialAssetDocument material = {};
                    ASSERT_TRUE(Codec::ReadMaterialAssetDocument(Bytes(), &material, MaterialUUID).Succeeded());
                    EXPECT_STREQ(material.Name, "Fixture material");
                    EXPECT_EQ(material.AlbedoColor, (Core::Containers::FixedArray<float, 4>{1, .5f, .25f, 1}));
                    EXPECT_EQ(material.Factors[2], .5f);
                    EXPECT_FALSE(material.HasSource);
                    const auto* texture = manifest.FindAsset({CookAssetKind::Texture, TextureUUID});
                    ASSERT_NE(texture, nullptr);
                    EXPECT_EQ(material.Textures[0].UUID, texture->Identity.UUID);
                    EXPECT_STREQ(material.Textures[0].Path, texture->Result.Object.Path.CStr());
                    ASSERT_EQ(dependencies.size(), 1u);
                    EXPECT_EQ(dependencies[0].Identity, texture->Identity);
                    EXPECT_EQ(dependencies[0].Slot, Core::Containers::StringView("albedo"));
                    for (size_t i = 1; i < material.Textures.size(); ++i)
                    {
                        EXPECT_TRUE(material.Textures[i].UUID.is_nil());
                        EXPECT_EQ(material.Textures[i].Path[0], '\0');
                    }
                    break;
                }
                case CookAssetKind::Texture:
                {
                    EXPECT_EQ(entry.Identity.UUID, TextureUUID);
                    CookCodecAllocationRoute                       route(Allocator);
                    Importers::ImageCodec::EncodedImageDescription description = {};
                    ASSERT_TRUE(Importers::ImageCodec::ValidateEncodedImage(bytes.Source(), route, {}, &description).Succeeded());
                    EXPECT_EQ(route.Error(), VFS::VFSError::OK);
                    EXPECT_EQ(description.Kind, Importers::ImageCodec::EncodedImageKind::BMP);
                    EXPECT_EQ(description.Width, 1u);
                    EXPECT_EQ(description.Height, 1u);
                    ASSERT_EQ(FileSize, sizeof(Pixel));
                    EXPECT_EQ(FileBytes[54], changed ? 90 : 10);
                    EXPECT_TRUE(dependencies.empty());
                    break;
                }
                case CookAssetKind::EnvironmentMap:
                {
                    EXPECT_EQ(entry.Identity.UUID, SkyUUID);
                    Codec::EnvironmentMapFileHeader header = {};
                    const auto                      hash   = rapidhash_withSeed(SkySource, Helpers::secure_strlen(SkySource), 0);
                    ASSERT_TRUE(Codec::ReadEnvironmentMapAsset(bytes.Source(), Stream, &header, hash).Succeeded());
                    EXPECT_EQ(header.FaceWidth, 1u);
                    EXPECT_EQ(header.LayerCount, 6u);
                    EXPECT_EQ(header.Exposure, 1.f);
                    EXPECT_EQ(header.BufferByteSize, 96u);
                    EXPECT_TRUE(dependencies.empty());
                    break;
                }
                default:
                    FAIL() << "Unexpected fixture asset kind";
            }
        }

        VFS::VFSResult<void> ConsumePackage(bool changed)
        {
            VFS::VFSReadRootLease root   = {};
            auto                  status = Files.AcquireReadRoot(VFS::VFSPath::Root(), &root);
            if (status.Failed())
            {
                return status;
            }
            const auto opened = root.Open(Relative("manifest.json"));
            if (opened.Failed())
            {
                return VFS::VFSResult<void>::Fail(opened.Error());
            }
            const auto read_manifest = [&]() -> VFS::VFSResult<void> {
                if (Options.Mode == "snapshot-read")
                {
                    // The pipeline starts a second process only after this
                    // reader holds the old manifest's replace-compatible handle.
                    std::cout << SnapshotReady << '\n' << std::flush;
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(25);
                    for (;;)
                    {
                        const auto complete = root.Stat(Relative(SnapshotComplete));
                        if (complete.Succeeded())
                        {
                            break;
                        }
                        if (complete.Error() != VFS::VFSError::NotFound)
                        {
                            return VFS::VFSResult<void>::Fail(complete.Error());
                        }
                        if (std::chrono::steady_clock::now() >= deadline)
                        {
                            return VFS::VFSResult<void>::Fail(VFS::VFSError::IOError);
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                }
                // In snapshot mode the file is read for the FIRST time after
                // publication; reopening by name would observe the new package.
                return ReadOpenFile(*opened.Value());
            }();
            const auto closed = root.CloseChecked(opened.Value());
            status            = read_manifest.Failed() ? read_manifest : closed;
            if (status.Failed())
            {
                return status;
            }
            CookManifest manifest = {};
            const auto   read     = manifest.Read(Bytes(), CookTarget::DesktopVulkanLE64, Limits, Allocator, &Diagnostics);
            if (read.Failed())
            {
                return VFS::VFSResult<void>::Fail(read.Error());
            }
            if (read.Value() != CookManifestReadStatus::Valid || manifest.Assets().size() != 4 || manifest.Scenes().size() != 1)
            {
                return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
            }
            for (const auto& scene : manifest.Scenes())
            {
                status = ReadFile(root, scene.Result.Object.Path, &scene.Result.Object);
                if (status.Failed())
                {
                    return status;
                }
                CheckScene(manifest, scene);
                if (HasFailure())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            for (const auto& asset : manifest.Assets())
            {
                status = ReadFile(root, asset.Result.Object.Path, &asset.Result.Object);
                if (status.Failed())
                {
                    return status;
                }
                CheckAsset(manifest, asset, changed);
                if (HasFailure())
                {
                    return VFS::VFSResult<void>::Fail(VFS::VFSError::Corrupted);
                }
            }
            return VFS::VFSResult<void>::Ok();
        }
    };

    TEST_F(SceneCookProcessTest, ExecutesRequestedPhase)
    {
        if (Options.Mode == "stress-fixture")
        {
            BuildStressFixture();
            if (!HasFailure())
            {
                std::cout << "ZE_COOK_STRESS_FIXTURE_WRITTEN\n";
            }
            return;
        }
#if defined(__linux__)
        if (DiskFullMode())
        {
            NativeDiskExhaustion exhaustion = {};
            ASSERT_TRUE(exhaustion.Initialize(Options.Root)) << "Disk-full phase requires a dedicated tmpfs of at most 16 MiB.";
            SavePixel(true);
            ASSERT_FALSE(HasFatalFailure());
            Faults.BeforeStagingWrite  = NativeDiskExhaustion::Fill;
            Faults.StagingWriteContext = &exhaustion;
            Faults.BeforeManifestWrite = Options.Mode == "disk-full-manifest";
            CookFixture(true, false);
            EXPECT_EQ(Faults.BeforeStagingWrite, nullptr);
            EXPECT_TRUE(exhaustion.Exhausted) << "The real filesystem never reported ENOSPC.";
            EXPECT_GT(exhaustion.Bytes, 0u);
            exhaustion.Reset();
            if (!HasFailure())
            {
                std::cout << "ZE_COOK_DISK_FULL_VERIFIED\n";
            }
            return;
        }
#endif
        if (Options.Mode == "corrupt-texture")
        {
            CorruptTexture();
            if (!HasFailure())
            {
                std::cout << "ZE_COOK_OBJECT_CORRUPTED\n";
            }
            return;
        }
        if (Reading)
        {
            const auto result = ConsumePackage(Options.Mode == "read-changed");
            if (Options.Mode == "reject-missing")
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::NotFound);
            }
            else if (Options.Mode == "reject-corrupt")
            {
                EXPECT_EQ(result.Error(), VFS::VFSError::Corrupted);
            }
            else
            {
                EXPECT_TRUE(result.Succeeded()) << static_cast<uint32_t>(result.Error());
            }
            if (!HasFailure())
            {
                std::cout << ((Options.Mode == "reject-schema" || Options.Mode == "reject-version") ? "ZE_COOK_SCHEMA_REJECTION_VERIFIED\n" : "ZE_COOK_PACKAGE_VERIFIED\n");
            }
            return;
        }
        const bool create = Options.Mode == "write" || Options.Mode == "write-reverse";
        if (Options.Mode == "snapshot-write")
        {
            bool ready     = false;
            char line[256] = {};
            while (std::cin.getline(line, sizeof(line)))
            {
                if (Core::Containers::StringView(line) == SnapshotReady)
                {
                    ready = true;
                    break;
                }
            }
            ASSERT_TRUE(ready) << "Snapshot reader never acquired the old manifest";
        }
        if (create)
        {
            BuildFixture();
        }
        else if (Options.Mode == "changed" || Options.Mode == "interrupt-before" || Options.Mode == "interrupt-after" || Options.Mode == "snapshot-write")
        {
            SavePixel(true);
        }
        ASSERT_FALSE(HasFatalFailure());
        if (Options.Mode == "interrupt-before" || Options.Mode == "interrupt-after")
        {
            Faults.ManifestBoundary = [](bool committed) {
                if (committed != (Options.Mode == "interrupt-after"))
                {
                    return;
                }
                // No destructors or test teardown: the disk contains exactly
                // the state left by an abruptly terminated writer.
                std::cout << (committed ? "ZE_COOK_INTERRUPTED_AFTER_COMMIT\n" : "ZE_COOK_INTERRUPTED_BEFORE_COMMIT\n") << std::flush;
                std::_Exit(committed ? 72 : 71);
            };
        }
        CookFixture(Options.Mode == "force", create || Options.Mode == "changed" || Options.Mode == "rebuild" || Options.Mode == "snapshot-write" || Options.Mode == "schema-change", Options.Mode == "busy");
        if (!HasFailure() && Options.Mode == "snapshot-write")
        {
            WriteText("/output/desktop-vulkan-le64/.ze-test-commit.done", "done");
            bool verified  = false;
            char line[256] = {};
            // Keep the pipe open through reader exit, including its trailing
            // GTest output; exiting on the marker alone could give it SIGPIPE.
            while (std::cin.getline(line, sizeof(line)))
            {
                verified |= Core::Containers::StringView(line) == "ZE_COOK_PACKAGE_VERIFIED";
            }
            EXPECT_TRUE(verified) << "Pinned manifest reader did not verify its old package";
        }
        if (!HasFailure())
        {
            std::cout << (Options.Mode == "busy" ? "ZE_COOK_WRITER_BUSY\n" : Options.Mode == "reject-migration" ? "ZE_COOK_MIGRATION_REJECTION_VERIFIED\n" : "ZE_COOK_FIXTURE_WRITTEN\n");
        }
    }

    template <typename Character>
    int Run(int argc, Character** argv)
    {
        if (argc == 5 && (std::filesystem::path(argv[1]) == "smoke-offline" || std::filesystem::path(argv[1]) == "smoke-stress"))
        {
            const auto  budget_text = std::filesystem::path(argv[4]).generic_u8string();
            const auto* first       = reinterpret_cast<cstring>(budget_text.data());
            uint64_t    budget      = 0;
            const auto  parsed      = std::from_chars(first, first + budget_text.size(), budget);
            if (parsed.ec != std::errc{} || parsed.ptr != first + budget_text.size() || !budget)
            {
                return 2;
            }
            return RunProjectSmoke(std::filesystem::path(argv[2]), std::filesystem::path(argv[3]), budget, std::filesystem::path(argv[1]) == "smoke-stress" ? 65536u : UINT32_MAX);
        }
        const bool custom = argc == 4 && std::filesystem::path(argv[1]) == "custom";
        if (argc != 3 && !custom)
        {
            return 2;
        }
        std::filesystem::path mode(argv[custom ? 2 : 1]);
        const bool            version_two = custom && (mode == "schema-change" || mode == "read-v2" || mode == "repeat-v2" || mode == "force-v2" || mode == "reject-migration");
        if (version_two && mode != "schema-change" && mode != "reject-migration")
        {
            mode = mode == "read-v2" ? "read" : mode == "repeat-v2" ? "repeat" : "force";
        }
        const bool custom_phase    = custom && (mode == "schema-change" || mode == "reject-migration" || mode == "reject-schema" || mode == "reject-version");
        bool       disk_full_phase = false;
#if defined(__linux__)
        disk_full_phase = !custom && (mode == "disk-full" || mode == "disk-full-manifest");
#endif
        if (!custom_phase && !disk_full_phase && mode != "stress-fixture" && mode != "write" && mode != "write-reverse" && mode != "repeat" && mode != "force" && mode != "changed" && mode != "read" && mode != "read-changed" && mode != "reject-missing" && mode != "reject-corrupt" && mode != "interrupt-before" && mode != "interrupt-after" && mode != "busy" && mode != "rebuild" && mode != "snapshot-read" && mode != "snapshot-write" && mode != "corrupt-texture")
        {
            return 2;
        }
        Options           = {mode, std::filesystem::path(argv[custom ? 3 : 2]), custom, version_two ? 2u : 1u};
        int   test_argc   = 1;
        char  name[]      = "SceneCookProcess";
        char* test_argv[] = {name, nullptr};
        testing::InitGoogleTest(&test_argc, test_argv);
        return RUN_ALL_TESTS();
    }
} // namespace ZEngine::Cook::Tests

#ifdef _WIN32
int wmain(int argc, wchar_t** argv)
#else
int main(int argc, char** argv)
#endif
{
    return ZEngine::Cook::Tests::Run(argc, argv);
}
