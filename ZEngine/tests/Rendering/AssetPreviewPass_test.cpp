#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <gtest/gtest.h>
#include <cmath>
#include <limits>
#include <thread>
#include "AssetPreviewPassTestAccess.h"

using namespace ZEngine;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::Core::VFS;
using namespace ZEngine::Rendering;
using namespace ZEngine::Rendering::Renderers;

namespace
{
    struct PreviewBindingTestPass : RenderPasses::GraphicPass
    {
        using RenderPasses::DescriptorBoundPass::BoundBindings;
        void InitializeNames(Hardwares::VulkanDevice* device)
        {
            InitializeDescriptorBindings(device);
        }
        void BindNames(Core::Memory::ArenaAllocator* arena)
        {
            BoundBindings.init(arena, 4);
            BoundBindings.insert("UBPreview");
        }
    };
} // namespace

TEST(AssetPreviewPassTest, ForwardBindingTrackingSupportsTheDescriptorReplayBudget)
{
    Core::Memory::MemoryManager memory;
    memory.Initialize(ZMega(1), {});
    auto device   = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena = &memory.MainArena;
    PreviewBindingTestPass pass;
    pass.InitializeNames(device.get());
    char names[RenderPasses::kMaxDescriptorReplayBindings][16] = {};
    for (uint32_t i = 0; i < RenderPasses::kMaxDescriptorReplayBindings; ++i)
    {
        snprintf(names[i], sizeof(names[i]), "Binding-%u", i);
        pass.BoundBindings.insert(names[i]);
    }
    EXPECT_EQ(pass.BoundBindings.size(), RenderPasses::kMaxDescriptorReplayBindings);
    for (const auto& name : names)
        EXPECT_TRUE(pass.BoundBindings.contains(name));
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, RetiredThumbnailDescriptorUpdatesAreDroppedRatherThanRequeuedForever)
{
    Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    auto device              = std::make_unique<Hardwares::VulkanDevice>();
    device->Arena            = &memory.MainArena;
    device->MaxGlobalTexture = 32;
    device->GlobalTextures.Initialize(&memory.MainArena, 32);
    device->RequestDescriptorUpdate({.Index = 7, .Generation = 1});
    EXPECT_FALSE(device->TextureHandleToUpdates.empty());
    // No live image means no Vulkan descriptor write is needed. The old
    // generation must not keep material-preview readiness pending indefinitely.
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, BindlessCapacityLeavesRoomForTheForwardMaterialEnvironment)
{
    EXPECT_EQ(Hardwares::GlobalTextureCapacity(8192, 128), 125u);
    EXPECT_EQ(Hardwares::GlobalTextureCapacity(32, 128), 32u);
    EXPECT_EQ(Hardwares::GlobalTextureCapacity(8192, 3), 0u);
    EXPECT_EQ(Hardwares::GlobalTextureCapacity(8192, 0), 0u);
}

TEST(AssetPreviewPassTest, OptionalAllocationPreservesBindlessHeadroom)
{
    Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    auto device              = std::make_unique<Hardwares::VulkanDevice>();
    device->MaxGlobalTexture = 32;
    device->GlobalTextures.Initialize(&memory.MainArena, 32);
    for (uint32_t i = 0; i < 16; ++i)
        ASSERT_TRUE(device->GlobalTextures.Create().Valid());
    // Rejection occurs before constructing any Vulkan image, under the same
    // allocation mutex as ordinary scene textures.
    EXPECT_FALSE(device->CreateTexture({}, "Preview", RenderResourceManager::ThumbnailTextureSlotReserve).Valid());
    EXPECT_EQ(device->GlobalTextures.Size(), 16u);
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, SharedUniformCountsOnceAcrossShaderStages)
{
    Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    Shaders::Shader shader;
    shader.LayoutBindingSpecificationMap.init(&memory.MainArena, 2);
    auto& bindings = shader.LayoutBindingSpecificationMap[0];
    bindings.init(&memory.MainArena, 4);
    bindings.push({.Set = 0, .Binding = 0, .Name = "UBPreview", .DescriptorTypeValue = Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = Specifications::ShaderStageFlags::VERTEX});
    bindings.push({.Set = 0, .Binding = 0, .Name = "UBPreview", .DescriptorTypeValue = Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = Specifications::ShaderStageFlags::FRAGMENT});
    EXPECT_EQ(shader.DynamicDescriptorCount(), 1u);
    shader.LayoutBindingSpecifications.init(&memory.MainArena, 2);
    shader.LayoutBindingSpecifications.push(bindings[0]);
    shader.LayoutBindingSpecifications.push(bindings[1]);
    Pipelines::GraphicPipeline pipeline = {};
    pipeline.Shader                     = &shader;
    PreviewBindingTestPass pass         = {};
    pass.Pipeline                       = &pipeline;
    pass.BindNames(&memory.MainArena);
    EXPECT_TRUE(pass.Verify());
    // This CPU-only pass does not own a Vulkan pipeline.
    pass.Pipeline   = nullptr;
    pipeline.Shader = nullptr;
    bindings.push({.Set = 0, .Binding = 1, .Count = 3, .Name = "Array", .DescriptorTypeValue = Specifications::DescriptorType::STORAGE_BUFFER_DYNAMIC, .Flags = Specifications::ShaderStageFlags::VERTEX});
    auto& second = shader.LayoutBindingSpecificationMap[2];
    second.init(&memory.MainArena, 1);
    second.push({.Set = 2, .Binding = 0, .Name = "Other", .DescriptorTypeValue = Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = Specifications::ShaderStageFlags::VERTEX});
    EXPECT_EQ(shader.DynamicDescriptorCount(), 5u);
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, VertexOnlyOrMissingShaderModulesCannotProduceAColorPreview)
{
    Core::Memory::MemoryManager memory;
    memory.Initialize(ZMega(1), {});
    Shaders::Shader shader;
    shader.ShaderCreateInfos.init(&memory.MainArena, 2);
    const auto required = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    EXPECT_FALSE(shader.HasStages(required));
    const auto module = reinterpret_cast<VkShaderModule>(uintptr_t(1)); // identity only; no Vulkan call
    shader.ShaderCreateInfos.push({.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = module});
    EXPECT_TRUE(shader.HasStages(VK_SHADER_STAGE_VERTEX_BIT));
    EXPECT_FALSE(shader.HasStages(required));
    shader.ShaderCreateInfos.push({.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = VK_NULL_HANDLE});
    EXPECT_FALSE(shader.HasStages(required));
    shader.ShaderCreateInfos[1].module = module;
    EXPECT_TRUE(shader.HasStages(required));
    shader.ShaderCreateInfos.clear();
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, IncompleteShaderDrawIsRejectedWithoutPublishingOrCachingASuccess)
{
    Core::Memory::MemoryManager memory;
    memory.Initialize(ZMega(1), {});
    Shaders::Shader shader;
    shader.ShaderCreateInfos.init(&memory.MainArena, 1);
    shader.ShaderCreateInfos.push({.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = reinterpret_cast<VkShaderModule>(uintptr_t(1))});
    Pipelines::GraphicPipeline pipeline;
    pipeline.Shader = &shader;
    PreviewBindingTestPass backend;
    backend.Pipeline = &pipeline;
    AssetPreviewPass              preview;
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    AssetPreviewPassTestAccess::Arm(preview, texture);
    AssetPreviewPassTestAccess::Published(preview);
    // A non-null sentinel proves rejection happens before touching commands or
    // the device. A colorless draw must not set the recorded/success latch.
    auto* commands = reinterpret_cast<Hardwares::CommandBuffer*>(uintptr_t(1));
    EXPECT_FALSE(preview.RecordDraw(nullptr, nullptr, nullptr, &backend, nullptr, commands));
    preview.EndFrame();
    EXPECT_EQ(preview.Observe(texture), AssetPreviewPass::Result::Failed);
    backend.Pipeline = nullptr;
    pipeline.Shader  = nullptr;
    shader.ShaderCreateInfos.clear();
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, ValidatedBoundsProduceFiniteFramingWithMargin)
{
    Mat4f matrix = {};
    ASSERT_TRUE(AssetPreviewPass::FrameBounds({12, -48, 5}, 0.01f, &matrix));
    // A radius-one sphere bounds the normalized vertices. Every sampled point
    // remains inside the viewport and between derived near/far planes.
    for (uint32_t y = 0; y <= 16; ++y)
        for (uint32_t x = 0; x < 32; ++x)
        {
            const float longitude = float(x) * 6.2831853f / 32;
            const float latitude  = float(y) * 3.14159265f / 16;
            const Vec4f p(std::sin(latitude) * std::cos(longitude), std::cos(latitude), std::sin(latitude) * std::sin(longitude), 1);
            const auto  clip = matrix * p;
            ASSERT_GT(clip.w, 0);
            EXPECT_LT(std::abs(clip.x / clip.w), 0.9f);
            EXPECT_LT(std::abs(clip.y / clip.w), 0.9f);
            EXPECT_GT(clip.z / clip.w, 0);
            EXPECT_LT(clip.z / clip.w, 1);
        }
    EXPECT_TRUE(AssetPreviewPass::FrameBounds({}, 100000, &matrix));
    EXPECT_FALSE(AssetPreviewPass::FrameBounds({}, 0, &matrix));
    EXPECT_FALSE(AssetPreviewPass::FrameBounds({}, -1, &matrix));
    EXPECT_FALSE(AssetPreviewPass::FrameBounds({}, std::numeric_limits<float>::infinity(), &matrix));
    EXPECT_FALSE(AssetPreviewPass::FrameBounds({NAN, 0, 0}, 1, &matrix));
}

TEST(AssetPreviewPassTest, RecordingCancellationRetainsOwnershipUntilTerminalResult)
{
    AssetPreviewPass              pass    = {};
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    AssetPreviewPassTestAccess::Arm(pass, texture);
    EXPECT_EQ(pass.Observe(texture), AssetPreviewPass::Result::Pending);
    EXPECT_TRUE(pass.Discard(texture));
    EXPECT_TRUE(AssetPreviewPassTestAccess::Abandoned(pass));
    EXPECT_FALSE(AssetPreviewPassTestAccess::Empty(pass));
    pass.Forget(texture);
    EXPECT_FALSE(AssetPreviewPassTestAccess::Empty(pass));
    AssetPreviewPassTestAccess::Cancel(pass);
    EXPECT_EQ(pass.Observe(texture), AssetPreviewPass::Result::Failed);
    pass.Forget(texture);
    EXPECT_TRUE(AssetPreviewPassTestAccess::Empty(pass));
}

TEST(AssetPreviewPassTest, SubmissionCarriesExactTimelineAndGeneration)
{
    AssetPreviewPass              pass    = {};
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    AssetPreviewPassTestAccess::Arm(pass, texture);
    AssetPreviewPassTestAccess::Submitted(pass, nullptr, 91);
    EXPECT_EQ(AssetPreviewPassTestAccess::Timeline(pass), 91u);
    EXPECT_EQ(pass.Observe(texture), AssetPreviewPass::Result::Pending);
    EXPECT_EQ(pass.Observe({.Index = 7, .Generation = 3}), AssetPreviewPass::Result::Failed);
    EXPECT_FALSE(pass.Discard({.Index = 7, .Generation = 3}));
    pass.Forget(texture);
    EXPECT_FALSE(AssetPreviewPassTestAccess::Empty(pass));
}

TEST(AssetPreviewPassTest, UsesFixedOffscreenDepthAndVersionedEnvironment)
{
    AssetPreviewPass pass = {};
    const auto       desc = pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_TRUE(desc.EnableDepthWrite);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "asset_preview");
    EXPECT_FALSE(pass.Publish.RequiresRenderPass());
    EXPECT_EQ(AssetPreviewPass::Extent, 128u);
    EXPECT_EQ(AssetPreviewPass::Capacity, 16u);
    EXPECT_NE(AssetPreviewPass::EnvironmentRevision, 0u);
}

TEST(AssetPreviewPassTest, FullyTransparentRequiresAProvenConstantCutoutNotOpaqueZeroAlphaOrTexturedAlbedo)
{
    Meshes::MeshMaterial material;
    material.AlbedoColor = {.8f, .8f, .8f, 0};
    material.Factors.z   = .5f;
    EXPECT_TRUE(AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoColor.w = .5f;
    EXPECT_FALSE(AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoColor.w = .49f;
    EXPECT_TRUE(AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoMap = 5;
    EXPECT_FALSE(AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoMap     = UINT32_MAX;
    material.AlbedoColor.w = 0;
    material.Factors.z     = 0;
    EXPECT_FALSE(AssetPreviewPass::IsFullyTransparent(material));
    material.Factors.z = NAN;
    EXPECT_FALSE(AssetPreviewPass::IsFullyTransparent(material));
    material.Factors.z     = .5f;
    material.AlbedoColor.w = NAN;
    EXPECT_FALSE(AssetPreviewPass::IsFullyTransparent(material));
}

TEST(AssetPreviewPassTest, GraphDeclaresFixedTargetsAndAnExactSampledPublication)
{
    Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    RenderGraph graph = {};
    memory.MainArena.CreateSubArena(ZMega(4), &graph.FrameArena, "PreviewGraphTest");
    auto* arena = &graph.FrameArena;
    graph.Resources.init(arena, 8);
    graph.ResourceIndex.init(arena, 16);
    graph.Passes.init(arena, 2, 2);
    graph.PassDependencies.init(arena, 8);
    graph.ExportedResources.init(arena, 2);
    for (auto& pass : graph.Passes)
    {
        pass.Reads.init(arena, 8);
        pass.Writes.init(arena, 4);
    }
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass                   = 0;
    AssetPreviewPass              preview = {};
    const Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    AssetPreviewPassTestAccess::Arm(preview, texture);
    auto device = std::make_unique<Hardwares::VulkanDevice>();
    ASSERT_TRUE(preview.Register(device.get(), "Preview", {}, &builder, nullptr));
    ASSERT_EQ(graph.Passes[0].Writes.size(), 2u);
    const auto color = graph.Passes[0].Writes[0].Handle;
    const auto depth = graph.Passes[0].Writes[1].Handle;
    EXPECT_EQ(graph.Resources[color.Index].TextureHandle.Index, texture.Index);
    EXPECT_TRUE(graph.Resources[color.Index].External);
    EXPECT_EQ(graph.Resources[depth.Index].Spec.Width, 128u);
    EXPECT_EQ(graph.Resources[depth.Index].Spec.Height, 128u);
    EXPECT_FALSE(graph.Resources[depth.Index].Spec.IsRenderTargetSized);
    EXPECT_EQ(graph.Passes[0].Writes[1].Access, RGAccess::DepthWrite);
    builder.CurrentPass = 1;
    ASSERT_TRUE(preview.Publish.Register(device.get(), "Publish", {}, &builder, nullptr));
    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Index, color.Index);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Version, color.Version);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, RGAccess::ShaderRead);
    ASSERT_EQ(graph.ExportedResources.size(), 1u);
    EXPECT_EQ(graph.ExportedResources[0].PassIndex, 0u);
    EXPECT_EQ(preview.Publish.GetPassFlags(), RGPassFlags::NeverCull);
    graph.FrameArena.Shutdown();
    memory.Shutdown();
}

class AssetPreviewDependencyTest : public ::testing::Test
{
protected:
    Core::Memory::MemoryManager Memory;
    AssetRecordSnapshot         Material    = {};
    uuids::uuid                 TextureUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    void                        SetUp() override
    {
        Memory.Initialize(ZMega(384), {});
        Managers::AssetManager::Initialize(&Memory.MainArena, nullptr, "/PreviewTest");
        auto* assets                      = Managers::AssetManager::Instance();
        Material.Record.UUID              = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        Material.Record.Type              = Managers::AssetType::MATERIAL;
        Material.Record.State             = AssetState::Loaded;
        Material.Record.Path              = VFSPath::Parse("/material.zematerial").Value();
        Importers::AssetMaterial material = {};
        material.MaterialUUID             = Material.Record.UUID;
        material.AlbedoTexUUID            = TextureUUID;
        assets->Materials.push(material);
        assets->UUIDToMaterialSlot.insert(material.MaterialUUID, 1);
        MetaFileData meta = {};
        meta.AssetUUID    = TextureUUID;
        meta.SourceHash   = 123;
        ASSERT_TRUE(assets->Registry->Register(TextureUUID, Managers::AssetType::TEXTURE, VFSPath::Parse("/texture.png").Value(), meta).IsOk());
        assets->UUIDToTextureHandle.insert(TextureUUID, {.Index = 5, .Generation = 1});
    }
    void TearDown() override
    {
        Managers::AssetManager::Shutdown();
        Memory.Shutdown();
    }
};

TEST_F(AssetPreviewDependencyTest, TransparencyClassificationUsesTheSameValidatedMaterialSnapshotAndResetsOnFailure)
{
    auto* assets            = Managers::AssetManager::Instance();
    auto& material          = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID  = {};
    material.AlbedoColor[3] = 0;
    material.Factors[2]     = .5f;
    uint64_t hash           = 0;
    bool     transparent    = false;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_TRUE(transparent);
    material.AlbedoTexUUID = TextureUUID;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_FALSE(transparent);
    transparent = true;
    assets->Registry->MarkStale(TextureUUID);
    EXPECT_FALSE(AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_FALSE(transparent);
}

TEST_F(AssetPreviewDependencyTest, ChangesForDependencyReimportAndGpuGeneration)
{
    uint64_t original = 0, changed = 0;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &original));
    auto*        assets = Managers::AssetManager::Instance();
    MetaFileData meta   = {};
    meta.AssetUUID      = TextureUUID;
    meta.SourceHash     = 456;
    ASSERT_TRUE(assets->Registry->UpdateMeta(TextureUUID, meta));
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &changed));
    EXPECT_NE(original, changed);
    original                                       = changed;
    *assets->UUIDToTextureHandle.find(TextureUUID) = {.Index = 5, .Generation = 2};
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &changed));
    EXPECT_NE(original, changed);
}

TEST_F(AssetPreviewDependencyTest, AlphaModeOpacityAndCutoffInvalidateBothCacheIdentities)
{
    auto* assets              = Managers::AssetManager::Instance();
    auto& material            = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID    = {};
    material.AlbedoColor[3]   = 0.f;
    material.Factors[2]       = .5f;
    uint64_t             live = 0, stable = 0, next_live = 0, next_stable = 0;
    bool                 invisible = false;
    Materials::AlphaInfo info      = {};
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &live, &stable, &invisible, &info));
    EXPECT_TRUE(invisible);
    EXPECT_EQ(info.Mode, Materials::AlphaMode::Legacy);
    for (auto mode : {Materials::AlphaMode::Opaque, Materials::AlphaMode::Mask, Materials::AlphaMode::Blend})
    {
        material.AlphaMode = mode;
        ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, &invisible, &info));
        EXPECT_NE(live, next_live);
        EXPECT_NE(stable, next_stable);
        EXPECT_EQ(info.Mode, mode);
        EXPECT_EQ(invisible, mode != Materials::AlphaMode::Opaque);
        live   = next_live;
        stable = next_stable;
    }
    material.AlbedoColor[3] = .25f;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, &invisible, &info));
    EXPECT_NE(live, next_live);
    EXPECT_NE(stable, next_stable);
    EXPECT_FALSE(invisible);
    EXPECT_FLOAT_EQ(info.BaseOpacity, .25f);
    live                = next_live;
    stable              = next_stable;
    material.Factors[2] = .7f;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, nullptr, &info));
    EXPECT_NE(live, next_live);
    EXPECT_NE(stable, next_stable);
    EXPECT_FLOAT_EQ(info.Cutoff, .7f);
    Material.Record.UUID = {};
    EXPECT_FALSE(AssetPreviewPass::DependencyFingerprint(Material, &next_live, nullptr, &invisible, &info));
    EXPECT_FALSE(invisible);
    EXPECT_EQ(info.Mode, Materials::AlphaMode::Legacy);
    EXPECT_FLOAT_EQ(info.BaseOpacity, 1.f);
    EXPECT_FLOAT_EQ(info.Cutoff, 0.f);
}

TEST_F(AssetPreviewDependencyTest, PersistentIdentityIgnoresGpuHandlesAndImportStateButTracksSource)
{
    uint64_t live = 0, stable = 0, changed_live = 0, changed_stable = 0;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &live, &stable));
    auto* assets                                   = Managers::AssetManager::Instance();
    *assets->UUIDToTextureHandle.find(TextureUUID) = {.Index = 17, .Generation = 9};
    assets->Registry->SetState(TextureUUID, AssetState::Loaded);
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(live, changed_live);
    EXPECT_EQ(stable, changed_stable);
    MetaFileData meta = {};
    meta.AssetUUID    = TextureUUID;
    meta.SourceHash   = 456;
    ASSERT_TRUE(assets->Registry->UpdateMeta(TextureUUID, meta));
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(stable, changed_stable);
}

TEST_F(AssetPreviewDependencyTest, PersistentIdentityIncludesResolvedPathFallbackUuid)
{
    auto* assets           = Managers::AssetManager::Instance();
    auto& material         = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID = {};
    material.AlbedoTexPath.init(&Memory.MainArena, "/texture.png");
    uint64_t live = 0, stable = 0, changed_live = 0, changed_stable = 0;
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &live, &stable));
    assets->Registry->Remove(TextureUUID);
    const auto   replacement = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    MetaFileData meta        = {};
    meta.AssetUUID           = replacement;
    meta.SourceHash          = 123;
    ASSERT_TRUE(assets->Registry->Register(replacement, Managers::AssetType::TEXTURE, VFSPath::Parse("/texture.png").Value(), meta).IsOk());
    assets->UUIDToTextureHandle.insert(replacement, {.Index = 5, .Generation = 1});
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(stable, changed_stable);
}

TEST_F(AssetPreviewDependencyTest, MissingStaleOrUnboundDependenciesUseFallback)
{
    auto*    assets = Managers::AssetManager::Instance();
    uint64_t hash   = 0;
    assets->Registry->MarkStale(TextureUUID);
    EXPECT_FALSE(AssetPreviewPass::DependencyFingerprint(Material, &hash));
    assets->Registry->SetState(TextureUUID, AssetState::Loaded);
    ASSERT_TRUE(AssetPreviewPass::DependencyFingerprint(Material, &hash));
    *assets->UUIDToTextureHandle.find(TextureUUID) = {};
    EXPECT_FALSE(AssetPreviewPass::DependencyFingerprint(Material, &hash));
    assets->Registry->Remove(TextureUUID);
    EXPECT_FALSE(AssetPreviewPass::DependencyFingerprint(Material, &hash));
}

TEST_F(AssetPreviewDependencyTest, ContendedIngestionNeverBlocksOptionalPreviewWork)
{
    auto*            assets = Managers::AssetManager::Instance();
    std::unique_lock lock(assets->IngestMutex);
    bool             accepted = true;
    std::thread      worker([&] {
        uint64_t hash = 0;
        accepted      = AssetPreviewPass::DependencyFingerprint(Material, &hash);
    });
    worker.join();
    EXPECT_FALSE(accepted);
}
