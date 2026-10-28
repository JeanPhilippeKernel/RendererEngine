#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSMemoryBackend.h>
#include <ZEngine/Importers/IAssetImporter.h>
#include <ZEngine/Managers/AssetManager.h>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <thread>

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
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 0u);
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
        EXPECT_EQ(manager->LoadMeshAsset(VFS, ID).Error(), VFSError::AlreadyExists);
    gate.Release.store(true, std::memory_order_release);
    worker.join();
    ASSERT_TRUE(result.Succeeded());
    EXPECT_EQ(VFS.Reads, 1u);
    EXPECT_EQ(manager->MeshCacheAllocations.size(), 1u);
    EXPECT_TRUE(manager->LoadMeshAsset(VFS, ID).Succeeded());
}
