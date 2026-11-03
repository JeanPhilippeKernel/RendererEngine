#include <Tetragrama/Thumbnails/ThumbnailService.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Core/VFS/VFSContext.h>
#include <ZEngine/Core/VFS/VFSDiskBackend.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <gtest/gtest.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>
#include <vector>

struct RRMDecodeTestHelper
{
    static bool Track(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Rendering::Textures::TextureHandle texture)
    {
        return manager.TrackTextureDecode(texture);
    }
    static void Complete(ZEngine::Rendering::RenderResourceManager& manager, ZEngine::Rendering::Textures::TextureHandle texture, bool success)
    {
        if (success)
            manager.MarkTextureUploadSubmitted(texture);
        else
            manager.PublishTextureDecodeCompletion(texture, false);
    }
};

namespace Tetragrama::Thumbnails
{
    struct ThumbnailServiceTestAccess
    {
        static void SetLoadSubmitter(Tetragrama::Thumbnails::ThumbnailService& service, void* context, bool (*submit)(void*, void*, void (*)(void*)))
        {
            service.m_load_submit_context = context;
            service.m_submit_asset_load   = submit;
        }
        static uint32_t AssetLoads(const Tetragrama::Thumbnails::ThumbnailService& service)
        {
            uint32_t count = 0;
            for (const auto& work : service.m_asset_loads)
                count += work.Active;
            return count;
        }
        static uint32_t AssetLoadLimit()
        {
            return Tetragrama::Thumbnails::ThumbnailService::kMaxAssetLoads;
        }
        static const void* MaterialScratch(const Tetragrama::Thumbnails::ThumbnailService& service)
        {
            for (const auto& work : service.m_asset_loads)
                if (work.Scratch.m_memory)
                    return work.Scratch.m_memory;
            return nullptr;
        }
        static void ColdMesh(Tetragrama::Thumbnails::ThumbnailService& service, const ZEngine::Core::VFS::AssetRecordSnapshot& record)
        {
            auto* entry = service.PrepareRequest(record.Record.UUID, record.Record.Meta.SourceHash);
            ASSERT_NE(entry, nullptr);
            entry->Kind           = record.Record.Type;
            entry->DependencyHash = ZEngine::Rendering::Renderers::AssetPreviewPass::EnvironmentRevision;
            entry->CacheMiss      = true;
        }
        static void SetProjectRoot(Tetragrama::Thumbnails::ThumbnailService& service, const char* path)
        {
            ZEngine::Helpers::secure_strcpy(service.m_project_root, sizeof(service.m_project_root), path);
        }

        static bool BuildCachePath(const Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid, uint64_t source_hash, char (&out_path)[MAX_FILE_PATH_COUNT], uint64_t dependency_hash = 0, uint32_t renderer_revision = 0)
        {
            return service.BuildCachePath(uuid, source_hash, out_path, dependency_hash, renderer_revision);
        }

        static void SetFrame(Tetragrama::Thumbnails::ThumbnailService& service, uint64_t frame)
        {
            service.m_frame = frame;
        }

        static Tetragrama::Thumbnails::ThumbnailService::Entry MakeEntry()
        {
            return {};
        }

        static void MarkFailed(Tetragrama::Thumbnails::ThumbnailService& service, Tetragrama::Thumbnails::ThumbnailService::Entry& entry)
        {
            service.MarkFailed(entry);
        }

        static void SetPendingEntry(Tetragrama::Thumbnails::ThumbnailService& service, uint32_t index, const uuids::uuid& uuid)
        {
            service.m_entries[index].AssetUUID    = uuid;
            service.m_entries[index].CurrentState = Tetragrama::Thumbnails::ThumbnailService::State::Pending;
            service.m_entries[index].Texture      = {.Index = index, .Generation = 1};
        }

        static bool TakeBudget(Tetragrama::Thumbnails::ThumbnailService& service)
        {
            return service.TakeSubmissionBudget();
        }
        static bool ReclaimForPressure(Tetragrama::Thumbnails::ThumbnailService& service)
        {
            return service.ReclaimForResourcePressure();
        }
        static uint32_t SubmissionLimit()
        {
            return Tetragrama::Thumbnails::ThumbnailService::kSubmissionsPerFrame;
        }
        static uint32_t InFlightLimit()
        {
            return Tetragrama::Thumbnails::ThumbnailService::kMaxInFlight;
        }

        static Tetragrama::Thumbnails::ThumbnailService::Entry* PrepareRequest(Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid, uint64_t source_hash)
        {
            return service.PrepareRequest(uuid, source_hash);
        }

        static Tetragrama::Thumbnails::ThumbnailService::Entry* Allocate(Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid, uint64_t source_hash)
        {
            return service.Allocate(uuid, source_hash);
        }

        static void SetResident(Tetragrama::Thumbnails::ThumbnailService::Entry& entry, uint32_t texture_index)
        {
            entry.CurrentState = Tetragrama::Thumbnails::ThumbnailService::State::Resident;
            entry.Texture      = {.Index = texture_index, .Generation = 1};
        }

        static void Invalidate(Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid)
        {
            service.Invalidate(uuid);
        }

        static void InvalidateAll(Tetragrama::Thumbnails::ThumbnailService& service)
        {
            service.InvalidateAll();
        }

        static bool HasEntry(Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid, uint64_t source_hash)
        {
            return service.Find(uuid, source_hash) != nullptr;
        }

        static uint32_t Capacity()
        {
            return Tetragrama::Thumbnails::ThumbnailService::kCapacity;
        }

        static uint32_t InvalidationCapacity()
        {
            return Tetragrama::Thumbnails::ThumbnailService::kInvalidationCapacity;
        }

        static bool IsPreviewable(const ZEngine::Core::VFS::AssetRecordSnapshot& record)
        {
            return Tetragrama::Thumbnails::ThumbnailService::IsPreviewable(record);
        }

        static bool IsDiscarded(const Tetragrama::Thumbnails::ThumbnailService& service, uint32_t index)
        {
            return service.m_entries[index].CurrentState == Tetragrama::Thumbnails::ThumbnailService::State::Discarded;
        }

        static bool IsResident(const Tetragrama::Thumbnails::ThumbnailService& service, uint32_t index)
        {
            return service.m_entries[index].CurrentState == Tetragrama::Thumbnails::ThumbnailService::State::Resident;
        }

        static void QueueInvalidation(Tetragrama::Thumbnails::ThumbnailService& service, const uuids::uuid& uuid)
        {
            service.QueueInvalidation(uuid);
        }
    };
} // namespace Tetragrama::Thumbnails

namespace
{

    class ThumbnailAssetLoadTest : public ::testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager                       Memory{};
        ZEngine::Core::VFS::VFSDiskBackend                         Backend{};
        ZEngine::Core::VFS::VFSContext                             VFS{};
        std::unique_ptr<ZEngine::Rendering::RenderResourceManager> Resources{};
        ZEngine::Rendering::Renderers::AssetPreviewPass            Previews{};
        Tetragrama::Thumbnails::ThumbnailService                   Service{};
        ZEngine::Managers::AssetManager*                           Assets = nullptr;
        std::string                                                Root{};
        struct Job
        {
            void* Context      = nullptr;
            void (*Run)(void*) = nullptr;
        };
        std::vector<Job> Jobs{};
        uint32_t         Submissions = 0;
        bool             Accept      = true;

        void             SetUp() override
        {
            Memory.Initialize(ZMega(384), {});
            Root = (std::filesystem::temp_directory_path() / ("thumbnail-load-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
            ASSERT_TRUE(std::filesystem::create_directory(Root));
            ZEngine::Managers::AssetManager::Initialize(&Memory.MainArena, nullptr, Root.c_str());
            Assets = ZEngine::Managers::AssetManager::Instance();
            Backend.Initialize(Root.c_str(), ZEngine::Core::VFS::VFSBackendCaps::Read | ZEngine::Core::VFS::VFSBackendCaps::Write | ZEngine::Core::VFS::VFSBackendCaps::List, &Memory.MainArena);
            VFS.Initialize(&Memory.MainArena);
            ASSERT_TRUE(VFS.Mount(&Backend, ZEngine::Core::VFS::VFSPath::Root(), 0).Succeeded());
            Resources = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
            Service.Initialize(Assets->Registry, Resources.get(), Root.c_str(), &Previews, &VFS);
            Jobs.reserve(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoadLimit());
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetLoadSubmitter(Service, this, [](void* context, void* work, void (*run)(void*)) {
                auto& self = *static_cast<ThumbnailAssetLoadTest*>(context);
                ++self.Submissions;
                if (!self.Accept)
                    return false;
                self.Jobs.push_back({work, run});
                return true;
            });
        }
        void RunJobs()
        {
            std::thread worker([&] {
                for (const auto& job : Jobs)
                    job.Run(job.Context);
            });
            worker.join();
            Jobs.clear(); // Keep the bounded test dispatcher's reserved storage.
        }
        void TearDown() override
        {
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InvalidateAll(Service);
            RunJobs();
            Service.Deinitialize();
            VFS.Shutdown();
            ZEngine::Managers::AssetManager::Shutdown();
            Resources.reset();
            Memory.Shutdown();
            std::filesystem::remove_all(Root);
        }
        ZEngine::Core::VFS::AssetRecordSnapshot Register(const char* path, const uuids::uuid& id, ZEngine::Managers::AssetType type)
        {
            ZEngine::Core::VFS::MetaFileData meta{};
            meta.AssetUUID    = id;
            meta.SourceHash   = 123;
            const auto parsed = ZEngine::Core::VFS::VFSPath::Parse(path).Value();
            EXPECT_TRUE(Assets->Registry->Register(id, type, parsed, meta).IsOk());
            ZEngine::Core::VFS::AssetRecordSnapshot record{};
            EXPECT_TRUE(Assets->Registry->TryReadRecord(id, &record));
            return record;
        }
        ZEngine::Core::VFS::AssetRecordSnapshot Material(uint32_t index)
        {
            auto                    id    = *uuids::uuid::from_string("12345678-1234-4234-8234-123456789000");
            auto                    bytes = id.as_bytes();
            std::array<uint8_t, 16> distinct{};
            EXPECT_EQ(ZEngine::Helpers::secure_memcpy(distinct.data(), distinct.size(), bytes.data(), bytes.size()), ZEngine::Helpers::MEMORY_OP_SUCCESS);
            distinct.back() = uint8_t(index);
            id              = uuids::uuid(distinct);
            ZEngine::Importers::AssetMaterial material{};
            material.MaterialUUID = id;
            material.Name.init(&Memory.MainArena, "surface");
            material.AlphaMode      = ZEngine::Rendering::Materials::AlphaMode::Blend;
            material.AlbedoColor[3] = 0;
            ZEngine::Importers::AssetCodec::ImportConfiguration config{};
            config.VFS = &VFS;
            config.AssetName.init(&Memory.MainArena, "fresh");
            config.OutputMaterialPath.init(&Memory.MainArena, "/Materials");
            auto output = ZEngine::Importers::AssetCodec::SerializeMaterialAssetFile(&Memory.MainArena, material, config, index);
            EXPECT_TRUE(output.Succeeded());
            return Register(output.Value().Path.c_str(), id, ZEngine::Managers::AssetType::MATERIAL);
        }
    };

    TEST_F(ThumbnailAssetLoadTest, RegisteredMeshLoadsForItsCardWithoutViewportPlacement)
    {
        ZEngine::Importers::AssetMesh mesh{};
        mesh.MeshUUID = *uuids::uuid::from_string("12345678-1234-4234-8234-123456789abc");
        mesh.Vertices.init(&Memory.MainArena, 24, 24);
        mesh.Vertices[8] = mesh.Vertices[17] = 1;
        mesh.Indices.init(&Memory.MainArena, 3);
        for (uint32_t i = 0; i < 3; ++i)
            mesh.Indices.push(i);
        mesh.SubMeshes.init(&Memory.MainArena, 1);
        mesh.SubMeshes.push({.VertexCount = 3, .IndexCount = 3, .VertexUnitStreamSize = 32, .IndexUnitStreamSize = 4, .TotalByteSize = 108});
        ZEngine::Importers::AssetNodeHierarchy hierarchy{};
        hierarchy.MeshUUID          = mesh.MeshUUID;
        hierarchy.NodeHierarchyUUID = *uuids::uuid::from_string("12345678-1234-4234-8234-123456789def");
        hierarchy.Hierarchies.init(&Memory.MainArena, 1);
        hierarchy.LocalTransforms.init(&Memory.MainArena, 1);
        hierarchy.GlobalTransforms.init(&Memory.MainArena, 1);
        hierarchy.Names.init(&Memory.MainArena, 0);
        hierarchy.MaterialNames.init(&Memory.MainArena, 0);
        hierarchy.NodeNames.init(&Memory.MainArena, 16);
        hierarchy.NodeMeshes.init(&Memory.MainArena, 16);
        hierarchy.NodeMaterials.init(&Memory.MainArena, 16);
        ZEngine::Importers::AddNode(hierarchy, -1, 0);
        hierarchy.NodeMeshes.insert(0, 0);
        ZEngine::Importers::AssetCodec::ImportConfiguration config{};
        config.VFS = &VFS;
        config.OutputAssetsPath.init(&Memory.MainArena, "/Meshes");
        config.OutputAssetFile.init(&Memory.MainArena, "triangle.zemesh");
        ASSERT_TRUE(ZEngine::Importers::AssetCodec::SerializeMeshAssetFile(&Memory.MainArena, mesh, hierarchy, config).Succeeded());
        const auto record = Register("/Meshes/triangle.zemesh", mesh.MeshUUID, ZEngine::Managers::AssetType::MESH);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::ColdMesh(Service, record);
        uint32_t draws = 0;
        ASSERT_FALSE(Assets->TryGetMeshDrawCount(mesh.MeshUUID, &draws));
        for (uint32_t i = 0; i < 10; ++i)
        {
            Service.Update();
            EXPECT_FALSE(Service.Acquire(record).Valid());
        }
        EXPECT_EQ(Submissions, 1u);
        EXPECT_FALSE(Assets->TryGetMeshDrawCount(mesh.MeshUUID, &draws)); // No inline IO.
        RunJobs();
        Service.Update();
        EXPECT_TRUE(Assets->TryGetMeshDrawCount(mesh.MeshUUID, &draws));
        EXPECT_EQ(draws, 1u);
        ZEngine::Core::VFS::AssetRecordSnapshot loaded{};
        ASSERT_TRUE(Assets->Registry->TryReadRecord(mesh.MeshUUID, &loaded));
        EXPECT_EQ(loaded.Record.State, ZEngine::Core::VFS::AssetState::Loaded);
        // Mesh index zero is a valid packed handle, not the unloaded sentinel.
        ZEngine::Managers::AssetHandle slot = UINT32_MAX;
        EXPECT_TRUE(Assets->Registry->TryReadSlot(mesh.MeshUUID, ZEngine::Managers::AssetType::MESH, &slot));
        EXPECT_EQ(Assets->ReadAssetHandleType(slot), ZEngine::Managers::AssetType::MESH);
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoads(Service), 0u);
    }

    TEST_F(ThumbnailAssetLoadTest, WarmMeshCacheDoesNotLoadGeometryWhilePublishingItsDecodedPreview)
    {
        const auto id     = *uuids::uuid::from_string("12345678-1234-4234-8234-123456789abc");
        const auto record = Register("/Meshes/triangle.zemesh", id, ZEngine::Managers::AssetType::MESH);
        auto*      entry  = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(Service, id, record.Record.Meta.SourceHash);
        ASSERT_NE(entry, nullptr);
        entry->Kind = ZEngine::Managers::AssetType::MESH;
        ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(record, &entry->DependencyHash));
        entry->Texture = {.Index = 7, .Generation = 1};
        ASSERT_TRUE(RRMDecodeTestHelper::Track(*Resources, entry->Texture));
        EXPECT_FALSE(Service.Acquire(record).Valid());
        EXPECT_EQ(Submissions, 0u);
        EXPECT_TRUE(Assets->Meshes.empty());
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, true);
        Service.Update();
        EXPECT_EQ(Service.GetStatistics().Resident, 1u);
        EXPECT_EQ(Service.GetStatistics().AssetLoads, 0u);
        EXPECT_FALSE(entry->GraphRequest);
        EXPECT_TRUE(Assets->Meshes.empty());
    }

    TEST_F(ThumbnailAssetLoadTest, StandaloneMaterialLoadsAndClassifiesWithoutAnySceneOrMesh)
    {
        const auto record      = Material(1);
        bool       transparent = false;
        EXPECT_FALSE(Service.Acquire(record, &transparent).Valid());
        EXPECT_FALSE(transparent);
        EXPECT_FALSE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
        RunJobs();
        Service.Update();
        EXPECT_TRUE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
        EXPECT_FALSE(Service.Acquire(record, &transparent).Valid());
        EXPECT_TRUE(transparent);
        EXPECT_EQ(Submissions, 1u);
        EXPECT_EQ(Service.GetStatistics().FullyTransparent, 1u);
    }

    TEST_F(ThumbnailAssetLoadTest, MaterialLoadSlotsReuseScratchAndReleaseItAfterWorkersJoin)
    {
        const void* scratch = nullptr;
        for (uint32_t i = 1; i <= 3; ++i)
        {
            const auto record = Material(i);
            Service.Update();
            EXPECT_FALSE(Service.Acquire(record).Valid());
            RunJobs();
            Service.Update();
            EXPECT_TRUE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
            const auto* current = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MaterialScratch(Service);
            ASSERT_NE(current, nullptr);
            if (scratch)
                EXPECT_EQ(current, scratch);
            scratch = current;
        }
        Service.Deinitialize(); // All fixture workers have joined.
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MaterialScratch(Service), nullptr);
    }

    TEST_F(ThumbnailAssetLoadTest, CancelledLoadsKeepTheirBoundedLeasesUntilWorkersFinish)
    {
        for (uint32_t i = 0; i < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoadLimit() + 2; ++i)
        {
            const auto record = Material(i);
            Service.Update();
            EXPECT_FALSE(Service.Acquire(record).Valid());
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(Service, record.Record.UUID);
        }
        EXPECT_EQ(Submissions, Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoadLimit());
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoads(Service), Submissions);
        RunJobs();
        Service.Update();
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoads(Service), 0u);
        EXPECT_EQ(Assets->Materials.size(), 1u); // Only the intentional fallback slot.
    }

    TEST_F(ThumbnailAssetLoadTest, ChangedSourceAndShutdownNeverPublishAnObsoleteQueuedLoad)
    {
        auto record = Material(1);
        Service.Acquire(record);
        ZEngine::Core::VFS::MetaFileData meta{};
        meta.AssetUUID  = record.Record.UUID;
        meta.SourceHash = record.Record.Meta.SourceHash + 1;
        ASSERT_TRUE(Assets->Registry->UpdateMeta(record.Record.UUID, meta));
        RunJobs();
        Service.Update();
        EXPECT_FALSE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
        record = Material(2);
        Service.Acquire(record);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InvalidateAll(Service);
        RunJobs();
        Service.Deinitialize();
        EXPECT_FALSE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
    }

    TEST_F(ThumbnailAssetLoadTest, RejectedDispatchRetriesWithoutInlineLoadingOrLeakingALease)
    {
        const auto record = Material(1);
        Accept            = false;
        Service.Acquire(record);
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoads(Service), 0u);
        EXPECT_TRUE(Jobs.empty());
        EXPECT_FALSE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
        Accept = true;
        Service.Update();
        Service.Acquire(record);
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::AssetLoads(Service), 1u);
        RunJobs();
        Service.Update();
        EXPECT_TRUE(Assets->UUIDToMaterialSlot.contains(record.Record.UUID));
    }

    TEST(ThumbnailMaterialTest, FullyTransparentMaterialsUseNoDecodeGraphOrDiskBudgetAndInvalidateOnEdit)
    {
        ZEngine::Core::Memory::MemoryManager memory{};
        memory.Initialize(ZMega(384), {});
        ZEngine::Managers::AssetManager::Initialize(&memory.MainArena, nullptr, "/PreviewTest");
        auto*                             assets = ZEngine::Managers::AssetManager::Instance();
        const auto                        uuid   = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        ZEngine::Importers::AssetMaterial material{};
        material.MaterialUUID   = uuid;
        material.AlbedoColor[3] = 0;
        material.Factors[2]     = .5f;
        assets->Materials.push(material);
        const uint32_t slot = static_cast<uint32_t>(assets->Materials.size() - 1);
        assets->UUIDToMaterialSlot.insert(uuid, slot);
        ZEngine::Core::VFS::MetaFileData meta{};
        meta.AssetUUID  = uuid;
        meta.SourceHash = 123;
        ASSERT_TRUE(assets->Registry->Register(uuid, ZEngine::Managers::AssetType::MATERIAL, ZEngine::Core::VFS::VFSPath::Parse("/surface.zematerial").Value(), meta).IsOk());
        auto                                            resources = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
        ZEngine::Rendering::Renderers::AssetPreviewPass previews{};
        Tetragrama::Thumbnails::ThumbnailService        service{};
        service.Initialize(assets->Registry, resources.get(), "/PreviewTest", &previews);
        ZEngine::Core::VFS::AssetRecordSnapshot snapshot{};
        ASSERT_TRUE(assets->Registry->TryReadRecord(uuid, &snapshot));
        bool transparent = false;
        for (uint32_t frame = 0; frame < 10; ++frame)
        {
            service.Update();
            EXPECT_FALSE(service.Acquire(snapshot, &transparent).Valid());
            EXPECT_TRUE(transparent);
        }
        auto stats = service.GetStatistics();
        EXPECT_EQ(stats.FullyTransparent, 1u);
        EXPECT_EQ(stats.Resident + stats.Queued + stats.InFlight + stats.Failed + stats.SubmittedThisFrame, 0u);
        EXPECT_EQ(stats.ResidentPixelBytes, 0u);
        assets->Registry->MarkStale(uuid);
        service.Update();
        EXPECT_EQ(service.GetStatistics().FullyTransparent, 0u);
        ASSERT_TRUE(assets->Registry->TryReadRecord(uuid, &snapshot));
        EXPECT_FALSE(service.Acquire(snapshot, &transparent).Valid());
        EXPECT_FALSE(transparent);
        assets->Materials[slot].AlbedoColor[3] = 1;
        meta.SourceHash                        = 456;
        ASSERT_TRUE(assets->Registry->UpdateMeta(uuid, meta));
        ASSERT_TRUE(assets->Registry->TryReadRecord(uuid, &snapshot));
        EXPECT_FALSE(service.Acquire(snapshot, &transparent).Valid());
        EXPECT_FALSE(transparent);
        EXPECT_EQ(service.GetStatistics().FullyTransparent, 0u);
        service.Deinitialize();
        ZEngine::Managers::AssetManager::Shutdown();
        memory.Shutdown();
    }

    class ThumbnailServiceIntegrationTest : public ::testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager                       Memory{};
        ZEngine::Core::VFS::AssetRegistry                          Registry{};
        std::unique_ptr<ZEngine::Rendering::RenderResourceManager> Resources{};
        Tetragrama::Thumbnails::ThumbnailService                   Service{};
        const uuids::uuid                                          UUID = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
        std::filesystem::path                                      Root{};

        void                                                       SetUp() override
        {
            Memory.Initialize(ZMega(64), {});
            Registry.Initialize(&Memory.MainArena);
            Resources = std::make_unique<ZEngine::Rendering::RenderResourceManager>();
            // Each fixture owns an isolated root; never touch a user's cache.
            Root      = std::filesystem::temp_directory_path() / ("thumbnail-service-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            ASSERT_TRUE(std::filesystem::create_directory(Root));
            ZEngine::Core::VFS::MetaFileData meta = {};
            meta.AssetUUID                        = UUID;
            meta.SourceHash                       = 123;
            ASSERT_TRUE(Registry.Register(UUID, ZEngine::Managers::AssetType::TEXTURE, ZEngine::Core::VFS::VFSPath::Parse("/texture.png").Value(), meta).IsOk());
            Service.Initialize(&Registry, Resources.get(), Root.string().c_str());
        }
        void TearDown() override
        {
            Service.Deinitialize();
            Resources.reset();
            Memory.Shutdown();
            std::error_code error{};
            std::filesystem::remove_all(Root, error);
        }
        auto* Request()
        {
            auto* entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(Service, UUID, 123);
            if (entry)
            {
                entry->Texture = {.Index = 7, .Generation = 1};
                EXPECT_TRUE(RRMDecodeTestHelper::Track(*Resources, entry->Texture));
            }
            return entry;
        }
    };

    // Repeatable profiler workloads, with fixture allocation outside the timed
    // loop. Record timings rather than imposing hardware-dependent CI limits.
    TEST_F(ThumbnailServiceIntegrationTest, RepeatedPendingAcquiresReuseTheFullTable)
    {
        const uint32_t                                       count = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Capacity();
        std::vector<ZEngine::Core::VFS::AssetRecordSnapshot> records(count);
        for (uint32_t i = 0; i < count; ++i)
        {
            const uint8_t bytes[16] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), 2, 0, 0, 0, 0x40, 0, 0x80};
            auto&         record    = records[i].Record;
            record.UUID             = uuids::uuid(bytes, bytes + 16);
            record.Type             = ZEngine::Managers::AssetType::TEXTURE;
            record.Path             = ZEngine::Core::VFS::VFSPath::Parse("/texture.png").Value();
            record.State            = ZEngine::Core::VFS::AssetState::Registered;
            record.Meta.SourceHash  = 123;
            auto* entry             = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(Service, record.UUID, 123);
            ASSERT_NE(entry, nullptr);
            entry->Texture = {.Index = i, .Generation = 1};
        }
        constexpr uint32_t rounds   = 256;
        uint32_t           resident = 0;
        const auto         start    = std::chrono::steady_clock::now();
        for (uint32_t round = 0; round < rounds; ++round)
            for (const auto& record : records)
                resident += Service.Acquire(record).Valid();
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_EQ(resident, 0u);
        EXPECT_EQ(Service.GetStatistics().InFlight, count);
        RecordProperty("acquires", rounds * count);
        RecordProperty("acquire_microseconds", static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
    }

    TEST(ThumbnailServiceTest, SpentFrameBudgetDefersWithoutFurtherSubmissions)
    {
        Tetragrama::Thumbnails::ThumbnailService service{};
        for (uint32_t i = 0; i < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SubmissionLimit(); ++i)
            ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service));
        constexpr uint32_t attempts = 131072;
        uint32_t           accepted = 0;
        const auto         start    = std::chrono::steady_clock::now();
        for (uint32_t i = 0; i < attempts; ++i)
            accepted += Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service);
        const auto elapsed = std::chrono::steady_clock::now() - start;
        EXPECT_EQ(accepted, 0u);
        EXPECT_EQ(service.GetStatistics().BudgetDeferrals, attempts);
        RecordProperty("budget_attempts", attempts);
        RecordProperty("budget_microseconds", static_cast<int>(std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()));
    }

    TEST_F(ThumbnailServiceIntegrationTest, CompletionWaitsForUploadSubmissionBeforePublishing)
    {
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        Service.Update();
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsResident(Service, 0));
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, true);
        Service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsResident(Service, 0));
        EXPECT_EQ(Resources->GetTextureDecodeState(entry->Texture), ZEngine::Rendering::RenderResourceManager::TextureDecodeState::Untracked);
    }

    TEST_F(ThumbnailServiceIntegrationTest, RenderedCacheMissQueuesGraphFallbackWithoutFailureBackoff)
    {
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        entry->Kind = ZEngine::Managers::AssetType::MESH;
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, false);
        Service.Update();
        EXPECT_TRUE(entry->CacheMiss);
        EXPECT_FALSE(entry->GraphRequest);
        EXPECT_FALSE(entry->Texture.Valid());
        EXPECT_EQ(entry->FailureCount, 0u);
        EXPECT_EQ(entry->NextRetryFrame, 0u);
        EXPECT_EQ(Service.GetStatistics().Queued, 1u);
        EXPECT_EQ(Service.GetStatistics().Failed, 0u);
    }

    TEST_F(ThumbnailServiceIntegrationTest, DiscardedRenderedCacheMissCannotStartGraphFallback)
    {
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        entry->Kind = ZEngine::Managers::AssetType::MATERIAL;
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(Service, UUID);
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, false);
        Service.Update();
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(Service, UUID, 123));
        EXPECT_FALSE(entry->Texture.Valid());
        EXPECT_EQ(Service.GetStatistics().Queued, 0u);
    }

    TEST_F(ThumbnailServiceIntegrationTest, RenderedWarmHitPublishesTheDecodedHandleWithoutGraphWork)
    {
        Registry.Remove(UUID);
        ZEngine::Core::VFS::MetaFileData meta = {};
        meta.AssetUUID                        = UUID;
        meta.SourceHash                       = 123;
        ASSERT_TRUE(Registry.Register(UUID, ZEngine::Managers::AssetType::MESH, ZEngine::Core::VFS::VFSPath::Parse("/mesh.zemesh").Value(), meta).IsOk());
        Service.Update();
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        entry->Kind           = ZEngine::Managers::AssetType::MESH;
        entry->DependencyHash = 1;
        const auto texture    = entry->Texture;
        RRMDecodeTestHelper::Complete(*Resources, texture, true);
        Service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsResident(Service, 0));
        EXPECT_FALSE(entry->CacheMiss);
        EXPECT_FALSE(entry->GraphRequest);
        EXPECT_EQ(entry->Texture.Index, texture.Index);
        EXPECT_EQ(Service.GetStatistics().SubmittedThisFrame, 0u);
    }

    TEST_F(ThumbnailServiceIntegrationTest, CompletionRejectsAChangedSourceHash)
    {
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        ZEngine::Core::VFS::MetaFileData meta = {};
        meta.AssetUUID                        = UUID;
        meta.SourceHash                       = 456;
        ASSERT_TRUE(Registry.UpdateMeta(UUID, meta));
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, true);
        Service.Update();
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(Service, UUID, 123));
        EXPECT_FALSE(entry->Texture.Valid());
    }

    TEST_F(ThumbnailServiceIntegrationTest, RegistryStaleEventDiscardsInFlightWorkUntilCompletion)
    {
        auto* entry = Request();
        ASSERT_NE(entry, nullptr);
        Registry.MarkStale(UUID);
        Service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(Service, 0));
        EXPECT_TRUE(entry->Texture.Valid());
        RRMDecodeTestHelper::Complete(*Resources, entry->Texture, true);
        Service.Update();
        EXPECT_FALSE(entry->Texture.Valid());
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(Service, UUID, 123));
    }

    TEST_F(ThumbnailServiceIntegrationTest, RegisteredLargeProjectDoesNoStartupWorkAndOnlyRequestsVisiblePages)
    {
        std::array<ZEngine::Core::VFS::AssetRecordSnapshot, 1024> records = {};
        for (uint32_t i = 0; i < records.size(); ++i)
        {
            const uint8_t     bytes[16] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), 1, 0, 0, 0, 0x40, 0, 0x80};
            const uuids::uuid uuid(bytes, bytes + 16);
            char              path[64] = {};
            std::snprintf(path, sizeof(path), "/Assets/texture-%04u.png", i);
            ZEngine::Core::VFS::MetaFileData meta = {};
            meta.AssetUUID                        = uuid;
            meta.SourceHash                       = 1;
            ASSERT_TRUE(Registry.Register(uuid, ZEngine::Managers::AssetType::TEXTURE, ZEngine::Core::VFS::VFSPath::Parse(path).Value(), meta).IsOk());
            ASSERT_TRUE(Registry.TryReadRecord(uuid, &records[i]));
        }
        Service.Update();
        EXPECT_EQ(Service.GetStatistics().SubmittedThisFrame, 0u);
        EXPECT_EQ(Service.GetStatistics().Queued, 0u);
        for (uint32_t page = 0; page < 64; ++page)
        {
            Service.Update();
            for (uint32_t tile = 0; tile < 24; ++tile)
                EXPECT_FALSE(Service.Acquire(records[(page * 17 + tile) % records.size()]).Valid());
            const auto stats = Service.GetStatistics();
            EXPECT_LE(stats.SubmittedThisFrame, Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SubmissionLimit());
            EXPECT_LE(stats.Queued + stats.Failed, 512u);
            EXPECT_EQ(stats.InFlight, 0u); // Uninitialized RRM rejects allocations, still budgeted.
        }
        Registry.MarkStale(records[0].Record.UUID);
        Service.Update();
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(Service, records[0].Record.UUID, 1));
        EXPECT_GT(Service.GetStatistics().BudgetDeferrals, 0u);
    }

    TEST(ThumbnailServiceTest, BindlessPressureReclaimsOffscreenResidentBeforeCpuTableIsFull)
    {
        Tetragrama::Thumbnails::ThumbnailService service    = {};
        const auto                               old_id     = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        const auto                               visible_id = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 1);
        auto* old = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, old_id, 1);
        ASSERT_NE(old, nullptr);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetResident(*old, 7);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 2);
        auto* visible = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, visible_id, 1);
        ASSERT_NE(visible, nullptr);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetResident(*visible, 8);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::ReclaimForPressure(service));
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, old_id, 1));
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, visible_id, 1));
        EXPECT_EQ(service.GetStatistics().Resident, 1u);
        EXPECT_EQ(service.GetStatistics().Evictions, 1u);
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::ReclaimForPressure(service));
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service));
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 5);
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service));
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 6);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service));
    }

    TEST(ThumbnailServiceTest, OnlyRegisteredOrLoadedSupportedSnapshotsHaveStablePreviewKeys)
    {
        ZEngine::Core::VFS::AssetRecordSnapshot snapshot = {};
        snapshot.Record.UUID                             = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        snapshot.Record.Type                             = ZEngine::Managers::AssetType::TEXTURE;
        snapshot.Record.Path                             = ZEngine::Core::VFS::VFSPath::Parse("/Assets/texture.png").Value();
        const ZEngine::Core::VFS::AssetState states[]    = {ZEngine::Core::VFS::AssetState::Unregistered, ZEngine::Core::VFS::AssetState::Registered, ZEngine::Core::VFS::AssetState::Importing, ZEngine::Core::VFS::AssetState::Loaded, ZEngine::Core::VFS::AssetState::Stale, ZEngine::Core::VFS::AssetState::Failed};
        for (const ZEngine::Core::VFS::AssetState state : states)
        {
            snapshot.Record.State = state;
            EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsPreviewable(snapshot), state == ZEngine::Core::VFS::AssetState::Registered || state == ZEngine::Core::VFS::AssetState::Loaded);
        }
        snapshot.Record.Type  = ZEngine::Managers::AssetType::MESH;
        snapshot.Record.State = ZEngine::Core::VFS::AssetState::Loaded;
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsPreviewable(snapshot));
        snapshot.Record.Type = ZEngine::Managers::AssetType::TEXTURE;
        snapshot.Record.UUID = {};
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsPreviewable(snapshot));
    }

    TEST(ThumbnailServiceTest, RenderedCachePathsIncludeStableDependenciesAndRendererRevision)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetProjectRoot(service, "/Project");
        const auto uuid                       = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        char       first[MAX_FILE_PATH_COUNT] = {}, dependency[MAX_FILE_PATH_COUNT] = {}, renderer[MAX_FILE_PATH_COUNT] = {};
        ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, uuid, 123, first, 456, 1));
        ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, uuid, 123, dependency, 789, 1));
        ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, uuid, 123, renderer, 456, 2));
        EXPECT_STRNE(first, dependency);
        EXPECT_STRNE(first, renderer);
        EXPECT_NE(std::string(first).find("000000000000007b-00000000000001c8-3-1.png"), std::string::npos);
    }

    TEST(ThumbnailServiceTest, OverlongCachePathIsDisabledRatherThanUsedTruncated)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const std::string                        root(MAX_FILE_PATH_COUNT - 2, 'x');
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetProjectRoot(service, root.c_str());
        char path[MAX_FILE_PATH_COUNT] = {};
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, {}, 123, path, 456, 1));
        EXPECT_EQ(path[0], '\0');
    }

    TEST(ThumbnailServiceTest, CachePathChangesWithTheSourceHash)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetProjectRoot(service, "/Project");
        const auto uuid                        = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        char       first[MAX_FILE_PATH_COUNT]  = {};
        char       second[MAX_FILE_PATH_COUNT] = {};

        ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, uuid, 0x0123456789abcdefULL, first));
        ASSERT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::BuildCachePath(service, uuid, 0xfedcba9876543210ULL, second));

        EXPECT_STREQ(first, "/Project/.zodiacengine/cache/thumbnails/11111111-1111-4111-8111-111111111111/0123456789abcdef-3.png");
        EXPECT_STRNE(first, second);
    }

    TEST(ThumbnailServiceTest, FailedRequestsBackOffAtACappedInterval)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        auto                                     entry   = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MakeEntry();
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 100);

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, entry);
        EXPECT_EQ(entry.FailureCount, 1);
        EXPECT_EQ(entry.NextRetryFrame, 160u);

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, entry);
        EXPECT_EQ(entry.FailureCount, 2);
        EXPECT_EQ(entry.NextRetryFrame, 220u);

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, entry);
        EXPECT_EQ(entry.FailureCount, 3);
        EXPECT_EQ(entry.NextRetryFrame, 340u);

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, entry);
        EXPECT_EQ(entry.FailureCount, 4);
        EXPECT_EQ(entry.NextRetryFrame, 580u);

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, entry);
        EXPECT_EQ(entry.FailureCount, 4);
        EXPECT_EQ(entry.NextRetryFrame, 580u);
    }

    TEST(ThumbnailServiceTest, RegistryInvalidationIsAppliedByTheEditorUpdate)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetPendingEntry(service, 0, uuid);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::QueueInvalidation(service, uuid);
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        service.Update();

        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
    }
    TEST(ThumbnailServiceTest, RetryRequestsPreserveFailureCountAndRespectTheDeadline)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        auto*                                    entry   = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(entry, nullptr);

        const uint64_t delays[] = {60, 120, 240, 480, 480};
        uint64_t       frame    = 100;
        for (const uint64_t delay : delays)
        {
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, frame);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, *entry);
            EXPECT_EQ(entry->NextRetryFrame, frame + delay);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, frame + delay - 1);
            EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123), nullptr);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, frame + delay);
            EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123), entry);
            frame += delay;
        }
    }

    TEST(ThumbnailServiceTest, RepeatedRequestsDeduplicateByUuidAndSourceHash)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        auto*                                    first   = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(first, nullptr);
        first->Texture = {.Index = 7, .Generation = 1};
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 42);

        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123), first);
        EXPECT_EQ(first->LastUseFrame, 42u);
        EXPECT_EQ(first->Texture.Index, 7u);
    }

    TEST(ThumbnailServiceTest, RepeatedInvalidationsRetainPendingDecodeOwnership)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        auto*                                    entry   = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(entry, nullptr);
        entry->Texture = {.Index = 7, .Generation = 1};

        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(service, uuid);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(service, uuid);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InvalidateAll(service);

        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        EXPECT_EQ(entry->Texture.Index, 7u);
        EXPECT_EQ(entry->Texture.Generation, 1u);
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 123));
    }

    TEST(ThumbnailServiceTest, DiscardedDecodeDoesNotBlockAReplacementRequest)
    {
        Tetragrama::Thumbnails::ThumbnailService service   = {};
        const auto                               uuid      = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        auto*                                    old_entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(old_entry, nullptr);
        old_entry->Texture = {.Index = 7, .Generation = 1};
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(service, uuid);

        auto* replacement = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(replacement, nullptr);
        EXPECT_NE(replacement, old_entry);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123), replacement);
    }

    TEST(ThumbnailServiceTest, SourceChangeDiscardsOldWorkAndStartsWithFreshRetryState)
    {
        Tetragrama::Thumbnails::ThumbnailService service   = {};
        const auto                               uuid      = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        auto*                                    old_entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 123);
        ASSERT_NE(old_entry, nullptr);
        old_entry->FailureCount = 4;
        old_entry->Texture      = {.Index = 7, .Generation = 1};

        auto* replacement       = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 456);
        ASSERT_NE(replacement, nullptr);
        EXPECT_NE(replacement, old_entry);
        EXPECT_EQ(replacement->SourceHash, 456u);
        EXPECT_EQ(replacement->FailureCount, 0u);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
    }

    TEST(ThumbnailServiceTest, PendingRequestsCannotBeEvictedAtCapacity)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        for (uint32_t index = 0; index < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Capacity(); ++index)
        {
            ASSERT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, index), nullptr);
        }

        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, 9999), nullptr);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 0));
    }

    TEST(ThumbnailServiceTest, FailedEntriesDoNotPermanentlyExhaustCapacity)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        for (uint32_t index = 0; index < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Capacity(); ++index)
        {
            auto* entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, index);
            ASSERT_NE(entry, nullptr);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::MarkFailed(service, *entry);
        }

        EXPECT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, 9999), nullptr);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 9999));
    }

    TEST(ThumbnailServiceTest, SubmissionBudgetResetsEachFrameAndCountsFailedAttempts)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        for (uint32_t frame = 0; frame < 8; ++frame)
        {
            service.Update();
            for (uint32_t attempt = 0; attempt < 1000; ++attempt)
                EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service), attempt < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SubmissionLimit());
            EXPECT_EQ(service.GetStatistics().SubmittedThisFrame, Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SubmissionLimit());
        }
        EXPECT_EQ(service.GetStatistics().BudgetDeferrals, 8u * (1000 - Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SubmissionLimit()));
    }

    TEST(ThumbnailServiceTest, InFlightLimitIncludesDiscardedWorkAndDoesNotResetEachFrame)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        for (uint32_t i = 0; i < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InFlightLimit(); ++i)
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetPendingEntry(service, i, uuid);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InvalidateAll(service);
        for (uint32_t frame = 0; frame < 5; ++frame)
        {
            service.Update();
            EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::TakeBudget(service));
            EXPECT_EQ(service.GetStatistics().InFlight, Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InFlightLimit());
        }
    }

    TEST(ThumbnailServiceTest, InvalidatingUnsubmittedWorkFreesItsSlotImmediately)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        for (uint32_t revision = 0; revision < 2048; ++revision)
        {
            ASSERT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, revision), nullptr);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(service, uuid);
            EXPECT_EQ(service.GetStatistics().Queued, 0u);
            EXPECT_EQ(service.GetStatistics().InFlight, 0u);
        }
    }

    TEST(ThumbnailServiceTest, ScrollsOver2048AssetsWithBoundedResidencyAndReimport)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        std::array<uuids::uuid, 2048>            assets  = {};
        for (uint32_t i = 0; i < assets.size(); ++i)
        {
            const uint8_t bytes[16] = {static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8), 0, 0, 0, 0, 0x40, 0, 0x80};
            assets[i]               = uuids::uuid(bytes, bytes + 16);
        }
        EXPECT_EQ(service.GetStatistics().SubmittedThisFrame, 0u);
        EXPECT_EQ(service.GetStatistics().Resident, 0u);
        constexpr uint32_t visible = 24;
        for (uint32_t page = 0; page < 160; ++page)
        {
            service.Update();
            const uint32_t start = (page * 19) % (assets.size() - visible);
            for (uint32_t tile = start; tile < start + visible; ++tile)
            {
                auto* entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, assets[tile], 1);
                ASSERT_NE(entry, nullptr);
                Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetResident(*entry, tile);
            }
            // Touch the current page again: no current-frame handle was evicted.
            for (uint32_t tile = start; tile < start + visible; ++tile)
                EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, assets[tile], 1));
            if (page % 7 == 0)
            {
                Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Invalidate(service, assets[start]);
                ASSERT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, assets[start], 2), nullptr);
                EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, assets[start], 1));
            }
            const auto stats = service.GetStatistics();
            EXPECT_LE(stats.Resident + stats.Queued + stats.Failed + stats.InFlight, 512u);
            EXPECT_LE(stats.ResidentPixelBytes, 32u * 1024 * 1024);
            EXPECT_EQ(stats.InFlight, 0u);
        }
        EXPECT_GT(service.GetStatistics().Evictions, 1024u);
    }

    TEST(ThumbnailServiceTest, EvictionUsesLeastRecentUseAndProtectsCurrentFrameImages)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        for (uint32_t index = 0; index < Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Capacity(); ++index)
        {
            auto* entry = Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, index);
            ASSERT_NE(entry, nullptr);
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetResident(*entry, index);
        }

        // All resident handles were borrowed this frame. Keep them alive for ZUI.
        EXPECT_EQ(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, 9999), nullptr);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetFrame(service, 1);
        ASSERT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::PrepareRequest(service, uuid, 0), nullptr);

        EXPECT_NE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::Allocate(service, uuid, 9999), nullptr);
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 0));
        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 1));
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::HasEntry(service, uuid, 2));
    }

    TEST(ThumbnailServiceTest, InvalidationOverflowConservativelyDiscardsAllWork)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               first   = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        const auto                               second  = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetPendingEntry(service, 0, first);
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetPendingEntry(service, 1, second);
        for (uint32_t index = 0; index <= Tetragrama::Thumbnails::ThumbnailServiceTestAccess::InvalidationCapacity(); ++index)
        {
            Tetragrama::Thumbnails::ThumbnailServiceTestAccess::QueueInvalidation(service, first);
        }

        service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 1));
        service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 1));
    }

    TEST(ThumbnailServiceTest, ConcurrentInvalidationProducersNeverMutateTheEntryTable)
    {
        Tetragrama::Thumbnails::ThumbnailService service = {};
        const auto                               uuid    = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
        Tetragrama::Thumbnails::ThumbnailServiceTestAccess::SetPendingEntry(service, 0, uuid);
        auto produce = [&] {
            for (uint32_t index = 0; index < 256; ++index)
            {
                Tetragrama::Thumbnails::ThumbnailServiceTestAccess::QueueInvalidation(service, uuid);
            }
        };
        std::thread first(produce);
        std::thread second(produce);
        first.join();
        second.join();

        EXPECT_FALSE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
        service.Update();
        EXPECT_TRUE(Tetragrama::Thumbnails::ThumbnailServiceTestAccess::IsDiscarded(service, 0));
    }
} // namespace
