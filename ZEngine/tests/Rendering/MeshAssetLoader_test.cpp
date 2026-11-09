#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Profiling/Profiling.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Importers;
using ZEngine::Managers::AssetManager;
using ZEngine::Managers::AssetType;

TEST(AssetManagerLifetimeTest, RegistryCallbacksDoNotLeakAcrossLifetimes)
{
    ASSERT_EQ(AssetManager::Instance(), nullptr);
    struct CacheLifetime
    {
        MemoryManager Memory = {};
        CacheLifetime()
        {
            Memory.Initialize(ZMega(512), {});
            AssetManager::Initialize(&Memory.MainArena, nullptr, "/project");
        }
        ~CacheLifetime()
        {
            AssetManager::Shutdown();
        }
    };
    struct CallbackCounts
    {
        uint32_t Reload  = 0;
        uint32_t Ready   = 0;
        uint32_t Stale   = 0;
        uint32_t Removed = 0;
    } retired = {}, current = {};
    const auto install = [](AssetRegistry& registry, CallbackCounts& counts) {
        registry.SetHotReloadCallback(&counts, [](void* context, std::span<const uuids::uuid>) { ++static_cast<CallbackCounts*>(context)->Reload; });
        registry.SetOnReadyCallback(&counts, [](void* context, const uuids::uuid&, ZEngine::Managers::AssetHandle) { ++static_cast<CallbackCounts*>(context)->Ready; });
        registry.SetOnStaleCallback(&counts, [](void* context, const uuids::uuid&) { ++static_cast<CallbackCounts*>(context)->Stale; });
        registry.SetOnRemovedCallback(&counts, [](void* context, const uuids::uuid&, AssetType) { ++static_cast<CallbackCounts*>(context)->Removed; });
    };
    {
        CacheLifetime first = {};
        install(*AssetManager::Instance()->Registry, retired);
    }
    EXPECT_EQ(AssetManager::Instance(), nullptr);
    CacheLifetime second   = {};
    auto&         registry = *AssetManager::Instance()->Registry;
    const auto    id       = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    const auto    path     = VFSPath::Parse("/Meshes/lifetime.zemesh").Value();
    MetaFileData  meta     = {};
    meta.AssetUUID         = id;
    const auto notify      = [&] {
        ASSERT_TRUE(registry.Register(id, AssetType::MESH, path, meta).IsOk());
        ASSERT_TRUE(registry.SetState(id, AssetState::Loaded));
        registry.MarkStale(id);
        registry.OnAssetDeleted(path);
        EXPECT_EQ(registry.RecordCount(), 0u);
    };
    notify();
    EXPECT_EQ(retired.Reload, 0u);
    EXPECT_EQ(retired.Ready, 0u);
    EXPECT_EQ(retired.Stale, 0u);
    EXPECT_EQ(retired.Removed, 0u);
    install(registry, current);
    notify();
    EXPECT_EQ(current.Reload, 3u); // Loaded, stale and removed each notify hot reload.
    EXPECT_EQ(current.Ready, 1u);
    EXPECT_EQ(current.Stale, 1u);
    EXPECT_EQ(current.Removed, 1u);
}

class MeshAssetLoaderTest : public testing::Test
{
protected:
    MemoryManager    Memory  = {};
    VFSMemoryBackend Backend = {};
    struct ReadVFS : VFSContext
    {
        uint32_t Reads             = 0;
        void*    BeforeReadContext = nullptr;
        void (*BeforeRead)(void*)  = nullptr;
        VFSResult<IVFSFile*> Open(const VFSPath& path, VFSOpenFlags flags) override
        {
            if (flags == VFSOpenFlags::Read && path.Extension().Equals(".zemesh"))
            {
                ++Reads;
                if (BeforeRead)
                    BeforeRead(BeforeReadContext);
            }
            return VFSContext::Open(path, flags);
        }
    } VFS                                         = {};
    AssetMesh             Mesh                    = {};
    AssetNodeHierarchy    Hierarchy               = {};
    uuids::uuid           ID                      = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    uuids::uuid           HierarchyID             = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
    VFSPath               Path                    = VFSPath::Parse("/Meshes/triangle.zemesh").Value();
    VFSPath               MaterialPath            = VFSPath::Parse("/Materials/triangle.zematerial").Value();
    uuids::uuid           MaterialID              = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    std::filesystem::path MaterialWorkspace       = {};
    std::string           MaterialWorkspaceNative = {};
    uint32_t              ReadyCalls              = 0;
    bool                  ReadySawCompleteData    = true;

    void                  SetUp() override
    {
        ASSERT_EQ(AssetManager::Instance(), nullptr);
        Memory.Initialize(ZMega(512), {});
        AssetManager::Initialize(&Memory.MainArena, nullptr, "/project");
        Backend.Initialize(&Memory.MainArena);
        VFS.Initialize(&Memory.MainArena);
        ASSERT_TRUE(VFS.Mount(&Backend, VFSPath::Root(), 0).Succeeded());
        Mesh.MeshUUID = ID;
        Mesh.Vertices.init(&Memory.MainArena, 24, 24);
        Mesh.Vertices[8]  = 2.f;
        Mesh.Vertices[17] = 3.f;
        Mesh.Indices.init(&Memory.MainArena, 3);
        Mesh.Indices.push(0);
        Mesh.Indices.push(1);
        Mesh.Indices.push(2);
        Mesh.SubMeshes.init(&Memory.MainArena, 1);
        Mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        Hierarchy.MeshUUID          = ID;
        Hierarchy.NodeHierarchyUUID = HierarchyID;
        Hierarchy.Hierarchies.init(&Memory.MainArena, 1);
        Hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
        Hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
        Hierarchy.Names.init(&Memory.MainArena, 1);
        Hierarchy.MaterialNames.init(&Memory.MainArena, 1);
        Hierarchy.NodeNames.init(&Memory.MainArena, 16);
        Hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
        Hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
        AddNode(Hierarchy, -1, 0);
        ZEngine::Core::Containers::String name = {}, material = {};
        name.init(&Memory.MainArena, "Triangle");
        material.init(&Memory.MainArena, "Material");
        Hierarchy.Names.push(name);
        Hierarchy.MaterialNames.push(material);
        Hierarchy.NodeNames.insert(0, 0);
        Hierarchy.NodeMeshes.insert(0, 0);
        Hierarchy.NodeMaterials.insert(0, 0);
        WriteMesh();
        MetaFileData meta = {};
        meta.AssetUUID    = ID;
        meta.SourceHash   = 42;
        ASSERT_TRUE(AssetManager::Instance()->Registry->Register(ID, AssetType::MESH, Path, meta).IsOk());
        AssetManager::Instance()->Registry->SetOnReadyCallback(this, [](void* context, const uuids::uuid& uuid, ZEngine::Managers::AssetHandle) {
            auto& test = *static_cast<MeshAssetLoaderTest*>(context);
            ++test.ReadyCalls;
            auto*    manager          = AssetManager::Instance();
            uint32_t count            = 0;
            test.ReadySawCompleteData = test.ReadySawCompleteData && manager->TryGetMeshDrawCount(test.ID, &count) && count == 1 && manager->GetMeshNodeHierarchy(test.ID) && (uuid == test.ID || uuid == test.HierarchyID);
        });
    }
    void TearDown() override
    {
        AssetManager::Shutdown();
        VFS.Shutdown();
        if (!MaterialWorkspace.empty())
        {
            std::error_code error = {};
            std::filesystem::remove_all(MaterialWorkspace, error);
        }
    }
    void WriteMesh()
    {
        AssetCodec::ImportConfiguration config = {};
        config.VFS                             = &VFS;
        config.OutputAssetsPath.init(&Memory.MainArena, "/Meshes");
        config.OutputAssetFile.init(&Memory.MainArena, "triangle.zemesh");
        ASSERT_TRUE(AssetCodec::SerializeMeshAssetFile(&Memory.MainArena, Mesh, Hierarchy, config).Succeeded());
    }
    void WriteMaterialDependency()
    {
        MaterialWorkspace       = std::filesystem::temp_directory_path() / "zengine_mesh_asset_loader_material";
        MaterialWorkspaceNative = MaterialWorkspace.string();
        std::error_code error   = {};
        ASSERT_TRUE(std::filesystem::create_directories(MaterialWorkspace / "Materials", error) || !error);
        std::ofstream material(MaterialWorkspace / "Materials" / "triangle.zematerial", std::ios::trunc);
        ASSERT_TRUE(material.is_open());
        material << R"({"uuid":"33333333-3333-4333-8333-333333333333","name":"triangle","albedo_color":[0.25,0.5,0.75,1.0],"ambient_color":[0.0,0.0,0.0,1.0],"emissive_color":[0.0,0.0,0.0,1.0],"roughness_color":[0.0,0.6,0.0,0.0],"specular_color":[0.0,0.0,0.0,0.0],"factors":[0.0,0.0,0.0,0.0],"textures":{}})";
        material.close();
        ASSERT_TRUE(material.good());
        auto* manager                    = AssetManager::Instance();
        manager->CurrentWorkingSpacePath = MaterialWorkspaceNative.c_str();
        MetaFileData material_meta       = {};
        material_meta.AssetUUID          = MaterialID;
        ASSERT_TRUE(manager->Registry->Register(MaterialID, AssetType::MATERIAL, MaterialPath, material_meta).IsOk());
        Mesh.SubMeshes[0].MaterialUUID = MaterialID;
        WriteMesh();
    }
    void ExpectUnpublished()
    {
        auto* manager = AssetManager::Instance();
        EXPECT_EQ(manager->Meshes.size(), 0u);
        EXPECT_EQ(manager->NodeHierarchies.size(), 0u);
        EXPECT_EQ(manager->MeshCacheAllocations.size(), 0u);
        EXPECT_EQ(manager->Registry->FindByUUID(HierarchyID), nullptr);
        EXPECT_EQ(manager->Registry->FindByUUID(ID)->State, AssetState::Registered);
        EXPECT_EQ(ReadyCalls, 0u);
    }
    static void RenameRecordBeforeRead(void* context)
    {
        auto& test = *static_cast<MeshAssetLoaderTest*>(context);
        AssetManager::Instance()->Registry->OnAssetRenamed(test.Path, VFSPath::Parse("/Meshes/renamed.zemesh").Value());
    }
    static void IngestBeforeRead(void* context)
    {
        auto& test = *static_cast<MeshAssetLoaderTest*>(context);
        AssetManager::IngestMesh(std::move(test.Mesh), std::move(test.Hierarchy));
    }
    static void RecycleRecordBeforeRead(void* context)
    {
        auto& test     = *static_cast<MeshAssetLoaderTest*>(context);
        auto* registry = AssetManager::Instance()->Registry;
        registry->OnAssetDeleted(test.Path);
        EXPECT_TRUE(registry->Register(test.ID, AssetType::MESH, test.Path, {}).IsOk());
    }
};

TEST_F(MeshAssetLoaderTest, LoadsCompleteMeshAndHierarchyWithOwnedNamesBoundsAndReadyNotifications)
{
    auto*      manager     = AssetManager::Instance();
    const auto root_cursor = Memory.MainArena.m_current_offset;
    const auto loaded      = manager->LoadMeshAsset(VFS, ID);
    ASSERT_TRUE(loaded.Succeeded());
    EXPECT_EQ(loaded.Value(), 1u);
    EXPECT_EQ(Memory.MainArena.m_current_offset, root_cursor);
    EXPECT_EQ(VFS.Reads, 1u);
    EXPECT_EQ(ReadyCalls, 2u);
    EXPECT_TRUE(ReadySawCompleteData);
    ASSERT_EQ(manager->MeshCacheAllocations.size(), 1u);
    const auto* mesh = manager->GetMeshAsset(ID);
    ASSERT_NE(mesh, nullptr);
    EXPECT_NE(mesh->Vertices.data(), Mesh.Vertices.data());
    EXPECT_FLOAT_EQ(mesh->Vertices[8], 2.f);
    EXPECT_TRUE(mesh->BoundsValid);
    EXPECT_FLOAT_EQ(mesh->BoundsMin.x, 0.f);
    EXPECT_FLOAT_EQ(mesh->BoundsMax.x, 2.f);
    EXPECT_FLOAT_EQ(mesh->BoundsMax.y, 3.f);
    EXPECT_GE(mesh->BoundsRadius, std::sqrt(3.25f));
    auto* hierarchy = manager->GetMeshNodeHierarchy(ID);
    ASSERT_NE(hierarchy, nullptr);
    EXPECT_STREQ(hierarchy->Names[0].c_str(), "Triangle");
    EXPECT_STREQ(hierarchy->MaterialNames[0].c_str(), "Material");
    EXPECT_NE(hierarchy->Names[0].c_str(), Hierarchy.Names[0].c_str());
    ASSERT_NE(hierarchy->NodeMeshes.find(0), nullptr);
    EXPECT_EQ(*hierarchy->NodeMeshes.find(0), 0u);
    EXPECT_EQ(manager->Registry->FindByUUID(ID)->Path, Path);
}

TEST_F(MeshAssetLoaderTest, LoadsReferencedMaterialBeforePublishingMeshDraws)
{
    WriteMaterialDependency();
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    const uint32_t* material_slot = manager->UUIDToMaterialSlot.find(MaterialID);
    ASSERT_NE(material_slot, nullptr);
    ASSERT_LT(*material_slot, manager->GPUMeshMaterials.size());
    const auto& material = manager->GPUMeshMaterials[*material_slot];
    EXPECT_FLOAT_EQ(material.AlbedoColor.x, 0.25f);
    EXPECT_FLOAT_EQ(material.AlbedoColor.y, 0.5f);
    EXPECT_FLOAT_EQ(material.AlbedoColor.z, 0.75f);
}

TEST_F(MeshAssetLoaderTest, CheckedMaterialReadRefusesOversizedMalformedAndExhaustedScratchWithoutChangingOutput)
{
    WriteMaterialDependency();
    const auto     file  = (MaterialWorkspace / "Materials" / "triangle.zematerial").string();
    ArenaAllocator small = {};
    small.Initialize(128, 0, "MaterialReadRefusal");
    AssetMaterial output = {};
    output.MaterialUUID  = ID;
    const auto before    = small.m_current_offset;
    EXPECT_EQ(AssetCodec::TryDeserializeMaterialAssetFile(&small, file.c_str(), output, MaterialID).Error(), VFSError::OutOfMemory);
    EXPECT_EQ(small.m_current_offset, before);
    EXPECT_EQ(output.MaterialUUID, ID);
    {
        std::ofstream stream(file, std::ios::binary | std::ios::trunc);
        stream << std::string(ZMega(1) + 1, ' ');
    }
    EXPECT_EQ(AssetCodec::TryDeserializeMaterialAssetFile(&Memory.MainArena, file.c_str(), output, MaterialID).Error(), VFSError::SizeLimitExceeded);
    EXPECT_EQ(output.MaterialUUID, ID);
    {
        std::ofstream stream(file, std::ios::trunc);
        stream << R"({"uuid":"33333333-3333-4333-8333-333333333333","metadata":[true,,false]})";
    }
    const auto cursor = Memory.MainArena.m_current_offset;
    EXPECT_EQ(AssetCodec::TryDeserializeMaterialAssetFile(&Memory.MainArena, file.c_str(), output, MaterialID).Error(), VFSError::Corrupted);
    EXPECT_EQ(Memory.MainArena.m_current_offset, cursor);
    EXPECT_EQ(output.MaterialUUID, ID);
}

TEST_F(MeshAssetLoaderTest, CheckedMaterialIngestionRetainsDeepCopiesAndRepeatedDependencyReadsDoNotGrowEitherAllocator)
{
    WriteMaterialDependency();
    auto* manager = AssetManager::Instance();
    {
        ArenaScratch decode(&Memory.MainArena);
        ASSERT_TRUE(AssetManager::IngestMaterialFromUUID(decode.Arena(), MaterialID));
    }
    const auto  cursor   = Memory.MainArena.m_current_offset;
    const auto  baseline = manager->ContainerSlab.GetStatistics();
    const auto* slot     = manager->UUIDToMaterialSlot.find(MaterialID);
    ASSERT_NE(slot, nullptr);
    ASSERT_EQ(manager->MaterialCacheAllocations.size(), manager->Materials.size());
    ASSERT_NE(manager->MaterialCacheAllocations[*slot], nullptr);
    EXPECT_STREQ(manager->Materials[*slot].Name.c_str(), "triangle");
    EXPECT_EQ(manager->Materials[*slot].AlphaMode, ZEngine::Rendering::Materials::AlphaMode::Legacy);
    for (uint32_t cycle = 0; cycle < 1100; ++cycle)
    {
        ArenaScratch decode(&Memory.MainArena);
        ASSERT_TRUE(AssetManager::IngestMaterialFromUUID(decode.Arena(), MaterialID));
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, cursor);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBytes, baseline.LiveBytes);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, baseline.LiveBlocks);
    EXPECT_STREQ(manager->Materials[*slot].Name.c_str(), "triangle");
}

TEST_F(MeshAssetLoaderTest, RequiredTextureRefusalDoesNotPublishPartialMaterialOrMeshAndValidRetrySucceeds)
{
    WriteMaterialDependency();
    {
        std::ofstream stream(MaterialWorkspace / "Materials" / "triangle.zematerial", std::ios::trunc);
        stream << R"({"uuid":"33333333-3333-4333-8333-333333333333","textures":{"albedo":{"uuid":"44444444-4444-4444-8444-444444444444","path":"/Textures/required.png"}}})";
    }
    auto*      manager = AssetManager::Instance();
    const auto live    = manager->ContainerSlab.GetStatistics();
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Failed());
    EXPECT_EQ(manager->UUIDToMaterialSlot.find(MaterialID), nullptr);
    EXPECT_EQ(manager->Materials.size(), 1u);
    EXPECT_EQ(manager->GPUMeshMaterials.size(), 1u);
    EXPECT_EQ(manager->MaterialCacheAllocations.size(), 1u);
    EXPECT_EQ(manager->Textures.size(), 0u);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBytes, live.LiveBytes);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, live.LiveBlocks);
    AssetRecordSnapshot material = {};
    ASSERT_TRUE(manager->Registry->TryReadRecord(MaterialID, &material));
    EXPECT_FALSE(material.Record.HasLoadedSlot);
    ExpectUnpublished();
    {
        std::ofstream stream(MaterialWorkspace / "Materials" / "triangle.zematerial", std::ios::trunc);
        stream << R"({"uuid":"33333333-3333-4333-8333-333333333333","name":"retry","albedo_color":[1,1,1,1]})";
    }
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_NE(manager->UUIDToMaterialSlot.find(MaterialID), nullptr);
    EXPECT_EQ(manager->Meshes.size(), 1u);
}

TEST_F(MeshAssetLoaderTest, RequiredLoadingRechecksCachedCompatibilityMaterialsAndRecoversAfterTexturePromotion)
{
    WriteMaterialDependency();
    auto*      manager             = AssetManager::Instance();
    const auto texture             = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    manager->FallbackTextureHandle = {.Index = 5, .Generation = 1};
    AssetMaterial material         = {};
    material.MaterialUUID          = MaterialID;
    material.AlbedoTexUUID         = texture;
    material.AlbedoTexPath.init(&Memory.MainArena, "/Textures/required.png");
    ASSERT_TRUE(AssetManager::IngestMaterial(std::move(material)));
    const auto slot = *manager->UUIDToMaterialSlot.find(MaterialID);
    ASSERT_EQ(manager->GPUMeshMaterials[slot].AlbedoMap, 5u);
    const auto baseline = manager->ContainerSlab.GetStatistics();
    EXPECT_FALSE(AssetManager::IngestMaterialFromUUID(&Memory.MainArena, MaterialID));
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Failed());
    EXPECT_EQ(manager->Meshes.size(), 0u);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBytes, baseline.LiveBytes);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, baseline.LiveBlocks);
    EXPECT_EQ(AssetManager::FindTextureHandle(texture), manager->FallbackTextureHandle);

    // A mesh populated by a compatibility consumer cannot bypass the same gate.
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Failed());
    EXPECT_EQ(manager->MeshCacheAllocations[0]->References.load(), 1u);
    EXPECT_EQ(manager->MaterialCacheAllocations[slot]->References.load(), 1u);

    const ZEngine::Rendering::Textures::TextureHandle real            = {.Index = 7, .Generation = 2};
    bool                                              retire_previous = true;
    ASSERT_TRUE(AssetManager::TryReplaceTexture(texture, manager->FallbackTextureHandle, real, &retire_previous));
    EXPECT_FALSE(retire_previous);
    EXPECT_EQ(manager->GPUMeshMaterials[slot].AlbedoMap, 7u);
    ASSERT_TRUE(AssetManager::IngestMaterialFromUUID(&Memory.MainArena, MaterialID));
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->MeshCacheAllocations[0]->References.load(), 2u);
    EXPECT_EQ(manager->Materials.size(), 2u);
    lease.Reset();
}

TEST_F(MeshAssetLoaderTest, TextureReleasesAreDeduplicatedAndCannotDropThe257thDistinctAdmittedKey)
{
    auto*                        manager = AssetManager::Instance();
    std::array<uuids::uuid, 300> ids     = {};
    for (uint32_t index = 0; index < ids.size(); ++index)
    {
        std::array<uint8_t, 16> bytes                      = {};
        bytes[0]                                           = uint8_t(index);
        bytes[1]                                           = uint8_t(index >> 8);
        bytes[15]                                          = 1;
        ids[index]                                         = uuids::uuid(bytes.begin(), bytes.end());
        ZEngine::Rendering::Textures::TextureHandle handle = {};
        handle.Index                                       = index;
        handle.Generation                                  = 1;
        manager->UUIDToTextureHandle.insert(ids[index], handle);
    }
    for (const auto& id : ids)
    {
        AssetManager::ReleaseTexture(id);
        AssetManager::ReleaseTexture(id);
    }
    EXPECT_EQ(manager->PendingTextureReleaseCount, ids.size());
    AssetManager::FlushTextureReleases();
    EXPECT_EQ(manager->PendingTextureReleaseCount, 0u);
    for (const auto& id : ids)
    {
        EXPECT_FALSE(AssetManager::FindTextureHandle(id).Valid());
    }
}

TEST_F(MeshAssetLoaderTest, MaterialSlabRefusalPreservesTablesAndChargesThenRetriesWithoutArenaRetention)
{
    auto*         manager  = AssetManager::Instance();
    AssetMaterial material = {};
    material.MaterialUUID  = MaterialID;
    material.Name.init(&Memory.MainArena, "immutable material");
    const auto cursor = Memory.MainArena.m_current_offset;
    {
        struct SlabFill
        {
            TLSFSlab*          Slab   = nullptr;
            std::vector<void*> Blocks = {};
            ~SlabFill()
            {
                for (auto* block : Blocks)
                {
                    Slab->Free(block);
                }
            }
        } fill = {&manager->ContainerSlab};
        for (const size_t bytes : {size_t(ZMega(1)), size_t(ZKilo(1)), size_t(1)})
        {
            while (auto* block = fill.Slab->TryRealloc(nullptr, bytes))
            {
                fill.Blocks.push_back(block);
            }
        }
        const auto full = manager->ContainerSlab.GetStatistics();
        EXPECT_FALSE(AssetManager::IngestMaterial(std::move(material)));
        EXPECT_EQ(manager->Materials.size(), 1u);
        EXPECT_EQ(manager->GPUMeshMaterials.size(), 1u);
        EXPECT_EQ(manager->MaterialCacheAllocations.size(), 1u);
        EXPECT_EQ(manager->UUIDToMaterialSlot.find(MaterialID), nullptr);
        EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBytes, full.LiveBytes);
        EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, full.LiveBlocks);
    }
    ASSERT_TRUE(AssetManager::IngestMaterial(std::move(material)));
    ASSERT_EQ(manager->Materials.size(), 2u);
    EXPECT_NE(manager->Materials[1].Name.c_str(), material.Name.c_str());
    material.Name[0] = 'X';
    EXPECT_STREQ(manager->Materials[1].Name.c_str(), "immutable material");
    EXPECT_EQ(Memory.MainArena.m_current_offset, cursor);
}

TEST_F(MeshAssetLoaderTest, RepeatedPlacementReusesTheBindingWithoutReadingAllocatingOrNotifyingAgain)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    const auto* vertices = manager->Meshes[0].Vertices.data();
    const auto  cursor   = Memory.MainArena.m_current_offset;
    for (uint32_t i = 0; i < 1000; ++i)
        ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_EQ(VFS.Reads, 1u);
    EXPECT_EQ(ReadyCalls, 2u);
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
    EXPECT_EQ(manager->Meshes[0].Vertices.data(), vertices);
    EXPECT_EQ(Memory.MainArena.m_current_offset, cursor);
}

TEST_F(MeshAssetLoaderTest, DeletedMeshSlotZeroIsNotResidentAfterScannerReregistration)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    manager->Registry->OnAssetDeleted(Path);
    EXPECT_EQ(manager->GetMeshAsset(ID), nullptr);
    EXPECT_EQ(manager->GetMeshNodeHierarchy(ID), nullptr);
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    // Import completion state alone must not restore the preceding CPU payload.
    ASSERT_TRUE(manager->Registry->SetState(ID, AssetState::Loaded));
    uint32_t draws = 73;
    EXPECT_FALSE(manager->TryGetMeshDrawCount(ID, &draws));
    EXPECT_EQ(draws, 73u);
    EXPECT_EQ(manager->GetMeshAsset(ID), nullptr);
    Mesh.Vertices[8] = 19.f;
    WriteMesh();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(ID)->Vertices[8], 19.f);
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
}

TEST_F(MeshAssetLoaderTest, DirectIngestRestoresDeletedNonzeroSlotWithoutAliasingAnotherMesh)
{
    auto*      manager          = AssetManager::Instance();
    const auto original         = ID;
    const auto other            = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    Mesh.MeshUUID               = other;
    Hierarchy.MeshUUID          = other;
    Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    Mesh.MeshUUID               = original;
    Hierarchy.MeshUUID          = original;
    Hierarchy.NodeHierarchyUUID = HierarchyID;
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    const auto handle = manager->Registry->FindByUUID(ID)->SlotHandle;
    ASSERT_EQ(AssetManager::ReadAssetHandleIndex(handle), 1u);
    manager->Registry->OnAssetDeleted(Path);
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    EXPECT_EQ(manager->GetMeshAsset(ID), nullptr);
    Mesh.Vertices[8]            = 23.f;
    Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("66666666-6666-4666-8666-666666666666");
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    EXPECT_EQ(manager->Registry->FindByUUID(ID)->SlotHandle, handle);
    EXPECT_EQ(manager->GetMeshNodeHierarchy(ID)->NodeHierarchyUUID, HierarchyID);
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(ID)->Vertices[8], 23.f);
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(other)->Vertices[8], 2.f);
    EXPECT_EQ(manager->Meshes.size(), 2u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 2u);
    EXPECT_EQ(manager->Registry->FindByUUID(ID)->State, AssetState::Loaded);
}

TEST_F(MeshAssetLoaderTest, RepeatedDeleteReimportReusesSlotsAndReplacesCheckedLoadStorage)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    const auto mesh_handle      = manager->Registry->FindByUUID(ID)->SlotHandle;
    const auto hierarchy_handle = manager->Registry->FindByUUID(HierarchyID)->SlotHandle;
    uint32_t   stale_calls      = 0;
    manager->Registry->SetOnStaleCallback(&stale_calls, [](void* context, const uuids::uuid&) { ++*static_cast<uint32_t*>(context); });
    for (uint32_t i = 0; i < 24; ++i)
    {
        manager->Registry->OnAssetDeleted(Path);
        ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
        Mesh.Vertices[8] = float(i + 4);
        WriteMesh();
        ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
        ASSERT_NE(manager->GetMeshAsset(ID), nullptr);
        EXPECT_FLOAT_EQ(manager->GetMeshAsset(ID)->Vertices[8], float(i + 4));
        EXPECT_EQ(manager->Registry->FindByUUID(ID)->SlotHandle, mesh_handle);
        EXPECT_EQ(manager->Registry->FindByUUID(HierarchyID)->SlotHandle, hierarchy_handle);
        EXPECT_EQ(manager->Registry->FindByUUID(ID)->State, AssetState::Loaded);
        EXPECT_EQ(manager->Meshes.size(), 1u);
        EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
        ASSERT_EQ(manager->MeshCacheAllocations.size(), 1u);
        EXPECT_NE(manager->MeshCacheAllocations[0], nullptr);
    }
    EXPECT_EQ(stale_calls, 48u); // Every replacement invalidates the old GPU payload.
    EXPECT_TRUE(ReadySawCompleteData);
}

TEST_F(MeshAssetLoaderTest, DirectReplacementAndRejectedSnapshotsReclaimTheirPayloadWithoutGrowingTheLifetimeArena)
{
    auto* manager = AssetManager::Instance();
    Mesh.Vertices.reserve(ZMega(1) / sizeof(float));
    Mesh.Vertices.m_size = Mesh.Vertices.capacity();
    std::fill(Mesh.Vertices.begin(), Mesh.Vertices.end(), 0.f);
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    AssetRecordSnapshot obsolete = {};
    ASSERT_TRUE(manager->Registry->TryReadRecord(ID, &obsolete));
    manager->Registry->OnAssetDeleted(Path);
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    const auto cursor = Memory.MainArena.m_current_offset;
    // Each transaction stages >1 MiB. Leaking successes OR rejected snapshots
    // would exhaust the 256 MiB slab long before this loop finishes.
    for (uint32_t i = 0; i < 320; ++i)
    {
        Mesh.Vertices[8] = float(i + 1);
        ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
        auto* payload = manager->MeshCacheAllocations[0];
        EXPECT_FALSE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy), &obsolete));
        EXPECT_EQ(manager->MeshCacheAllocations[0], payload);
        EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], float(i + 1));
    }
    EXPECT_EQ(Memory.MainArena.m_current_offset, cursor);
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
    EXPECT_EQ(manager->MeshBindings.size(), 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
    auto* spare = manager->ContainerSlab.TryRealloc(nullptr, ZMega(128));
    EXPECT_NE(spare, nullptr);
    manager->ContainerSlab.Free(spare);
}

TEST_F(MeshAssetLoaderTest, SharedCloneCompactsSparseMapsAndDeepCopiesNamesIncludingEmptyStrings)
{
    Hierarchy.NodeNames.init(&Memory.MainArena, 4096);
    Hierarchy.NodeNames.insert(0, 0);
    Hierarchy.NodeMaterials.clear();
    Hierarchy.Names.push({}); // Default String has no backing NUL to copy.
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    auto* retained = manager->GetMeshNodeHierarchy(ID);
    ASSERT_NE(retained, nullptr);
    EXPECT_EQ(retained->NodeNames.capacity(), 16u);
    EXPECT_EQ(retained->NodeMaterials.capacity(), 0u);
    ASSERT_NE(retained->NodeNames.find(0), nullptr);
    EXPECT_EQ(*retained->NodeNames.find(0), 0u);
    ASSERT_EQ(retained->Names.size(), 2u);
    EXPECT_STREQ(retained->Names[0].c_str(), "Triangle");
    EXPECT_STREQ(retained->Names[1].c_str(), "");
    EXPECT_NE(retained->Names[0].c_str(), Hierarchy.Names[0].c_str());
    Hierarchy.Names[0][0] = 'X';
    EXPECT_STREQ(retained->Names[0].c_str(), "Triangle");
    EXPECT_LT(manager->MeshCacheAllocations[0]->Arena.m_total_size, ZKilo(4));
}

TEST_F(MeshAssetLoaderTest, ImportBatchGrowsStorageGeometricallyAndKeepsEachPayloadAtItsMeshSlot)
{
    auto* manager = AssetManager::Instance();
    for (uint32_t i = 0; i < 200; ++i)
    {
        std::array<uint8_t, 16> bytes = {};
        bytes[0]                      = 1;
        bytes[15]                     = uint8_t(i);
        Mesh.MeshUUID                 = uuids::uuid(bytes);
        Hierarchy.MeshUUID            = Mesh.MeshUUID;
        bytes[0]                      = 2;
        Hierarchy.NodeHierarchyUUID   = uuids::uuid(bytes);
        Mesh.Vertices[8]              = float(i + 1);
        ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    }
    EXPECT_EQ(manager->MeshCacheAllocations.capacity(), 256u);
    ASSERT_EQ(manager->MeshCacheAllocations.size(), 200u);
    ASSERT_EQ(manager->Meshes.size(), 200u);
    for (size_t i = 0; i < manager->Meshes.size(); ++i)
    {
        auto*       payload  = manager->MeshCacheAllocations[i];
        const auto* vertices = reinterpret_cast<const uint8_t*>(manager->Meshes[i].Vertices.data());
        EXPECT_GE(vertices, payload->Arena.m_memory);
        EXPECT_LT(vertices, payload->Arena.m_memory + payload->Arena.m_total_size);
        EXPECT_FLOAT_EQ(manager->Meshes[i].Vertices[8], float(i + 1));
        EXPECT_EQ(manager->MeshBindings.find(manager->Meshes[i].MeshUUID)->MeshSlot, i);
    }
}

TEST_F(MeshAssetLoaderTest, ReimportWithNewIdentityAtSamePathDoesNotOverwriteRetainedActorsMesh)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    manager->Registry->OnAssetDeleted(Path);
    const auto replacement      = *uuids::uuid::from_string("44444444-4444-4444-8444-444444444444");
    Mesh.MeshUUID               = replacement;
    Hierarchy.MeshUUID          = replacement;
    Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    Mesh.Vertices[8]            = 31.f;
    WriteMesh();
    ASSERT_TRUE(manager->Registry->Register(replacement, AssetType::MESH, Path, {}).IsOk());
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, replacement).Succeeded());
    EXPECT_EQ(manager->GetMeshAsset(ID), nullptr);
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 2.f);
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(replacement)->Vertices[8], 31.f);
    EXPECT_EQ(manager->Meshes.size(), 2u);
}

TEST_F(MeshAssetLoaderTest, RecycledRecordDuringDecodeCannotRestoreAnObsoletePayload)
{
    VFS.BeforeReadContext = this;
    VFS.BeforeRead        = &RecycleRecordBeforeRead;
    EXPECT_EQ(AssetManager::Instance()->LoadMeshAsset(VFS, ID).Error(), VFSError::Cancelled);
    ExpectUnpublished();
    VFS.BeforeRead = nullptr;
    ASSERT_TRUE(AssetManager::Instance()->LoadMeshAsset(VFS, ID).Succeeded());
}

TEST_F(MeshAssetLoaderTest, DirectIngestRejectsPreReadSnapshotAfterDeleteAndReregistration)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    AssetRecordSnapshot before = {};
    ASSERT_TRUE(manager->Registry->TryReadRecord(ID, &before));
    manager->Registry->OnAssetDeleted(Path);
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    Mesh.Vertices[8] = 99.f;
    EXPECT_FALSE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy), &before));
    EXPECT_FALSE(manager->Registry->FindByUUID(ID)->HasLoadedSlot);
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 2.f);
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
}

TEST_F(MeshAssetLoaderTest, RestoresBothDeletedRecordsUsingTheRetainedHierarchyIdentity)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    manager->Registry->OnAssetDeleted(Path);
    ASSERT_TRUE(manager->Registry->Remove(HierarchyID));
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("66666666-6666-4666-8666-666666666666");
    Mesh.Vertices[8]            = 15.f;
    WriteMesh();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    ASSERT_NE(manager->GetMeshNodeHierarchy(ID), nullptr);
    EXPECT_EQ(manager->GetMeshNodeHierarchy(ID)->NodeHierarchyUUID, HierarchyID);
    EXPECT_NE(manager->Registry->FindByUUID(HierarchyID), nullptr);
    EXPECT_EQ(manager->Registry->FindByUUID(Hierarchy.NodeHierarchyUUID), nullptr);
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(ID)->Vertices[8], 15.f);
    EXPECT_EQ(manager->Registry->RecordCount(), 2u);
}

TEST_F(MeshAssetLoaderTest, ConflictingRetainedHierarchyRejectsReplacementWithoutChangingCpuData)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    manager->Registry->OnAssetDeleted(Path);
    ASSERT_TRUE(manager->Registry->Register(ID, AssetType::MESH, Path, {}).IsOk());
    ASSERT_TRUE(manager->Registry->Remove(HierarchyID));
    ASSERT_TRUE(manager->Registry->Register(HierarchyID, AssetType::MATERIAL, {}, {}).IsOk());
    Mesh.Vertices[8] = 99.f;
    EXPECT_FALSE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 2.f);
    EXPECT_FALSE(manager->Registry->FindByUUID(ID)->HasLoadedSlot);
    EXPECT_EQ(manager->Registry->FindByUUID(HierarchyID)->Type, AssetType::MATERIAL);
    WriteMesh();
    EXPECT_EQ(manager->LoadMeshAsset(VFS, ID).Error(), VFSError::Cancelled);
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 2.f);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
}

TEST_F(MeshAssetLoaderTest, CorruptOrSubstitutedIdentityNeverPublishesPartialCacheData)
{
    Mesh.MeshUUID = HierarchyID;
    WriteMesh();
    EXPECT_EQ(AssetManager::Instance()->LoadMeshAsset(VFS, ID).Error(), VFSError::Corrupted);
    ExpectUnpublished();
    Mesh.MeshUUID   = ID;
    Mesh.Indices[0] = UINT32_MAX;
    WriteMesh();
    EXPECT_EQ(AssetManager::Instance()->LoadMeshAsset(VFS, ID).Error(), VFSError::Corrupted);
    ExpectUnpublished();
}

TEST_F(MeshAssetLoaderTest, CancellationAndDecodeBudgetFailureLeaveTheCacheUntouched)
{
    auto*             manager   = AssetManager::Instance();
    std::atomic<bool> cancelled = true;
    EXPECT_EQ(manager->LoadMeshAsset(VFS, ID, {}, &cancelled).Error(), VFSError::Cancelled);
    EXPECT_EQ(VFS.Reads, 0u);
    AssetCodec::MeshAssetReadLimits limits = {};
    limits.MaxDecodedBytes                 = 32;
    EXPECT_NE(manager->LoadMeshAsset(VFS, ID, limits).Error(), VFSError::OK);
    ExpectUnpublished();
    cancelled.store(false);
    VFS.BeforeReadContext = &cancelled;
    VFS.BeforeRead        = [](void* context) { static_cast<std::atomic<bool>*>(context)->store(true); };
    EXPECT_EQ(manager->LoadMeshAsset(VFS, ID, {}, &cancelled).Error(), VFSError::Cancelled);
    ExpectUnpublished();
}

TEST_F(MeshAssetLoaderTest, RenamedRecordDuringReadRejectsTheOriginalSnapshotAndCanBeRetried)
{
    VFS.BeforeReadContext = this;
    VFS.BeforeRead        = &RenameRecordBeforeRead;
    auto* manager         = AssetManager::Instance();
    EXPECT_EQ(manager->LoadMeshAsset(VFS, ID).Error(), VFSError::Cancelled);
    ExpectUnpublished();
    VFS.BeforeRead = nullptr;
    manager->Registry->OnAssetRenamed(VFSPath::Parse("/Meshes/renamed.zemesh").Value(), Path);
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
}

TEST_F(MeshAssetLoaderTest, AConcurrentCompleteBindingWinsInsteadOfBeingHotReloadedByTheLoader)
{
    VFS.BeforeReadContext = this;
    VFS.BeforeRead        = &IngestBeforeRead;
    auto* manager         = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
    EXPECT_EQ(ReadyCalls, 2u);
    EXPECT_TRUE(ReadySawCompleteData);
}

TEST_F(MeshAssetLoaderTest, SlabExhaustionIsRecoverableAndDoesNotPublishAPartialBinding)
{
    auto*  manager     = AssetManager::Instance();
    void*  blocks[512] = {};
    size_t count       = 0;
    while (count < 512)
    {
        auto* block = manager->ContainerSlab.TryRealloc(nullptr, ZMega(1));
        if (!block)
            break;
        blocks[count++] = block;
    }
    void*  tail[1024] = {};
    size_t tail_count = 0;
    while (tail_count < 1024)
    {
        auto* block = manager->ContainerSlab.TryRealloc(nullptr, 1024);
        if (!block)
            break;
        tail[tail_count++] = block;
    }
    const auto result = manager->LoadMeshAsset(VFS, ID);
    for (size_t i = 0; i < count; ++i)
        manager->ContainerSlab.Free(blocks[i]);
    for (size_t i = 0; i < tail_count; ++i)
        manager->ContainerSlab.Free(tail[i]);
    EXPECT_EQ(result.Error(), VFSError::OutOfMemory);
    ExpectUnpublished();
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
}

TEST_F(MeshAssetLoaderTest, ExhaustedSlabRejectsDirectReplacementAndPreservesTheExistingPayload)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    auto*      payload     = manager->MeshCacheAllocations[0];
    auto*      vertices    = manager->Meshes[0].Vertices.data();
    const auto ready_calls = ReadyCalls;
    void*      blocks[512] = {};
    size_t     count       = 0;
    while (count < 512)
    {
        auto* block = manager->ContainerSlab.TryRealloc(nullptr, ZMega(1));
        if (!block)
            break;
        blocks[count++] = block;
    }
    void*  tail[2048] = {};
    size_t tail_count = 0;
    while (tail_count < 2048)
    {
        auto* block = manager->ContainerSlab.TryRealloc(nullptr, 512);
        if (!block)
            break;
        tail[tail_count++] = block;
    }
    Mesh.Vertices[8]    = 41.f;
    const bool replaced = AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy));
    for (size_t i = 0; i < count; ++i)
        manager->ContainerSlab.Free(blocks[i]);
    for (size_t i = 0; i < tail_count; ++i)
        manager->ContainerSlab.Free(tail[i]);
    EXPECT_FALSE(replaced);
    EXPECT_EQ(manager->MeshCacheAllocations[0], payload);
    EXPECT_EQ(manager->Meshes[0].Vertices.data(), vertices);
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 2.f);
    EXPECT_EQ(ReadyCalls, ready_calls);
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    EXPECT_FLOAT_EQ(manager->Meshes[0].Vertices[8], 41.f);
}

TEST_F(MeshAssetLoaderTest, ConcurrentLoadsCannotMultiplyDecodeMemoryOrWaitOnAnExistingLoad)
{
    struct Gate
    {
        std::atomic<bool> Started = false;
        std::atomic<bool> Release = false;
    } gate                = {};
    VFS.BeforeReadContext = &gate;
    VFS.BeforeRead        = [](void* context) {
        auto& value = *static_cast<Gate*>(context);
        value.Started.store(true, std::memory_order_release);
        while (!value.Release.load(std::memory_order_acquire))
            std::this_thread::yield();
    };
    auto*               manager = AssetManager::Instance();
    VFSResult<uint32_t> result  = {};
    std::thread         worker([&] { result = manager->LoadMeshAsset(VFS, ID); });
    const auto          deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!gate.Started.load(std::memory_order_acquire) && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    const bool started = gate.Started.load(std::memory_order_acquire);
    EXPECT_TRUE(started);
    if (started)
    {
        EXPECT_EQ(manager->LoadMeshAsset(VFS, ID).Error(), VFSError::AlreadyExists);
        EXPECT_EQ(manager->TrimCpuCache(), 0u); // No reclamation inside a live decode.
    }
    gate.Release.store(true, std::memory_order_release);
    worker.join();
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(VFS.Reads, 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
}

TEST_F(MeshAssetLoaderTest, UninitializedCpuLeaseRefusesBeforeReadOrPublication)
{
    ZEngine::Managers::AssetCacheLease lease = {};
    EXPECT_EQ(AssetManager::Instance()->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Error(), VFSError::OutOfMemory);
    EXPECT_EQ(VFS.Reads, 0u);
    ExpectUnpublished();
}

TEST_F(MeshAssetLoaderTest, CpuLeaseAdmissionIsBoundedAndDeduplicationDoesNotGrowItsOwner)
{
    ArenaAllocator tiny = {};
    tiny.Initialize(ZKilo(4), 0, "CpuLease/Refusal");
    ZEngine::Managers::AssetCacheLease refused = {};
    EXPECT_FALSE(refused.Initialize(&tiny));
    EXPECT_EQ(tiny.m_current_offset, 0u);

    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    auto*      manager = AssetManager::Instance();
    const auto offset  = Memory.MainArena.m_current_offset;
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    auto* allocation = manager->MeshCacheAllocations[0];
    ASSERT_NE(allocation, nullptr);
    const auto stable_offset = Memory.MainArena.m_current_offset;
    for (uint32_t iteration = 0; iteration < 1100; ++iteration)
    {
        ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    }
    EXPECT_EQ(allocation->References.load(), 2u);
    EXPECT_EQ(manager->CpuAssetOwnerPins[0].load(), 1u);
    // The registry creates the hierarchy once; repeated reads allocate nothing.
    EXPECT_GE(stable_offset, offset);
    EXPECT_EQ(manager->TrimCpuCache(), 0u);
    EXPECT_EQ(Memory.MainArena.m_current_offset, stable_offset);
    lease.Reset();
    EXPECT_EQ(allocation->References.load(), 1u);
    EXPECT_EQ(manager->CpuAssetOwnerPins[0].load(), 0u);
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
}

TEST_F(MeshAssetLoaderTest, CpuEvictionReclaimsPayloadAndReloadsStableMeshAndHierarchySlots)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    const auto mesh_binding   = *manager->MeshBindings.find(ID);
    const auto hierarchy_uuid = manager->NodeHierarchies[mesh_binding.HierarchySlot].NodeHierarchyUUID;
    const auto live           = manager->ContainerSlab.GetStatistics();
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
    const auto evicted = manager->ContainerSlab.GetStatistics();
    EXPECT_LT(evicted.LiveBytes, live.LiveBytes);
    EXPECT_EQ(evicted.LiveBlocks + 1, live.LiveBlocks);
    EXPECT_EQ(manager->GetMeshAsset(ID), nullptr);
    EXPECT_EQ(manager->GetMeshNodeHierarchy(ID), nullptr);
    uint32_t draws = 91;
    EXPECT_FALSE(manager->TryGetMeshDrawCount(ID, &draws));
    EXPECT_EQ(draws, 91u);
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_EQ(manager->Meshes.size(), 1u);
    EXPECT_EQ(manager->NodeHierarchies.size(), 1u);
    EXPECT_EQ(manager->MeshBindings.find(ID)->MeshSlot, mesh_binding.MeshSlot);
    EXPECT_EQ(manager->MeshBindings.find(ID)->HierarchySlot, mesh_binding.HierarchySlot);
    EXPECT_EQ(manager->GetMeshNodeHierarchy(ID)->NodeHierarchyUUID, hierarchy_uuid);
    EXPECT_EQ(VFS.Reads, 2u);
    EXPECT_TRUE(ReadySawCompleteData);
}

TEST_F(MeshAssetLoaderTest, CpuSourceLeaseProtectsBothImmutableOldAndCurrentReimportVersions)
{
    auto*                              manager = AssetManager::Instance();
    ZEngine::Managers::AssetCacheLease lease   = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    auto*       previous     = manager->MeshCacheAllocations[0];
    const auto* old_vertices = manager->Meshes[0].Vertices.data();
    const auto  before       = manager->ContainerSlab.GetStatistics();
    Mesh.Vertices[8]         = 31.f;
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    ASSERT_NE(manager->MeshCacheAllocations[0], previous);
    EXPECT_EQ(previous->References.load(), 1u); // Lease, not cache, owns old bytes.
    EXPECT_FLOAT_EQ(old_vertices[8], 2.f);
    EXPECT_EQ(manager->TrimCpuCache(), 0u); // UUID ownership protects new bytes too.
    EXPECT_FLOAT_EQ(manager->GetMeshAsset(ID)->Vertices[8], 31.f);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, before.LiveBlocks + 1);
    lease.Reset();
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks, before.LiveBlocks);
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
    EXPECT_EQ(manager->ContainerSlab.GetStatistics().LiveBlocks + 1, before.LiveBlocks);
}

TEST_F(MeshAssetLoaderTest, CpuClosurePinsDependenciesAndRehydratesMaterialStringsIntoTheSameSlot)
{
    WriteMaterialDependency();
    auto*                              manager = AssetManager::Instance();
    ZEngine::Managers::AssetCacheLease lease   = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    const auto slot = *manager->UUIDToMaterialSlot.find(MaterialID);
    ASSERT_NE(manager->MaterialCacheAllocations[slot], nullptr);
    EXPECT_STREQ(manager->Materials[slot].Name.c_str(), "triangle");
    EXPECT_EQ(manager->TrimCpuCache(), 0u);
    lease.Reset();
    EXPECT_EQ(manager->TrimCpuCache(), 2u);
    EXPECT_EQ(manager->MaterialCacheAllocations[slot], nullptr);
    EXPECT_TRUE(manager->Materials[slot].Name.empty());
    EXPECT_EQ(manager->Materials[slot].MaterialUUID, MaterialID);
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    EXPECT_EQ(*manager->UUIDToMaterialSlot.find(MaterialID), slot);
    EXPECT_EQ(manager->Materials.size(), 2u); // Includes the permanent fallback.
    EXPECT_STREQ(manager->Materials[slot].Name.c_str(), "triangle");
    EXPECT_FLOAT_EQ(manager->GPUMeshMaterials[slot].AlbedoColor.x, 0.25f);
}

TEST_F(MeshAssetLoaderTest, MissingCpuDependencyCannotPartiallyRetainAMesh)
{
    auto* manager                  = AssetManager::Instance();
    // Direct importer admission may precede material admission.
    Mesh.SubMeshes[0].MaterialUUID = MaterialID;
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    EXPECT_FALSE(manager->RetainMeshAssets(ID, lease));
    EXPECT_EQ(manager->MeshCacheAllocations[0]->References.load(), 1u);
    EXPECT_EQ(manager->CpuAssetOwnerPins[0].load(), 0u);
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
}

TEST_F(MeshAssetLoaderTest, ZeroRetentionReloadsOnlyUnownedAssets)
{
    auto* manager = AssetManager::Instance();
    manager->CpuRetention.store(AssetManager::CpuCacheRetention::ZeroRetention);
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_EQ(VFS.Reads, 2u);
    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    EXPECT_EQ(VFS.Reads, 3u);
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
    EXPECT_EQ(VFS.Reads, 3u);
    lease.Reset();
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
}

TEST_F(MeshAssetLoaderTest, TargetedCpuEvictionUsesLeastRecentlyUsedOrderAndStopsAfterItsBudget)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    const auto other            = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    Mesh.MeshUUID               = other;
    Hierarchy.MeshUUID          = other;
    Hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("66666666-6666-4666-8666-666666666666");
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    // Touch the first entry after the second was published.
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    EXPECT_EQ(manager->TrimCpuCache(0), 0u);
    EXPECT_EQ(manager->TrimCpuCache(1), 1u);
    EXPECT_NE(manager->GetMeshAsset(ID), nullptr);
    EXPECT_EQ(manager->GetMeshAsset(other), nullptr);
    EXPECT_NE(manager->MeshCacheAllocations[0], nullptr);
    EXPECT_EQ(manager->MeshCacheAllocations[1], nullptr);
}

TEST_F(MeshAssetLoaderTest, CpuTextureStringsFollowSceneClosureWithoutReleasingGpuOwnership)
{
    auto*      manager                        = AssetManager::Instance();
    const auto texture                        = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    manager->FallbackTextureHandle.Index      = 5;
    manager->FallbackTextureHandle.Generation = 1;
    AssetMaterial material                    = {};
    material.MaterialUUID                     = MaterialID;
    material.Name.init(&Memory.MainArena, "textured");
    material.AlbedoTexUUID = texture;
    material.AlbedoTexPath.init(&Memory.MainArena, "Textures/a.png");
    ASSERT_TRUE(AssetManager::IngestMaterial(std::move(material)));
    Mesh.SubMeshes[0].MaterialUUID = MaterialID;
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    ASSERT_TRUE(manager->RetainMeshAssets(ID, lease));
    ASSERT_EQ(manager->TextureCacheAllocations.size(), 1u);
    EXPECT_EQ(manager->TextureCacheAllocations[0]->References.load(), 2u);
    EXPECT_EQ(manager->TrimCpuCache(), 0u);
    lease.Reset();
    EXPECT_EQ(manager->TrimCpuCache(), 3u);
    EXPECT_EQ(manager->TextureCacheAllocations[0], nullptr);
    EXPECT_TRUE(manager->Textures[0].Path.empty());
    EXPECT_EQ(AssetManager::FindTextureHandle(texture), manager->FallbackTextureHandle);
    const auto slot = *manager->UUIDToMaterialSlot.find(MaterialID);
    EXPECT_EQ(manager->GPUMeshMaterials[slot].AlbedoMap, 5u);
    EXPECT_EQ(manager->Materials[slot].AlbedoTexUUID, texture);
}

TEST_F(MeshAssetLoaderTest, OperationCpuPinTransfersExactlyOnceAndProtectsReimport)
{
    auto* manager = AssetManager::Instance();
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    auto pin = manager->PinMeshPayload(ID);
    ASSERT_TRUE(pin);
    auto* previous = manager->MeshCacheAllocations[0];
    EXPECT_EQ(previous->References.load(), 2u);
    auto moved = std::move(pin);
    EXPECT_FALSE(pin);
    EXPECT_TRUE(moved);
    Mesh.Vertices[8] = 44.f;
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
    EXPECT_EQ(previous->References.load(), 1u);
    EXPECT_EQ(manager->TrimCpuCache(), 0u);
    auto current = manager->PinMeshPayload(ID);
    ASSERT_TRUE(current);
    current = std::move(moved); // Releases its preceding version once.
    EXPECT_FALSE(moved);
    EXPECT_EQ(manager->CpuAssetOwnerPins[0].load(), 1u);
    current.Reset();
    current.Reset();
    EXPECT_EQ(manager->CpuAssetOwnerPins[0].load(), 0u);
    EXPECT_EQ(manager->TrimCpuCache(), 1u);
    EXPECT_FALSE(manager->PinMeshPayload(ID));
}

TEST_F(MeshAssetLoaderTest, WorkerPublicationRecoversFromPressureByReclaimingColdCpuPayload)
{
    auto*     manager = AssetManager::Instance();
    AssetMesh cold    = {};
    cold.MeshUUID     = *uuids::uuid::from_string("55555555-5555-4555-8555-555555555555");
    cold.Vertices.init(&Memory.MainArena, 65536, 65536);
    cold.Indices.init(&Memory.MainArena, 3, 3);
    cold.Indices[1] = 1;
    cold.Indices[2] = 2;
    cold.SubMeshes.init(&Memory.MainArena, 1);
    cold.SubMeshes.push(Mesh.SubMeshes[0]);
    AssetNodeHierarchy cold_hierarchy = {};
    cold_hierarchy.MeshUUID           = cold.MeshUUID;
    cold_hierarchy.NodeHierarchyUUID  = *uuids::uuid::from_string("66666666-6666-4666-8666-666666666666");
    ASSERT_TRUE(AssetManager::IngestMesh(std::move(cold), std::move(cold_hierarchy)));
    std::vector<void*> pressure = {};
    for (const auto bytes : {size_t(ZMega(1)), size_t(512), size_t(128)})
    {
        while (auto* block = manager->ContainerSlab.TryRealloc(nullptr, bytes))
        {
            pressure.push_back(block);
        }
    }
    ZEngine::Managers::AssetCacheLease lease = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    const auto loaded = manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease);
    for (auto* block : pressure)
    {
        manager->ContainerSlab.Free(block);
    }
    ASSERT_TRUE(loaded.Succeeded());
    EXPECT_EQ(manager->MeshCacheAllocations[0], nullptr);
    EXPECT_NE(manager->GetMeshAsset(ID), nullptr);
    EXPECT_EQ(manager->TrimCpuCache(), 0u);
}

#if ZENGINE_PROFILING && !ZENGINE_TRACY
TEST_F(MeshAssetLoaderTest, CpuLoadAndMaintenanceExposeBoundedSceneLoadTraceEvents)
{
    ASSERT_TRUE(ZEngine::Profiling::ProfilerBuffer::StartCapture("SceneLoad.Worker."));
    const auto loaded    = AssetManager::Instance()->LoadMeshAsset(VFS, ID);
    const auto reclaimed = AssetManager::Instance()->TrimCpuCache();
    ZEngine::Profiling::ProfilerBuffer::StopCapture();
    EXPECT_TRUE(loaded.Succeeded());
    EXPECT_EQ(reclaimed, 1u);
    EXPECT_EQ(ZEngine::Profiling::ProfilerBuffer::CaptureCount(), 2u);
    EXPECT_EQ(ZEngine::Profiling::ProfilerBuffer::CaptureDropped(), 0u);
}
#endif

TEST_F(MeshAssetLoaderTest, MixedCpuLoadCancelReimportAndZeroRetentionReturnToExactWarmBaseline)
{
    auto*                              manager = AssetManager::Instance();
    ZEngine::Managers::AssetCacheLease lease   = {};
    ASSERT_TRUE(lease.Initialize(&Memory.MainArena));
    ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
    ASSERT_EQ(manager->TrimCpuCache(), 1u);
    const auto baseline = manager->ContainerSlab.GetStatistics();
    const auto offset   = Memory.MainArena.m_current_offset;
    for (uint32_t iteration = 0; iteration < 1100; ++iteration)
    {
        std::atomic<bool> cancelled = true;
        ASSERT_EQ(manager->LoadMeshAsset(VFS, ID, {}, &cancelled, &lease).Error(), VFSError::Cancelled);
        ASSERT_TRUE(manager->LoadMeshAsset(VFS, ID, {}, nullptr, &lease).Succeeded());
        if (iteration % 7 == 0)
        {
            ASSERT_TRUE(AssetManager::IngestMesh(std::move(Mesh), std::move(Hierarchy)));
        }
        ASSERT_EQ(manager->TrimCpuCache(), 0u);
        lease.Reset();
        ASSERT_EQ(manager->TrimCpuCache(), 1u);
        const auto current = manager->ContainerSlab.GetStatistics();
        ASSERT_EQ(current.LiveBytes, baseline.LiveBytes) << iteration;
        ASSERT_EQ(current.LiveBlocks, baseline.LiveBlocks) << iteration;
        ASSERT_EQ(Memory.MainArena.m_current_offset, offset) << iteration;
    }
}
