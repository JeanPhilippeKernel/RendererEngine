#include <ZEngine/Applications/AppRenderPipeline.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/GeometryPool.h>
#include <ZEngine/Rendering/GeometryStreamingManager.h>
#include <ZEngine/Rendering/RenderResourceManager.h>
#include <ZEngine/Rendering/Renderers/GraphicRenderer.h>
#include <ZEngine/Rendering/Scenes/RenderScene.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <limits>
#include <memory>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "ScenePipelinePreparationTestHost.h"
#include "SceneTextureDescriptorTestHost.h"

using namespace ZEngine::Core::Memory;
using namespace ZEngine::Rendering;

// Test-only helper — friend-declared in RenderResourceManager to allow direct
// slot state manipulation without requiring a live Vulkan device.
struct RRMTestHelper
{
    static bool AcquireSkyFrame(ZEngine::Rendering::Renderers::GraphicRenderer& renderer)
    {
        renderer.m_sky_frame_acquired = renderer.m_sky_environment.AcquireForFrame() != nullptr;
        return renderer.m_sky_frame_acquired;
    }

    static void FinishSkyFrame(ZEngine::Rendering::Renderers::GraphicRenderer& renderer, bool recorded)
    {
        renderer.FinishSkyFrameRecording(recorded);
    }

    static ZEngine::Rendering::Scenes::SkyEnvironment& Sky(ZEngine::Rendering::Renderers::GraphicRenderer& renderer)
    {
        return renderer.m_sky_environment;
    }

    static void InitializeSky(ZEngine::Rendering::Renderers::GraphicRenderer& renderer, ZEngine::Hardwares::VulkanDevice* device, ZEngine::Tests::ScenePipelinePreparationTestHost& pipelines, ZEngine::Rendering::Textures::TextureHandle source, const ZEngine::Rendering::EnvironmentLightingResources& lighting)
    {
        renderer.Device                       = device;
        renderer.m_scene_pipeline_preparation = &pipelines;
        renderer.m_sky_environment.Initialize(source, lighting);
    }

    static void ReleaseSkyAfterIdle(ZEngine::Rendering::Renderers::GraphicRenderer& renderer)
    {
        renderer.m_sky_environment.Shutdown();
        renderer.ReleaseSceneEnvironmentPreparation();
        renderer.Device = nullptr;
    }

    static void ExhaustSkyRevisions(ZEngine::Rendering::Renderers::GraphicRenderer& renderer)
    {
        renderer.m_sky_revision_sequence = UINT64_MAX;
    }

    static void SetPipelineHost(ZEngine::Rendering::Renderers::GraphicRenderer& renderer, ZEngine::Rendering::Renderers::IScenePipelinePreparation* host)
    {
        renderer.m_scene_pipeline_preparation = host;
    }

    static void UseLastScenePreparationSerial(RenderResourceManager& resources)
    {
        resources.m_scene_resources_sequence = UINT64_MAX - 1;
    }

    static void BeginSubmittedSceneSky(ZEngine::Rendering::Renderers::GraphicRenderer& renderer, RenderResourceManager& resources)
    {
        RenderResourceManager::SceneEnvironmentPreparation preparation = {};
        renderer.m_scene_sky_lease                                     = resources.ClaimSceneEnvironmentPreparation(&preparation);
        ASSERT_TRUE(renderer.m_scene_sky_lease);
        renderer.m_scene_sky_id       = preparation.Id;
        renderer.m_scene_sky_revision = ++renderer.m_sky_revision_sequence;
        auto& sky                     = renderer.m_sky_environment;
        ASSERT_TRUE(sky.SubmitConfig(preparation.Input.Sky, renderer.m_scene_sky_revision, {}, preparation.Input.CelestialLight, 1, true, ZEngine::Rendering::Scenes::SkyEnvironmentPublication::Explicit));
        ZEngine::Rendering::Scenes::SkyEnvironmentBakeRequest request = {};
        ASSERT_TRUE(sky.TakeBakeRequest(request));
        ASSERT_TRUE(sky.AttachBakeResource(request.Revision, {20, 1}));
        ASSERT_TRUE(sky.AttachBakeLighting(
            request.Revision,
            {
            {30, 1},
            {31, 1},
            {12, 1}
        }));
        ASSERT_TRUE(sky.BeginGpuBake(request.Revision));
        ASSERT_TRUE(sky.NotifyGpuBakeStageRecorded(request.Revision, sky.GetActiveBakeStage()));
        ASSERT_TRUE(sky.MarkGpuBakeStageSubmitted(request.Revision, 50));
    }

    static constexpr uint32_t RegistrationCapacity()
    {
        return RenderResourceManager::MAX_UUID_MAP;
    }

    static void SetSlot(RenderResourceManager& rrm, uint32_t idx, uint32_t gen, RenderResourceManager::StreamingState state, bool referenced, bool pinned, const GeometryRegion& region = {})
    {
        if (idx >= RenderResourceManager::MAX_BUFFERS)
        {
            return;
        }
        rrm.m_mesh_slots[idx].Generation      = gen;
        rrm.m_mesh_slots[idx].Data            = {};
        rrm.m_mesh_slots[idx].Data.State      = state;
        rrm.m_mesh_slots[idx].Data.Referenced = referenced;
        rrm.m_mesh_slots[idx].Data.Pinned     = pinned;
        rrm.m_mesh_slots[idx].Data.Region     = region;
        rrm.m_mesh_slots[idx].Data.Version    = rrm.NextMeshGeometryVersion();
        if (idx >= rrm.m_mesh_slot_count)
        {
            rrm.m_mesh_slot_count = idx + 1;
        }
    }

    static RenderResourceManager::StreamingState GetState(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.State;
    }

    static bool GetReferenced(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.Referenced;
    }

    static const GeometryRegion& GetRegion(const RenderResourceManager& rrm, uint32_t idx)
    {
        return rrm.m_mesh_slots[idx].Data.Region;
    }

    static void SetupPool(RenderResourceManager& rrm, ArenaAllocator* arena, VkDeviceSize vtx_cap, VkDeviceSize idx_cap, uint32_t max_free_nodes)
    {
        rrm.m_pool.Initialize(arena, vtx_cap, idx_cap, max_free_nodes);
    }

    static GeometryPool& GetPool(RenderResourceManager& rrm)
    {
        return rrm.m_pool;
    }

    static void RegisterMesh(RenderResourceManager& rrm, const uuids::uuid& uuid, BufferHandle handle)
    {
        std::lock_guard lock(rrm.m_uuid_map_mutex);
        for (uint32_t i = 0; i < rrm.m_uuid_to_buffer_count; ++i)
        {
            if (rrm.m_uuid_to_buffer[i].UUID == uuid)
            {
                rrm.m_uuid_to_buffer[i] = {uuid, handle};
                return;
            }
        }
        ASSERT_LT(rrm.m_uuid_to_buffer_count, RenderResourceManager::MAX_UUID_MAP);
        rrm.m_uuid_to_buffer[rrm.m_uuid_to_buffer_count++] = {uuid, handle};
    }

    static uint32_t PendingMeshReleases(RenderResourceManager& rrm)
    {
        std::lock_guard lock(rrm.m_uuid_map_mutex);
        uint32_t        count = 0;
        for (uint32_t i = 0; i < rrm.m_uuid_to_buffer_count; ++i)
        {
            if (rrm.m_uuid_to_buffer[i].ReleaseRequested)
            {
                ++count;
            }
        }
        return count;
    }

    static void FlushMeshReleases(RenderResourceManager& rrm)
    {
        rrm.FlushPendingMeshReleases();
    }

    static uint32_t SlotGeneration(const RenderResourceManager& rrm, uint32_t index)
    {
        return rrm.m_mesh_slots[index].Generation;
    }

    static void ReplaceGeometry(RenderResourceManager& rrm, uint32_t index, const GeometryRegion& region, uint32_t vertex_count = 6, uint32_t index_count = 3, const RenderResourceManager::MeshDrawMetadata* metadata = nullptr)
    {
        RenderResourceManager::MeshSlot replacement = {};
        replacement.Region                          = region;
        replacement.VtxCount                        = vertex_count;
        replacement.IdxCount                        = index_count;
        replacement.Version                         = rrm.NextMeshGeometryVersion();
        replacement.DrawMetadata                    = metadata;
        rrm.ReplaceMeshGeometry(rrm.m_mesh_slots[index].Data, replacement);
    }

    static const RenderResourceManager::MeshDrawMetadata* CaptureMetadata(TLSFSlab& slab, const ZEngine::Importers::AssetMesh& mesh)
    {
        return RenderResourceManager::CaptureMeshDrawMetadata(slab, mesh);
    }

    static void ReleaseMetadata(const RenderResourceManager::MeshDrawMetadata* metadata)
    {
        RenderResourceManager::ReleaseMeshDrawMetadata(metadata);
    }

    static uint32_t PinnedVersions(const RenderResourceManager& rrm)
    {
        return rrm.m_pinned_mesh_geometry_count;
    }

    static void ResetGeometry(RenderResourceManager& rrm)
    {
        rrm.ResetGeometryBuffersInternal();
    }

    static void CompactGeometry(RenderResourceManager& rrm)
    {
        rrm.RunCompaction();
    }

    static void ExhaustVersionIdentities(RenderResourceManager& rrm)
    {
        rrm.m_mesh_geometry_version = UINT64_MAX;
    }

    static uint64_t NextVersion(RenderResourceManager& rrm)
    {
        return rrm.NextMeshGeometryVersion();
    }

    static void StampGeometry(RenderResourceManager& rrm, uint32_t timeline_index, uint64_t value)
    {
        rrm.StampResourceSubmission(timeline_index, value);
    }

    static void CompleteGeometry(RenderResourceManager& rrm, const std::array<uint64_t, RenderResourceManager::ResourceTimelineCount>& completed)
    {
        for (uint32_t i = 0; i < RenderResourceManager::ResourceTimelineCount; ++i)
        {
            rrm.m_resource_completed[i] = completed[i];
        }
        rrm.CollectFrameResources(rrm.m_resource_completed);
    }

    static uint32_t FrameHeldVersions(const RenderResourceManager& rrm)
    {
        return rrm.m_frame_geometry_count;
    }

    static uint32_t PinReferences(const RenderResourceManager& rrm, uint32_t slot_index)
    {
        const auto index = rrm.m_mesh_slots[slot_index].Data.PinRecord;
        return index == UINT32_MAX ? 0 : rrm.m_pinned_mesh_geometry[index].PinCount;
    }

    static void TickPreparation(RenderResourceManager& rrm, uint64_t completed = 0, bool failed = false)
    {
        rrm.TickSceneResourcePreparations(completed, failed);
    }

    static void SetUploadProgress(RenderResourceManager& rrm, uint64_t recorded, uint64_t accepted, bool open)
    {
        rrm.m_batch_next_value     = recorded;
        rrm.m_batch_accepted_value = accepted;
        rrm.m_batch_mode           = open;
    }

    static uint32_t PreparedPins(const RenderResourceManager& rrm, uint32_t slot)
    {
        return rrm.m_scene_resources[slot].Cursor;
    }

    static void DrainPreparationAfterIdle(RenderResourceManager& rrm)
    {
        rrm.ReleaseSceneResourcePreparationsAfterIdle();
    }

    static void DrainSceneAndFrameResourcesAfterIdle(RenderResourceManager& rrm)
    {
        rrm.ReleaseSceneAndFrameResourcesAfterIdle();
    }

    static uint32_t FrameMaterialCount(const RenderResourceManager& rrm)
    {
        return rrm.m_frame_material_count;
    }

    static void SealPreparation(RenderResourceManager& rrm)
    {
        rrm.m_scene_resources_sealed.store(true);
    }

    static void SetDevice(RenderResourceManager& rrm, ZEngine::Hardwares::VulkanDevice* device)
    {
        rrm.m_device = device;
    }

    static void SetDescriptorHost(RenderResourceManager& rrm, ISceneTextureDescriptorPreparation* host)
    {
        rrm.m_scene_descriptor_preparation = host;
    }

    static void SetRegistry(RenderResourceManager& rrm, ZEngine::Core::VFS::AssetRegistry* registry)
    {
        rrm.m_registry = registry;
        rrm.m_streaming_mgr.Initialize(nullptr, &rrm);
    }

    static bool LoadFailed(const RenderResourceManager& rrm, uint32_t index)
    {
        return rrm.m_mesh_slots[index].Data.LoadFailed;
    }

    static void FailReload(RenderResourceManager& rrm, uint32_t index)
    {
        rrm.m_mesh_slots[index].Data.State      = RenderResourceManager::StreamingState::Unloaded;
        rrm.m_mesh_slots[index].Data.LoadFailed = true;
    }

    static bool QueueReload(RenderResourceManager& rrm, BufferHandle handle)
    {
        return rrm.m_streaming_mgr.RequestLoad({{}, 1, handle});
    }

    static void ResetReloadQueue(RenderResourceManager& rrm)
    {
        rrm.m_streaming_mgr.Deinitialize();
        rrm.m_streaming_mgr.Initialize(nullptr, &rrm);
    }

    static uint32_t AllocateMeshSlot(RenderResourceManager& rrm)
    {
        return rrm.AllocMeshSlot();
    }

    static void ExhaustMeshSlotGeneration(RenderResourceManager& rrm, uint32_t index)
    {
        rrm.m_mesh_slot_gen_counter[index] = RenderResourceManager::GBUF_GEN_TAG - 1;
    }

    static void SetUnsubmittedTextureWork(RenderResourceManager& rrm, bool pending)
    {
        if (pending)
        {
            rrm.m_async_uploads.Enqueue({});
        }
        else
        {
            rrm.m_async_uploads.Clear();
        }
    }

    static void FailTextureDecode(RenderResourceManager& rrm, ZEngine::Rendering::Textures::TextureHandle texture)
    {
        ASSERT_TRUE(rrm.TrackTextureDecode(texture));
        rrm.PublishTextureDecodeCompletion(texture, false);
    }

    static bool BeginTextureDecode(RenderResourceManager& rrm, ZEngine::Rendering::Textures::TextureHandle texture)
    {
        return rrm.TrackTextureDecode(texture);
    }

    static uint32_t PreparedMaterials(const RenderResourceManager& rrm, uint32_t slot)
    {
        return rrm.m_scene_resources[slot].MaterialCount;
    }
    static uint32_t PreparedDraws(const RenderResourceManager& rrm, uint32_t slot)
    {
        return rrm.m_scene_resources[slot].InitialDrawCount;
    }

    static uint32_t TextureReferences(const RenderResourceManager& rrm)
    {
        return rrm.m_texture_version_pin_count;
    }

    static void ReleaseFrameResourcesAfterIdle(RenderResourceManager& rrm)
    {
        rrm.ReleaseFrameResourcesAfterIdle();
    }
    static uint32_t PreparationReaders(const RenderResourceManager& rrm, uint32_t slot)
    {
        return rrm.m_scene_resources[slot].Readers.load(std::memory_order_acquire);
    }
};

namespace
{
    constexpr uint64_t ARENA_SIZE = 512 * 1024;

    uuids::uuid        MeshUUID(uint32_t value = 1)
    {
        std::array<uint8_t, 16> bytes = {};
        for (uint32_t i = 0; i < sizeof(value); ++i)
        {
            bytes[i] = static_cast<uint8_t>(value >> (i * 8));
        }
        return uuids::uuid(bytes);
    }

    struct StreamingFixture
    {
        ArenaAllocator                         arena{};
        std::unique_ptr<RenderResourceManager> rrm_storage{};
        RenderResourceManager&                 rrm;
        GeometryStreamingManager               mgr{};

        StreamingFixture() : rrm_storage(std::make_unique<RenderResourceManager>()), rrm(*rrm_storage)
        {
            arena.Initialize(ARENA_SIZE, {});
            RRMTestHelper::SetupPool(rrm, &arena, 4096, 4096, 256);
            mgr.Initialize(nullptr, &rrm);
        }

        ~StreamingFixture()
        {
            mgr.Deinitialize();
            rrm_storage.reset();
            arena.Shutdown();
        }
    };

    size_t AvailableMetadataBlocks(TLSFSlab& slab)
    {
        const ZEngine::Importers::AssetMesh                         mesh   = {};
        std::vector<const RenderResourceManager::MeshDrawMetadata*> blocks = {};
        while (const auto* block = RRMTestHelper::CaptureMetadata(slab, mesh))
        {
            blocks.push_back(block);
        }
        for (const auto* block : blocks)
        {
            RRMTestHelper::ReleaseMetadata(block);
        }
        return blocks.size();
    }
} // namespace

class SceneResourceBundleTest : public ::testing::Test
{
protected:
    using RRM                                                        = RenderResourceManager;
    using Assets                                                     = ZEngine::Managers::AssetManager;
    MemoryManager                                     Memory         = {};
    std::unique_ptr<StreamingFixture>                 Fixture        = {};
    std::unique_ptr<ZEngine::Hardwares::VulkanDevice> Device         = {};
    ZEngine::Tests::SceneTextureDescriptorTestHost    DescriptorHost = {};

    void                                              SetUp() override
    {
        ASSERT_EQ(Assets::Instance(), nullptr);
        Memory.Initialize(512 * 1024 * 1024, {});
        Assets::Initialize(&Memory.MainArena, nullptr, "/project");
        Fixture                  = std::make_unique<StreamingFixture>();
        Device                   = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
        Device->MaxGlobalTexture = 8;
        Device->GlobalTextures.Initialize(&Memory.MainArena, Device->MaxGlobalTexture);
        Device->ImageBufferManager.Initialize(&Memory.MainArena, Device->MaxGlobalTexture);
        RRMTestHelper::SetDevice(Fixture->rrm, Device.get());
        RRMTestHelper::SetDescriptorHost(Fixture->rrm, &DescriptorHost);
        Device->RRM = &Fixture->rrm;
    }

    void TearDown() override
    {
        if (Fixture)
        {
            RRMTestHelper::DrainSceneAndFrameResourcesAfterIdle(Fixture->rrm);
            EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
            RRMTestHelper::ResetGeometry(Fixture->rrm);
            RRMTestHelper::SetDevice(Fixture->rrm, nullptr);
            Fixture.reset();
        }
        Device.reset();
        Assets::Shutdown();
        Memory.Shutdown();
    }

    void Material(uint32_t value, uint32_t map = UINT32_MAX)
    {
        ZEngine::Importers::AssetMaterial material = {};
        material.MaterialUUID                      = MeshUUID(value);
        material.AlbedoColor[3]                    = 0.25f;
        Assets::IngestMaterial(std::move(material));
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        const auto      slot                     = *assets->UUIDToMaterialSlot.find(MeshUUID(value));
        assets->GPUMeshMaterials[slot].AlbedoMap = map;
        if (map != UINT32_MAX)
        {
            const auto identity                   = MeshUUID(0x10000000u + map);
            assets->Materials[slot].AlbedoTexUUID = identity;
            assets->UUIDToTextureHandle.insert(identity, Device->GlobalTextures.ToHandle(map));
        }
    }

    ZEngine::Rendering::Textures::TextureHandle Texture()
    {
        const auto handle                                                                    = Device->GlobalTextures.Create();
        auto*      texture                                                                   = Device->GlobalTextures.Access(handle);
        texture->BufferHandle                                                                = Device->ImageBufferManager.Create();
        Device->ImageBufferManager.Access(texture->BufferHandle)->GetBuffer().OwnsAllocation = true;
        return handle;
    }

    void Binding(uint32_t material, uint32_t map, const uuids::uuid& identity, uint32_t index)
    {
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        const auto      slot      = *assets->UUIDToMaterialSlot.find(MeshUUID(material));
        auto&           source    = assets->Materials[slot];
        auto&           gpu       = assets->GPUMeshMaterials[slot];
        uuids::uuid*    ids[]     = {&source.AlbedoTexUUID, &source.EmissiveTexUUID, &source.NormalTexUUID, &source.OpacityTexUUID, &source.SpecularTexUUID};
        uint32_t*       indices[] = {&gpu.AlbedoMap, &gpu.EmissiveMap, &gpu.NormalMap, &gpu.OpacityMap, &gpu.SpecularMap};
        *ids[map]                 = identity;
        *indices[map]             = index;
    }

    void Mesh(uint32_t slot, uint32_t uuid, std::span<const uuids::uuid> materials, bool drawable = false)
    {
        ZEngine::Importers::AssetMesh mesh = {};
        if (drawable)
        {
            mesh.Vertices.init(&Memory.MainArena, 8, 8);
            mesh.Indices.init(&Memory.MainArena, 1, 1);
            mesh.Indices[0] = 0;
        }
        mesh.SubMeshes.init(&Memory.MainArena, std::max<size_t>(materials.size(), 1));
        for (const auto& material : materials)
        {
            mesh.SubMeshes.push({.MaterialUUID = material, .VertexCount = drawable ? 1u : 0u, .IndexCount = drawable ? 1u : 0u});
        }
        const auto* metadata = RRMTestHelper::CaptureMetadata(Assets::Instance()->ContainerSlab, mesh);
        ASSERT_NE(metadata, nullptr);
        if (RRMTestHelper::SlotGeneration(Fixture->rrm, slot) == 0)
        {
            RRMTestHelper::SetSlot(Fixture->rrm, slot, 1, RRM::StreamingState::Resident, false, false);
        }
        GeometryRegion region = {};
        ASSERT_TRUE(RRMTestHelper::GetPool(Fixture->rrm).Allocate(32, 4, region));
        RRMTestHelper::ReplaceGeometry(Fixture->rrm, slot, region, drawable ? 1u : 0u, drawable ? 1u : 0u, metadata);
        RRMTestHelper::RegisterMesh(Fixture->rrm, MeshUUID(uuid), {slot, 1});
    }

    ZEngine::Rendering::Textures::TextureHandle TexturedMesh()
    {
        Material(100);
        const auto texture  = Texture();
        const auto identity = MeshUUID(200);
        Assets::Instance()->UUIDToTextureHandle.insert(identity, texture);
        Binding(100, 0, identity, static_cast<uint32_t>(texture.Index));
        Mesh(0, 1, std::array{MeshUUID(100)});
        return texture;
    }

    RRM::SceneResourcePreparationId Prepare(std::span<const uuids::uuid> meshes, uint64_t sequence = 1)
    {
        RRM::SceneResourcePreparationId id = {};
        EXPECT_TRUE(Fixture->rrm.PrepareSceneResources({1, sequence}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials));
        return id;
    }

    RRM::SceneResourcePreparationResult Poll(RRM::SceneResourcePreparationId id)
    {
        RRM::SceneResourcePreparationResult result = {};
        EXPECT_TRUE(Fixture->rrm.PollSceneResourcePreparation(id, &result));
        return result;
    }
};

TEST_F(SceneResourceBundleTest, InitialFrameCopiesSceneInputAndExpandsOnlyRetainedResourceVersions)
{
    Material(100);
    Material(101);
    {
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        assets->GPUMeshMaterials[*assets->UUIDToMaterialSlot.find(MeshUUID(100))].AlphaMode = Materials::AlphaMode::Opaque;
        assets->GPUMeshMaterials[*assets->UUIDToMaterialSlot.find(MeshUUID(101))].AlphaMode = Materials::AlphaMode::Blend;
    }
    Mesh(0, 1, std::array{MeshUUID(100), MeshUUID(101)}, true);
    std::array<Scenes::MeshInstance, 2> instances = {};
    for (uint32_t index = 0; index < instances.size(); ++index)
    {
        instances[index].MeshUUID        = MeshUUID(1);
        instances[index].Entity          = {index + 1, 7};
        instances[index].Transform       = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        instances[index].Transform(0, 3) = 10.f + index;
    }
    RRM::SceneInitialFrameInput input         = {.Instances = instances};
    input.Lights.PointCount                   = 1;
    input.Grid.CellSize                       = 2.f;
    input.View                                = {{}, 640, 480, 7, false, true};
    input.View.Camera.Position.x              = 12.f;
    RRM::SceneResourcePreparationId id        = {};
    auto&                           resources = Fixture->rrm;
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, std::array{MeshUUID(1)}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    instances[0].Transform(0, 3) = 900.f;
    instances[1].MeshUUID        = MeshUUID(999);
    input.Lights.PointCount      = 0;
    input.Grid.CellSize          = 9.f;
    input.View.Width             = 1920;
    input.View.Camera.Position.x = 99.f;
    RRMTestHelper::TickPreparation(resources);
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_TRUE(Poll(id).InitialDrawsPrepared);
    EXPECT_EQ(Poll(id).InitialInstanceCount, 2u);
    EXPECT_EQ(Poll(id).InitialDrawCount, 4u);
    Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(resources.GetPreparedSceneFrame(id, &frame));
    ASSERT_EQ(frame.Transforms.size(), 2u);
    ASSERT_EQ(frame.Draws.size(), 4u);
    EXPECT_FLOAT_EQ(frame.Transforms[0](0, 3), 10.f);
    EXPECT_FLOAT_EQ(frame.Draws[0].WorldBounds.x, 10.f);
    EXPECT_FLOAT_EQ(frame.Draws[0].WorldBounds.w, -1.f);
    EXPECT_EQ(frame.Draws[0].Entity, (ZEngine::ECS::EntityID{1, 7}));
    EXPECT_EQ(frame.Draws[2].Allocation.TransformId, 1u);
    EXPECT_FALSE(frame.Draws[0].Transparent);
    EXPECT_TRUE(frame.Draws[1].Transparent);
    EXPECT_EQ(frame.Draws[0].Allocation.PickToken, 1u);
    EXPECT_EQ(frame.Draws[3].Allocation.PickToken, 4u);
    EXPECT_EQ(frame.Lights.PointCount, 1u);
    EXPECT_FLOAT_EQ(frame.Grid.CellSize, 2.f);
    EXPECT_TRUE(frame.View.Captured);
    EXPECT_EQ(frame.View.Width, 640u);
    EXPECT_EQ(frame.View.Height, 480u);
    EXPECT_EQ(frame.View.ResizeSequence, 7u);
    EXPECT_FALSE(frame.View.ShowGrid);
    EXPECT_FLOAT_EQ(frame.View.Camera.Position.x, 12.f);
    const auto original_vertex_offset = frame.Draws[0].Allocation.VertexOffset;
    Mesh(0, 1, std::array{MeshUUID(100)}, true);
    {
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        assets->GPUMeshMaterials[*assets->UUIDToMaterialSlot.find(MeshUUID(101))].AlphaMode = Materials::AlphaMode::Opaque;
    }
    ASSERT_TRUE(resources.GetFramePreparedSceneFrame(id, &frame));
    EXPECT_EQ(frame.Draws[0].Allocation.VertexOffset, original_vertex_offset);
    EXPECT_TRUE(frame.Draws[1].Transparent);
    RRMTestHelper::StampGeometry(resources, 0, 4);
    RRMTestHelper::StampGeometry(resources, 1, 7);
    resources.FinishResourceFrame();
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(resources);
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(resources), 1u);
    RRMTestHelper::CompleteGeometry(resources, {4, 6, 0, 0});
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(resources), 1u);
    RRMTestHelper::CompleteGeometry(resources, {4, 7, 0, 0});
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(resources), 0u);
    EXPECT_FALSE(resources.GetPreparedSceneFrame(id, &frame));
}

TEST_F(SceneResourceBundleTest, InitialDrawExpansionIsSlicedAndRejectsActualDrawOverflowBeforeReadiness)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100), MeshUUID(100)}, true);
    std::vector<Scenes::MeshInstance> instances(RRM::SceneDrawCapacity);
    for (auto& instance : instances)
    {
        instance.MeshUUID = MeshUUID(1);
    }
    RRM::SceneInitialFrameInput     input     = {.Instances = instances};
    RRM::SceneResourcePreparationId id        = {};
    auto&                           resources = Fixture->rrm;
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, std::array{MeshUUID(1)}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    for (uint32_t slice = 0; slice < RRM::SceneDrawCapacity; ++slice)
    {
        const auto before = RRMTestHelper::PreparedDraws(resources, id.Slot);
        RRMTestHelper::TickPreparation(resources);
        EXPECT_LE(RRMTestHelper::PreparedDraws(resources, id.Slot) - before, RRM::SceneResourceSliceOperations);
        const auto result = Poll(id);
        EXPECT_FALSE(result.InitialDrawsPrepared);
        if (result.State == RRM::SceneResourcePreparationState::Failed)
        {
            EXPECT_EQ(result.Error, RRM::SceneResourcePreparationError::DrawCapacity);
            EXPECT_EQ(RRMTestHelper::PreparedDraws(resources, id.Slot), RRM::SceneDrawCapacity);
            EXPECT_FALSE(resources.AcquireSceneResourceLease(id, 2));
            Scenes::PreparedSceneFrame frame = {};
            EXPECT_FALSE(resources.GetPreparedSceneFrame(id, &frame));
            return;
        }
        ASSERT_EQ(result.State, RRM::SceneResourcePreparationState::Preparing);
    }
    FAIL() << "Initial draw expansion never reached its bounded capacity failure.";
}

TEST_F(SceneResourceBundleTest, ExactDrawCapacityPreparesEveryInstanceAndRemainsUsableAfterPlusOneRefusal)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100)}, true);
    std::vector<Scenes::MeshInstance> instances(RRM::SceneDrawCapacity + 1);
    for (uint32_t index = 0; index < instances.size(); ++index)
    {
        instances[index].MeshUUID  = MeshUUID(1);
        instances[index].Entity    = {index + 1, 7};
        instances[index].Transform = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    }
    auto&                           resources = Fixture->rrm;
    RRM::SceneInitialFrameInput     input     = {.Instances = instances};
    RRM::SceneResourcePreparationId id        = {};
    const std::array                meshes{MeshUUID(1)};
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_FALSE(id);
    input.Instances = std::span<const Scenes::MeshInstance>(instances.data(), RRM::SceneDrawCapacity);
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input, true));
    for (uint32_t offset = 0; offset < RRM::SceneDrawCapacity; offset += 64)
    {
        EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
        ASSERT_TRUE(resources.AppendSceneResourceInstances(id, input.Instances.subspan(offset, 64)));
    }
    bool ready = false;
    for (uint32_t slice = 0; slice < RRM::SceneDrawCapacity; ++slice)
    {
        const auto before = RRMTestHelper::PreparedDraws(resources, id.Slot);
        RRMTestHelper::TickPreparation(resources);
        EXPECT_LE(RRMTestHelper::PreparedDraws(resources, id.Slot) - before, RRM::SceneResourceSliceOperations);
        const auto result = Poll(id);
        ASSERT_NE(result.State, RRM::SceneResourcePreparationState::Failed);
        if (result.State == RRM::SceneResourcePreparationState::ResourcesRetained)
        {
            ready = true;
            break;
        }
    }
    ASSERT_TRUE(ready);
    Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(resources.GetPreparedSceneFrame(id, &frame));
    ASSERT_EQ(frame.Draws.size(), RRM::SceneDrawCapacity);
    ASSERT_EQ(frame.Transforms.size(), RRM::SceneDrawCapacity);
    for (uint32_t index = 0; index < frame.Draws.size(); ++index)
    {
        EXPECT_EQ(frame.Draws[index].Entity, instances[index].Entity);
        EXPECT_EQ(frame.Draws[index].Allocation.TransformId, index);
    }
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(resources);
    EXPECT_EQ(RRMTestHelper::PreparationReaders(resources, id.Slot), 0u);
}

TEST_F(SceneResourceBundleTest, DeferredInstanceInputIsBoundedInvisibleUntilCompleteAndOwnsItsCopies)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100)}, true);
    std::array<Scenes::MeshInstance, 65> instances = {};
    for (uint32_t index = 0; index < instances.size(); ++index)
    {
        instances[index].MeshUUID        = MeshUUID(1);
        instances[index].Entity          = {index + 1, 7};
        instances[index].Transform       = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
        instances[index].Transform(0, 3) = static_cast<float>(index);
    }
    auto&                           resources = Fixture->rrm;
    const std::array                meshes{MeshUUID(1)};
    RRM::SceneInitialFrameInput     input = {.Instances = instances};
    RRM::SceneResourcePreparationId id    = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input, true));
    ASSERT_TRUE(resources.AppendSceneResourceInstances(id, std::span(instances).first(64)));
    for (uint32_t pump = 0; pump < 8; ++pump)
    {
        RRMTestHelper::TickPreparation(resources);
        EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
        EXPECT_EQ(RRMTestHelper::PreparedDraws(resources, id.Slot), 0u);
        EXPECT_FALSE(resources.AcquireSceneResourceLease(id, 2));
    }
    instances[0].Transform(0, 3) = 999.f;
    ASSERT_TRUE(resources.AppendSceneResourceInstances(id, std::span(instances).last(1)));
    EXPECT_FALSE(resources.AppendSceneResourceInstances(id, std::span(instances).last(1)));
    for (uint32_t pump = 0; pump < 100; ++pump)
    {
        RRMTestHelper::TickPreparation(resources);
        if (Poll(id).State == RRM::SceneResourcePreparationState::ResourcesRetained)
        {
            break;
        }
    }
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    Scenes::PreparedSceneFrame frame = {};
    ASSERT_TRUE(resources.GetPreparedSceneFrame(id, &frame));
    ASSERT_EQ(frame.Draws.size(), instances.size());
    EXPECT_FLOAT_EQ(frame.Transforms[0](0, 3), 0.f);
    EXPECT_FLOAT_EQ(frame.Transforms[64](0, 3), 64.f);
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(resources);
}

TEST_F(SceneResourceBundleTest, DeferredInstanceInputRejectsInvalidAndOversizedBatchesWithoutPartialReadiness)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100)}, true);
    std::array<Scenes::MeshInstance, 65> instances = {};
    for (auto& instance : instances)
    {
        instance.MeshUUID  = MeshUUID(1);
        instance.Transform = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    }
    auto&                       resources = Fixture->rrm;
    const std::array            meshes{MeshUUID(1)};
    RRM::SceneInitialFrameInput input = {.Instances = instances};
    for (uint32_t scenario = 0; scenario < 4; ++scenario)
    {
        RRM::SceneResourcePreparationId id = {};
        ASSERT_TRUE(resources.PrepareSceneResources({1, scenario + 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input, true));
        auto batch = std::span<const Scenes::MeshInstance>(instances).first(1);
        if (scenario == 0)
        {
            batch = instances;
        }
        else if (scenario == 1)
        {
            instances[0].Transform(0, 0) = std::numeric_limits<float>::quiet_NaN();
        }
        else if (scenario == 2)
        {
            instances[0].MeshUUID = MeshUUID(999);
        }
        else
        {
            batch = {};
        }
        EXPECT_FALSE(resources.AppendSceneResourceInstances(id, batch));
        EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::InvalidInitialFrame);
        EXPECT_FALSE(resources.AcquireSceneResourceLease(id, 2));
        ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(resources);
        EXPECT_FALSE(resources.AppendSceneResourceInstances(id, std::span(instances).first(1)));
        instances[0].MeshUUID  = MeshUUID(1);
        instances[0].Transform = ZEngine::Core::Maths::Identity<ZEngine::Core::Maths::Mat4f>();
    }
}

TEST_F(SceneResourceBundleTest, DeferredInstanceCancellationReclaimsCapacityAndRejectsStaleAppends)
{
    auto&                               resources = Fixture->rrm;
    std::array<Scenes::MeshInstance, 1> instances = {};
    const std::array                    meshes{MeshUUID(1)};
    RRM::SceneInitialFrameInput         input = {.Instances = instances};
    RRM::SceneResourcePreparationId     old   = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, meshes, &old, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input, true));
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(old));
    RRMTestHelper::TickPreparation(resources);
    RRM::SceneResourcePreparationId next = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 2}, 1, meshes, &next, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input, true));
    EXPECT_EQ(next.Slot, old.Slot);
    EXPECT_FALSE(resources.AppendSceneResourceInstances(old, instances));
    EXPECT_EQ(Poll(next).State, RRM::SceneResourcePreparationState::Preparing);
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(next));
    RRMTestHelper::TickPreparation(resources);
}

TEST_F(SceneResourceBundleTest, InitialFrameAdmissionRefusesInvalidInputsWithoutClaimingARequest)
{
    auto&                               resources = Fixture->rrm;
    std::array<Scenes::MeshInstance, 1> instances = {};
    instances[0].MeshUUID                         = MeshUUID(2);
    RRM::SceneInitialFrameInput     input         = {.Instances = instances};
    RRM::SceneResourcePreparationId id            = {};
    const std::array                meshes{MeshUUID(1)};
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    instances[0].MeshUUID        = MeshUUID(1);
    instances[0].Transform(0, 0) = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    instances[0].Transform(0, 0) = 1.f;
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::GeometryOnly, nullptr, &input));
    input.Lights.PointCount = 9;
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    input.Lights.PointCount = 0;
    std::vector<Scenes::MeshInstance> excessive(RRM::SceneDrawCapacity + 1);
    input.Instances = excessive;
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, meshes, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_FALSE(id);
    input.Instances = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_EQ(id.Serial, 1u);
}

TEST_F(SceneResourceBundleTest, InitialViewOverridesCoalescedMailboxUntilFirstRecordingWithoutLosingNewResize)
{
    auto&                       resources = Fixture->rrm;
    RRM::SceneInitialFrameInput input     = {};
    input.View                            = {{}, 640, 480, 7, false, true};
    input.View.Camera.Position.x          = 12.f;
    RRM::SceneResourcePreparationId id    = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    ZEngine::Applications::AppRenderPipeline pipeline = {};
    pipeline.Device                                   = Device.get();
    ZEngine::Applications::RenderFrameState latest    = {};
    latest.SceneResources                             = id;
    latest.RenderTargetW                              = 1920;
    latest.RenderTargetH                              = 1080;
    latest.ResizeSequence                             = 8;
    latest.Camera.Position.x                          = 99.f;
    latest.ShowGrid                                   = true;
    auto effective                                    = latest;
    EXPECT_FALSE(pipeline.ResolvePreparedSceneView(effective)); // Not ready yet.
    EXPECT_EQ(effective.ResizeSequence, 8u);
    RRMTestHelper::TickPreparation(resources);
    auto lease = resources.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(lease);
    ASSERT_TRUE(pipeline.ResolvePreparedSceneView(effective));
    EXPECT_EQ(effective.RenderTargetW, 640u);
    EXPECT_EQ(effective.RenderTargetH, 480u);
    EXPECT_EQ(effective.ResizeSequence, 7u);
    EXPECT_FLOAT_EQ(effective.Camera.Position.x, 12.f);
    EXPECT_FALSE(effective.ShowGrid);
    EXPECT_TRUE(pipeline.ResolvePreparedSceneView(effective)); // Failed acquire/retry.
    EXPECT_EQ(latest.ResizeSequence, 8u);
    EXPECT_FLOAT_EQ(latest.Camera.Position.x, 99.f);
    resources.NotifyPreparedSceneFrameRecorded(id);
    effective = latest;
    EXPECT_FALSE(pipeline.ResolvePreparedSceneView(effective));
    EXPECT_EQ(effective.RenderTargetW, 1920u);
    EXPECT_EQ(effective.RenderTargetH, 1080u);
    EXPECT_EQ(effective.ResizeSequence, 8u);
    EXPECT_FLOAT_EQ(effective.Camera.Position.x, 99.f);
    EXPECT_TRUE(effective.ShowGrid);
}

TEST_F(SceneResourceBundleTest, HeadlessInitialFrameDoesNotOverridePresentation)
{
    auto&                           resources = Fixture->rrm;
    RRM::SceneInitialFrameInput     input     = {};
    RRM::SceneResourcePreparationId id        = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    RRMTestHelper::TickPreparation(resources);
    ZEngine::Applications::AppRenderPipeline pipeline = {};
    pipeline.Device                                   = Device.get();
    ZEngine::Applications::RenderFrameState state     = {};
    state.SceneResources                              = id;
    state.RenderTargetW                               = 1920;
    state.RenderTargetH                               = 1080;
    state.ResizeSequence                              = 8;
    EXPECT_FALSE(pipeline.ResolvePreparedSceneView(state));
    EXPECT_EQ(state.RenderTargetW, 1920u);
    EXPECT_EQ(state.RenderTargetH, 1080u);
    EXPECT_EQ(state.ResizeSequence, 8u);
}

TEST_F(SceneResourceBundleTest, InitialViewAdmissionRefusesZeroExtentWithoutConsumingRequest)
{
    auto&                       resources = Fixture->rrm;
    RRM::SceneInitialFrameInput input     = {};
    input.View.Captured                   = true;
    RRM::SceneResourcePreparationId id    = {};
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    input.View.Width = 640;
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    input.View.Height = 480;
    input.View.Width  = 0;
    EXPECT_FALSE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_FALSE(id);
    input.View.Width = 640;
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    EXPECT_EQ(id.Serial, 1u);
}

TEST_F(SceneResourceBundleTest, EmptyInitialFrameIsPreparedAndOnlyFirstRecordingEndsItsUse)
{
    auto&                           resources = Fixture->rrm;
    RRM::SceneInitialFrameInput     input     = {};
    RRM::SceneResourcePreparationId id        = {};
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, {}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    Scenes::PreparedSceneFrame frame = {};
    EXPECT_FALSE(resources.GetPreparedSceneFrame(id, &frame));
    EXPECT_FALSE(resources.GetPreparedSceneFrame(id, nullptr));
    RRMTestHelper::TickPreparation(resources);
    ASSERT_TRUE(resources.GetFramePreparedSceneFrame(id, &frame));
    EXPECT_TRUE(frame.Draws.empty());
    EXPECT_TRUE(frame.Transforms.empty());
    auto lease = resources.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(lease);
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
    ASSERT_TRUE(resources.GetFramePreparedSceneFrame(id, &frame));
    resources.NotifyPreparedSceneFrameRecorded(id);
    EXPECT_FALSE(resources.GetPreparedSceneFrame(id, &frame));
    EXPECT_TRUE(Poll(id).FrameRecorded);
    lease.Reset();
    RRMTestHelper::TickPreparation(resources);
    EXPECT_FALSE(resources.GetFramePreparedSceneFrame(id, &frame));
}

TEST_F(SceneResourceBundleTest, CancellationDuringInitialDrawExpansionReleasesItsResourcePins)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100)}, true);
    std::vector<Scenes::MeshInstance> instances(64);
    for (auto& instance : instances)
    {
        instance.MeshUUID = MeshUUID(1);
    }
    RRM::SceneInitialFrameInput     input     = {.Instances = instances};
    RRM::SceneResourcePreparationId id        = {};
    auto&                           resources = Fixture->rrm;
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, std::array{MeshUUID(1)}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    RRMTestHelper::TickPreparation(resources);
    EXPECT_GT(RRMTestHelper::PreparedDraws(resources, id.Slot), 0u);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    ASSERT_TRUE(resources.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(resources);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(resources.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(RRMTestHelper::PinnedVersions(resources), 0u);
}

TEST_F(SceneResourceBundleTest, InvalidInitialDrawRangeFailsWithoutPublishingAPartialSnapshot)
{
    Material(100);
    Mesh(0, 1, std::array{MeshUUID(100)}); // Deliberately zero-sized geometry.
    std::array<Scenes::MeshInstance, 1> instances = {};
    instances[0].MeshUUID                         = MeshUUID(1);
    RRM::SceneInitialFrameInput     input         = {.Instances = instances};
    RRM::SceneResourcePreparationId id            = {};
    auto&                           resources     = Fixture->rrm;
    ASSERT_TRUE(resources.PrepareSceneResources({1, 1}, 1, std::array{MeshUUID(1)}, &id, RRM::ScenePreparationContent::MeshMaterials, nullptr, &input));
    RRMTestHelper::TickPreparation(resources);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::InvalidInitialFrame);
    EXPECT_FALSE(Poll(id).InitialDrawsPrepared);
    EXPECT_FALSE(resources.AcquireSceneResourceLease(id, 2));
    Scenes::PreparedSceneFrame frame = {};
    frame.Lights.PointCount          = 5;
    EXPECT_FALSE(resources.GetPreparedSceneFrame(id, &frame));
    EXPECT_FALSE(resources.GetFramePreparedSceneFrame(id, &frame));
    EXPECT_EQ(frame.Lights.PointCount, 5u);
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(resources), 0u);
}

class SceneEnvironmentHandoffTest : public SceneResourceBundleTest
{
protected:
    std::unique_ptr<ZEngine::Rendering::Renderers::GraphicRenderer> Renderer     = {};
    ZEngine::Tests::ScenePipelinePreparationTestHost                PipelineHost = {};

    void                                                            SetUp() override
    {
        SceneResourceBundleTest::SetUp();
        Renderer                                                        = std::make_unique<ZEngine::Rendering::Renderers::GraphicRenderer>();
        const auto                                             source   = Texture();
        const ZEngine::Rendering::EnvironmentLightingResources lighting = {Texture(), Texture(), Texture()};
        RRMTestHelper::InitializeSky(*Renderer, Device.get(), PipelineHost, source, lighting);
    }

    void TearDown() override
    {
        if (Renderer)
        {
            RRMTestHelper::ReleaseSkyAfterIdle(*Renderer);
            Renderer.reset();
        }
        SceneResourceBundleTest::TearDown();
    }

    static RRM::SceneEnvironmentInput Sphere(float intensity = 2.0f, uint64_t revision = 2)
    {
        RRM::SceneEnvironmentInput input = {};
        input.Sky.Mode                   = Scenes::SkyMode::SkySphere;
        input.Sky.EnvironmentIntensity   = intensity;
        input.SkyRevision                = revision;
        return input;
    }

    RRM::SceneResourcePreparationId Queue(const RRM::SceneEnvironmentInput& input, uint64_t sequence = 1, uint64_t generation = 1, const RRM::SceneInitialFrameInput* initial = nullptr)
    {
        RRM::SceneResourcePreparationId id = {};
        EXPECT_TRUE(Fixture->rrm.PrepareSceneResources({1, sequence}, generation, {}, &id, RRM::ScenePreparationContent::MeshMaterials, &input, initial));
        RRMTestHelper::TickPreparation(Fixture->rrm);
        return id;
    }

    RRM::SceneResourcePreparationResult Result(RRM::SceneResourcePreparationId id)
    {
        RRM::SceneResourcePreparationResult result = {};
        EXPECT_TRUE(Fixture->rrm.PollSceneResourcePreparation(id, &result));
        return result;
    }
};

TEST_F(SceneEnvironmentHandoffTest, EnvironmentReadinessGatesMailboxAdmissionAndRecordedAcknowledgement)
{
    auto       input               = Sphere();
    const auto id                  = Queue(input);
    input.Sky.EnvironmentIntensity = 9.0f;
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    RRM::SceneEnvironmentPreparation work  = {};
    auto                             owner = Fixture->rrm.ClaimSceneEnvironmentPreparation(&work);
    ASSERT_TRUE(owner);
    EXPECT_FLOAT_EQ(work.Input.Sky.EnvironmentIntensity, 2.0f);
    EXPECT_EQ(work.Id, id);
    EXPECT_EQ(work.State, RRM::SceneEnvironmentPreparationState::Preparing);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    ASSERT_TRUE(Fixture->rrm.CompleteSceneEnvironmentPreparation(id, true));
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    Fixture->rrm.NotifyPreparedSceneFrameRecorded(id);
    EXPECT_FALSE(Result(id).FrameRecorded);
    ASSERT_TRUE(Fixture->rrm.ActivateSceneEnvironmentPreparation(id));
    Fixture->rrm.NotifyPreparedSceneFrameRecorded(id);
    EXPECT_TRUE(Result(id).FrameRecorded);
    EXPECT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    owner.Reset();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Releasing);
    frame.Reset();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult stale = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(id, &stale));
}

TEST_F(SceneEnvironmentHandoffTest, PreparedEnvironmentCannotPublishBeforeRequiredPipelines)
{
    PipelineHost.State  = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Preparing;
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    const auto input = Sphere(4.0f);
    const auto id    = Queue(input);
    Renderer->PollSceneEnvironmentPreparations();
    ASSERT_NE(RRMTestHelper::Sky(*Renderer).GetPreparedSnapshot(RRMTestHelper::Sky(*Renderer).GetLatestRevision()), nullptr);
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    EXPECT_TRUE(PipelineHost.Active);
    EXPECT_EQ(PipelineHost.Usage, ZEngine::Rendering::Renderers::RGScenePipelineUsage::Core | ZEngine::Rendering::Renderers::RGScenePipelineUsage::Sphere);
    PipelineHost.State = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Ready;
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 4u);
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    ASSERT_TRUE(Renderer->ApplySkyConfig(input.Sky, {}, input.SkyRevision, 2, id));
    EXPECT_TRUE(PipelineHost.Active);
    RRMTestHelper::FinishSkyFrame(*Renderer, false);
    EXPECT_TRUE(PipelineHost.Active);
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 4u);
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
    EXPECT_FALSE(PipelineHost.Active);
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
    EXPECT_EQ(PipelineHost.EndCalls, 1u);
}

TEST_F(SceneEnvironmentHandoffTest, InitialFrameDescriptorPreparationGatesPublicationAndActivation)
{
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    PipelineHost.DescriptorState        = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Preparing;
    RRM::SceneInitialFrameInput initial = {};
    const auto                  input   = Sphere(4.0f);
    const auto                  id      = Queue(input, 1, 1, &initial);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_GT(PipelineHost.DescriptorPollCalls, 0u);
    EXPECT_TRUE(PipelineHost.EnvironmentSourcesValid);
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    PipelineHost.DescriptorState = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Ready;
    Renderer->PollSceneEnvironmentPreparations();
    ASSERT_EQ(Result(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    PipelineHost.ActivateDescriptors = false;
    EXPECT_FALSE(Renderer->ApplySkyConfig(input.Sky, {}, input.SkyRevision, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    PipelineHost.ActivateDescriptors = true;
    ASSERT_TRUE(Renderer->ApplySkyConfig(input.Sky, {}, input.SkyRevision, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), input.Sky);
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
}

TEST_F(SceneEnvironmentHandoffTest, FailedInitialFrameDescriptorsPreserveOutgoingAndReleasePreparation)
{
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    RRM::SceneInitialFrameInput initial = {};
    for (uint64_t attempt = 1; attempt <= 2; ++attempt)
    {
        SCOPED_TRACE(attempt);
        PipelineHost.DescriptorState = attempt == 1 ? ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Failed : ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Idle;
        const auto id                = Queue(Sphere(4.0f), attempt, 1, &initial);
        Renderer->PollSceneEnvironmentPreparations();
        EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredDescriptor);
        EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
        EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
        EXPECT_FALSE(PipelineHost.Active);
        EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
        ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(Fixture->rrm);
    }
}

TEST_F(SceneEnvironmentHandoffTest, EnvironmentTextureRetentionDefersReadinessAndCancellationReleasesAllSources)
{
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    const auto id = Queue(Sphere(4.0f));
    RRMTestHelper::SetUnsubmittedTextureWork(Fixture->rrm, true);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    RRMTestHelper::SetUnsubmittedTextureWork(Fixture->rrm, false);
    // Refuse the final input after earlier inputs were retained. None of those
    // partial holds may escape a deferred transaction.
    const auto lut = RRMTestHelper::Sky(*Renderer).GetFallbackLighting().BrdfIntegrationLut;
    ASSERT_TRUE(RRMTestHelper::BeginTextureDecode(Fixture->rrm, lut));
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
    Fixture->rrm.ForgetTextureDecode(lut);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 4u);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
}

TEST_F(SceneEnvironmentHandoffTest, InvalidEnvironmentTextureFailsBeforeUnrelatedUploadBackpressure)
{
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    const auto lut                                                = RRMTestHelper::Sky(*Renderer).GetFallbackLighting().BrdfIntegrationLut;
    Device->GlobalTextures.Access(lut)->Specification.IsAliasable = true;
    const auto id                                                 = Queue(Sphere(4.0f));
    RRMTestHelper::SetUnsubmittedTextureWork(Fixture->rrm, true);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredDescriptor);
    EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(RRMTestHelper::TextureReferences(Fixture->rrm), 0u);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    EXPECT_FALSE(PipelineHost.Active);
    RRMTestHelper::SetUnsubmittedTextureWork(Fixture->rrm, false);
}

TEST_F(SceneEnvironmentHandoffTest, PipelineFailureOrUnavailableHostPreservesOutgoingAndReportsItsOwnCause)
{
    const auto outgoing = Sphere(1.0f);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, outgoing.SkyRevision, 1));
    for (uint64_t attempt = 1; attempt <= 4; ++attempt)
    {
        SCOPED_TRACE(attempt);
        PipelineHost.Accept = attempt != 2;
        PipelineHost.State  = attempt == 3 ? ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Idle : ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Failed;
        RRMTestHelper::SetPipelineHost(*Renderer, attempt == 4 ? nullptr : &PipelineHost);
        const auto id = Queue(Sphere(4.0f), attempt);
        Renderer->PollSceneEnvironmentPreparations();
        EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredPipeline);
        EXPECT_FALSE(Fixture->rrm.CompleteSceneEnvironmentPreparation(id, false));
        EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredPipeline);
        EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
        EXPECT_FALSE(PipelineHost.Active);
        EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
        ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(Fixture->rrm);
    }
    RRMTestHelper::SetPipelineHost(*Renderer, &PipelineHost);
}

TEST_F(SceneEnvironmentHandoffTest, CancellationWhilePipelinePreparationIsPendingReleasesItsTransaction)
{
    PipelineHost.State = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Preparing;
    const auto id      = Queue(Sphere());
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_TRUE(PipelineHost.Active);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_FALSE(PipelineHost.Active);
    EXPECT_EQ(PipelineHost.EndCalls, 1u);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult stale = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(id, &stale));
}

TEST_F(SceneEnvironmentHandoffTest, EnvironmentBakeSchedulingWaitsForRequiredPipelinePreparation)
{
    PipelineHost.State               = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Preparing;
    auto input                       = Sphere();
    input.Sky.Mode                   = Scenes::SkyMode::Atmosphere;
    input.CelestialLight.IsAvailable = true;
    const auto id                    = Queue(input);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Preparing);
    // SubmitConfig queues the request without changing the outgoing bake state.
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetState(), Scenes::SkyEnvironmentState::Fallback);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetActiveBake(), nullptr);
    EXPECT_EQ(PipelineHost.Usage, ZEngine::Rendering::Renderers::RGScenePipelineUsage::Core | ZEngine::Rendering::Renderers::RGScenePipelineUsage::Atmosphere | ZEngine::Rendering::Renderers::RGScenePipelineUsage::EnvironmentLighting);
    // This CPU-only host deliberately has no atmosphere GPU capability. Its
    // bake may be attempted (and rejected) only after the pipeline gate opens.
    PipelineHost.State = ZEngine::Rendering::Renderers::RGScenePipelinePreparationState::Ready;
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredEnvironment);
    EXPECT_FALSE(PipelineHost.Active);
}

TEST_F(SceneEnvironmentHandoffTest, RejectedGraphDoesNotPublishSkySubmissionCallbacks)
{
    ZEngine::Hardwares::DeviceSwapchain swapchain = {};
    swapchain.RenderWorkSubmittedCallbacks.init(&Fixture->arena, 4);
    Device->SwapchainPtr = &swapchain;
    ASSERT_TRUE(RRMTestHelper::AcquireSkyFrame(*Renderer));
    RRMTestHelper::FinishSkyFrame(*Renderer, false);
    EXPECT_TRUE(swapchain.RenderWorkSubmittedCallbacks.empty());
    // A rejected frame's pin was cancelled and a later good frame can acquire
    // and release its own pin without inheriting a stale callback.
    ASSERT_TRUE(RRMTestHelper::AcquireSkyFrame(*Renderer));
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
    ASSERT_EQ(swapchain.RenderWorkSubmittedCallbacks.size(), 1u);
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
    EXPECT_EQ(swapchain.RenderWorkSubmittedCallbacks.size(), 1u);
    const auto& callback = swapchain.RenderWorkSubmittedCallbacks[0];
    ASSERT_NE(callback.Cancel, nullptr);
    callback.Cancel(callback.Context);
    swapchain.RenderWorkSubmittedCallbacks.clear();
    Device->SwapchainPtr = nullptr;
}

TEST_F(SceneEnvironmentHandoffTest, CandidateLeavesOutgoingSkyUnchangedUntilItsMailboxFrame)
{
    const auto outgoing = Sphere(1.0f, 500);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, outgoing.CelestialLight, outgoing.SkyRevision, 1));
    const auto candidate = Sphere(4.0f, 2);
    const auto id        = Queue(candidate);
    Renderer->PollSceneEnvironmentPreparations();
    ASSERT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, outgoing.CelestialLight, 501, 1));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    ASSERT_TRUE(Renderer->ApplySkyConfig(candidate.Sky, candidate.CelestialLight, candidate.SkyRevision, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), candidate.Sky);
    EXPECT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Activated);
    Fixture->rrm.NotifyPreparedSceneFrameRecorded(id);
    EXPECT_TRUE(Result(id).FrameRecorded);
}

TEST_F(SceneEnvironmentHandoffTest, CancellationBeforePublicationReleasesTheClaimAndOutgoingEditsCanResume)
{
    const auto outgoing = Sphere(1.0f, 500);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, 500, 1));
    const auto id = Queue(Sphere(4.0f));
    Renderer->PollSceneEnvironmentPreparations();
    ASSERT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Releasing);
    Renderer->PollSceneEnvironmentPreparations();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult stale = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(id, &stale));
    auto changed                     = outgoing;
    changed.Sky.EnvironmentIntensity = 3.0f;
    ASSERT_TRUE(Renderer->ApplySkyConfig(changed.Sky, {}, 501, 1));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), changed.Sky);
    EXPECT_FALSE(Renderer->ApplySkyConfig({}, {}, 2, 2, id));
}

TEST_F(SceneEnvironmentHandoffTest, MailboxRetentionPreventsTheNextCandidateFromSupersedingTheFirstSky)
{
    const auto first    = Sphere(3.0f);
    const auto first_id = Queue(first);
    Renderer->PollSceneEnvironmentPreparations();
    auto first_frame = Fixture->rrm.AcquireSceneResourceLease(first_id, 2);
    ASSERT_TRUE(first_frame);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(first_id));
    const auto second    = Sphere(5.0f);
    const auto second_id = Queue(second, 2, 2);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(first_id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    EXPECT_EQ(Result(second_id).Environment, RRM::SceneEnvironmentPreparationState::Queued);
    ASSERT_TRUE(Renderer->ApplySkyConfig(first.Sky, {}, 2, 2, first_id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), first.Sky);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(second_id).Environment, RRM::SceneEnvironmentPreparationState::Queued);
    RRMTestHelper::FinishSkyFrame(*Renderer, false);
    EXPECT_TRUE(PipelineHost.Active);
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
    EXPECT_FALSE(PipelineHost.Active);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(second_id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    auto second_frame = Fixture->rrm.AcquireSceneResourceLease(second_id, 3);
    ASSERT_TRUE(second_frame);
    ASSERT_TRUE(Renderer->ApplySkyConfig(second.Sky, {}, 2, 3, second_id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), second.Sky);
}

TEST_F(SceneEnvironmentHandoffTest, DroppedMailboxAndCancelledOwnerDoNotLeaveTheSchedulerClaimed)
{
    const auto id = Queue(Sphere());
    Renderer->PollSceneEnvironmentPreparations();
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    frame.Reset();
    Renderer->PollSceneEnvironmentPreparations();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult stale = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(id, &stale));
    const auto next = Queue(Sphere(), 2);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(next).Environment, RRM::SceneEnvironmentPreparationState::Ready);
}

TEST_F(SceneEnvironmentHandoffTest, RequiredEnvironmentFailurePreservesTheVisibleScene)
{
    const auto outgoing = Sphere(1.0f, 100);
    ASSERT_TRUE(Renderer->ApplySkyConfig(outgoing.Sky, {}, 100, 1));
    for (uint64_t sequence = 1; sequence <= 2; ++sequence)
    {
        auto input     = Sphere();
        input.Sky.Mode = sequence == 1 ? Scenes::SkyMode::Atmosphere : Scenes::SkyMode::HDRI;
        const auto id  = Queue(input, sequence);
        Renderer->PollSceneEnvironmentPreparations();
        EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(Result(id).Error, RRM::SceneResourcePreparationError::RequiredEnvironment);
        EXPECT_FALSE(Fixture->rrm.AcquireSceneResourceLease(id, 2));
        EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), outgoing.Sky);
        ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(Fixture->rrm);
    }
}

TEST_F(SceneEnvironmentHandoffTest, WrongSceneGenerationCannotActivateThePreparedEnvironment)
{
    const auto input = Sphere();
    const auto id    = Queue(input);
    Renderer->PollSceneEnvironmentPreparations();
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    EXPECT_FALSE(Renderer->ApplySkyConfig(input.Sky, {}, 2, 3, id));
    EXPECT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    EXPECT_TRUE(Renderer->ApplySkyConfig(input.Sky, {}, 2, 2, id));
    EXPECT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Activated);
}

TEST_F(SceneEnvironmentHandoffTest, FirstFrameUsesThePreparedInputEvenIfAnEditorEditCoalescesTheInitialMailbox)
{
    const auto input = Sphere();
    const auto id    = Queue(input);
    Renderer->PollSceneEnvironmentPreparations();
    auto frame = Fixture->rrm.AcquireSceneResourceLease(id, 2);
    ASSERT_TRUE(frame);
    auto edited                     = input;
    edited.Sky.EnvironmentIntensity = 7.0f;
    ASSERT_TRUE(Renderer->ApplySkyConfig(edited.Sky, {}, 3, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), input.Sky);
    ASSERT_TRUE(Renderer->ApplySkyConfig(edited.Sky, {}, 3, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), input.Sky);
    RRMTestHelper::FinishSkyFrame(*Renderer, false);
    ASSERT_TRUE(Renderer->ApplySkyConfig(edited.Sky, {}, 3, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), input.Sky);
    RRMTestHelper::FinishSkyFrame(*Renderer, true);
    ASSERT_TRUE(Renderer->ApplySkyConfig(edited.Sky, {}, 3, 2, id));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), edited.Sky);
}

TEST_F(SceneEnvironmentHandoffTest, RendererRevisionsDoNotTreatANewScenesLowerAuthoringRevisionAsStale)
{
    const auto first = Sphere(2.0f, 500);
    ASSERT_TRUE(Renderer->ApplySkyConfig(first.Sky, {}, first.SkyRevision, 1));
    const auto second = Sphere(4.0f, 1);
    ASSERT_TRUE(Renderer->ApplySkyConfig(second.Sky, {}, second.SkyRevision, 2));
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetPresentationConfig(), second.Sky);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetLatestRevision(), 2u);
}

TEST_F(SceneEnvironmentHandoffTest, RendererRevisionExhaustionFailsTheCandidateWithoutWrapping)
{
    const auto id = Queue(Sphere());
    RRMTestHelper::ExhaustSkyRevisions(*Renderer);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(RRMTestHelper::Sky(*Renderer).GetLatestRevision(), 0u);
    EXPECT_FALSE(Renderer->ApplySkyConfig(Sphere().Sky, {}, 1, 1));
}

TEST_F(SceneEnvironmentHandoffTest, LastValidRequestSerialCanStillReachTheEnvironmentReadinessGate)
{
    RRMTestHelper::UseLastScenePreparationSerial(Fixture->rrm);
    const auto id = Queue(Sphere());
    ASSERT_EQ(id.Serial, UINT64_MAX);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
    RRM::SceneResourcePreparationId rejected = {};
    const auto                      input    = Sphere();
    EXPECT_FALSE(Fixture->rrm.PrepareSceneResources({1, 2}, 1, {}, &rejected, RRM::ScenePreparationContent::MeshMaterials, &input));
    EXPECT_FALSE(rejected);
}

TEST_F(SceneEnvironmentHandoffTest, ZeroAuthoringRevisionIsRejectedBeforeClaimingRequestStorage)
{
    const auto                      input    = Sphere(1.0f, 0);
    RRM::SceneResourcePreparationId rejected = {};
    EXPECT_FALSE(Fixture->rrm.PrepareSceneResources({1, 1}, 1, {}, &rejected, RRM::ScenePreparationContent::MeshMaterials, &input));
    EXPECT_FALSE(rejected);
    const auto id = Queue(Sphere());
    EXPECT_EQ(id.Serial, 1u);
    Renderer->PollSceneEnvironmentPreparations();
    EXPECT_EQ(Result(id).Environment, RRM::SceneEnvironmentPreparationState::Ready);
}

TEST_F(SceneEnvironmentHandoffTest, CancelledSubmittedBakeRetainsRequestStorageUntilGpuWorkDrains)
{
    auto input     = Sphere();
    input.Sky.Mode = Scenes::SkyMode::HDRI;
    const auto id  = Queue(input);
    RRMTestHelper::BeginSubmittedSceneSky(*Renderer, Fixture->rrm);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(id));
    Renderer->PollSceneEnvironmentPreparations();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Result(id).State, RRM::SceneResourcePreparationState::Releasing);
    auto& sky = RRMTestHelper::Sky(*Renderer);
    ASSERT_NE(sky.GetActiveBake(), nullptr);
    const auto revision = sky.GetActiveBake()->Revision;
    EXPECT_EQ(sky.CompleteBake(revision, {}, false), Scenes::SkyEnvironmentBakeResult::Ignored);
    EXPECT_FALSE(sky.AdvanceCompletedGpuBakeStage(49));
    ASSERT_TRUE(sky.AdvanceCompletedGpuBakeStage(50));
    EXPECT_EQ(sky.CompleteBake(revision, {}, false), Scenes::SkyEnvironmentBakeResult::Discarded);
    Renderer->PollSceneEnvironmentPreparations();
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult stale = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(id, &stale));
}

TEST_F(SceneResourceBundleTest, RequiredPolicyDistinguishesAbsentOptionalMapsFromLostAuthoredBindings)
{
    Material(100);
    auto&                     rrm   = Fixture->rrm;
    RRM::MaterialVersionError error = RRM::MaterialVersionError::MissingMaterial;
    EXPECT_TRUE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::None);
    for (uint32_t map = 0; map < 5; ++map)
    {
        Binding(100, map, MeshUUID(200 + map), UINT32_MAX);
        EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
        EXPECT_EQ(error, RRM::MaterialVersionError::MissingRequiredTexture);
        EXPECT_TRUE(rrm.PinMaterialVersion(MeshUUID(100))); // Ordinary editor compatibility remains explicit.
        EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
        Binding(100, map, {}, UINT32_MAX);
    }
    auto* assets = Assets::Instance();
    assets->Materials[*assets->UUIDToMaterialSlot.find(MeshUUID(100))].NormalTexPath.init(&Memory.MainArena, "/missing.png");
    EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::MissingRequiredTexture);
}

TEST_F(SceneResourceBundleTest, RequiredPolicyRejectsDebugFallbackInEveryMapButKeepsExactTypedBindings)
{
    Material(100);
    const auto real               = Texture();
    const auto fallback           = Texture();
    auto*      assets             = Assets::Instance();
    assets->FallbackTextureHandle = fallback;
    auto& rrm                     = Fixture->rrm;
    for (uint32_t map = 0; map < 5; ++map)
    {
        const auto identity = MeshUUID(200 + map);
        assets->UUIDToTextureHandle.insert(identity, real);
        Binding(100, map, identity, static_cast<uint32_t>(real.Index));
    }
    {
        auto retained = rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings);
        ASSERT_TRUE(retained);
        EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 5u);
        for (uint32_t map = 0; map < 5; ++map)
        {
            EXPECT_EQ(retained.GetTexture(map)->Handle.Index, real.Index);
            EXPECT_EQ(retained.GetTexture(map)->Handle.Generation, real.Generation);
        }
    }
    for (uint32_t map = 0; map < 5; ++map)
    {
        const auto identity = MeshUUID(200 + map);
        assets->UUIDToTextureHandle.insert(identity, fallback);
        Binding(100, map, identity, static_cast<uint32_t>(fallback.Index));
        RRM::MaterialVersionError error = {};
        EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
        EXPECT_EQ(error, RRM::MaterialVersionError::DebugFallback);
        EXPECT_TRUE(rrm.PinMaterialVersion(MeshUUID(100)));
        EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
        assets->UUIDToTextureHandle.insert(identity, real);
        Binding(100, map, identity, static_cast<uint32_t>(real.Index));
    }
}

TEST_F(SceneResourceBundleTest, RequiredPolicyRejectsUntypedAndMismatchedOrRecycledTextureIdentities)
{
    Material(100);
    const auto                first  = Texture();
    const auto                second = Texture();
    auto*                     assets = Assets::Instance();
    auto&                     rrm    = Fixture->rrm;
    RRM::MaterialVersionError error  = {};
    Binding(100, 0, {}, static_cast<uint32_t>(first.Index));
    EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::MissingRequiredTexture);
    const auto identity = MeshUUID(200);
    assets->UUIDToTextureHandle.insert(identity, first);
    Binding(100, 0, identity, static_cast<uint32_t>(second.Index));
    EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::TextureIdentityMismatch);
    auto stale        = first;
    stale.Generation += 2;
    assets->UUIDToTextureHandle.insert(identity, stale);
    Binding(100, 0, identity, static_cast<uint32_t>(first.Index));
    EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100), RRM::MaterialVersionPolicy::RequiredSceneBindings, &error));
    EXPECT_EQ(error, RRM::MaterialVersionError::TextureIdentityMismatch);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u); // No current-generation lease escaped.
}

TEST_F(SceneResourceBundleTest, DescriptorPublicationMustCompleteBeforeMaterialBundleIsReady)
{
    const auto texture   = TexturedMesh();
    auto&      rrm       = Fixture->rrm;
    DescriptorHost.State = SceneTextureDescriptorState::Pending;
    const auto id        = Prepare(std::array{MeshUUID(1)});
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_FALSE(rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_GT(DescriptorHost.Calls, 0u);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
    DescriptorHost.State = SceneTextureDescriptorState::Ready;
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_TRUE(rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(DescriptorHost.Bindings[texture.Index].Texture.Generation, texture.Generation);
    EXPECT_EQ(DescriptorHost.Bindings[texture.Index].Image.Index, Device->GlobalTextures.Access(texture)->BufferHandle.Index);
}

TEST_F(SceneResourceBundleTest, DescriptorFailureOrMissingServiceFailsBeforePublication)
{
    TexturedMesh();
    auto& rrm = Fixture->rrm;
    for (uint64_t sequence = 1; sequence <= 2; ++sequence)
    {
        DescriptorHost.State = SceneTextureDescriptorState::Failed;
        RRMTestHelper::SetDescriptorHost(rrm, sequence == 1 ? &DescriptorHost : nullptr);
        const auto id = Prepare(std::array{MeshUUID(1)}, sequence);
        RRMTestHelper::TickPreparation(rrm);
        EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::RequiredDescriptor);
        EXPECT_FALSE(rrm.AcquireSceneResourceLease(id, 2));
        EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
        ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(rrm);
    }
}

TEST_F(SceneResourceBundleTest, StaleDescriptorProofCannotTransferOrOverwriteFrameOutputs)
{
    TexturedMesh();
    auto&      rrm = Fixture->rrm;
    const auto id  = Prepare(std::array{MeshUUID(1)});
    RRMTestHelper::TickPreparation(rrm);
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    DescriptorHost.Current                = false;
    uint32_t                     index    = 123;
    RRM::MaterialVersionSnapshot snapshot = {.Version = 456};
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &index, &snapshot));
    EXPECT_EQ(index, 123u);
    EXPECT_EQ(snapshot.Version, 456u);
    DescriptorHost.Current = true;
    EXPECT_TRUE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &index, &snapshot));
}

TEST_F(SceneResourceBundleTest, CancellationWhileDescriptorsArePendingDrainsWithoutPublication)
{
    TexturedMesh();
    auto& rrm            = Fixture->rrm;
    DescriptorHost.State = SceneTextureDescriptorState::Pending;
    const auto id        = Prepare(std::array{MeshUUID(1)});
    RRMTestHelper::TickPreparation(rrm);
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    DescriptorHost.State = SceneTextureDescriptorState::Ready;
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_FALSE(rrm.AcquireSceneResourceLease(id, 2));
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
}

TEST_F(SceneResourceBundleTest, RequiredTextureDecodeFailureIsTerminalEvenDuringUnrelatedUploads)
{
    Material(100);
    const auto texture  = Texture();
    const auto identity = MeshUUID(200);
    Assets::Instance()->UUIDToTextureHandle.insert(identity, texture);
    Binding(100, 0, identity, static_cast<uint32_t>(texture.Index));
    auto& rrm = Fixture->rrm;
    RRMTestHelper::FailTextureDecode(rrm, texture);
    RRMTestHelper::SetUnsubmittedTextureWork(rrm, true);
    Mesh(0, 1, std::array{MeshUUID(100)});
    const auto id = Prepare(std::array{MeshUUID(1)});
    RRMTestHelper::TickPreparation(rrm);
    const auto result = Poll(id);
    EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(result.Error, RRM::SceneResourcePreparationError::RequiredMaterialBinding);
    EXPECT_EQ(result.MaterialError, RRM::MaterialVersionError::TextureLoadFailed);
    EXPECT_EQ(result.MaterialCount, 0u);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
    RRMTestHelper::SetUnsubmittedTextureWork(rrm, false);
    rrm.ForgetTextureDecode(texture);
}

TEST_F(SceneResourceBundleTest, RequiredBindingAndMissingMaterialFailuresAreNotHiddenByUnrelatedTextureWork)
{
    Material(100);
    Binding(100, 0, MeshUUID(200), UINT32_MAX);
    auto& rrm = Fixture->rrm;
    RRMTestHelper::SetUnsubmittedTextureWork(rrm, true);
    EXPECT_TRUE(rrm.HasPendingTextureUploads());
    for (uint32_t material : {100u, 101u})
    {
        Mesh(0, 1, std::array{MeshUUID(material)});
        const auto id = Prepare(std::array{MeshUUID(1)}, material);
        RRMTestHelper::TickPreparation(rrm);
        const auto result = Poll(id);
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(result.Error, material == 100 ? RRM::SceneResourcePreparationError::RequiredMaterialBinding : RRM::SceneResourcePreparationError::MaterialNotRetainable);
        EXPECT_EQ(result.MaterialError, material == 100 ? RRM::MaterialVersionError::MissingRequiredTexture : RRM::MaterialVersionError::MissingMaterial);
        EXPECT_EQ(result.MaterialCount, 0u);
        EXPECT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(rrm);
    }
    RRMTestHelper::SetUnsubmittedTextureWork(rrm, false);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
}

TEST_F(SceneResourceBundleTest, CachedMeshesRestoreRegistrationAndShareResidencyBeforePublication)
{
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::MetaFileData meta = {};
    meta.AssetUUID                        = MeshUUID(1);
    ASSERT_TRUE(registry.RegisterLoaded(MeshUUID(1), ZEngine::Managers::AssetType::MESH, {}, meta, Assets::CreateHandle(0, ZEngine::Managers::AssetType::MESH)).IsOk());
    RRMTestHelper::SetRegistry(Fixture->rrm, &registry);
    Material(2);
    const std::array meshes{MeshUUID(1)};
    const auto       first  = Prepare(meshes, 1);
    const auto       second = Prepare(meshes, 2);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(first).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(Poll(second).State, RRM::SceneResourcePreparationState::Preparing);
    const auto handle = Fixture->rrm.FindMeshBuffer(MeshUUID(1));
    ASSERT_TRUE(handle.IsValid());
    EXPECT_EQ(RRMTestHelper::GetState(Fixture->rrm, handle.Index), RRM::StreamingState::Pending);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 0u);

    Mesh(handle.Index, 1, std::array{MeshUUID(2)}); // The shared reload completes recording.
    RRMTestHelper::SetUploadProgress(Fixture->rrm, 0, 0, true);
    RRMTestHelper::TickPreparation(Fixture->rrm, 100);
    EXPECT_EQ(Poll(first).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(Poll(second).State, RRM::SceneResourcePreparationState::Preparing);
    RRMTestHelper::SetUploadProgress(Fixture->rrm, 1, 1, false);
    RRMTestHelper::TickPreparation(Fixture->rrm, 1);
    EXPECT_EQ(Poll(first).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(Poll(second).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 1u);
    EXPECT_EQ(RRMTestHelper::PinReferences(Fixture->rrm, handle.Index), 2u);
}

TEST_F(SceneResourceBundleTest, FailedResidencyFailsTheCandidateAndANewRequestCanRetry)
{
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::MetaFileData meta = {};
    meta.AssetUUID                        = MeshUUID(2);
    ASSERT_TRUE(registry.RegisterLoaded(MeshUUID(2), ZEngine::Managers::AssetType::MESH, {}, meta, Assets::CreateHandle(0, ZEngine::Managers::AssetType::MESH)).IsOk());
    RRMTestHelper::SetRegistry(Fixture->rrm, &registry);
    Material(3);
    Mesh(0, 1, std::array{MeshUUID(3)});
    RRMTestHelper::SetSlot(Fixture->rrm, 1, 1, RRM::StreamingState::Unloaded, false, false);
    RRMTestHelper::RegisterMesh(Fixture->rrm, MeshUUID(2), {1, 1});
    const std::array meshes{MeshUUID(1), MeshUUID(2)};
    const auto       failed = Prepare(meshes);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(failed).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 1u);
    RRMTestHelper::FailReload(Fixture->rrm, 1);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(failed).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Poll(failed).Error, RRM::SceneResourcePreparationError::UploadFailed);
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(failed));
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 0u);

    RRMTestHelper::ResetReloadQueue(Fixture->rrm);
    const auto retry = Prepare(meshes, 2);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(retry).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(RRMTestHelper::GetState(Fixture->rrm, 1), RRM::StreamingState::Pending);
    EXPECT_FALSE(RRMTestHelper::LoadFailed(Fixture->rrm, 1));
    ASSERT_TRUE(Fixture->rrm.ReleaseSceneResourcePreparation(retry));
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 0u);
    Mesh(1, 2, std::array{MeshUUID(3)}); // Late shared-cache completion cannot revive it.
    RRMTestHelper::TickPreparation(Fixture->rrm);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(Fixture->rrm.PollSceneResourcePreparation(retry, &result));
}

TEST_F(SceneResourceBundleTest, ResidencyQueueSaturationIsRetryableBackpressure)
{
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::MetaFileData meta = {};
    meta.AssetUUID                        = MeshUUID(1);
    ASSERT_TRUE(registry.RegisterLoaded(MeshUUID(1), ZEngine::Managers::AssetType::MESH, {}, meta, Assets::CreateHandle(0, ZEngine::Managers::AssetType::MESH)).IsOk());
    RRMTestHelper::SetRegistry(Fixture->rrm, &registry);
    RRMTestHelper::SetSlot(Fixture->rrm, 0, 1, RRM::StreamingState::Unloaded, false, false);
    RRMTestHelper::RegisterMesh(Fixture->rrm, MeshUUID(1), {0, 1});
    for (uint32_t i = 1; i <= 255; ++i)
    {
        RRMTestHelper::SetSlot(Fixture->rrm, i, 1, RRM::StreamingState::Unloaded, false, false);
        ASSERT_TRUE(RRMTestHelper::QueueReload(Fixture->rrm, {i, 1}));
    }
    const auto id = Prepare(std::array{MeshUUID(1)});
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::None);
    EXPECT_EQ(RRMTestHelper::GetState(Fixture->rrm, 0), RRM::StreamingState::Unloaded);
    RRMTestHelper::ResetReloadQueue(Fixture->rrm);
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(RRMTestHelper::GetState(Fixture->rrm, 0), RRM::StreamingState::Pending);
}

TEST_F(SceneResourceBundleTest, CachedResidencyRegistrationCapacityFailsWithoutAllocatingAnExtraSlot)
{
    ZEngine::Core::VFS::AssetRegistry registry = {};
    registry.Initialize(&Memory.MainArena);
    ZEngine::Core::VFS::MetaFileData meta     = {};
    const auto                       required = MeshUUID(RRM::SceneGeometryMeshCapacity + 1);
    meta.AssetUUID                            = required;
    ASSERT_TRUE(registry.RegisterLoaded(required, ZEngine::Managers::AssetType::MESH, {}, meta, 0).IsOk());
    RRMTestHelper::SetRegistry(Fixture->rrm, &registry);
    for (uint32_t i = 0; i < RRM::SceneGeometryMeshCapacity; ++i)
    {
        RRMTestHelper::SetSlot(Fixture->rrm, i, 1, RRM::StreamingState::Resident, false, false);
        RRMTestHelper::RegisterMesh(Fixture->rrm, MeshUUID(i + 1), {i, 1});
    }
    const auto id = Prepare(std::array{required});
    RRMTestHelper::TickPreparation(Fixture->rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::GeometryCapacity);
    EXPECT_FALSE(Fixture->rrm.FindMeshBuffer(required).IsValid());
    EXPECT_EQ(RRMTestHelper::PinnedVersions(Fixture->rrm), 0u);
    EXPECT_EQ(RRMTestHelper::AllocateMeshSlot(Fixture->rrm), UINT32_MAX);
    EXPECT_TRUE(Fixture->rrm.IsMeshResident({0, 1}));
}

TEST(RenderResourceManagerSceneGeometry, ExhaustedSlotGenerationsNeverWrapOrEnterTheGenericBufferTag)
{
    StreamingFixture f;
    RRMTestHelper::ExhaustMeshSlotGeneration(f.rrm, 0);
    EXPECT_EQ(RRMTestHelper::AllocateMeshSlot(f.rrm), 1u);
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 0), 0u);
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 1), 1u);
    RRMTestHelper::ResetGeometry(f.rrm);
    EXPECT_EQ(RRMTestHelper::AllocateMeshSlot(f.rrm), 1u);
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 1), 2u);
}

TEST_F(SceneResourceBundleTest, SharedMaterialsRetainTheirOriginalValuesAndTexturesAcrossReplacement)
{
    const auto texture                                                                 = Device->GlobalTextures.Create();
    auto*      image                                                                   = Device->GlobalTextures.Access(texture);
    image->BufferHandle                                                                = Device->ImageBufferManager.Create();
    Device->ImageBufferManager.Access(image->BufferHandle)->GetBuffer().OwnsAllocation = true;
    Material(100, static_cast<uint32_t>(texture.Index));
    const std::array materials{MeshUUID(100), MeshUUID(100)};
    Mesh(0, 1, materials);
    Mesh(1, 2, materials);
    const std::array             meshes{MeshUUID(1), MeshUUID(2)};
    auto&                        rrm    = Fixture->rrm;
    const auto                   id     = Prepare(meshes);
    RRM::MaterialVersionSnapshot before = {}, after = {};
    EXPECT_FALSE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &before));
    RRMTestHelper::TickPreparation(rrm);
    const auto result = Poll(id);
    ASSERT_EQ(result.State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(result.Content, RRM::ScenePreparationContent::MeshMaterials);
    EXPECT_EQ(result.MaterialCount, 1u);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 2u); // bundle and reserved frame-table entry
    EXPECT_FALSE(rrm.CanWriteTextureVersion(texture));
    ASSERT_TRUE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &before));
    {
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        assets->GPUMeshMaterials[*assets->UUIDToMaterialSlot.find(MeshUUID(100))].AlbedoColor.w = 1.f;
    }
    const std::array replacement{MeshUUID(200)};
    Mesh(0, 1, replacement);
    ASSERT_TRUE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &after));
    EXPECT_EQ(after.Version, before.Version);
    EXPECT_FLOAT_EQ(after.Material.AlbedoColor.w, .25f);
    EXPECT_FALSE(rrm.GetPreparedSceneMaterial(id, MeshUUID(200), &after));
    RRM::MeshGeometrySnapshot geometry = {};
    ASSERT_TRUE(rrm.GetPreparedSceneGeometry(id, MeshUUID(1), &geometry));
    EXPECT_EQ(geometry.DrawMetadata->SubMeshes[0].MaterialUUID, MeshUUID(100));
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    EXPECT_FALSE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &after));
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_TRUE(rrm.CanWriteTextureVersion(texture));
}

TEST_F(SceneResourceBundleTest, DuplicateTraversalAndCancellationShareTheOperationBudget)
{
    Material(100);
    const std::vector<uuids::uuid> materials(RRM::SceneResourceSliceOperations * 2, MeshUUID(100));
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    const auto       id  = Prepare(meshes);
    auto&            rrm = Fixture->rrm;
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(Poll(id).MaterialCount, 0u); // No render-progress fields escape while preparing.
    EXPECT_EQ(RRMTestHelper::PreparedMaterials(rrm, id.Slot), 1u);
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(rrm.PollSceneResourcePreparation(id, &result));
}

TEST_F(SceneResourceBundleTest, MissingMaterialOrTextureFailsWithoutPublishingAPartialBundle)
{
    Material(100);
    Material(101, 7); // Explicit map has no live texture.
    auto& rrm = Fixture->rrm;
    for (uint32_t missing : {101u, 102u})
    {
        const std::array materials{MeshUUID(100), MeshUUID(missing)};
        Mesh(0, 1, materials);
        const std::array meshes{MeshUUID(1)};
        const auto       id = Prepare(meshes, missing);
        RRMTestHelper::TickPreparation(rrm);
        const auto result = Poll(id);
        EXPECT_EQ(result.State, RRM::SceneResourcePreparationState::Failed);
        EXPECT_EQ(result.Error, missing == 101 ? RRM::SceneResourcePreparationError::RequiredMaterialBinding : RRM::SceneResourcePreparationError::MaterialNotRetainable);
        EXPECT_EQ(result.MaterialCount, 1u);
        RRM::MaterialVersionSnapshot material = {};
        EXPECT_FALSE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &material));
        EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
        ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(rrm);
    }
}

TEST_F(SceneResourceBundleTest, MaterialCapacityRefusalPreservesEarlierPinsAndCleanupIsSliced)
{
    std::vector<uuids::uuid> materials = {};
    for (uint32_t i = 0; i <= RRM::SceneMaterialCapacity; ++i)
    {
        Material(i + 100);
        materials.push_back(MeshUUID(i + 100));
    }
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    const auto       id  = Prepare(meshes);
    auto&            rrm = Fixture->rrm;
    for (uint32_t i = 0; i < RRM::SceneMaterialCapacity / RRM::SceneResourceSliceOperations + 2; ++i)
    {
        RRMTestHelper::TickPreparation(rrm);
    }
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Poll(id).Error, RRM::SceneResourcePreparationError::MaterialCapacity);
    EXPECT_EQ(Poll(id).MaterialCount, RRM::SceneMaterialCapacity);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(RRMTestHelper::PreparedMaterials(rrm, id.Slot), RRM::SceneMaterialCapacity - RRM::SceneResourceSliceOperations);
    EXPECT_EQ(RRMTestHelper::PreparedPins(rrm, id.Slot), 1u);
    RRMTestHelper::DrainPreparationAfterIdle(rrm);
}

TEST_F(SceneResourceBundleTest, EmptyAndMissingMetadataAreDistinctAndSlotReuseClearsMaterialIndex)
{
    auto&      rrm   = Fixture->rrm;
    const auto empty = Prepare({});
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(empty).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(empty));
    RRMTestHelper::TickPreparation(rrm);
    RRMTestHelper::SetSlot(rrm, 0, 1, RRM::StreamingState::Resident, false, false);
    RRMTestHelper::RegisterMesh(rrm, MeshUUID(1), {0, 1});
    const std::array meshes{MeshUUID(1)};
    const auto       missing = Prepare(meshes, 2);
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(missing).Error, RRM::SceneResourcePreparationError::MissingDrawMetadata);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(missing));
    RRMTestHelper::TickPreparation(rrm);
    for (uint32_t value : {100u, 200u})
    {
        Material(value);
        const std::array materials{MeshUUID(value)};
        Mesh(0, 1, materials);
        const auto id = Prepare(meshes, value);
        RRMTestHelper::TickPreparation(rrm);
        EXPECT_EQ(Poll(id).MaterialCount, 1u);
        RRM::MaterialVersionSnapshot material = {};
        EXPECT_TRUE(rrm.GetPreparedSceneMaterial(id, MeshUUID(value), &material));
        if (value == 200)
        {
            EXPECT_FALSE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &material));
        }
        ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(rrm);
    }
}

TEST_F(SceneResourceBundleTest, CancellationDuringPreparationWaitsForAcceptedProducerBeforeDroppingTextures)
{
    const auto texture                                                                 = Device->GlobalTextures.Create();
    auto*      image                                                                   = Device->GlobalTextures.Access(texture);
    image->BufferHandle                                                                = Device->ImageBufferManager.Create();
    Device->ImageBufferManager.Access(image->BufferHandle)->GetBuffer().OwnsAllocation = true;
    Material(100, static_cast<uint32_t>(texture.Index));
    const std::vector<uuids::uuid> materials(RRM::SceneResourceSliceOperations * 2, MeshUUID(100));
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    auto&            rrm = Fixture->rrm;
    RRMTestHelper::SetUploadProgress(rrm, 3, 3, true);
    const auto id = Prepare(meshes);
    RRMTestHelper::TickPreparation(rrm, 3);
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Preparing);
    ASSERT_EQ(RRMTestHelper::TextureReferences(rrm), 2u);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm, 100); // A signal alone cannot accept the open batch.
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 2u);
    RRMTestHelper::SetUploadProgress(rrm, 4, 4, false);
    RRMTestHelper::TickPreparation(rrm, 3);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 2u);
    RRMTestHelper::TickPreparation(rrm, 4);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(rrm.PollSceneResourcePreparation(id, &result));
}

TEST_F(SceneResourceBundleTest, FrameTransferPreservesDetachedGeometryAndSealedTexturesThroughEveryAcceptedGate)
{
    auto&      rrm                                                                     = Fixture->rrm;
    const auto texture                                                                 = Device->GlobalTextures.Create();
    auto*      image                                                                   = Device->GlobalTextures.Access(texture);
    image->BufferHandle                                                                = Device->ImageBufferManager.Create();
    Device->ImageBufferManager.Access(image->BufferHandle)->GetBuffer().OwnsAllocation = true;
    Material(100, static_cast<uint32_t>(texture.Index));
    const std::array materials{MeshUUID(100)};
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    const auto       id = Prepare(meshes);
    RRMTestHelper::TickPreparation(rrm);
    ASSERT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    RRM::MeshGeometrySnapshot    original_geometry = {}, geometry = {};
    RRM::MaterialVersionSnapshot original_material = {}, material = {};
    ASSERT_TRUE(rrm.GetPreparedSceneGeometry(id, MeshUUID(1), &original_geometry));
    ASSERT_TRUE(rrm.GetPreparedSceneMaterial(id, MeshUUID(100), &original_material));
    const std::array replacement{MeshUUID(200)};
    Mesh(0, 1, replacement);
    {
        auto*           assets = Assets::Instance();
        std::lock_guard lock(assets->IngestMutex);
        assets->GPUMeshMaterials[*assets->UUIDToMaterialSlot.find(MeshUUID(100))].AlbedoColor.w = .75f;
    }
    EXPECT_TRUE(rrm.RetainTextureForRetirement(texture));
    EXPECT_FALSE(rrm.PinTextureVersion(texture));
    EXPECT_FALSE(rrm.PinMaterialVersion(MeshUUID(100)));
    uint32_t index = UINT32_MAX, repeated = UINT32_MAX;
    for (uint32_t draw = 0; draw < 100; ++draw)
    {
        ASSERT_TRUE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(1), &geometry));
        ASSERT_TRUE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &repeated, &material));
        if (!draw)
        {
            index = repeated;
        }
        EXPECT_EQ(index, repeated);
    }
    EXPECT_EQ(geometry.Version, original_geometry.Version);
    EXPECT_EQ(geometry.Region.VtxByteOffset, original_geometry.Region.VtxByteOffset);
    EXPECT_EQ(geometry.DrawMetadata->SubMeshes[0].MaterialUUID, MeshUUID(100));
    EXPECT_FLOAT_EQ(material.Material.AlbedoColor.w, original_material.Material.AlbedoColor.w);
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(rrm), 1u);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 2u); // bundle + one deduplicated frame entry
    RRMTestHelper::StampGeometry(rrm, 0, 4);
    RRMTestHelper::StampGeometry(rrm, 1, 7);
    rrm.FinishResourceFrame(); // Presentation abort preserves both accepted gates.
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 1u);
    EXPECT_EQ(RRMTestHelper::GetPool(rrm).VtxUsed, 64u);
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(1), &geometry));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &repeated, &material));
    RRMTestHelper::CompleteGeometry(rrm, {4, 6, 0, 0});
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 1u);
    EXPECT_EQ(RRMTestHelper::GetPool(rrm).VtxUsed, 64u);
    RRMTestHelper::CompleteGeometry(rrm, {4, 7, 0, 0});
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(rrm).VtxUsed, 32u);
}

TEST_F(SceneResourceBundleTest, RefusedFrameTransfersPreserveOutputsAndDoNotRetainAnything)
{
    Material(100);
    const std::array materials{MeshUUID(100)};
    Mesh(0, 1, materials);
    const std::array             meshes{MeshUUID(1)};
    const auto                   id       = Prepare(meshes);
    auto&                        rrm      = Fixture->rrm;
    RRM::MeshGeometrySnapshot    geometry = {};
    RRM::MaterialVersionSnapshot material = {};
    geometry.Version                      = 123;
    material.Version                      = 456;
    uint32_t index                        = 789;
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(1), &geometry));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &index, &material));
    RRMTestHelper::TickPreparation(rrm);
    auto stale = id;
    ++stale.Serial;
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(stale, MeshUUID(1), &geometry));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(stale, MeshUUID(100), &index, &material));
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(2), &geometry));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(200), &index, &material));
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(1), nullptr));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), nullptr, &material));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &index, nullptr));
    RRMTestHelper::SealPreparation(rrm);
    EXPECT_FALSE(rrm.GetFramePreparedSceneGeometry(id, MeshUUID(1), &geometry));
    EXPECT_FALSE(rrm.GetFramePreparedSceneMaterial(id, MeshUUID(100), &index, &material));
    EXPECT_EQ(geometry.Version, 123u);
    EXPECT_EQ(material.Version, 456u);
    EXPECT_EQ(index, 789u);
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(rrm), 0u);
}

TEST_F(SceneResourceBundleTest, MailboxCoalescingAndOwnerCancellationKeepTheSelectedBundleAlive)
{
    Material(100);
    const std::array materials{MeshUUID(100)};
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    const auto       id  = Prepare(meshes);
    auto&            rrm = Fixture->rrm;
    RRMTestHelper::TickPreparation(rrm);
    ZEngine::Applications::AppRenderPipeline pipeline = {};
    pipeline.Device                                   = Device.get();
    pipeline.InitializeFrameStorage(&Fixture->arena, 4);
    ZEngine::Applications::RenderFrameState frame = {};
    frame.SceneGeneration                         = 2;
    frame.SceneResources                          = id;
    EXPECT_TRUE(pipeline.PublishFrameState(frame));
    EXPECT_TRUE(pipeline.PublishFrameState(frame));
    EXPECT_TRUE(pipeline.PublishFrameState(frame));
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 3u);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(id).State, RRM::SceneResourcePreparationState::Releasing);
    EXPECT_FALSE(pipeline.PublishFrameState(frame)); // Cancellation seals further producers, not held consumers.
    ASSERT_TRUE(pipeline.TryReadFrameState(frame));
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 1u);
    RRM::MeshGeometrySnapshot    geometry = {};
    RRM::MaterialVersionSnapshot material = {};
    uint32_t                     index    = 0;
    EXPECT_TRUE(rrm.GetFramePreparedSceneGeometry(frame.SceneResources, MeshUUID(1), &geometry));
    EXPECT_TRUE(rrm.GetFramePreparedSceneMaterial(frame.SceneResources, MeshUUID(100), &index, &material));
    rrm.NotifyPreparedSceneFrameRecorded(id);
    EXPECT_TRUE(Poll(id).FrameRecorded);
    pipeline.ReleaseFrameState();
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 0u);
    RRMTestHelper::TickPreparation(rrm);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(rrm.PollSceneResourcePreparation(id, &result));
    rrm.FinishResourceFrame();
}

TEST_F(SceneResourceBundleTest, JoinedShutdownReleasesSharedSceneMaterialReservationsBeforeClearingFrameTable)
{
    Material(100);
    const std::array materials{MeshUUID(100)};
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    auto&            rrm   = Fixture->rrm;
    const auto       first = Prepare(meshes, 2);
    RRMTestHelper::TickPreparation(rrm);
    ASSERT_EQ(Poll(first).MaterialCount, 1u);
    const auto second = Prepare(meshes, 3);
    RRMTestHelper::TickPreparation(rrm);
    ASSERT_EQ(Poll(second).MaterialCount, 1u);
    EXPECT_EQ(RRMTestHelper::FrameMaterialCount(rrm), 1u);
    // A recorded/submitted frame and both preparations can share this entry.
    uint32_t                     index    = 0;
    RRM::MaterialVersionSnapshot snapshot = {};
    ASSERT_TRUE(rrm.GetFrameMaterialVersion(MeshUUID(100), &index, &snapshot));
    RRMTestHelper::StampGeometry(rrm, 0, 100);
    rrm.FinishResourceFrame();
    RRMTestHelper::DrainSceneAndFrameResourcesAfterIdle(rrm);
    EXPECT_EQ(RRMTestHelper::FrameMaterialCount(rrm), 0u);
    RRM::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(rrm.PollSceneResourcePreparation(first, &result));
    EXPECT_FALSE(rrm.PollSceneResourcePreparation(second, &result));
    EXPECT_EQ(RRMTestHelper::TextureReferences(rrm), 0u);
    // The joined-owner cleanup is safe on an already drained table too.
    RRMTestHelper::DrainSceneAndFrameResourcesAfterIdle(rrm);
    EXPECT_EQ(RRMTestHelper::FrameMaterialCount(rrm), 0u);
}

TEST_F(SceneResourceBundleTest, MailboxRollbackReplacementAndJoinedReaderShutdownReleaseEveryLease)
{
    const auto id  = Prepare({});
    auto&      rrm = Fixture->rrm;
    RRMTestHelper::TickPreparation(rrm);
    ZEngine::Applications::AppRenderPipeline pipeline = {};
    pipeline.Device                                   = Device.get();
    ZEngine::Applications::RenderFrameState frame     = {};
    frame.SceneResources                              = id;
    frame.SceneGeneration                             = 3;
    EXPECT_FALSE(pipeline.PublishFrameState(frame)); // Expected activation generation is 2.
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 0u);
    frame.SceneGeneration                                                 = 2;
    ZEngine::Applications::AppRenderPipeline::PreparedFrameState reserved = {};
    ASSERT_TRUE(pipeline.PrepareFrameState(frame, nullptr, &reserved));
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 1u);
    reserved.Reset();
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 0u);
    for (uint32_t i = 0; i < 10; ++i)
    {
        ASSERT_TRUE(pipeline.PublishFrameState(frame));
    }
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 3u);
    pipeline.ReleaseSceneResourceFramesAfterReadersJoined();
    EXPECT_EQ(RRMTestHelper::PreparationReaders(rrm, id.Slot), 0u);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(rrm);
}

TEST_F(SceneResourceBundleTest, OutgoingMaterialCapacityMustBeAvailableBeforeTheCandidateCanBecomeReady)
{
    auto&                        rrm      = Fixture->rrm;
    RRM::MaterialVersionSnapshot snapshot = {};
    uint32_t                     index    = 0;
    for (uint32_t value = 0; value < RRM::FrameMaterialCapacity; ++value)
    {
        ZEngine::Rendering::Meshes::MeshMaterial material = {};
        material.AlbedoColor.x                            = static_cast<float>(value + 1);
        ASSERT_TRUE(rrm.GetFrameMaterialVersion(material, &index, &snapshot));
    }
    RRMTestHelper::StampGeometry(rrm, 0, 1);
    rrm.FinishResourceFrame();
    Material(100);
    const std::array materials{MeshUUID(100)};
    Mesh(0, 1, materials);
    const std::array meshes{MeshUUID(1)};
    const auto       refused = Prepare(meshes);
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(refused).State, RRM::SceneResourcePreparationState::Failed);
    EXPECT_EQ(Poll(refused).Error, RRM::SceneResourcePreparationError::MaterialCapacity);
    EXPECT_EQ(Poll(refused).MaterialCount, 0u);
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(refused));
    RRMTestHelper::TickPreparation(rrm);
    RRMTestHelper::CompleteGeometry(rrm, {1, 0, 0, 0});
    const auto admitted = Prepare(meshes, 2);
    RRMTestHelper::TickPreparation(rrm);
    EXPECT_EQ(Poll(admitted).State, RRM::SceneResourcePreparationState::ResourcesRetained);
    EXPECT_EQ(Poll(admitted).MaterialCount, 1u);
    // Ordinary polling cannot reclaim this reserved entry before its first draw.
    rrm.FinishResourceFrame();
    EXPECT_TRUE(rrm.GetFramePreparedSceneMaterial(admitted, MeshUUID(100), &index, &snapshot));
    ASSERT_TRUE(rrm.ReleaseSceneResourcePreparation(admitted));
    RRMTestHelper::TickPreparation(rrm);
    rrm.FinishResourceFrame();
}

TEST(GeometryStreamingManager, TickClearsReferencedBitsOnResidentSlots)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/true, false);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Unloaded, /*referenced=*/true, false);
    f.mgr.Tick(0);
    EXPECT_FALSE(RRMTestHelper::GetReferenced(f.rrm, 0)); // Resident — bit cleared
    EXPECT_TRUE(RRMTestHelper::GetReferenced(f.rrm, 1));  // Unloaded — untouched
}

TEST(GeometryStreamingManager, EvictionSweepSkipsReferencedSlot)
{
    StreamingFixture f;
    GeometryRegion   r0, r1;
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r0);
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r1);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/true, false, r0);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, false, r1);

    // Force eviction pressure by filling pool above 85%.
    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 256 - 2, 4096 - 128 - 2, filler);

    f.mgr.Tick(0);
    // Slot 0 (Referenced) gets second-chance — stays Resident (bit cleared).
    // Slot 1 (not referenced) is evicted.
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
    EXPECT_FALSE(RRMTestHelper::GetReferenced(f.rrm, 0)); // second-chance cleared the bit
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 1), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, EvictionSweepFreesPoolRegion)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, false, r);

    VkDeviceSize   used_before = RRMTestHelper::GetPool(f.rrm).VtxUsed;

    // Force eviction pressure.
    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 200 - 2, 4096 - 100 - 2, filler);

    f.mgr.Tick(0);

    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteSize, 0u);                      // region zeroed
    EXPECT_LT(RRMTestHelper::GetPool(f.rrm).VtxUsed, used_before + filler.VtxByteSize); // bytes returned
}

TEST(GeometryStreamingManager, EvictionSweepSkipsPinnedSlots)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(128, 64, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, /*referenced=*/false, /*pinned=*/true, r);

    GeometryRegion filler;
    RRMTestHelper::GetPool(f.rrm).Allocate(4096 - 128 - 2, 4096 - 64 - 2, filler);

    f.mgr.Tick(0);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, RequestEvictFreesRegionAndSetsUnloaded)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, r);
    BufferHandle h{0, 1};

    VkDeviceSize vtx_used_before = RRMTestHelper::GetPool(f.rrm).VtxUsed;
    f.mgr.RequestEvict(h);

    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteSize, 0u);
    EXPECT_LT(RRMTestHelper::GetPool(f.rrm).VtxUsed, vtx_used_before);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnInvalidHandle)
{
    StreamingFixture f;
    BufferHandle     invalid{};
    EXPECT_NO_FATAL_FAILURE(f.mgr.RequestEvict(invalid));
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnPinnedSlot)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(100, 50, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, /*pinned=*/true, r);
    BufferHandle h{0, 1};
    f.mgr.RequestEvict(h);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnUnloadedSlot)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    BufferHandle h{0, 1};
    f.mgr.RequestEvict(h);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, RequestEvictNoOpOnStaleHandle)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(100, 50, r);
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false, r); // gen=2
    BufferHandle stale{0, 1};                                                                              // gen=1 — stale
    f.mgr.RequestEvict(stale);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
}

TEST(GeometryStreamingManager, FragmentationAboveThresholdSetsCompactionRequest)
{
    StreamingFixture f;
    GeometryRegion   r;
    // Allocate then free enough to push fragmentation above 0.30.
    RRMTestHelper::GetPool(f.rrm).Allocate(2000, 2000, r);
    RRMTestHelper::GetPool(f.rrm).Free(r);

    EXPECT_FALSE(f.mgr.IsCompactionRequested());
    f.mgr.Tick(0);
    EXPECT_TRUE(f.mgr.IsCompactionRequested());
}

TEST(GeometryStreamingManager, ClearCompactionRequestResetsFlag)
{
    StreamingFixture f;
    GeometryRegion   r;
    RRMTestHelper::GetPool(f.rrm).Allocate(2000, 2000, r);
    RRMTestHelper::GetPool(f.rrm).Free(r);
    f.mgr.Tick(0);
    ASSERT_TRUE(f.mgr.IsCompactionRequested());
    f.mgr.ClearCompactionRequest();
    EXPECT_FALSE(f.mgr.IsCompactionRequested());
}

TEST(GeometryStreamingManager, RequestLoadQueueOverflowReturnsFalse)
{
    // SPSCQueue<T, N> holds at most N-1 items (head==tail = empty sentinel).
    // kLoadQueueCapacity=256 → effective capacity 255.
    StreamingFixture f;
    StreamRequest    req{};
    req.Asset          = 1;
    uint32_t succeeded = 0;
    for (uint32_t i = 0; i < 256; ++i)
    {
        RRMTestHelper::SetSlot(f.rrm, i, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
        req.Handle = {i, 1};
        if (f.mgr.RequestLoad(req))
        {
            ++succeeded;
        }
        else
        {
            break;
        }
    }
    EXPECT_EQ(succeeded, 255u);           // N-1 items fit
    EXPECT_FALSE(f.mgr.RequestLoad(req)); // next push must fail
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 255), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, DuplicateInstancesShareOneQueuedReloadAndFailuresCanRetry)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    const StreamRequest request = {
        MeshUUID(), 1, {0, 1}
    };
    for (uint32_t i = 0; i < 4096; ++i)
    {
        EXPECT_TRUE(f.mgr.RequestLoad(request));
    }
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Pending);
    f.mgr.Tick(0); // No CPU asset manager: the single admitted reload fails.
    EXPECT_TRUE(RRMTestHelper::LoadFailed(f.rrm, 0));
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
    EXPECT_TRUE(f.mgr.RequestLoad(request));
    EXPECT_FALSE(RRMTestHelper::LoadFailed(f.rrm, 0));
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Pending);
    f.mgr.Deinitialize();
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Unloaded);
}

TEST(GeometryStreamingManager, ReloadDrainIsSlicedAndStaleGenerationsCannotMutateTheirReplacement)
{
    StreamingFixture   f;
    constexpr uint32_t count = GeometryStreamingManager::LoadSliceOperations + 2;
    for (uint32_t i = 0; i < count; ++i)
    {
        RRMTestHelper::SetSlot(f.rrm, i, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
        ASSERT_TRUE(f.mgr.RequestLoad({
            MeshUUID(i + 1), 1, {i, 1}
        }));
    }
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false);
    EXPECT_FALSE(f.mgr.RequestLoad({
        MeshUUID(), 1, {0, 1}
    }));
    f.mgr.Tick(0);
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, 0), RenderResourceManager::StreamingState::Resident);
    EXPECT_FALSE(RRMTestHelper::LoadFailed(f.rrm, 0));
    EXPECT_TRUE(RRMTestHelper::LoadFailed(f.rrm, GeometryStreamingManager::LoadSliceOperations - 1));
    EXPECT_EQ(RRMTestHelper::GetState(f.rrm, GeometryStreamingManager::LoadSliceOperations), RenderResourceManager::StreamingState::Pending);
    f.mgr.Tick(0);
    EXPECT_TRUE(RRMTestHelper::LoadFailed(f.rrm, count - 1));
}

TEST(RenderResourceManagerMeshRelease, ProducerDoesNotMutateGeometryBeforeRenderDrain)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    const uuids::uuid uuid = MeshUUID();
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});

    std::thread producer([&]() { f.rrm.ReleaseMeshGeometry(uuid); });
    producer.join();

    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 0), 1u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 200u);
    EXPECT_TRUE(f.rrm.FindMeshBuffer(uuid).IsValid());
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 1u);

    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 0), 0u);
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteSize, 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).IdxUsed, 0u);
    EXPECT_FALSE(f.rrm.FindMeshBuffer(uuid).IsValid());
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 0u);
    f.rrm.ReleaseMeshGeometry(uuid);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshRelease, ConcurrentDuplicatesCoalesceAndFreeOnlyOnce)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    const uuids::uuid uuid = MeshUUID();
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});

    auto enqueue = [&]() {
        for (uint32_t i = 0; i < 1000; ++i)
        {
            f.rrm.ReleaseMeshGeometry(uuid);
        }
    };
    std::thread first(enqueue);
    std::thread second(enqueue);
    enqueue();
    first.join();
    second.join();
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 1u);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).IdxUsed, 0u);
}

TEST(RenderResourceManagerMeshRelease, DelayedReleasePreservesReplacementRegistrationAndSlot)
{
    StreamingFixture  f;
    const uuids::uuid uuid = MeshUUID();
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});
    f.rrm.ReleaseMeshGeometry(uuid);

    GeometryRegion region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 2});
    RRMTestHelper::FlushMeshReleases(f.rrm);

    EXPECT_EQ(f.rrm.FindMeshBuffer(uuid).Generation, 2u);
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 0), 2u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 200u);
    f.rrm.ReleaseMeshGeometry(uuid);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshRelease, StaleMappingCannotInvalidateReusedSlot)
{
    StreamingFixture  f;
    const uuids::uuid uuid = MeshUUID();
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});
    f.rrm.ReleaseMeshGeometry(uuid);

    GeometryRegion region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::FlushMeshReleases(f.rrm);

    EXPECT_FALSE(f.rrm.FindMeshBuffer(uuid).IsValid());
    EXPECT_EQ(RRMTestHelper::SlotGeneration(f.rrm, 0), 2u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 200u);
}

TEST(RenderResourceManagerMeshRelease, FullRegistrationMapAcceptsAllReleasesAndDuplicates)
{
    StreamingFixture f;
    uuids::uuid      first = {};
    uuids::uuid      last  = {};
    for (uint32_t i = 0; i < RRMTestHelper::RegistrationCapacity(); ++i)
    {
        const uuids::uuid uuid = MeshUUID(i + 1);
        if (i == 0)
        {
            first = uuid;
        }
        last = uuid;
        RRMTestHelper::RegisterMesh(f.rrm, uuid, {i, 1});
        f.rrm.ReleaseMeshGeometry(uuid);
    }
    f.rrm.ReleaseMeshGeometry(first);
    f.rrm.ReleaseMeshGeometry(last);
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), RRMTestHelper::RegistrationCapacity());
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 0u);
    EXPECT_FALSE(f.rrm.FindMeshBuffer(first).IsValid());
    EXPECT_FALSE(f.rrm.FindMeshBuffer(last).IsValid());
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshRelease, DrainPreservesUnrequestedEntriesAfterSwapRemoval)
{
    StreamingFixture  f;
    const uuids::uuid first    = MeshUUID(1);
    const uuids::uuid retained = MeshUUID(2);
    const uuids::uuid last     = MeshUUID(3);
    RRMTestHelper::RegisterMesh(f.rrm, first, {0, 1});
    RRMTestHelper::RegisterMesh(f.rrm, retained, {1, 1});
    RRMTestHelper::RegisterMesh(f.rrm, last, {2, 1});
    f.rrm.ReleaseMeshGeometry(first);
    f.rrm.ReleaseMeshGeometry(last);
    f.rrm.ReleaseMeshGeometry(MeshUUID(4));
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 2u);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_FALSE(f.rrm.FindMeshBuffer(first).IsValid());
    EXPECT_FALSE(f.rrm.FindMeshBuffer(last).IsValid());
    EXPECT_TRUE(f.rrm.FindMeshBuffer(retained).IsValid());
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 0u);
}

TEST(RenderResourceManagerMeshRelease, ProducerCanOverlapRenderDrain)
{
    StreamingFixture  f;
    const uuids::uuid uuid   = MeshUUID();
    GeometryRegion    region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});
    std::atomic<bool> done = false;
    std::thread       producer([&]() {
        for (uint32_t i = 0; i < 10000; ++i)
        {
            f.rrm.ReleaseMeshGeometry(uuid);
        }
        done.store(true, std::memory_order_release);
    });
    while (!done.load(std::memory_order_acquire))
    {
        RRMTestHelper::FlushMeshReleases(f.rrm);
        std::this_thread::yield();
    }
    producer.join();
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_FALSE(f.rrm.FindMeshBuffer(uuid).IsValid());
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).IdxUsed, 0u);
}

TEST(RenderResourceManagerMeshRelease, SceneRemovalQueuesOnlyTheLastSharedMeshInstance)
{
    StreamingFixture  f;
    const uuids::uuid uuid   = MeshUUID();
    GeometryRegion    region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(200, 100, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});
    Scenes::RenderScene scene = {};
    scene.Instances.init(&f.arena, 2);
    const uint32_t first  = scene.AddMeshInstance(uuid, "first");
    const uint32_t second = scene.AddMeshInstance(uuid, "second");

    scene.RemoveMeshInstance(first, &f.rrm);
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 0u);
    scene.RemoveMeshInstance(second, &f.rrm);
    EXPECT_EQ(RRMTestHelper::PendingMeshReleases(f.rrm), 1u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 200u);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

static_assert(!std::is_copy_constructible_v<RenderResourceManager::MeshGeometryPin>);
static_assert(!std::is_copy_assignable_v<RenderResourceManager::MeshGeometryPin>);
static_assert(std::is_nothrow_move_constructible_v<RenderResourceManager::MeshGeometryPin>);
static_assert(std::is_nothrow_move_assignable_v<RenderResourceManager::MeshGeometryPin>);

TEST(RenderResourceManagerMeshPin, InvalidStaleGenericAndUnloadedHandlesAreRefused)
{
    StreamingFixture                       f;
    RenderResourceManager::MeshGeometryPin empty = {};
    EXPECT_FALSE(empty);
    EXPECT_EQ(empty.Get(), nullptr);
    empty.Reset();
    EXPECT_FALSE(f.rrm.PinMeshGeometry({}));
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 1}));
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false);
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 1}));
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 0x80000002u}));
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Unloaded, false, false);
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 2}));
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerMeshPin, SameVersionSharesRecordAndFinalUnpinKeepsCurrentRegion)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    auto first  = f.rrm.PinMeshGeometry({0, 1});
    auto second = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    EXPECT_EQ(first.Get(), second.Get());
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
    first.Reset();
    EXPECT_FALSE(first);
    EXPECT_TRUE(second);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
    second.Reset();
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    auto again = f.rrm.PinMeshGeometry({0, 1});
    EXPECT_TRUE(again);
}

TEST(RenderResourceManagerMeshMetadata, UploadVersionsRetainMatchingLayoutBoundsAndMaterialIdentities)
{
    StreamingFixture f;
    TLSFSlab         slab = {};
    slab.Init(&f.arena, 32 * 1024);
    const auto                    initial_capacity = AvailableMetadataBlocks(slab);
    ZEngine::Importers::AssetMesh mesh             = {};
    mesh.Vertices.init(&f.arena, 48, 48);
    mesh.Indices.init(&f.arena, 6, 6);
    mesh.SubMeshes.init(&f.arena, 2);
    mesh.SubMeshes.push({.MaterialUUID = MeshUUID(10), .VertexCount = 6, .IndexCount = 6});
    mesh.BoundsCenter        = {1.f, 2.f, 3.f};
    mesh.BoundsRadius        = 4.f;
    mesh.BoundsMin           = {-3.f, -2.f, -1.f};
    mesh.BoundsMax           = {5.f, 6.f, 7.f};
    mesh.BoundsValid         = true;
    const auto* old_metadata = RRMTestHelper::CaptureMetadata(slab, mesh);
    ASSERT_NE(old_metadata, nullptr);

    GeometryRegion old_region = {}, new_region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 24, old_region));
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 24, new_region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, old_region, 6, 6, old_metadata);
    auto old_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(old_pin);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId preparation = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &preparation));
    RRMTestHelper::TickPreparation(f.rrm);

    mesh.SubMeshes[0] = {.MaterialUUID = MeshUUID(20), .VertexCount = 3, .IndexCount = 3};
    mesh.SubMeshes.push({.MaterialUUID = MeshUUID(21), .VertexCount = 3, .IndexCount = 3, .VertexOffset = 3, .IndexOffset = 3});
    mesh.BoundsCenter        = {10.f, 20.f, 30.f};
    mesh.BoundsRadius        = 40.f;
    mesh.BoundsValid         = false;
    const auto* new_metadata = RRMTestHelper::CaptureMetadata(slab, mesh);
    ASSERT_NE(new_metadata, nullptr);
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, new_region, 6, 6, new_metadata);
    auto new_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(new_pin);

    // Destroy the source views: neither version may borrow CPU cache payloads.
    mesh                  = {};
    const auto* old_draws = old_pin.Get()->DrawMetadata;
    const auto* new_draws = new_pin.Get()->DrawMetadata;
    ASSERT_EQ(old_draws->SubMeshes.size(), 1u);
    ASSERT_EQ(new_draws->SubMeshes.size(), 2u);
    EXPECT_EQ(old_draws->SubMeshes[0].MaterialUUID, MeshUUID(10));
    EXPECT_EQ(old_draws->SubMeshes[0].IndexCount, 6u);
    EXPECT_FLOAT_EQ(old_draws->BoundsCenter.x, 1.f);
    EXPECT_FLOAT_EQ(old_draws->BoundsRadius, 4.f);
    EXPECT_FLOAT_EQ(old_draws->BoundsMin.x, -3.f);
    EXPECT_FLOAT_EQ(old_draws->BoundsMax.z, 7.f);
    EXPECT_TRUE(old_draws->BoundsValid);
    EXPECT_EQ(new_draws->SubMeshes[1].MaterialUUID, MeshUUID(21));
    EXPECT_EQ(new_draws->SubMeshes[1].VertexOffset, 3u);
    EXPECT_EQ(new_draws->SubMeshes[1].IndexOffset, 3u);
    EXPECT_FLOAT_EQ(new_draws->BoundsCenter.x, 10.f);
    EXPECT_FLOAT_EQ(new_draws->BoundsRadius, 40.f);
    EXPECT_FALSE(new_draws->BoundsValid);
    RenderResourceManager::MeshGeometrySnapshot prepared = {};
    ASSERT_TRUE(f.rrm.GetPreparedSceneGeometry(preparation, MeshUUID(), &prepared));
    EXPECT_EQ(prepared.DrawMetadata, old_metadata);
    EXPECT_EQ(prepared.Version, old_pin.Get()->Version);

    RRMTestHelper::ResetGeometry(f.rrm);
    EXPECT_EQ(old_pin.Get()->DrawMetadata, old_metadata);
    EXPECT_EQ(new_pin.Get()->DrawMetadata, new_metadata);
    old_pin.Reset();
    new_pin.Reset();
    EXPECT_LT(AvailableMetadataBlocks(slab), initial_capacity);
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(preparation));
    RRMTestHelper::TickPreparation(f.rrm);
    // All per-version blocks must be reclaimed, including the detached old one.
    EXPECT_EQ(AvailableMetadataBlocks(slab), initial_capacity);
    slab.Shutdown();
}

TEST(RenderResourceManagerMeshMetadata, InvalidRangesAndSlabExhaustionRefuseWithoutFallback)
{
    StreamingFixture              f;
    TLSFSlab                      slab = {};
    ZEngine::Importers::AssetMesh mesh = {};
    mesh.Vertices.init(&f.arena, 24, 24);
    mesh.Indices.init(&f.arena, 3, 3);
    mesh.SubMeshes.init(&f.arena, 1);
    mesh.SubMeshes.push({.MaterialUUID = MeshUUID(), .VertexCount = 3, .IndexCount = 3});
    EXPECT_EQ(RRMTestHelper::CaptureMetadata(slab, mesh), nullptr);
    slab.Init(&f.arena, 16 * 1024);
    mesh.SubMeshes[0].IndexOffset = UINT32_MAX;
    EXPECT_EQ(RRMTestHelper::CaptureMetadata(slab, mesh), nullptr);
    mesh.SubMeshes[0].IndexOffset = 0;
    mesh.SubMeshes[0].VertexCount = 4;
    EXPECT_EQ(RRMTestHelper::CaptureMetadata(slab, mesh), nullptr);
    mesh.SubMeshes[0].VertexCount = 3;
    mesh.Vertices.push(0.f);
    EXPECT_EQ(RRMTestHelper::CaptureMetadata(slab, mesh), nullptr);
    mesh.Vertices.pop();
    std::vector<const RenderResourceManager::MeshDrawMetadata*> held = {};
    while (const auto* metadata = RRMTestHelper::CaptureMetadata(slab, mesh))
    {
        held.push_back(metadata);
    }
    ASSERT_FALSE(held.empty());
    EXPECT_EQ(RRMTestHelper::CaptureMetadata(slab, mesh), nullptr);
    EXPECT_EQ(held.front()->SubMeshes[0].MaterialUUID, MeshUUID());
    for (const auto* metadata : held)
    {
        RRMTestHelper::ReleaseMetadata(metadata);
    }
    const auto* recovered = RRMTestHelper::CaptureMetadata(slab, mesh);
    ASSERT_NE(recovered, nullptr);
    RRMTestHelper::ReleaseMetadata(recovered);
    slab.Shutdown();
}

TEST(RenderResourceManagerMeshMetadata, UnpinnedVersionsReclaimOnReleaseAndReset)
{
    StreamingFixture f;
    TLSFSlab         slab = {};
    slab.Init(&f.arena, 16 * 1024);
    const auto                          initial_capacity = AvailableMetadataBlocks(slab);
    const ZEngine::Importers::AssetMesh mesh             = {};
    for (uint32_t cycle = 0; cycle < 200; ++cycle)
    {
        RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
        GeometryRegion region = {};
        ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(96, 12, region));
        const auto* metadata = RRMTestHelper::CaptureMetadata(slab, mesh);
        ASSERT_NE(metadata, nullptr);
        RRMTestHelper::ReplaceGeometry(f.rrm, 0, region, 3, 3, metadata);
        if (cycle % 2 == 0)
        {
            f.rrm.Release({0, 1});
        }
        else
        {
            RRMTestHelper::ResetGeometry(f.rrm);
        }
        EXPECT_EQ(AvailableMetadataBlocks(slab), initial_capacity);
        EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    }
    slab.Shutdown();
}

TEST(RenderResourceManagerMeshMetadata, FrameAndCancelledPreparationRetainMetadataUntilTheirOwnGatesRetire)
{
    StreamingFixture f;
    TLSFSlab         slab = {};
    slab.Init(&f.arena, 16 * 1024);
    const auto                    initial_capacity = AvailableMetadataBlocks(slab);
    ZEngine::Importers::AssetMesh mesh             = {};
    mesh.Vertices.init(&f.arena, 24, 24);
    mesh.Indices.init(&f.arena, 3, 3);
    mesh.SubMeshes.init(&f.arena, 1);
    mesh.SubMeshes.push({.MaterialUUID = MeshUUID(), .VertexCount = 3, .IndexCount = 3});
    for (uint64_t cycle = 1; cycle <= 200; ++cycle)
    {
        RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
        GeometryRegion region = {};
        ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(96, 12, region));
        mesh.BoundsRadius    = static_cast<float>(cycle);
        const auto* metadata = RRMTestHelper::CaptureMetadata(slab, mesh);
        ASSERT_NE(metadata, nullptr);
        RRMTestHelper::ReplaceGeometry(f.rrm, 0, region, 3, 3, metadata);
        RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
        RRMTestHelper::SetUploadProgress(f.rrm, cycle, cycle, false);
        const std::array                                  meshes{MeshUUID()};
        RenderResourceManager::SceneResourcePreparationId id = {};
        ASSERT_TRUE(f.rrm.PrepareSceneResources({1, cycle}, cycle, meshes, &id));
        RRMTestHelper::TickPreparation(f.rrm, cycle - 1);
        RenderResourceManager::MeshGeometrySnapshot frame = {};
        ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &frame));
        EXPECT_EQ(frame.DrawMetadata, metadata);
        RRMTestHelper::StampGeometry(f.rrm, 0, cycle);
        f.rrm.FinishResourceFrame();
        ASSERT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::ResetGeometry(f.rrm);
        RRMTestHelper::TickPreparation(f.rrm, cycle - 1);
        EXPECT_LT(AvailableMetadataBlocks(slab), initial_capacity);
        EXPECT_FLOAT_EQ(frame.DrawMetadata->BoundsRadius, static_cast<float>(cycle));
        // Dropping the upload gate must not drop the accepted frame consumer.
        RRMTestHelper::TickPreparation(f.rrm, cycle);
        EXPECT_LT(AvailableMetadataBlocks(slab), initial_capacity);
        EXPECT_FLOAT_EQ(frame.DrawMetadata->BoundsRadius, static_cast<float>(cycle));
        RRMTestHelper::CompleteGeometry(f.rrm, {cycle, 0, 0, 0});
        EXPECT_EQ(AvailableMetadataBlocks(slab), initial_capacity);
        EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    }
    slab.Shutdown();
}

TEST(RenderResourceManagerMeshPin, HotReloadRetainsOldImmutableOffsetsAndCounts)
{
    StreamingFixture f;
    GeometryRegion   old_region = {};
    GeometryRegion   new_region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, old_region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, old_region, 6, 3);
    auto old_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(old_pin);
    const auto*    snapshot    = old_pin.Get();
    const uint64_t old_version = snapshot->Version;
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(320, 24, new_region));
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, new_region, 10, 6);
    auto new_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(new_pin);
    EXPECT_NE(old_version, new_pin.Get()->Version);
    EXPECT_EQ(old_pin.Get(), snapshot);
    EXPECT_EQ(snapshot->Region.VtxByteOffset, old_region.VtxByteOffset);
    EXPECT_EQ(snapshot->VertexCount, 6u);
    EXPECT_EQ(snapshot->IndexCount, 3u);
    EXPECT_EQ(new_pin.Get()->Region.VtxByteOffset, new_region.VtxByteOffset);
    EXPECT_EQ(new_pin.Get()->VertexCount, 10u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 512u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 2u);
    old_pin.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 320u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
    f.rrm.Release({0, 1});
    EXPECT_TRUE(new_pin);
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 1}));
    new_pin.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshPin, EvictionAndCompactionPreservePinnedGeometry)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    GeometryRegion   filler = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    auto pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(pin);
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(3500, 3500, filler));
    f.mgr.RequestEvict({0, 1});
    f.mgr.Tick(0);
    RRMTestHelper::CompactGeometry(f.rrm);
    EXPECT_TRUE(f.rrm.IsMeshResident({0, 1}));
    EXPECT_EQ(RRMTestHelper::GetRegion(f.rrm, 0).VtxByteOffset, pin.Get()->Region.VtxByteOffset);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 3692u);
    pin.Reset();
    f.mgr.RequestEvict({0, 1});
    EXPECT_FALSE(f.rrm.IsMeshResident({0, 1}));
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 3500u);
}

TEST_F(SceneResourceBundleTest, CompactionWithEvictedCpuSourcePreservesResidentGeometry)
{
    auto*      assets = Assets::Instance();
    const auto uuid   = MeshUUID(42);
    const auto asset  = Assets::CreateHandle(0, ZEngine::Managers::AssetType::MESH);
    ASSERT_TRUE(assets->Registry->RegisterLoaded(uuid, ZEngine::Managers::AssetType::MESH, {}, {}, asset).IsOk());
    RRMTestHelper::SetRegistry(Fixture->rrm, assets->Registry);
    GeometryRegion region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(Fixture->rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(Fixture->rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(Fixture->rrm, uuid, {0, 1});
    // Loaded slot identity and GPU residency deliberately outlive CPU payloads.
    EXPECT_EQ(assets->GetMeshAsset(uuid), nullptr);
    const auto cursor = RRMTestHelper::GetPool(Fixture->rrm).VtxCursor;
    RRMTestHelper::CompactGeometry(Fixture->rrm);
    EXPECT_TRUE(Fixture->rrm.IsMeshResident({0, 1}));
    EXPECT_EQ(RRMTestHelper::GetRegion(Fixture->rrm, 0).VtxByteOffset, region.VtxByteOffset);
    EXPECT_EQ(RRMTestHelper::GetPool(Fixture->rrm).VtxCursor, cursor);
    EXPECT_EQ(RRMTestHelper::GetPool(Fixture->rrm).VtxUsed, 192u);
}

TEST(RenderResourceManagerMeshPin, QueuedReleaseDetachesWithoutReclaimingPinnedBytes)
{
    StreamingFixture  f;
    const uuids::uuid uuid   = MeshUUID();
    GeometryRegion    region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, uuid, {0, 1});
    auto pin    = f.rrm.PinMeshGeometry({0, 1});
    auto shared = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(pin);
    f.rrm.ReleaseMeshGeometry(uuid);
    RRMTestHelper::FlushMeshReleases(f.rrm);
    EXPECT_FALSE(f.rrm.FindMeshBuffer(uuid).IsValid());
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    pin.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    shared.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshPin, ResetAndSlotReuseCannotOverwriteAnOlderPinnedVersion)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    auto old_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(old_pin);
    const uint64_t version = old_pin.Get()->Version;
    RRMTestHelper::ResetGeometry(f.rrm);
    RRMTestHelper::ResetGeometry(f.rrm);
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 1}));
    GeometryRegion new_region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(320, 24, new_region));
    EXPECT_GE(new_region.VtxByteOffset, region.VtxByteSize);
    RRMTestHelper::SetSlot(f.rrm, 0, 2, RenderResourceManager::StreamingState::Resident, false, false, new_region);
    auto new_pin = f.rrm.PinMeshGeometry({0, 2});
    ASSERT_TRUE(new_pin);
    EXPECT_NE(new_pin.Get()->Version, version);
    f.rrm.Release({0, 1}); // stale release must not touch the reused slot
    EXPECT_TRUE(f.rrm.IsMeshResident({0, 2}));
    EXPECT_EQ(old_pin.Get()->Version, version);
    old_pin.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 320u);
    new_pin.Reset();
    RRMTestHelper::ResetGeometry(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxCursor, 0u);
}

TEST(RenderResourceManagerMeshPin, MoveAssignmentRetiresPreviousPinAndTransfersExactlyOnce)
{
    StreamingFixture f;
    GeometryRegion   first_region  = {};
    GeometryRegion   second_region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, first_region));
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(320, 24, second_region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, first_region);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Resident, false, false, second_region);
    auto first  = f.rrm.PinMeshGeometry({0, 1});
    auto second = f.rrm.PinMeshGeometry({1, 1});
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const uint64_t version = second.Get()->Version;
    RRMTestHelper::ResetGeometry(f.rrm);
    first = std::move(second);
    EXPECT_FALSE(second);
    EXPECT_EQ(first.Get()->Version, version);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 320u);
    auto moved = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_EQ(moved.Get()->Version, version);
    moved.Reset();
    moved.Reset();
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerMeshPin, SaturationRefusesNewVersionWithoutInvalidatingExistingPins)
{
    StreamingFixture f;
    auto&            pool = RRMTestHelper::GetPool(f.rrm);
    pool.VtxCapacity      = RRMTestHelper::RegistrationCapacity() * 64;
    pool.IdxCapacity      = RRMTestHelper::RegistrationCapacity() * 32;
    std::vector<RenderResourceManager::MeshGeometryPin> pins;
    pins.reserve(RRMTestHelper::RegistrationCapacity());
    GeometryRegion region = {};
    ASSERT_TRUE(pool.Allocate(32, 4, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    for (uint32_t i = 0; i < RRMTestHelper::RegistrationCapacity(); ++i)
    {
        auto pin = f.rrm.PinMeshGeometry({0, 1});
        ASSERT_TRUE(pin);
        pins.push_back(std::move(pin));
        ASSERT_TRUE(pool.Allocate(32, 4, region));
        RRMTestHelper::ReplaceGeometry(f.rrm, 0, region);
    }
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), RRMTestHelper::RegistrationCapacity());
    EXPECT_FALSE(f.rrm.PinMeshGeometry({0, 1}));
    EXPECT_TRUE(pins.front());
    EXPECT_TRUE(pins.back());
    const uint64_t oldest = pins.front().Get()->Version;
    pins.front().Reset();
    auto current = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(current);
    EXPECT_NE(current.Get()->Version, oldest);
    RRMTestHelper::ResetGeometry(f.rrm);
    pins.clear();
    current.Reset();
    EXPECT_EQ(pool.VtxUsed, 0u);
    EXPECT_EQ(pool.IdxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerMeshPin, VersionIdentityExhaustionDoesNotWrapOrInvalidatePins)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    auto pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(pin);
    const uint64_t version = pin.Get()->Version;
    RRMTestHelper::ExhaustVersionIdentities(f.rrm);
    EXPECT_EQ(RRMTestHelper::NextVersion(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::NextVersion(f.rrm), 0u);
    EXPECT_EQ(pin.Get()->Version, version);
}

TEST(RenderResourceManagerSceneGeometry, AdmissionValidatesAndCopiesImmutableRequirements)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(1), {0, 1});
    std::array                                        meshes{MeshUUID(1)};
    RenderResourceManager::SceneResourcePreparationId id = {};
    EXPECT_FALSE(f.rrm.PrepareSceneResources({}, 1, meshes, &id));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 0, meshes, &id));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, UINT64_MAX, meshes, &id));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, nullptr));
    const std::array nil{uuids::uuid{}};
    const std::array duplicate{MeshUUID(1), MeshUUID(1)};
    const std::array unsorted{MeshUUID(2), MeshUUID(1)};
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, nil, &id));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, duplicate, &id));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, unsorted, &id));
    const std::vector<uuids::uuid> oversized(RenderResourceManager::SceneGeometryMeshCapacity + 1);
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, oversized, &id));
    EXPECT_FALSE(id);
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 7, meshes, &id));
    meshes[0]                                                    = MeshUUID(2);
    RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Preparing);
    EXPECT_EQ(result.ExpectedGeneration, 7u);
    RRMTestHelper::TickPreparation(f.rrm);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::GeometryReady);
    EXPECT_EQ(result.MeshCount, 1u);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    EXPECT_TRUE(f.rrm.GetPreparedSceneGeometry(id, MeshUUID(1), &snapshot));
    EXPECT_FALSE(f.rrm.GetPreparedSceneGeometry(id, MeshUUID(2), &snapshot));
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(f.rrm);
    EXPECT_FALSE(f.rrm.PollSceneResourcePreparation(id, &result));
}

TEST(RenderResourceManagerSceneGeometry, ReadinessRequiresBothAcceptedSubmissionAndCompletedUpload)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    RRMTestHelper::SetUploadProgress(f.rrm, 3, 3, true);
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm, 100);
    RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Preparing);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    EXPECT_FALSE(f.rrm.GetPreparedSceneGeometry(id, MeshUUID(), &snapshot));
    RRMTestHelper::SetUploadProgress(f.rrm, 4, 4, false);
    RRMTestHelper::TickPreparation(f.rrm, 3);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Preparing);
    RRMTestHelper::TickPreparation(f.rrm, 4);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::GeometryReady);
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(f.rrm, 4);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerSceneGeometry, CancellationRetainsDetachedBytesUntilTheProducerRetires)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    RRMTestHelper::SetUploadProgress(f.rrm, 0, 0, true);
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm);
    ASSERT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::ResetGeometry(f.rrm);
    RRMTestHelper::TickPreparation(f.rrm, 100);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u); // not submitted yet
    RRMTestHelper::SetUploadProgress(f.rrm, 1, 1, false);
    RRMTestHelper::TickPreparation(f.rrm, 0);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    RRMTestHelper::TickPreparation(f.rrm, 1);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerSceneGeometry, ReadySnapshotSurvivesHotReloadReleaseAndReset)
{
    StreamingFixture f;
    auto&            pool       = RRMTestHelper::GetPool(f.rrm);
    GeometryRegion   old_region = {}, replacement = {};
    ASSERT_TRUE(pool.Allocate(192, 12, old_region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, old_region);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm);
    RenderResourceManager::MeshGeometrySnapshot before = {}, after = {};
    ASSERT_TRUE(f.rrm.GetPreparedSceneGeometry(id, MeshUUID(), &before));
    ASSERT_TRUE(pool.Allocate(320, 24, replacement));
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, replacement);
    f.rrm.Release({0, 1});
    RRMTestHelper::ResetGeometry(f.rrm);
    ASSERT_TRUE(f.rrm.GetPreparedSceneGeometry(id, MeshUUID(), &after));
    EXPECT_EQ(after.Version, before.Version);
    EXPECT_EQ(after.Region.VtxByteOffset, old_region.VtxByteOffset);
    EXPECT_EQ(pool.VtxUsed, 192u);
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(f.rrm);
    EXPECT_EQ(pool.VtxUsed, 0u);
}

TEST(RenderResourceManagerSceneGeometry, MissingOrNonresidentRequiredGeometryFailsWithoutFallback)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    RRMTestHelper::SetSlot(f.rrm, 1, 1, RenderResourceManager::StreamingState::Unloaded, false, false);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(1), {0, 1});
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(2), {1, 1});
    for (uint32_t i = 2; i < 4; ++i)
    {
        const std::array                                  meshes{MeshUUID(1), MeshUUID(i)};
        RenderResourceManager::SceneResourcePreparationId id = {};
        ASSERT_TRUE(f.rrm.PrepareSceneResources({1, i}, 1, meshes, &id));
        RRMTestHelper::TickPreparation(f.rrm);
        RenderResourceManager::SceneResourcePreparationResult result = {};
        ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
        EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Failed);
        EXPECT_EQ(result.Error, i == 2 ? RenderResourceManager::SceneResourcePreparationError::NotResident : RenderResourceManager::SceneResourcePreparationError::MissingGeometry);
        EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
        EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
        RRMTestHelper::TickPreparation(f.rrm);
        EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
    }
}

TEST(RenderResourceManagerSceneGeometry, ProcessingAndCleanupRespectTheSharedSliceBudget)
{
    StreamingFixture                                                                 f;
    std::array<uuids::uuid, RenderResourceManager::SceneResourceSliceOperations + 1> meshes = {};
    for (uint32_t i = 0; i < meshes.size(); ++i)
    {
        meshes[i] = MeshUUID(i + 1);
        RRMTestHelper::SetSlot(f.rrm, i, 1, RenderResourceManager::StreamingState::Resident, false, false);
        RRMTestHelper::RegisterMesh(f.rrm, meshes[i], {i, 1});
    }
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm);
    EXPECT_EQ(RRMTestHelper::PreparedPins(f.rrm, id.Slot), RenderResourceManager::SceneResourceSliceOperations);
    RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Preparing);
    RRMTestHelper::TickPreparation(f.rrm);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::GeometryReady);
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(f.rrm);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 1u);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Releasing);
    RRMTestHelper::TickPreparation(f.rrm);
    EXPECT_FALSE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerSceneGeometry, SaturationAndStaleResultsCannotOverwriteAdmittedRequests)
{
    StreamingFixture                                  f;
    RenderResourceManager::SceneResourcePreparationId first = {}, second = {}, rejected = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, {}, &first));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 1}, 1, {}, &rejected));
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 2}, 1, {}, &second));
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 3}, 1, {}, &rejected));
    EXPECT_FALSE(rejected);
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 3}, 1, {}, &first));
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(first));
    RRMTestHelper::TickPreparation(f.rrm);
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 3}, 2, {}, &rejected));
    EXPECT_NE(first.Serial, rejected.Serial);
    EXPECT_FALSE(f.rrm.ReleaseSceneResourcePreparation(first));
    RenderResourceManager::SceneResourcePreparationResult result = {};
    EXPECT_FALSE(f.rrm.PollSceneResourcePreparation(first, &result));
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(second));
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(rejected));
    RRMTestHelper::TickPreparation(f.rrm);
    RRMTestHelper::SealPreparation(f.rrm);
    first = {};
    EXPECT_FALSE(f.rrm.PrepareSceneResources({1, 4}, 2, {}, &first));
}

TEST(RenderResourceManagerSceneGeometry, UploadFailureIsTerminalAndShutdownDropsOnlyOwnedPins)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    auto                                              explicit_pin = f.rrm.PinMeshGeometry({0, 1});
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm);
    RRMTestHelper::TickPreparation(f.rrm, 100, true); // device loss after geometry-ready
    RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Failed);
    EXPECT_EQ(result.Error, RenderResourceManager::SceneResourcePreparationError::UploadFailed);
    RRMTestHelper::TickPreparation(f.rrm, 1000);
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Failed);
    RRMTestHelper::DrainPreparationAfterIdle(f.rrm);
    EXPECT_FALSE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_TRUE(explicit_pin);
    EXPECT_EQ(RRMTestHelper::PinReferences(f.rrm, 0), 1u);
    explicit_pin.Reset();
}

TEST(RenderResourceManagerSceneGeometry, ConcurrentCancellationAndCompletionDoNotLoseCleanupOrPublishStaleResults)
{
    StreamingFixture f;
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    const std::array  meshes{MeshUUID()};
    std::atomic<bool> stop{false};
    std::thread       render([&] {
        while (!stop.load(std::memory_order_acquire))
        {
            RRMTestHelper::TickPreparation(f.rrm);
            std::this_thread::yield();
        }
    });
    for (uint64_t i = 1; i <= 1000; ++i)
    {
        RenderResourceManager::SceneResourcePreparationId id = {};
        while (!f.rrm.PrepareSceneResources({1, i}, 1, meshes, &id))
        {
            std::this_thread::yield();
        }
        RenderResourceManager::SceneResourcePreparationResult result = {};
        EXPECT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
        EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
        while (f.rrm.PollSceneResourcePreparation(id, &result))
        {
            EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Releasing);
            std::this_thread::yield();
        }
    }
    stop.store(true, std::memory_order_release);
    render.join();
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerSceneGeometry, PinTableExhaustionFailsWithoutStealingRetainedVersions)
{
    StreamingFixture f;
    auto&            pool = RRMTestHelper::GetPool(f.rrm);
    pool.VtxCapacity      = RenderResourceManager::SceneGeometryMeshCapacity * 64;
    pool.IdxCapacity      = RenderResourceManager::SceneGeometryMeshCapacity * 32;
    GeometryRegion region = {};
    ASSERT_TRUE(pool.Allocate(32, 4, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RRMTestHelper::RegisterMesh(f.rrm, MeshUUID(), {0, 1});
    std::vector<RenderResourceManager::MeshGeometryPin> retained = {};
    retained.reserve(RenderResourceManager::SceneGeometryMeshCapacity);
    for (uint32_t i = 0; i < RenderResourceManager::SceneGeometryMeshCapacity; ++i)
    {
        auto pin = f.rrm.PinMeshGeometry({0, 1});
        ASSERT_TRUE(pin);
        retained.push_back(std::move(pin));
        ASSERT_TRUE(pool.Allocate(32, 4, region));
        RRMTestHelper::ReplaceGeometry(f.rrm, 0, region);
    }
    const std::array                                  meshes{MeshUUID()};
    RenderResourceManager::SceneResourcePreparationId id = {};
    ASSERT_TRUE(f.rrm.PrepareSceneResources({1, 1}, 1, meshes, &id));
    RRMTestHelper::TickPreparation(f.rrm);
    RenderResourceManager::SceneResourcePreparationResult result = {};
    ASSERT_TRUE(f.rrm.PollSceneResourcePreparation(id, &result));
    EXPECT_EQ(result.State, RenderResourceManager::SceneResourcePreparationState::Failed);
    EXPECT_EQ(result.Error, RenderResourceManager::SceneResourcePreparationError::PinCapacity);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), retained.size());
    for (const auto& pin : retained)
    {
        EXPECT_TRUE(pin);
    }
    EXPECT_TRUE(f.rrm.ReleaseSceneResourcePreparation(id));
    RRMTestHelper::TickPreparation(f.rrm);
    retained.clear();
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
    EXPECT_EQ(pool.VtxUsed, 32u);
}

TEST(RenderResourceManagerFrameGeometry, RepeatedDrawsShareOneImplicitPinAndUnsubmittedWorkReleasesIt)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    EXPECT_FALSE(f.rrm.GetFrameMeshGeometry({0, 1}, nullptr));
    EXPECT_FALSE(f.rrm.GetFrameMeshGeometry({0, 2}, &snapshot));
    for (uint32_t i = 0; i < 10000; ++i)
    {
        ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    }
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 1u);
    EXPECT_EQ(RRMTestHelper::PinReferences(f.rrm, 0), 1u);
    EXPECT_EQ(snapshot.VertexOffset(), region.VtxByteOffset / 32);
    EXPECT_EQ(snapshot.IndexOffset(), region.IdxByteOffset / 4);
    f.rrm.FinishResourceFrame();
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
}

TEST(RenderResourceManagerFrameGeometry, EveryAcceptedQueueAndFinalSubmissionMustComplete)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    RRMTestHelper::StampGeometry(f.rrm, static_cast<uint32_t>(QueueType::GRAPHIC_QUEUE), 3);
    RRMTestHelper::StampGeometry(f.rrm, static_cast<uint32_t>(QueueType::TRANSFER_QUEUE), 4);
    RRMTestHelper::StampGeometry(f.rrm, static_cast<uint32_t>(QueueType::COMPUTE_QUEUE), 5);
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 7);
    f.rrm.FinishResourceFrame();
    f.rrm.Release({0, 1});
    RRMTestHelper::CompleteGeometry(f.rrm, {3, 4, 5, 6});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    RRMTestHelper::CompleteGeometry(f.rrm, {3, 4, 4, 7});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    RRMTestHelper::CompleteGeometry(f.rrm, {3, 4, 5, 7});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerFrameGeometry, AbortedPresentationCannotReleaseAcceptedGraphWork)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    RRMTestHelper::StampGeometry(f.rrm, static_cast<uint32_t>(QueueType::GRAPHIC_QUEUE), 3);
    f.rrm.FinishResourceFrame(); // no final submission accepted
    RRMTestHelper::ResetGeometry(f.rrm);
    RRMTestHelper::CompleteGeometry(f.rrm, {2, 0, 0, 100});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    RRMTestHelper::CompleteGeometry(f.rrm, {3, 0, 0, 100});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerFrameGeometry, CancelledRecordingDoesNotTakeOwnershipOfExplicitPins)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    auto explicit_pin = f.rrm.PinMeshGeometry({0, 1});
    ASSERT_TRUE(explicit_pin);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    EXPECT_EQ(RRMTestHelper::PinReferences(f.rrm, 0), 2u);
    f.rrm.Release({0, 1});
    f.rrm.FinishResourceFrame();
    EXPECT_TRUE(explicit_pin);
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    explicit_pin.Reset();
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerFrameGeometry, RepeatedFramesExtendTheLastConsumerWithoutAddingPins)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 4);
    f.rrm.FinishResourceFrame();
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    EXPECT_EQ(RRMTestHelper::PinReferences(f.rrm, 0), 1u);
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 9);
    f.rrm.FinishResourceFrame();
    f.rrm.Release({0, 1});
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 4});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 192u);
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 9});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerFrameGeometry, RecordingCannotLoseItsPinToAnOlderCompletion)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 4);
    f.rrm.FinishResourceFrame();
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 4});
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 1u);
    f.rrm.FinishResourceFrame(); // second recording cancelled, prior work is complete
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 0u);
    EXPECT_EQ(RRMTestHelper::PinnedVersions(f.rrm), 0u);
}

TEST(RenderResourceManagerFrameGeometry, ResetRetainsEachVersionUntilItsOwnLastSubmission)
{
    StreamingFixture f;
    GeometryRegion   region = {};
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(192, 12, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot old_snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &old_snapshot));
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 1);
    f.rrm.FinishResourceFrame();
    ASSERT_TRUE(RRMTestHelper::GetPool(f.rrm).Allocate(320, 24, region));
    RRMTestHelper::ReplaceGeometry(f.rrm, 0, region);
    RenderResourceManager::MeshGeometrySnapshot new_snapshot = {};
    ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &new_snapshot));
    EXPECT_NE(old_snapshot.Version, new_snapshot.Version);
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 2);
    f.rrm.FinishResourceFrame();
    RRMTestHelper::ResetGeometry(f.rrm);
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 512u);
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 1});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 320u);
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 2});
    EXPECT_EQ(RRMTestHelper::GetPool(f.rrm).VtxUsed, 0u);
}

TEST(RenderResourceManagerFrameGeometry, SaturationSkipsNewDrawsAndResumesAfterRetirement)
{
    StreamingFixture f;
    auto&            pool = RRMTestHelper::GetPool(f.rrm);
    pool.VtxCapacity      = RRMTestHelper::RegistrationCapacity() * 64;
    pool.IdxCapacity      = RRMTestHelper::RegistrationCapacity() * 32;
    GeometryRegion region = {};
    ASSERT_TRUE(pool.Allocate(32, 4, region));
    RRMTestHelper::SetSlot(f.rrm, 0, 1, RenderResourceManager::StreamingState::Resident, false, false, region);
    RenderResourceManager::MeshGeometrySnapshot snapshot = {};
    for (uint32_t i = 0; i < RRMTestHelper::RegistrationCapacity(); ++i)
    {
        ASSERT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
        ASSERT_TRUE(pool.Allocate(32, 4, region));
        RRMTestHelper::ReplaceGeometry(f.rrm, 0, region);
    }
    const uint64_t previous = snapshot.Version;
    EXPECT_FALSE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    EXPECT_EQ(snapshot.Version, previous);
    RRMTestHelper::StampGeometry(f.rrm, RenderResourceManager::ResourceFrameTimelineIndex, 1);
    f.rrm.FinishResourceFrame();
    RRMTestHelper::CompleteGeometry(f.rrm, {0, 0, 0, 1});
    EXPECT_EQ(RRMTestHelper::FrameHeldVersions(f.rrm), 0u);
    EXPECT_EQ(pool.VtxUsed, 32u);
    EXPECT_TRUE(f.rrm.GetFrameMeshGeometry({0, 1}, &snapshot));
    f.rrm.FinishResourceFrame();
}
