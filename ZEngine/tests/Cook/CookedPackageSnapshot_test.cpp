#include <ZEngine/Cook/CookCodecAllocationRoute.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/Memory/SlabAllocationRoute.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/ECS/BuiltInSceneComponentSchemas.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/CookedAssetReader.h>
#include <ZEngine/Managers/CookedCpuAssetSet.h>
#include <ZEngine/Managers/CookedCpuClosureCache.h>
#include <ZEngine/Managers/CookedPackageSnapshot.h>
#include <ZEngine/Rendering/Textures/CookedTextureUpload.h>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <rapidhash.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>
#include "CookManifestGolden.h"
#include "CookTestAllocator.h"

using ZEngine::Managers::CookedObjectSnapshot;
using ZEngine::Managers::CookedPackageSnapshot;

namespace ZEngine::Cook
{
    namespace VFS   = Core::VFS;
    namespace Codec = Importers::AssetCodec;
    using JSON      = nlohmann::json;

    struct ClosureCacheDrain
    {
        Managers::CookedCpuClosureCache& Cache;
        ~ClosureCacheDrain()
        {
            while (Cache.PumpOne())
            {
            }
            EXPECT_TRUE(Cache.Shutdown());
        }
    };

    class CookedPackageSnapshotTest : public testing::Test
    {
    protected:
        Tests::FixedAllocator<ZKilo(256), 12> Allocator        = {};
        Core::Memory::MemoryManager           Memory           = {};
        VFS::VFSMemoryBackend                 Backend          = {};
        VFS::VFSContext                       Context          = {};
        ECS::SceneComponentSchemaRegistry     Schemas          = {};
        ECS::SceneDiagnostics                 SceneDiagnostics = {};
        CookDiagnostics                       Diagnostics      = {};
        CookLimits                            Limits           = {.MaxWorkingBytes = ZKilo(256)};
        CookedPackageSnapshot                 Package          = {};
        JSON                                  Manifest         = {};
        uuids::uuid                           SceneId          = {};

        void                                  SetUp() override
        {
            Memory.Initialize(ZMega(8), {});
            Schemas.Initialize(&Memory.MainArena);
            ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(Schemas, &SceneDiagnostics));
            ASSERT_TRUE(Schemas.FreezeForWorker(&SceneDiagnostics));
            ASSERT_TRUE(Backend.InitializeOwned(ZMega(2), 128).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(VFS::VFSPath::Parse("/output").Value()).Succeeded());
            ASSERT_TRUE(Backend.CreateDir(VFS::VFSPath::Parse("/output/objects").Value()).Succeeded());
            Context.Initialize(&Memory.MainArena);
            ASSERT_TRUE(Context.Mount(&Backend, VFS::VFSPath::Root(), 0).Succeeded());
            Manifest         = JSON::parse(Tests::ManifestGolden.begin(), Tests::ManifestGolden.end());
            SceneId          = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000001");
            const auto words = CookSchemaKeyWorkspaceWords(Schemas);
            ASSERT_TRUE(words.Succeeded());
            std::vector<uint32_t> workspace(words.Value());
            Digest256             key = {};
            ASSERT_TRUE(ComputeCookSchemaKey(Allocator, Schemas, {workspace.data(), workspace.size()}, &key).Succeeded());
            const auto hex         = key.Hex();
            Manifest["schema_key"] = std::string(hex.data(), 64);
        }

        void TearDown() override
        {
            Package.Reset();
            Context.Shutdown();
            EXPECT_EQ(Allocator.Statistics().LiveBytes, 0u);
        }

        void WriteManifest(const JSON& value)
        {
            const auto text = value.dump();
            ASSERT_TRUE(Backend.WriteFile(VFS::VFSPath::Parse("/output/manifest.json").Value(), {reinterpret_cast<const uint8_t*>(text.data()), text.size()}).Succeeded());
        }

        CookObjectDescriptor WriteObjectBytes(cstring extension, Core::Containers::ArrayView<const uint8_t> bytes)
        {
            Core::Hash::SHA256 hash   = {};
            Digest256          digest = {};
            EXPECT_TRUE(hash.Update(bytes).Succeeded());
            EXPECT_TRUE(hash.Finalize(&digest).Succeeded());
            CookObjectDescriptor object = {};
            EXPECT_TRUE(MakeCookObjectDescriptor(digest, bytes.size(), extension, &object).Succeeded());
            const std::string path = std::string("/output/") + object.Path.CStr();
            EXPECT_TRUE(Backend.WriteFile(VFS::VFSPath::Parse(path.c_str()).Value(), bytes).Succeeded());
            return object;
        }

        CookObjectDescriptor WriteObject(cstring extension, uint8_t value)
        {
            const std::array<uint8_t, 8> bytes = {value, 1, 2, 3, 4, 5, 6, 7};
            return WriteObjectBytes(extension, {bytes.data(), bytes.size()});
        }

        JSON ObjectJSON(const CookObjectDescriptor& object)
        {
            const auto hex = object.Digest.Hex();
            return {
                {"path", object.Path.CStr()},
                {"digest", std::string(hex.data(), 64)},
                {"size", object.Size}
            };
        }

        static std::array<uint8_t, 58> Pixel(uint8_t blue)
        {
            std::array<uint8_t, 58> bytes = {0x42, 0x4d, 58, 0, 0, 0, 0, 0, 0, 0, 54, 0, 0, 0, 40, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 24, 0, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, blue, 20, 30, 0};
            return bytes;
        }

        CookObjectDescriptor SetTexture(const uuids::uuid& texture, uint8_t blue)
        {
            const auto bytes               = Pixel(blue);
            const auto object              = WriteObjectBytes("bmp", {bytes.data(), bytes.size()});
            Manifest["assets"]             = JSON::array({
                {{"kind", "texture"}, {"uuid", uuids::to_string(texture)}, {"format", "image-bmp"}, {"adapter_revision", 1}, {"input_key", std::string(64, '1')}, {"object", ObjectJSON(object)}, {"dependencies", JSON::array()}}
            });
            Manifest["scenes"][0]["roots"] = JSON::array({
                {{"kind", "texture"}, {"uuid", uuids::to_string(texture)}}
            });
            WriteManifest(Manifest);
            return object;
        }

        VFS::VFSReadRootLease Root()
        {
            VFS::VFSReadRootLease root = {};
            EXPECT_TRUE(Context.AcquireReadRoot(VFS::VFSPath::Parse("/output").Value(), &root).Succeeded());
            return root;
        }

        VFS::VFSResult<void> Capture(VFS::VFSReadRootLease&& root, const uuids::uuid& scene)
        {
            return CookedPackageSnapshot::Capture(std::move(root), scene, Schemas, Limits, Allocator, &Diagnostics, &Package);
        }
    };

    TEST_F(CookedPackageSnapshotTest, RetainsManifestAndRootAfterSourceManifestReplacement)
    {
        const auto scene_object         = WriteObject(CookSceneObjectExtension, 17);
        Manifest["scenes"][0]["object"] = ObjectJSON(scene_object);
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        EXPECT_FALSE(root.IsValid());
        ASSERT_TRUE(Package.IsValid());
        ASSERT_NE(Package.Scene(), nullptr);
        EXPECT_EQ(Package.Scene()->Result.Object.Digest, scene_object.Digest);
        const auto retained_digest = Package.ManifestDigest();
        WriteManifest(JSON::object());
        EXPECT_EQ(Package.ManifestDigest(), retained_digest);
        CookCodecAllocationRoute route(Allocator);
        CookedObjectSnapshot     bytes = {};
        ASSERT_TRUE(Package.CaptureScene(route, 8, &bytes).Succeeded());
        ASSERT_EQ(bytes.Bytes().size(), 8u);
        EXPECT_EQ(bytes.Bytes()[0], 17u);
        bytes.Reset();
        CookedPackageSnapshot moved = std::move(Package);
        EXPECT_FALSE(Package.IsValid());
        EXPECT_EQ(moved.Scene()->UUID, SceneId);
        moved.Reset();
    }

    TEST_F(CookedPackageSnapshotTest, RejectsMissingSceneSchemaMismatchAndInvalidManifestWithoutConsumingRoot)
    {
        WriteManifest(Manifest);
        auto       root   = Root();
        const auto absent = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000099");
        EXPECT_EQ(Capture(std::move(root), absent).Error(), VFS::VFSError::NotFound);
        EXPECT_TRUE(root.IsValid());
        EXPECT_FALSE(Package.IsValid());
        auto changed          = Manifest;
        changed["schema_key"] = std::string(64, '4');
        WriteManifest(changed);
        EXPECT_EQ(Capture(std::move(root), SceneId).Error(), VFS::VFSError::Unsupported);
        EXPECT_TRUE(root.IsValid());
        changed                    = Manifest;
        changed["recipe_revision"] = 0;
        WriteManifest(changed);
        EXPECT_TRUE(Capture(std::move(root), SceneId).Failed());
        EXPECT_TRUE(root.IsValid());
        EXPECT_FALSE(Package.IsValid());
    }

    TEST_F(CookedPackageSnapshotTest, PreflightSelectsOnlyTheRequestedSceneTransitiveClosure)
    {
        const auto mesh_id        = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000004");
        const auto material_id    = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000003");
        const auto texture_id     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const auto environment_id = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000005");
        const auto identity       = [](cstring kind, const uuids::uuid& uuid, cstring slot = nullptr) {
            JSON result = {
                {"kind",                   kind},
                {"uuid", uuids::to_string(uuid)}
            };
            if (slot)
            {
                result["slot"] = slot;
            }
            return result;
        };
        const auto asset = [&](cstring kind, const uuids::uuid& uuid, cstring format, cstring extension, uint8_t seed, JSON dependencies) {
            return JSON{
                {"kind", kind},
                {"uuid", uuids::to_string(uuid)},
                {"format", format},
                {"adapter_revision", 1},
                {"input_key", std::string(64, '1')},
                {"object", ObjectJSON(WriteObject(extension, seed))},
                {"dependencies", dependencies}
            };
        };
        Manifest["assets"]             = JSON::array({
            asset("environment-map", environment_id, "zenvmap-v2-le64", "zenvmap", 1, JSON::array()),
            asset("material", material_id, "zematerial-v1-json", "zematerial", 2, JSON::array({identity("texture", texture_id, "albedo")})),
            asset("mesh", mesh_id, "zemesh-v1-le64", "zemesh", 3, JSON::array({identity("material", material_id, "submeshes/0/material")})),
            asset("texture", texture_id, "image-png", "png", 4, JSON::array()),
        });
        Manifest["scenes"][0]["roots"] = JSON::array({identity("mesh", mesh_id)});
        auto second                    = Manifest["scenes"][0];
        second["uuid"]                 = "00000000-0000-4000-8000-000000000009";
        second["source"]               = "scenes/other.yaml";
        second["roots"]                = JSON::array({identity("environment-map", environment_id)});
        Manifest["scenes"].push_back(second);
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        EXPECT_NE(Package.FindSelectedAsset({CookAssetKind::Mesh, mesh_id}), nullptr);
        EXPECT_NE(Package.FindSelectedAsset({CookAssetKind::Material, material_id}), nullptr);
        EXPECT_NE(Package.FindSelectedAsset({CookAssetKind::Texture, texture_id}), nullptr);
        EXPECT_EQ(Package.FindSelectedAsset({CookAssetKind::EnvironmentMap, environment_id}), nullptr);
        std::vector<CookAssetIdentity> visited = {};
        const auto                     visit   = [](void* context, const CookManifestAsset& entry) {
            static_cast<std::vector<CookAssetIdentity>*>(context)->push_back(entry.Identity);
            return VFS::VFSResult<void>::Ok();
        };
        ASSERT_TRUE(Package.VisitSelectedAssets(visit, &visited).Succeeded());
        ASSERT_EQ(visited.size(), 3u);
        EXPECT_EQ(visited[0], (CookAssetIdentity{CookAssetKind::Material, material_id}));
        EXPECT_EQ(visited[1], (CookAssetIdentity{CookAssetKind::Mesh, mesh_id}));
        EXPECT_EQ(visited[2], (CookAssetIdentity{CookAssetKind::Texture, texture_id}));
        std::atomic<bool> cancelled = true;
        EXPECT_EQ(Package.VisitSelectedAssets(visit, &visited, &cancelled).Error(), VFS::VFSError::Cancelled);
        EXPECT_EQ(visited.size(), 3u);
        EXPECT_EQ(Package.VisitSelectedAssets([](void*, const CookManifestAsset&) { return VFS::VFSResult<void>::Fail(VFS::VFSError::PermissionDenied); }, nullptr).Error(), VFS::VFSError::PermissionDenied);
        CookCodecAllocationRoute route(Allocator);
        CookedObjectSnapshot     bytes = {};
        EXPECT_EQ(Package.CaptureAsset({CookAssetKind::EnvironmentMap, environment_id}, route, 8, &bytes).Error(), VFS::VFSError::NotFound);
        ASSERT_TRUE(Package.CaptureAsset({CookAssetKind::Texture, texture_id}, route, 8, &bytes).Succeeded());
        EXPECT_EQ(bytes.Bytes()[0], 4u);
    }

    TEST_F(CookedPackageSnapshotTest, MeshReaderMatchesLexicographicSlotsAndRejectsMissingDependency)
    {
        const auto           mesh_id     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000004");
        const auto           material_id = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000003");
        Importers::AssetMesh mesh        = {};
        mesh.MeshUUID                    = mesh_id;
        mesh.Vertices.init(&Memory.MainArena, 24, 24);
        mesh.Vertices[8]  = 1.f;
        mesh.Vertices[17] = 1.f;
        mesh.Indices.init(&Memory.MainArena, 36);
        mesh.SubMeshes.init(&Memory.MainArena, 12);
        for (uint32_t i = 0; i < 12; ++i)
        {
            mesh.Indices.push(0);
            mesh.Indices.push(1);
            mesh.Indices.push(2);
            mesh.SubMeshes.push({.MaterialUUID = material_id, .VertexCount = 3, .IndexCount = 3, .IndexOffset = i * 3, .IndexStreamOffset = i * 12, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        }
        Importers::AssetNodeHierarchy hierarchy = {};
        hierarchy.NodeHierarchyUUID             = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000005");
        hierarchy.MeshUUID                      = mesh_id;
        hierarchy.Hierarchies.init(&Memory.MainArena, 1);
        hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
        hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
        hierarchy.Names.init(&Memory.MainArena, 1);
        hierarchy.MaterialNames.init(&Memory.MainArena, 0);
        hierarchy.NodeNames.init(&Memory.MainArena, 16);
        hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
        hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
        ASSERT_EQ(Importers::AddNode(hierarchy, -1, 0), 0);
        hierarchy.Names.push_use({}).init(&Memory.MainArena, "Root");
        hierarchy.NodeNames.insert(0, 0);
        hierarchy.NodeMeshes.insert(0, 0);
        Codec::ImportConfiguration config = {};
        config.VFS                        = &Context;
        config.OutputAssetsPath.init(&Memory.MainArena, "/assets");
        config.OutputAssetFile.init(&Memory.MainArena, "mesh.zemesh");
        ASSERT_TRUE(Codec::SerializeMeshAssetFile(&Memory.MainArena, mesh, hierarchy, config).Succeeded());
        const auto opened = Context.Open(VFS::VFSPath::Parse("/assets/mesh.zemesh").Value(), VFS::VFSOpenFlags::Read);
        ASSERT_TRUE(opened.Succeeded());
        const auto size = opened.Value()->Size();
        ASSERT_TRUE(size.Succeeded());
        std::vector<uint8_t> bytes(static_cast<size_t>(size.Value()));
        const auto           read = opened.Value()->ReadAll({bytes.data(), bytes.size()});
        ASSERT_TRUE(read.Succeeded());
        ASSERT_EQ(read.Value(), bytes.size());
        ASSERT_TRUE(Context.CloseChecked(opened.Value()).Succeeded());
        const auto               mesh_object     = WriteObjectBytes("zemesh", {bytes.data(), bytes.size()});
        const auto               material_object = WriteObject("zematerial", 1);
        std::vector<std::string> slots           = {};
        for (uint32_t i = 0; i < 12; ++i)
        {
            slots.push_back("submeshes/" + std::to_string(i) + "/material");
        }
        std::sort(slots.begin(), slots.end());
        JSON dependencies = JSON::array();
        for (const auto& slot : slots)
        {
            dependencies.push_back({
                {"kind",                    "material"},
                {"uuid", uuids::to_string(material_id)},
                {"slot",                          slot}
            });
        }
        Manifest["assets"]             = JSON::array({
            {{"kind", "material"}, {"uuid", uuids::to_string(material_id)}, {"format", "zematerial-v1-json"}, {"adapter_revision", 1}, {"input_key", std::string(64, '1')}, {"object", ObjectJSON(material_object)}, {"dependencies", JSON::array()}},
            {    {"kind", "mesh"},     {"uuid", uuids::to_string(mesh_id)},     {"format", "zemesh-v1-le64"}, {"adapter_revision", 1}, {"input_key", std::string(64, '1')},     {"object", ObjectJSON(mesh_object)},  {"dependencies", dependencies}}
        });
        Manifest["scenes"][0]["roots"] = JSON::array({
            {{"kind", "mesh"}, {"uuid", uuids::to_string(mesh_id)}}
        });
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookCodecAllocationRoute route(Allocator);
        CookedObjectSnapshot     object   = {};
        const CookAssetIdentity  identity = {CookAssetKind::Mesh, mesh_id};
        ASSERT_TRUE(Package.CaptureAsset(identity, route, ZKilo(64), &object).Succeeded());
        Core::Memory::ArenaAllocator arena = {};
        Memory.MainArena.CreateSubArena(ZKilo(128), &arena, "CookedMeshReaderTest");
        Importers::AssetMesh          decoded           = {};
        Importers::AssetNodeHierarchy decoded_hierarchy = {};
        ASSERT_TRUE(Managers::CookedAssetReader::ReadMesh(Package, identity, object, &arena, *route.WorkingMemoryBudget(), &decoded, &decoded_hierarchy).Succeeded());
        EXPECT_EQ(decoded.SubMeshes.size(), 12u);
        size_t measured_bytes = 0;
        ASSERT_TRUE(Codec::MeasureMeshAssetReadWorkspace(object.Source(), &measured_bytes, mesh_id).Succeeded());
        void* exact_memory = Memory.MainArena.AllocateNoZero(measured_bytes, DEFAULT_ALIGNMENT);
        ASSERT_NE(exact_memory, nullptr);
        Core::Memory::ArenaAllocator exact_arena = {};
        ASSERT_TRUE(exact_arena.InitializeBorrowed(exact_memory, measured_bytes));
        Importers::AssetMesh          exact_mesh      = {};
        Importers::AssetNodeHierarchy exact_hierarchy = {};
        const auto                    exact_result    = Managers::CookedAssetReader::ReadMesh(Package, identity, object, &exact_arena, *route.WorkingMemoryBudget(), &exact_mesh, &exact_hierarchy);
        ASSERT_TRUE(exact_result.Succeeded()) << "Measured workspace " << measured_bytes << ", error " << static_cast<uint32_t>(exact_result.Error());
        const auto before = arena.m_current_offset;
        struct RefusedBudget final : Core::Memory::IWorkingMemoryBudget
        {
            bool TryReserveWorkingBytes(size_t) noexcept override
            {
                return false;
            }
            void ReleaseWorkingBytes(size_t) noexcept override
            {
                ADD_FAILURE();
            }
        } refused;
        EXPECT_EQ(Managers::CookedAssetReader::ReadMesh(Package, identity, object, &arena, refused, &decoded, &decoded_hierarchy).Error(), VFS::VFSError::SizeLimitExceeded);
        EXPECT_EQ(arena.m_current_offset, before);
        Package.Reset();
        Manifest["assets"][1]["dependencies"].erase(2); // lexicographic slot 10
        WriteManifest(Manifest);
        root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        EXPECT_EQ(Managers::CookedAssetReader::ReadMesh(Package, identity, object, &arena, *route.WorkingMemoryBudget(), &decoded, &decoded_hierarchy).Error(), VFS::VFSError::Corrupted);
        EXPECT_EQ(arena.m_current_offset, before);
        EXPECT_EQ(decoded.SubMeshes.size(), 12u);

        // The same mesh bytes may be reused across packages, but each package
        // must still describe the material edge encoded by every submesh.
        const auto other_material = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000006");
        const auto make_material  = [&](const uuids::uuid& id, float opacity) {
            Codec::MaterialAssetDocument value                          = {};
            value.UUID                                                  = id;
            value.Name[0]                                               = 'M';
            value.Factors[0]                                            = opacity;
            Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
            std::string                                           text  = {};
            const auto                                            write = [](void* context, Core::Containers::ArrayView<const uint8_t> input) {
                static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(input.data()), input.size());
                return VFS::VFSResult<void>::Ok();
            };
            EXPECT_TRUE(Codec::WriteCanonicalMaterialAsset(value, paths, write, &text).Succeeded());
            return WriteObjectBytes("zematerial", {reinterpret_cast<const uint8_t*>(text.data()), text.size()});
        };
        const auto first_material_object = make_material(material_id, 1.f);
        const auto other_material_object = make_material(other_material, 1.f);
        const auto material_entry        = [&](const uuids::uuid& id, const CookObjectDescriptor& descriptor) {
            return JSON{
                {"kind", "material"},
                {"uuid", uuids::to_string(id)},
                {"format", "zematerial-v1-json"},
                {"adapter_revision", 1},
                {"input_key", std::string(64, '1')},
                {"object", ObjectJSON(descriptor)},
                {"dependencies", JSON::array()}
            };
        };
        Manifest["assets"]             = JSON::array({
            material_entry(material_id, first_material_object), material_entry(other_material, other_material_object), {{"kind", "mesh"}, {"uuid", uuids::to_string(mesh_id)}, {"format", "zemesh-v1-le64"}, {"adapter_revision", 1}, {"input_key", std::string(64, '1')}, {"object", ObjectJSON(mesh_object)}, {"dependencies", dependencies}}
        });
        Manifest["scenes"][0]["roots"] = JSON::array({
            {{"kind", "material"}, {"uuid", uuids::to_string(other_material)}},
            {    {"kind", "mesh"},        {"uuid", uuids::to_string(mesh_id)}}
        });
        WriteManifest(Manifest);
        Package.Reset();
        root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        Core::Memory::SlabAllocationRoute cache_route = {};
        ASSERT_TRUE(cache_route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits cache_limits = {.MaxAssets = 3, .MaxObjectBytes = ZKilo(64), .MaxMeshWorkspaceBytes = ZKilo(128)};
        Managers::CookedCpuClosureCache      cache        = {};
        ASSERT_TRUE(cache.Initialize(cache_route, Schemas, cache_limits, 3, 2, 4));
        ClosureCacheDrain                      drain{cache};
        Managers::CookedCpuClosureCache::Lease valid = {}, compatible = {}, invalid = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &valid));
        ASSERT_TRUE(cache.PumpOne());
        Managers::CookedCpuClosureCache::Result result = {};
        ASSERT_TRUE(valid.Poll(&result));
        ASSERT_EQ(result.StateValue, Managers::CookedCpuClosureCache::State::Ready) << "VFS error " << static_cast<uint32_t>(result.Error) << ", workspace " << measured_bytes << ", route peak " << cache_route.PeakBytes() << ", capacity " << cache_route.Capacity();
        const auto* retained_mesh = result.Assets->FindMesh(identity);
        ASSERT_NE(retained_mesh, nullptr);
        const CookAssetIdentity material_identity = {CookAssetKind::Material, material_id};
        const auto*             retained_material = result.Assets->FindMaterial(material_identity);
        ASSERT_NE(retained_material, nullptr);
        const auto replacement_material = make_material(material_id, .5f);
        Manifest["assets"][0]["object"] = ObjectJSON(replacement_material);
        WriteManifest(Manifest);
        CookedPackageSnapshot changed_material = {};
        root                                   = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &changed_material).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(changed_material), &compatible));
        ASSERT_TRUE(cache.PumpOne());
        Managers::CookedCpuClosureCache::Result compatible_result = {};
        ASSERT_TRUE(compatible.Poll(&compatible_result));
        ASSERT_EQ(compatible_result.StateValue, Managers::CookedCpuClosureCache::State::Ready);
        EXPECT_EQ(compatible_result.Assets->FindMesh(identity), retained_mesh);
        ASSERT_NE(compatible_result.Assets->FindMaterial(material_identity), nullptr);
        EXPECT_NE(compatible_result.Assets->FindMaterial(material_identity), retained_material);
        EXPECT_FLOAT_EQ(retained_material->Factors[0], 1.f);
        EXPECT_FLOAT_EQ(compatible_result.Assets->FindMaterial(material_identity)->Factors[0], .5f);
        const auto retained_bytes                        = cache_route.AllocatedBytes();
        Manifest["assets"][2]["dependencies"][2]["uuid"] = uuids::to_string(other_material);
        WriteManifest(Manifest);
        CookedPackageSnapshot inconsistent = {};
        root                               = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &inconsistent).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(inconsistent), &invalid));
        ASSERT_TRUE(cache.PumpOne());
        Managers::CookedCpuClosureCache::Result failed = {};
        ASSERT_TRUE(invalid.Poll(&failed));
        EXPECT_EQ(failed.StateValue, Managers::CookedCpuClosureCache::State::Failed);
        EXPECT_EQ(failed.Error, VFS::VFSError::Corrupted);
        EXPECT_EQ(cache_route.AllocatedBytes(), retained_bytes);
        ASSERT_TRUE(valid.Poll(&result));
        EXPECT_EQ(result.Assets->FindMesh(identity), retained_mesh);
        EXPECT_EQ(retained_mesh->SubMeshes[10].MaterialUUID, material_id);
        valid.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(compatible.Poll(&compatible_result));
        EXPECT_EQ(compatible_result.Assets->FindMesh(identity), retained_mesh);
        compatible.Reset();
        invalid.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, IndependentCpuVersionsSurvivePackageReplacementAndCorruption)
    {
        const auto              texture_id = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const CookAssetIdentity identity   = {CookAssetKind::Texture, texture_id};
        auto                    bytes      = Pixel(10);
        (void) SetTexture(texture_id, 10);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookCodecAllocationRoute       route(Allocator);
        Managers::CookedCpuAssetLimits limits     = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Managers::CookedCpuAssetSet    old_assets = {};
        ASSERT_TRUE(Managers::CookedCpuAssetSet::Capture(Package, route, limits, &old_assets).Succeeded());
        ASSERT_NE(old_assets.FindTexture(identity), nullptr);
        EXPECT_EQ(old_assets.FindTexture(identity)->Pixels()[2], 10u);

        bytes[54]                          = 90;
        const auto second                  = WriteObjectBytes("bmp", {bytes.data(), bytes.size()});
        Manifest["assets"][0]["object"]    = ObjectJSON(second);
        Manifest["assets"][0]["input_key"] = std::string(64, '2');
        WriteManifest(Manifest);
        CookedPackageSnapshot next_package = {};
        root                               = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &next_package).Succeeded());
        Managers::CookedCpuAssetSet next_assets = {};
        ASSERT_TRUE(Managers::CookedCpuAssetSet::Capture(next_package, route, limits, &next_assets).Succeeded());
        EXPECT_NE(old_assets.FindVersion(identity)->ObjectDigest, next_assets.FindVersion(identity)->ObjectDigest);
        EXPECT_EQ(old_assets.FindTexture(identity)->Pixels()[2], 10u);
        EXPECT_EQ(next_assets.FindTexture(identity)->Pixels()[2], 90u);
        bytes[54]              = 42;
        const std::string path = std::string("/output/") + second.Path.CStr();
        ASSERT_TRUE(Backend.WriteFile(VFS::VFSPath::Parse(path.c_str()).Value(), {bytes.data(), bytes.size()}).Succeeded());
        Managers::CookedCpuAssetSet rejected = {};
        const auto                  baseline = Allocator.Statistics().LiveBytes;
        EXPECT_EQ(Managers::CookedCpuAssetSet::Capture(next_package, route, limits, &rejected).Error(), VFS::VFSError::Corrupted);
        EXPECT_FALSE(rejected.IsValid());
        EXPECT_EQ(Allocator.Statistics().LiveBytes, baseline);
        Package.Reset();
        next_package.Reset();
        EXPECT_EQ(old_assets.FindTexture(identity)->Pixels()[2], 10u);
        EXPECT_EQ(next_assets.FindTexture(identity)->Pixels()[2], 90u);
    }

    TEST_F(CookedPackageSnapshotTest, EmptyClosureHasAValidOwnerWithoutAssetAllocations)
    {
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookCodecAllocationRoute       route(Allocator);
        const auto                     baseline = Allocator.Statistics().LiveBytes;
        Managers::CookedCpuAssetLimits limits   = {.MaxAssets = 0, .MaxObjectBytes = 1};
        Managers::CookedCpuAssetSet    assets   = {};
        ASSERT_TRUE(Managers::CookedCpuAssetSet::Capture(Package, route, limits, &assets).Succeeded());
        EXPECT_TRUE(assets.IsValid());
        EXPECT_EQ(assets.Size(), 0u);
        EXPECT_EQ(assets.VersionAt(0), nullptr);
        EXPECT_EQ(Allocator.Statistics().LiveBytes, baseline);
        EXPECT_EQ(Managers::CookedCpuAssetSet::Capture(Package, route, limits, &assets).Error(), VFS::VFSError::InvalidPath);
        EXPECT_TRUE(assets.IsValid());
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheSharesOneBuildAndRetiresOnlyOnWorker)
    {
        using Cache                      = Managers::CookedCpuClosureCache;
        const auto              texture  = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const CookAssetIdentity identity = {CookAssetKind::Texture, texture};
        (void) SetTexture(texture, 10);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot other = {};
        root                        = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &other).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 8));
        ClosureCacheDrain drain{cache};
        const auto        empty_bytes = route.AllocatedBytes();
        Cache::Lease      first = {}, second = {}, refused = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &first));
        EXPECT_FALSE(Package.IsValid());
        EXPECT_FALSE(first.Retain(nullptr));
        ASSERT_TRUE(first.Retain(&second));
        EXPECT_FALSE(first.Retain(&second));
        EXPECT_FALSE(first.Retain(&refused));
        EXPECT_TRUE(other.IsValid());
        EXPECT_FALSE(cache.Request(std::move(other), &refused));
        EXPECT_FALSE(refused);
        EXPECT_TRUE(other.IsValid());
        Cache::Result first_result = {}, second_result = {};
        ASSERT_TRUE(first.Poll(&first_result));
        EXPECT_EQ(first_result.StateValue, Cache::State::Pending);
        EXPECT_FALSE(cache.Shutdown());
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_FALSE(cache.PumpOne());
        ASSERT_TRUE(first.Poll(&first_result));
        ASSERT_TRUE(second.Poll(&second_result));
        ASSERT_EQ(first_result.StateValue, Cache::State::Ready);
        EXPECT_EQ(first_result.Assets, second_result.Assets);
        ASSERT_NE(first_result.Assets->FindTexture(identity), nullptr);
        EXPECT_EQ(first_result.Assets->FindTexture(identity)->Pixels()[2], 10u);
        const auto live_bytes = route.AllocatedBytes();
        EXPECT_GT(live_bytes, empty_bytes);
        first.Reset();
        EXPECT_EQ(route.AllocatedBytes(), live_bytes);
        ASSERT_TRUE(second.Poll(&second_result));
        EXPECT_EQ(second_result.Assets->FindTexture(identity)->Pixels()[2], 10u);
        ASSERT_TRUE(second.Retain(&first));
        second.Reset();
        ASSERT_TRUE(first.Poll(&first_result));
        EXPECT_EQ(first_result.Assets, second_result.Assets);
        first.Reset();
        EXPECT_EQ(route.AllocatedBytes(), live_bytes);
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_EQ(route.AllocatedBytes(), empty_bytes);
        ASSERT_TRUE(cache.Request(std::move(other), &refused));
        EXPECT_FALSE(other.IsValid());
        refused.Reset();
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_EQ(route.AllocatedBytes(), empty_bytes);
        ASSERT_TRUE(cache.Shutdown());
        EXPECT_EQ(route.AllocatedBytes(), 0u);
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheCancellationKeepsOtherWaitersAndDrainsLastCancel)
    {
        using Cache        = Managers::CookedCpuClosureCache;
        const auto texture = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        (void) SetTexture(texture, 10);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot other = {};
        root                        = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &other).Succeeded());
        Core::Memory::SlabAllocationRoute base = {};
        ASSERT_TRUE(base.Initialize(&Memory.MainArena, ZMega(1)));
        struct BlockingRoute final : Core::Memory::IAllocationRoute
        {
            Core::Memory::SlabAllocationRoute* Base    = nullptr;
            std::atomic<bool>                  Armed   = false;
            std::atomic<bool>                  Entered = false;
            std::atomic<bool>                  Resume  = false;
            void*                              Allocate(size_t size) noexcept override
            {
                void* memory = Base->Allocate(size);
                if (Armed.exchange(false, std::memory_order_acq_rel))
                {
                    Entered.store(true, std::memory_order_release);
                    while (!Resume.load(std::memory_order_acquire))
                    {
                        std::this_thread::yield();
                    }
                }
                return memory;
            }
            void* Reallocate(void* pointer, size_t old_size, size_t new_size) noexcept override
            {
                return Base->Reallocate(pointer, old_size, new_size);
            }
            void Free(void* pointer) noexcept override
            {
                Base->Free(pointer);
            }
            Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
            {
                return Base->WorkingMemoryBudget();
            }
        } route;
        route.Base                                  = &base;
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 1, 2, 8));
        ClosureCacheDrain drain{cache};
        const auto        baseline = base.AllocatedBytes();
        Cache::Lease      first = {}, second = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &first));
        route.Armed.store(true, std::memory_order_release);
        std::atomic<bool> pumped = false;
        std::thread       worker([&] { pumped.store(cache.PumpOne(), std::memory_order_release); });
        const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!route.Entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }
        const bool    entered             = route.Entered.load(std::memory_order_acquire);
        const bool    joined              = entered && cache.Request(std::move(other), &second);
        Cache::Result pending             = {};
        const bool    pending_visible     = joined && second.Poll(&pending) && pending.StateValue == Cache::State::Pending;
        const bool    shutdown_while_busy = cache.Shutdown();
        first.Reset();
        route.Resume.store(true, std::memory_order_release);
        worker.join();
        ASSERT_TRUE(entered);
        ASSERT_TRUE(joined);
        EXPECT_TRUE(pending_visible);
        EXPECT_FALSE(shutdown_while_busy);
        ASSERT_TRUE(pumped.load(std::memory_order_acquire));
        Cache::Result result = {};
        ASSERT_TRUE(second.Poll(&result));
        EXPECT_EQ(result.StateValue, Cache::State::Ready);
        EXPECT_TRUE(other.IsValid());
        second.Reset();
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_EQ(base.AllocatedBytes(), baseline);

        Cache::Lease last = {};
        ASSERT_TRUE(cache.Request(std::move(other), &last));
        route.Entered.store(false, std::memory_order_release);
        route.Resume.store(false, std::memory_order_release);
        route.Armed.store(true, std::memory_order_release);
        pumped.store(false, std::memory_order_release);
        std::thread cancelled_worker([&] { pumped.store(cache.PumpOne(), std::memory_order_release); });
        const auto  cancel_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!route.Entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < cancel_deadline)
        {
            std::this_thread::yield();
        }
        const bool cancelling = route.Entered.load(std::memory_order_acquire);
        last.Reset();
        route.Resume.store(true, std::memory_order_release);
        cancelled_worker.join();
        ASSERT_TRUE(cancelling);
        ASSERT_TRUE(pumped.load(std::memory_order_acquire));
        EXPECT_EQ(base.AllocatedBytes(), baseline);
        EXPECT_FALSE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheAdmitsNewWorkWhileRetirementWorkerIsBlocked)
    {
        using Cache        = Managers::CookedCpuClosureCache;
        const auto texture = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        (void) SetTexture(texture, 10);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot next = {};
        root                       = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &next).Succeeded());
        Core::Memory::SlabAllocationRoute base = {};
        ASSERT_TRUE(base.Initialize(&Memory.MainArena, ZMega(1)));
        struct BlockingFreeRoute final : Core::Memory::IAllocationRoute
        {
            Core::Memory::SlabAllocationRoute* Base  = nullptr;
            std::atomic<bool>                  Armed = false, Entered = false, Resume = false;
            void*                              Allocate(size_t size) noexcept override
            {
                return Base->Allocate(size);
            }
            void* Reallocate(void* pointer, size_t old_size, size_t new_size) noexcept override
            {
                return Base->Reallocate(pointer, old_size, new_size);
            }
            void Free(void* pointer) noexcept override
            {
                if (Armed.exchange(false, std::memory_order_acq_rel))
                {
                    Entered.store(true, std::memory_order_release);
                    while (!Resume.load(std::memory_order_acquire))
                        std::this_thread::yield();
                }
                Base->Free(pointer);
            }
            Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
            {
                return Base->WorkingMemoryBudget();
            }
        } route;
        route.Base                                  = &base;
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 8));
        ClosureCacheDrain drain{cache};
        Cache::Lease      first = {}, second = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &first));
        ASSERT_TRUE(cache.PumpOne());
        first.Reset();
        Cache::Statistics before = {};
        ASSERT_TRUE(cache.TryGetStatistics(&before));
        EXPECT_EQ(before.Retiring, 1u);
        EXPECT_EQ(before.Leases, 0u);
        route.Armed.store(true, std::memory_order_release);
        std::atomic<bool> pumped = false;
        std::thread       worker([&] { pumped.store(cache.PumpOne(), std::memory_order_release); });
        const auto        deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!route.Entered.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
            std::this_thread::yield();
        const bool        entered     = route.Entered.load(std::memory_order_acquire);
        const bool        admitted    = entered && cache.Request(std::move(next), &second);
        Cache::Statistics busy        = {.Closures = 99};
        const bool        snapshot    = cache.TryGetStatistics(&busy);
        const bool        second_pump = cache.PumpOne();
        route.Resume.store(true, std::memory_order_release);
        worker.join();
        ASSERT_TRUE(entered);
        ASSERT_TRUE(admitted);
        EXPECT_FALSE(snapshot);
        EXPECT_EQ(busy.Closures, 99u);
        EXPECT_FALSE(second_pump);
        ASSERT_TRUE(pumped.load(std::memory_order_acquire));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result result = {};
        ASSERT_TRUE(second.Poll(&result));
        EXPECT_EQ(result.StateValue, Cache::State::Ready);
        second.Reset();
        ASSERT_TRUE(cache.PumpOne());
        Cache::Statistics final = {};
        ASSERT_TRUE(cache.TryGetStatistics(&final));
        EXPECT_EQ(final.Closures, 0u);
        EXPECT_EQ(final.Leases, 0u);
        EXPECT_EQ(final.AssetVersions, 0u);
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheKeepsFailureIsolatedAndAllowsRetry)
    {
        using Cache        = Managers::CookedCpuClosureCache;
        const auto texture = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const auto object  = SetTexture(texture, 10);
        auto       root    = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot retry_package = {};
        root                                = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &retry_package).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 8));
        ClosureCacheDrain drain{cache};
        Cache::Lease      failed = {}, retry = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &failed));
        const std::string path  = std::string("/output/") + object.Path.CStr();
        auto              bytes = Pixel(42);
        ASSERT_TRUE(Backend.WriteFile(VFS::VFSPath::Parse(path.c_str()).Value(), {bytes.data(), bytes.size()}).Succeeded());
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result failed_result = {};
        ASSERT_TRUE(failed.Poll(&failed_result));
        EXPECT_EQ(failed_result.StateValue, Cache::State::Failed);
        EXPECT_EQ(failed_result.Error, VFS::VFSError::Corrupted);
        EXPECT_EQ(failed_result.Assets, nullptr);
        bytes = Pixel(10);
        ASSERT_TRUE(Backend.WriteFile(VFS::VFSPath::Parse(path.c_str()).Value(), {bytes.data(), bytes.size()}).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(retry_package), &retry));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result retry_result = {};
        ASSERT_TRUE(retry.Poll(&retry_result));
        EXPECT_EQ(retry_result.StateValue, Cache::State::Ready);
        ASSERT_TRUE(failed.Poll(&failed_result));
        EXPECT_EQ(failed_result.StateValue, Cache::State::Failed);
        failed.Reset();
        retry.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_FALSE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheKeepsOldAndNewVersionsIndependent)
    {
        using Cache                      = Managers::CookedCpuClosureCache;
        const auto              texture  = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const CookAssetIdentity identity = {CookAssetKind::Texture, texture};
        (void) SetTexture(texture, 10);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 8));
        ClosureCacheDrain drain{cache};
        Cache::Lease      old_lease = {}, new_lease = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &old_lease));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result old_result = {};
        ASSERT_TRUE(old_lease.Poll(&old_result));
        ASSERT_EQ(old_result.StateValue, Cache::State::Ready);
        ASSERT_NE(old_result.Assets->FindTexture(identity), nullptr);
        EXPECT_EQ(old_result.Assets->FindTexture(identity)->Pixels()[2], 10u);

        (void) SetTexture(texture, 90);
        CookedPackageSnapshot replacement = {};
        root                              = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &replacement).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(replacement), &new_lease));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result new_result = {};
        ASSERT_TRUE(new_lease.Poll(&new_result));
        ASSERT_EQ(new_result.StateValue, Cache::State::Ready);
        ASSERT_NE(new_result.Assets->FindTexture(identity), nullptr);
        EXPECT_NE(old_result.Assets, new_result.Assets);
        EXPECT_NE(old_result.Assets->FindVersion(identity)->ObjectDigest, new_result.Assets->FindVersion(identity)->ObjectDigest);
        EXPECT_EQ(old_result.Assets->FindTexture(identity)->Pixels()[2], 10u);
        EXPECT_EQ(new_result.Assets->FindTexture(identity)->Pixels()[2], 90u);
        old_lease.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(new_lease.Poll(&new_result));
        EXPECT_EQ(new_result.Assets->FindTexture(identity)->Pixels()[2], 90u);
        new_lease.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, CookedTextureUploadRetainsExactSourceAndPreservesLeaseOnRefusal)
    {
        using Cache                      = Managers::CookedCpuClosureCache;
        using Upload                     = Rendering::Textures::CookedTextureUpload;
        const auto              texture  = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const CookAssetIdentity identity = {CookAssetKind::Texture, texture};
        const auto              object   = SetTexture(texture, 10);
        auto                    root     = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 1, 2, 4));
        ClosureCacheDrain drain{cache};
        Cache::Lease      lease = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &lease));
        Upload upload = {};
        EXPECT_EQ(upload.Poll(), Upload::Readiness::Failed);
        EXPECT_FALSE(upload.Capture(std::move(lease), identity)); // Still queued.
        EXPECT_TRUE(lease);
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_FALSE(upload.Capture(std::move(lease), {CookAssetKind::Mesh, texture}));
        EXPECT_TRUE(lease);
        ASSERT_TRUE(upload.Capture(std::move(lease), identity));
        EXPECT_FALSE(lease);
        EXPECT_EQ(upload.State(), Upload::Phase::SourceReady);
        EXPECT_EQ(upload.Poll(), Upload::Readiness::Pending);
        EXPECT_EQ(upload.Identity(), identity);
        EXPECT_EQ(upload.Digest(), object.Digest);
        EXPECT_EQ(upload.SubmittedBinding(), nullptr);
        EXPECT_FALSE(cache.Shutdown()); // Candidate owns the only closure lease.
        auto no_device = std::make_unique<Rendering::RenderResourceManager>();
        EXPECT_FALSE(upload.Prepare(*no_device));
        EXPECT_EQ(upload.State(), Upload::Phase::SourceReady);
        EXPECT_FALSE(upload.StageOne());
        EXPECT_FALSE(upload.Submit(0, 0, {}));
        upload.Reset();
        upload.Reset();
        EXPECT_EQ(upload.State(), Upload::Phase::Empty);
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, ClosureCacheRequiresItsFrozenSchemaRegistryAndBoundedCapacity)
    {
        using Cache = Managers::CookedCpuClosureCache;
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        ECS::SceneComponentSchemaRegistry other_schemas = {};
        other_schemas.Initialize(&Memory.MainArena);
        ASSERT_TRUE(ECS::RegisterBuiltInSceneComponentSchemas(other_schemas, &SceneDiagnostics));
        ASSERT_TRUE(other_schemas.FreezeForWorker(&SceneDiagnostics));
        CookedPackageSnapshot other = {};
        root                        = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, other_schemas, Limits, Allocator, &Diagnostics, &other).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 0, .MaxObjectBytes = 1};
        Cache                                cache  = {};
        EXPECT_FALSE(cache.Initialize(route, Schemas, limits, Cache::MaxClosures + 1, 1, 0));
        EXPECT_EQ(route.AllocatedBytes(), 0u);
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 1, 1, 0));
        ClosureCacheDrain drain{cache};
        Cache::Lease      lease = {};
        EXPECT_FALSE(cache.Request(std::move(other), &lease));
        EXPECT_TRUE(other.IsValid());
        EXPECT_FALSE(lease);
        ASSERT_TRUE(cache.Request(std::move(Package), &lease));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result result = {};
        ASSERT_TRUE(lease.Poll(&result));
        ASSERT_EQ(result.StateValue, Cache::State::Ready);
        EXPECT_EQ(result.Assets->Size(), 0u);
        lease.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, DifferentScenesShareAnExactAssetVersionAndRespectVersionCapacity)
    {
        using Cache                         = Managers::CookedCpuClosureCache;
        const auto              texture     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const auto              other_scene = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000009");
        const CookAssetIdentity identity    = {CookAssetKind::Texture, texture};
        const auto              object      = SetTexture(texture, 10);
        auto                    scene       = Manifest["scenes"][0];
        scene["uuid"]                       = uuids::to_string(other_scene);
        scene["source"]                     = "scenes/other.yaml";
        Manifest["scenes"].push_back(scene);
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot other = {};
        root                        = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), other_scene, Schemas, Limits, Allocator, &Diagnostics, &other).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 3, 2, 1));
        ClosureCacheDrain drain{cache};
        const auto        baseline = route.AllocatedBytes();
        Cache::Lease      first = {}, second = {}, exhausted = {}, retry = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &first));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result first_result = {};
        ASSERT_TRUE(first.Poll(&first_result));
        ASSERT_EQ(first_result.StateValue, Cache::State::Ready);
        const auto* original = first_result.Assets->FindTexture(identity);
        ASSERT_NE(original, nullptr);
        const std::string path = std::string("/output/") + object.Path.CStr();
        ASSERT_TRUE(Backend.Remove(VFS::VFSPath::Parse(path.c_str()).Value()).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(other), &second));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result second_result = {};
        ASSERT_TRUE(second.Poll(&second_result));
        ASSERT_EQ(second_result.StateValue, Cache::State::Ready);
        EXPECT_NE(first_result.Assets, second_result.Assets);
        EXPECT_EQ(original, second_result.Assets->FindTexture(identity));
        EXPECT_EQ(second_result.Assets->FindTexture(identity)->Pixels()[2], 10u);

        (void) SetTexture(texture, 90);
        CookedPackageSnapshot replacement = {};
        root                              = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &replacement).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(replacement), &exhausted));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result failure = {};
        ASSERT_TRUE(exhausted.Poll(&failure));
        EXPECT_EQ(failure.StateValue, Cache::State::Failed);
        EXPECT_EQ(failure.Error, VFS::VFSError::SizeLimitExceeded);
        first.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(second.Poll(&second_result));
        EXPECT_EQ(second_result.Assets->FindTexture(identity), original);
        second.Reset();
        ASSERT_TRUE(cache.PumpOne());
        root = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &replacement).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(replacement), &retry));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result retry_result = {};
        ASSERT_TRUE(retry.Poll(&retry_result));
        ASSERT_EQ(retry_result.StateValue, Cache::State::Ready);
        ASSERT_NE(retry_result.Assets->FindTexture(identity), nullptr);
        EXPECT_EQ(retry_result.Assets->FindTexture(identity)->Pixels()[2], 90u);
        exhausted.Reset();
        retry.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        EXPECT_EQ(route.AllocatedBytes(), baseline);
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, SharedMaterialStillRejectsDifferentManifestDependencies)
    {
        using Cache                               = Managers::CookedCpuClosureCache;
        const auto              material_id       = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000003");
        const auto              first_texture     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000004");
        const auto              other_texture     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000005");
        const CookAssetIdentity material_key      = {CookAssetKind::Material, material_id};
        auto                    pixel             = Pixel(10);
        const auto              first_object      = WriteObjectBytes("bmp", {pixel.data(), pixel.size()});
        pixel                                     = Pixel(90);
        const auto                   other_object = WriteObjectBytes("bmp", {pixel.data(), pixel.size()});
        Codec::MaterialAssetDocument material     = {};
        material.UUID                             = material_id;
        material.Name[0]                          = 'M';
        material.Textures[0].UUID                 = first_texture;
        ASSERT_EQ(Helpers::secure_strcpy(material.Textures[0].Path, sizeof(material.Textures[0].Path), first_object.Path.CStr()), Helpers::MEMORY_OP_SUCCESS);
        Core::Containers::FixedArray<VFS::VFSRelativePath, 5> paths = {};
        paths[0]                                                    = first_object.Path;
        std::string material_bytes                                  = {};
        const auto  write                                           = [](void* context, Core::Containers::ArrayView<const uint8_t> bytes) {
            static_cast<std::string*>(context)->append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return VFS::VFSResult<void>::Ok();
        };
        ASSERT_TRUE(Codec::WriteCanonicalMaterialAsset(material, paths, write, &material_bytes).Succeeded());
        const auto material_object = WriteObjectBytes("zematerial", {reinterpret_cast<const uint8_t*>(material_bytes.data()), material_bytes.size()});
        const auto texture_entry   = [&](const uuids::uuid& uuid, const CookObjectDescriptor& object) {
            return JSON{
                {"kind", "texture"},
                {"uuid", uuids::to_string(uuid)},
                {"format", "image-bmp"},
                {"adapter_revision", 1},
                {"input_key", std::string(64, '1')},
                {"object", ObjectJSON(object)},
                {"dependencies", JSON::array()}
            };
        };
        Manifest["assets"]             = JSON::array({
            {{"kind", "material"}, {"uuid", uuids::to_string(material_id)}, {"format", "zematerial-v1-json"}, {"adapter_revision", 1}, {"input_key", std::string(64, '1')}, {"object", ObjectJSON(material_object)}, {"dependencies", JSON::array({{{"kind", "texture"}, {"uuid", uuids::to_string(first_texture)}, {"slot", "albedo"}}})}},
            texture_entry(first_texture, first_object), texture_entry(other_texture, other_object)
        });
        Manifest["scenes"][0]["roots"] = JSON::array({
            {{"kind", "material"},   {"uuid", uuids::to_string(material_id)}},
            { {"kind", "texture"}, {"uuid", uuids::to_string(first_texture)}},
            { {"kind", "texture"}, {"uuid", uuids::to_string(other_texture)}}
        });
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 3, .MaxObjectBytes = ZKilo(4), .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 3, 2, 3));
        ClosureCacheDrain drain{cache};
        Cache::Lease      valid = {}, invalid = {}, invalid_version = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &valid));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result valid_result = {};
        ASSERT_TRUE(valid.Poll(&valid_result));
        ASSERT_EQ(valid_result.StateValue, Cache::State::Ready);
        ASSERT_NE(valid_result.Assets->FindMaterial(material_key), nullptr);
        EXPECT_EQ(valid_result.Assets->FindMaterial(material_key)->Textures[0].UUID, first_texture);

        Manifest["assets"][0]["dependencies"][0]["uuid"] = uuids::to_string(other_texture);
        WriteManifest(Manifest);
        CookedPackageSnapshot changed = {};
        root                          = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &changed).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(changed), &invalid));
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result invalid_result = {};
        ASSERT_TRUE(invalid.Poll(&invalid_result));
        EXPECT_EQ(invalid_result.StateValue, Cache::State::Failed);
        EXPECT_EQ(invalid_result.Error, VFS::VFSError::Corrupted);
        Manifest["assets"][0]["dependencies"][0]["uuid"] = uuids::to_string(first_texture);
        Manifest["assets"][1]["object"]                  = ObjectJSON(other_object);
        WriteManifest(Manifest);
        root = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &changed).Succeeded());
        ASSERT_TRUE(cache.Request(std::move(changed), &invalid_version));
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(invalid_version.Poll(&invalid_result));
        EXPECT_EQ(invalid_result.StateValue, Cache::State::Failed);
        EXPECT_EQ(invalid_result.Error, VFS::VFSError::Corrupted);
        ASSERT_TRUE(valid.Poll(&valid_result));
        EXPECT_EQ(valid_result.Assets->FindMaterial(material_key)->Textures[0].UUID, first_texture);
        valid.Reset();
        invalid.Reset();
        invalid_version.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, VersionPoolInitializationReleasesPartialAllocations)
    {
        using Cache                            = Managers::CookedCpuClosureCache;
        Core::Memory::SlabAllocationRoute base = {};
        ASSERT_TRUE(base.Initialize(&Memory.MainArena, ZMega(1)));
        struct RefusingRoute final : Core::Memory::IAllocationRoute
        {
            Core::Memory::SlabAllocationRoute* Base     = nullptr;
            uint32_t                           Calls    = 0;
            uint32_t                           RefuseAt = 4;
            void*                              Allocate(size_t bytes) noexcept override
            {
                return ++Calls == RefuseAt ? nullptr : Base->Allocate(bytes);
            }
            void* Reallocate(void* pointer, size_t old_size, size_t new_size) noexcept override
            {
                return Base->Reallocate(pointer, old_size, new_size);
            }
            void Free(void* pointer) noexcept override
            {
                Base->Free(pointer);
            }
            Core::Memory::IWorkingMemoryBudget* WorkingMemoryBudget() noexcept override
            {
                return Base->WorkingMemoryBudget();
            }
        } route;
        route.Base                                  = &base;
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        EXPECT_FALSE(cache.Initialize(route, Schemas, limits, 2, 2, 4));
        EXPECT_EQ(route.Calls, 4u);
        EXPECT_EQ(base.AllocatedBytes(), 0u);
        route.RefuseAt = UINT32_MAX;
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 4));
        ASSERT_TRUE(cache.Shutdown());
        EXPECT_EQ(base.AllocatedBytes(), 0u);
    }

    TEST_F(CookedPackageSnapshotTest, VersionPoolRemovesHashCollisionsWithoutLosingOtherVersions)
    {
        using Cache                         = Managers::CookedCpuClosureCache;
        const auto              texture     = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const auto              other_scene = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000009");
        const CookAssetIdentity identity    = {CookAssetKind::Texture, texture};
        const auto              uuid_bytes  = texture.as_bytes();
        const auto              seed        = rapidhash_withSeed(uuid_bytes.data(), uuid_bytes.size(), static_cast<uint64_t>(CookAssetKind::Texture));
        std::array<int, 4>      seen        = {-1, -1, -1, -1};
        int                     first_pixel = -1, second_pixel = -1;
        for (int value = 0; value <= 255; ++value)
        {
            const auto         bytes  = Pixel(static_cast<uint8_t>(value));
            Core::Hash::SHA256 hash   = {};
            Digest256          digest = {};
            ASSERT_TRUE(hash.Update({bytes.data(), bytes.size()}).Succeeded());
            ASSERT_TRUE(hash.Finalize(&digest).Succeeded());
            const auto bucket = static_cast<size_t>(rapidhash_withSeed(digest.Bytes.data(), digest.Bytes.size(), seed) & 3u);
            if (seen[bucket] >= 0)
            {
                first_pixel  = seen[bucket];
                second_pixel = value;
                break;
            }
            seen[bucket] = value;
        }
        ASSERT_GE(first_pixel, 0);
        ASSERT_GE(second_pixel, 0);
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 3, 2, 2));
        ClosureCacheDrain drain{cache};
        const auto        baseline = route.AllocatedBytes();
        for (bool release_tail : {true, false})
        {
            (void) SetTexture(texture, static_cast<uint8_t>(first_pixel));
            auto root = Root();
            ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
            Cache::Lease first = {}, second = {}, shared = {};
            ASSERT_TRUE(cache.Request(std::move(Package), &first));
            ASSERT_TRUE(cache.PumpOne());
            (void) SetTexture(texture, static_cast<uint8_t>(second_pixel));
            CookedPackageSnapshot next = {};
            root                       = Root();
            ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), SceneId, Schemas, Limits, Allocator, &Diagnostics, &next).Succeeded());
            ASSERT_TRUE(cache.Request(std::move(next), &second));
            ASSERT_TRUE(cache.PumpOne());
            Cache::Result first_result = {}, second_result = {};
            ASSERT_TRUE(first.Poll(&first_result));
            ASSERT_TRUE(second.Poll(&second_result));
            ASSERT_EQ(first_result.StateValue, Cache::State::Ready);
            ASSERT_EQ(second_result.StateValue, Cache::State::Ready);
            const auto* survivor = release_tail ? second_result.Assets->FindTexture(identity) : first_result.Assets->FindTexture(identity);
            ASSERT_NE(survivor, nullptr);
            if (release_tail)
            {
                first.Reset();
            }
            else
            {
                second.Reset();
                (void) SetTexture(texture, static_cast<uint8_t>(first_pixel));
            }
            ASSERT_TRUE(cache.PumpOne());
            if (Manifest["scenes"].size() == 1)
            {
                auto scene      = Manifest["scenes"][0];
                scene["uuid"]   = uuids::to_string(other_scene);
                scene["source"] = "scenes/other.yaml";
                Manifest["scenes"].push_back(scene);
                WriteManifest(Manifest);
            }
            root = Root();
            ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), other_scene, Schemas, Limits, Allocator, &Diagnostics, &next).Succeeded());
            ASSERT_TRUE(cache.Request(std::move(next), &shared));
            ASSERT_TRUE(cache.PumpOne());
            Cache::Result shared_result = {};
            ASSERT_TRUE(shared.Poll(&shared_result));
            ASSERT_EQ(shared_result.StateValue, Cache::State::Ready);
            EXPECT_EQ(shared_result.Assets->FindTexture(identity), survivor);
            first.Reset();
            second.Reset();
            shared.Reset();
            ASSERT_TRUE(cache.PumpOne());
            ASSERT_TRUE(cache.PumpOne());
            EXPECT_EQ(route.AllocatedBytes(), baseline);
        }
        ASSERT_TRUE(cache.Shutdown());
    }

    TEST_F(CookedPackageSnapshotTest, VersionPoolIncludesTypedIdentityEvenWhenObjectBytesMatch)
    {
        using Cache            = Managers::CookedCpuClosureCache;
        const auto first_id    = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000002");
        const auto second_id   = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000003");
        const auto other_scene = *uuids::uuid::from_string("00000000-0000-4000-8000-000000000009");
        (void) SetTexture(first_id, 10);
        auto second_asset    = Manifest["assets"][0];
        second_asset["uuid"] = uuids::to_string(second_id);
        Manifest["assets"].push_back(second_asset);
        auto scene                = Manifest["scenes"][0];
        scene["uuid"]             = uuids::to_string(other_scene);
        scene["source"]           = "scenes/other.yaml";
        scene["roots"][0]["uuid"] = uuids::to_string(second_id);
        Manifest["scenes"].push_back(scene);
        WriteManifest(Manifest);
        auto root = Root();
        ASSERT_TRUE(Capture(std::move(root), SceneId).Succeeded());
        CookedPackageSnapshot other = {};
        root                        = Root();
        ASSERT_TRUE(CookedPackageSnapshot::Capture(std::move(root), other_scene, Schemas, Limits, Allocator, &Diagnostics, &other).Succeeded());
        Core::Memory::SlabAllocationRoute route = {};
        ASSERT_TRUE(route.Initialize(&Memory.MainArena, ZMega(1)));
        const Managers::CookedCpuAssetLimits limits = {.MaxAssets = 1, .MaxObjectBytes = 58, .MaxMeshWorkspaceBytes = ZKilo(64)};
        Cache                                cache  = {};
        ASSERT_TRUE(cache.Initialize(route, Schemas, limits, 2, 2, 2));
        ClosureCacheDrain drain{cache};
        Cache::Lease      first = {}, second = {};
        ASSERT_TRUE(cache.Request(std::move(Package), &first));
        ASSERT_TRUE(cache.Request(std::move(other), &second));
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        Cache::Result first_result = {}, second_result = {};
        ASSERT_TRUE(first.Poll(&first_result));
        ASSERT_TRUE(second.Poll(&second_result));
        ASSERT_EQ(first_result.StateValue, Cache::State::Ready);
        ASSERT_EQ(second_result.StateValue, Cache::State::Ready);
        const CookAssetIdentity first_key  = {CookAssetKind::Texture, first_id};
        const CookAssetIdentity second_key = {CookAssetKind::Texture, second_id};
        ASSERT_NE(first_result.Assets->FindVersion(first_key), nullptr);
        ASSERT_NE(second_result.Assets->FindVersion(second_key), nullptr);
        EXPECT_EQ(first_result.Assets->FindVersion(first_key)->ObjectDigest, second_result.Assets->FindVersion(second_key)->ObjectDigest);
        ASSERT_NE(first_result.Assets->FindTexture(first_key), nullptr);
        ASSERT_NE(second_result.Assets->FindTexture(second_key), nullptr);
        EXPECT_NE(first_result.Assets->FindTexture(first_key), second_result.Assets->FindTexture(second_key));
        EXPECT_EQ(first_result.Assets->FindTexture(second_key), nullptr);
        EXPECT_EQ(second_result.Assets->FindTexture(first_key), nullptr);
        first.Reset();
        second.Reset();
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.PumpOne());
        ASSERT_TRUE(cache.Shutdown());
    }
} // namespace ZEngine::Cook
