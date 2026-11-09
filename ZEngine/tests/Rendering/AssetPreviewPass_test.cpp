#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Hardwares/VulkanDevice.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/AssetPreviewPass.h>
#include <ZEngine/Rendering/Shaders/Shader.h>
#include <gtest/gtest.h>
#include <array>
#include <atomic>
#include <cmath>
#include <limits>
#include <thread>
#include "AssetPreviewPassTestAccess.h"

using ZEngine::Rendering::Renderers::RenderPasses::DescriptorPreparation;
using ZEngine::Rendering::Specifications::DescriptorType;
using ZEngine::Rendering::Specifications::LayoutBindingSpecification;

TEST(DescriptorPreparationTest, PriorPreparationAndOtherFramesCannotSatisfyCurrentWrites)
{
    DescriptorPreparation            proof   = {};
    const LayoutBindingSpecification binding = {.Set = 0, .Binding = 2, .Count = 1, .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER};
    const std::array                 reflection{binding};
    proof.Begin(0, 3);
    proof.Record(binding, 0, 0, false);
    EXPECT_TRUE(proof.Verify(reflection, 0, 3));
    EXPECT_FALSE(proof.Verify(reflection, 1, 3));
    EXPECT_FALSE(proof.Verify(reflection, 0, 4));
    proof.Begin(1, 3);
    EXPECT_FALSE(proof.Verify(reflection, 1, 3));
    proof.Record(binding, 0, 0, false);
    EXPECT_FALSE(proof.Verify(reflection, 1, 3));
    proof.Record(binding, 1, 0, false);
    EXPECT_TRUE(proof.Verify(reflection, 1, 3));
}

TEST(DescriptorPreparationTest, ArrayCoverageRequiresEveryElementAndHandlesThe64ElementBoundary)
{
    DescriptorPreparation            proof   = {};
    const LayoutBindingSpecification binding = {.Set = 0, .Binding = 2, .Count = 64, .DescriptorTypeValue = DescriptorType::STORAGE_IMAGE};
    const std::array                 reflection{binding};
    proof.Begin(0, 3);
    for (uint32_t element = 0; element < 63; ++element)
    {
        proof.Record(binding, 0, element, false);
    }
    proof.Record(binding, 0, 0, false); // A duplicate cannot fill the last hole.
    EXPECT_FALSE(proof.Verify(reflection, 0, 3));
    proof.Record(binding, 0, 63, false);
    EXPECT_TRUE(proof.Verify(reflection, 0, 3));
    proof.Record(binding, 0, 64, false);
    EXPECT_FALSE(proof.Verify(reflection, 0, 3));
}

TEST(DescriptorPreparationTest, SharedSampledArraysAreExplicitAndCannotCoverStorageArrays)
{
    DescriptorPreparation      proof   = {};
    LayoutBindingSpecification binding = {.Set = 1, .Binding = 0, .Count = 8192, .DescriptorTypeValue = DescriptorType::SAMPLED_IMAGE};
    proof.Begin(0, 3);
    proof.Record(binding, 0, 0, true);
    EXPECT_TRUE(proof.Verify(std::array{binding}, 0, 3));
    binding.DescriptorTypeValue = DescriptorType::STORAGE_IMAGE;
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
    proof.Begin(0, 3);
    proof.Record(binding, 0, 0, true);
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
}

TEST(DescriptorPreparationTest, RejectionIsStickyUntilANewPreparation)
{
    DescriptorPreparation            proof   = {};
    const LayoutBindingSpecification binding = {.Set = 0, .Binding = 0, .DescriptorTypeValue = DescriptorType::SAMPLER};
    proof.Begin(0, 3);
    proof.Reject();
    proof.Record(binding, 0, 0, false);
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
    proof.Begin(0, 3);
    proof.Record(binding, 0, 0, false);
    EXPECT_TRUE(proof.Verify(std::array{binding}, 0, 3));
    proof.Reset();
    EXPECT_FALSE(proof.Active());
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
}

TEST(DescriptorPreparationTest, CapacityRefusalCannotReportAPartiallyTrackedPreparationAsComplete)
{
    DescriptorPreparation                                                   proof    = {};
    std::array<LayoutBindingSpecification, DescriptorPreparation::Capacity> bindings = {};
    proof.Begin(0, 3);
    for (uint32_t index = 0; index < bindings.size(); ++index)
    {
        bindings[index] = {.Set = 0, .Binding = index, .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER};
        proof.Record(bindings[index], 0, 0, false);
    }
    EXPECT_TRUE(proof.Verify(bindings, 0, 3));
    auto overflow    = bindings[0];
    overflow.Binding = DescriptorPreparation::Capacity;
    proof.Record(overflow, 0, 0, false);
    EXPECT_FALSE(proof.Verify(bindings, 0, 3));
}

TEST(DescriptorPreparationTest, ReflectionMustMatchCountAndTypeButMayRepeatStageBindings)
{
    DescriptorPreparation      proof   = {};
    LayoutBindingSpecification binding = {.Set = 0, .Binding = 0, .Count = 1, .DescriptorTypeValue = DescriptorType::STORAGE_BUFFER};
    proof.Begin(0, 3);
    proof.Record(binding, 0, 0, false);
    EXPECT_TRUE(proof.Verify(std::array{binding, binding}, 0, 3));
    binding.Count = 2;
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
    binding.Count               = 1;
    binding.DescriptorTypeValue = DescriptorType::SAMPLER;
    EXPECT_FALSE(proof.Verify(std::array{binding}, 0, 3));
}

namespace
{
    struct PreviewBindingTestPass : ZEngine::Rendering::Renderers::RenderPasses::GraphicPass
    {
        using ZEngine::Rendering::Renderers::RenderPasses::DescriptorBoundPass::BoundBindings;
        void InitializeNames(ZEngine::Hardwares::VulkanDevice* device)
        {
            InitializeDescriptorBindings(device);
        }
        void BindNames(ZEngine::Core::Memory::ArenaAllocator* arena)
        {
            BoundBindings.init(arena, 4);
            BoundBindings.insert("UBPreview");
        }
        bool VerifyNames()
        {
            return VerifyDescriptorBindings();
        }
        bool Replay()
        {
            return DescriptorBoundPass::ReplayDescriptorBindings(static_cast<DescriptorBoundPass*>(this));
        }
    };
} // namespace

TEST(AssetPreviewPassTest, FreshDescriptorReplayDefersToInitialBindingButDoesNotBypassVerification)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    auto device                                = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->Arena                              = &memory.MainArena;
    ZEngine::Rendering::Shaders::Shader shader = {};
    shader.LayoutBindingSpecifications.init(device->Arena, 1);
    shader.LayoutBindingSpecifications.push({.Set = 0, .Binding = 0, .Name = "UBPreview"});
    ZEngine::Rendering::Renderers::Pipelines::GraphicPipeline pipeline = {};
    pipeline.Shader                                                    = &shader;
    PreviewBindingTestPass pass                                        = {};
    pass.InitializeNames(device.get());
    pass.Pipeline = &pipeline;
    EXPECT_TRUE(pass.Replay());
    EXPECT_FALSE(pass.VerifyNames());
    pass.BindNames(device->Arena);
    EXPECT_TRUE(pass.VerifyNames());
    shader.LayoutBindingSpecifications[0].Name = "NewRequiredBinding";
    EXPECT_FALSE(pass.Replay());
    EXPECT_FALSE(pass.VerifyNames());
    pass.Pipeline = nullptr;
}

TEST(AssetPreviewPassTest, PreviouslyVerifiedEmptyInterfaceCannotSkipIncompatibleReloadReplay)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(1), {});
    auto device                                                        = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->Arena                                                      = &memory.MainArena;
    ZEngine::Rendering::Shaders::Shader                       shader   = {};
    ZEngine::Rendering::Renderers::Pipelines::GraphicPipeline pipeline = {};
    pipeline.Shader                                                    = &shader;
    PreviewBindingTestPass pass                                        = {};
    pass.InitializeNames(device.get());
    pass.Pipeline = &pipeline;
    EXPECT_TRUE(pass.Replay());
    EXPECT_TRUE(pass.VerifyNames());
    shader.LayoutBindingSpecifications.init(device->Arena, 1);
    shader.LayoutBindingSpecifications.push({.Set = 0, .Binding = 0, .Name = "NewRequiredBinding"});
    EXPECT_FALSE(pass.Replay());
    EXPECT_FALSE(pass.VerifyNames());
    pass.Pipeline = nullptr;
}

TEST(AssetPreviewPassTest, ForwardBindingTrackingSupportsTheDescriptorReplayBudget)
{
    ZEngine::Core::Memory::MemoryManager memory{};
    memory.Initialize(ZMega(1), {});
    auto device   = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->Arena = &memory.MainArena;
    PreviewBindingTestPass pass{};
    pass.InitializeNames(device.get());
    char names[ZEngine::Rendering::Renderers::RenderPasses::kMaxDescriptorReplayBindings][16] = {};
    for (uint32_t i = 0; i < ZEngine::Rendering::Renderers::RenderPasses::kMaxDescriptorReplayBindings; ++i)
    {
        snprintf(names[i], sizeof(names[i]), "Binding-%u", i);
        pass.BoundBindings.insert(names[i]);
    }
    EXPECT_EQ(pass.BoundBindings.size(), ZEngine::Rendering::Renderers::RenderPasses::kMaxDescriptorReplayBindings);
    for (const auto& name : names)
        EXPECT_TRUE(pass.BoundBindings.contains(name));
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, RetiredThumbnailDescriptorUpdatesAreDroppedRatherThanRequeuedForever)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    auto device              = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
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

TEST(AssetPreviewPassTest, DescriptorBacklogDrainsInBoundedSlicesBeforeDeferredWork)
{
    auto               device = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    constexpr uint32_t slice  = ZEngine::Hardwares::VulkanDevice::BindlessTextureUpdateSlice;
    for (uint32_t index = 0; index < slice + 1; ++index)
    {
        // Stale handles still consume the slice's operation budget.
        device->RequestDescriptorUpdate({.Index = index, .Generation = 1});
    }
    device->RequestDeferredDescriptorUpdate({.Index = 0, .Generation = 1});
    device->FlushBindlessTextureUpdates();
    EXPECT_FALSE(device->TextureHandleToUpdates.empty());
    EXPECT_FALSE(device->DeferredTextureDescriptorUpdates.empty());
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
}

TEST(AssetPreviewPassTest, DeferredDescriptorBacklogAlsoHasABoundedSlice)
{
    auto               device = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    constexpr uint32_t slice  = ZEngine::Hardwares::VulkanDevice::BindlessTextureUpdateSlice;
    for (uint32_t index = 0; index < slice + 1; ++index)
    {
        device->RequestDeferredDescriptorUpdate({.Index = index, .Generation = 1});
    }
    device->FlushBindlessTextureUpdates();
    EXPECT_FALSE(device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
}

TEST(AssetPreviewPassTest, DeferredDescriptorForRetiredGenerationCannotTargetReusedSlot)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    auto device              = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->MaxGlobalTexture = 8;
    device->GlobalTextures.Initialize(&memory.MainArena, 8);
    const auto retired = device->GlobalTextures.Create();
    ASSERT_TRUE(retired.Valid());
    auto removed = retired;
    device->GlobalTextures.Remove(removed);
    const auto replacement = device->GlobalTextures.Create();
    ASSERT_EQ(replacement.Index, retired.Index);
    ASSERT_NE(replacement.Generation, retired.Generation);
    device->RequestDeferredDescriptorUpdate(retired);
    device->FlushBindlessTextureUpdates();
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_TRUE(device->TextureHandleToUpdates.empty());
    EXPECT_TRUE(device->GlobalTextures.IsLive(replacement));
}

TEST(AssetPreviewPassTest, DeferredDescriptorAdmissionReportsCapacityAndPreservesAcceptedRequests)
{
    auto               device   = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    constexpr uint32_t capacity = decltype(device->DeferredTextureDescriptorUpdates)::CAPACITY;
    for (uint32_t index = 0; index < capacity; ++index)
    {
        ASSERT_TRUE(device->RequestDeferredDescriptorUpdate({.Index = index, .Generation = 2}));
    }
    EXPECT_FALSE(device->RequestDeferredDescriptorUpdate({.Index = capacity, .Generation = 2}));
    ZEngine::Rendering::Textures::TextureHandle handle = {};
    for (uint32_t index = 0; index < capacity; ++index)
    {
        ASSERT_TRUE(device->DeferredTextureDescriptorUpdates.pop(handle));
        EXPECT_EQ(handle.Index, index);
        EXPECT_EQ(handle.Generation, 2u);
    }
    EXPECT_TRUE(device->DeferredTextureDescriptorUpdates.empty());
    EXPECT_TRUE(device->RequestDeferredDescriptorUpdate({.Index = capacity, .Generation = 2}));
}

TEST(AssetPreviewPassTest, ConcurrentDecodeWorkersDoNotLoseDeferredDescriptorRequests)
{
    auto                  device             = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    constexpr uint32_t    workers            = 8;
    constexpr uint32_t    per_worker         = 8;
    std::atomic<uint32_t> admitted           = 0;
    std::thread           producers[workers] = {};
    for (uint32_t worker = 0; worker < workers; ++worker)
    {
        producers[worker] = std::thread([&, worker] {
            for (uint32_t index = 0; index < per_worker; ++index)
            {
                if (device->RequestDeferredDescriptorUpdate({.Index = worker * per_worker + index, .Generation = 2}))
                {
                    admitted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (auto& producer : producers)
    {
        producer.join();
    }
    EXPECT_EQ(admitted.load(std::memory_order_relaxed), workers * per_worker);
    bool                                        seen[workers * per_worker] = {};
    ZEngine::Rendering::Textures::TextureHandle handle                     = {};
    uint32_t                                    received                   = 0;
    while (device->DeferredTextureDescriptorUpdates.pop(handle))
    {
        ASSERT_LT(handle.Index, workers * per_worker);
        EXPECT_FALSE(seen[handle.Index]);
        seen[handle.Index] = true;
        EXPECT_EQ(handle.Generation, 2u);
        ++received;
    }
    EXPECT_EQ(received, workers * per_worker);
}

TEST(AssetPreviewPassTest, BindlessCapacityLeavesRoomForTheForwardMaterialEnvironment)
{
    EXPECT_EQ(ZEngine::Hardwares::GlobalTextureCapacity(8192, 128), 125u);
    EXPECT_EQ(ZEngine::Hardwares::GlobalTextureCapacity(32, 128), 32u);
    EXPECT_EQ(ZEngine::Hardwares::GlobalTextureCapacity(8192, 3), 0u);
    EXPECT_EQ(ZEngine::Hardwares::GlobalTextureCapacity(8192, 0), 0u);
}

TEST(AssetPreviewPassTest, OptionalAllocationPreservesBindlessHeadroom)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    auto device              = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    device->MaxGlobalTexture = 32;
    device->GlobalTextures.Initialize(&memory.MainArena, 32);
    for (uint32_t i = 0; i < 16; ++i)
        ASSERT_TRUE(device->GlobalTextures.Create().Valid());
    // Rejection occurs before constructing any Vulkan image, under the same
    // allocation mutex as ordinary scene textures.
    EXPECT_FALSE(device->CreateTexture({}, "Preview", ZEngine::Rendering::RenderResourceManager::ThumbnailTextureSlotReserve).Valid());
    EXPECT_EQ(device->GlobalTextures.Size(), 16u);
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, SharedUniformCountsOnceAcrossShaderStages)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    ZEngine::Rendering::Shaders::Shader shader{};
    shader.LayoutBindingSpecificationMap.init(&memory.MainArena, 2);
    auto& bindings = shader.LayoutBindingSpecificationMap[0];
    bindings.init(&memory.MainArena, 4);
    bindings.push({.Set = 0, .Binding = 0, .Name = "UBPreview", .DescriptorTypeValue = ZEngine::Rendering::Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = ZEngine::Rendering::Specifications::ShaderStageFlags::VERTEX});
    bindings.push({.Set = 0, .Binding = 0, .Name = "UBPreview", .DescriptorTypeValue = ZEngine::Rendering::Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = ZEngine::Rendering::Specifications::ShaderStageFlags::FRAGMENT});
    EXPECT_EQ(shader.DynamicDescriptorCount(), 1u);
    shader.LayoutBindingSpecifications.init(&memory.MainArena, 2);
    shader.LayoutBindingSpecifications.push(bindings[0]);
    shader.LayoutBindingSpecifications.push(bindings[1]);
    ZEngine::Rendering::Renderers::Pipelines::GraphicPipeline pipeline = {};
    pipeline.Shader                                                    = &shader;
    PreviewBindingTestPass pass                                        = {};
    pass.Pipeline                                                      = &pipeline;
    pass.BindNames(&memory.MainArena);
    EXPECT_TRUE(pass.VerifyNames());
    // Complete pass readiness also requires a usable PSO, not only matching names.
    EXPECT_FALSE(pass.Verify());
    // This CPU-only pass does not own a Vulkan pipeline.
    pass.Pipeline   = nullptr;
    pipeline.Shader = nullptr;
    bindings.push({.Set = 0, .Binding = 1, .Count = 3, .Name = "Array", .DescriptorTypeValue = ZEngine::Rendering::Specifications::DescriptorType::STORAGE_BUFFER_DYNAMIC, .Flags = ZEngine::Rendering::Specifications::ShaderStageFlags::VERTEX});
    auto& second = shader.LayoutBindingSpecificationMap[2];
    second.init(&memory.MainArena, 1);
    second.push({.Set = 2, .Binding = 0, .Name = "Other", .DescriptorTypeValue = ZEngine::Rendering::Specifications::DescriptorType::UNIFORM_BUFFER_DYNAMIC, .Flags = ZEngine::Rendering::Specifications::ShaderStageFlags::VERTEX});
    EXPECT_EQ(shader.DynamicDescriptorCount(), 5u);
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, VertexOnlyOrMissingShaderModulesCannotProduceAColorPreview)
{
    ZEngine::Core::Memory::MemoryManager memory{};
    memory.Initialize(ZMega(1), {});
    ZEngine::Rendering::Shaders::Shader shader{};
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
    ZEngine::Core::Memory::MemoryManager memory{};
    memory.Initialize(ZMega(1), {});
    ZEngine::Rendering::Shaders::Shader shader{};
    shader.ShaderCreateInfos.init(&memory.MainArena, 1);
    shader.ShaderCreateInfos.push({.stage = VK_SHADER_STAGE_VERTEX_BIT, .module = reinterpret_cast<VkShaderModule>(uintptr_t(1))});
    ZEngine::Rendering::Renderers::Pipelines::GraphicPipeline pipeline{};
    pipeline.Shader = &shader;
    PreviewBindingTestPass backend{};
    backend.Pipeline = &pipeline;
    ZEngine::Rendering::Renderers::AssetPreviewPass   preview{};
    const ZEngine::Rendering::Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Arm(preview, texture);
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Published(preview);
    // A non-null sentinel proves rejection happens before touching commands or
    // the device. A colorless draw must not set the recorded/success latch.
    auto* commands = reinterpret_cast<ZEngine::Hardwares::CommandBuffer*>(uintptr_t(1));
    EXPECT_FALSE(preview.RecordDraw(nullptr, nullptr, nullptr, &backend, nullptr, commands));
    preview.EndFrame();
    EXPECT_EQ(preview.Observe(texture), ZEngine::Rendering::Renderers::AssetPreviewPass::Result::Failed);
    backend.Pipeline = nullptr;
    pipeline.Shader  = nullptr;
    shader.ShaderCreateInfos.clear();
    memory.Shutdown();
}

TEST(AssetPreviewPassTest, ValidatedBoundsProduceFiniteFramingWithMargin)
{
    ZEngine::Core::Maths::Mat4f matrix = {};
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({12, -48, 5}, 0.01f, &matrix));
    // A radius-one sphere bounds the normalized vertices. Every sampled point
    // remains inside the viewport and between derived near/far planes.
    for (uint32_t y = 0; y <= 16; ++y)
        for (uint32_t x = 0; x < 32; ++x)
        {
            const float                       longitude = float(x) * 6.2831853f / 32;
            const float                       latitude  = float(y) * 3.14159265f / 16;
            const ZEngine::Core::Maths::Vec4f p(std::sin(latitude) * std::cos(longitude), std::cos(latitude), std::sin(latitude) * std::sin(longitude), 1);
            const auto                        clip = matrix * p;
            ASSERT_GT(clip.w, 0);
            EXPECT_LT(std::abs(clip.x / clip.w), 0.9f);
            EXPECT_LT(std::abs(clip.y / clip.w), 0.9f);
            EXPECT_GT(clip.z / clip.w, 0);
            EXPECT_LT(clip.z / clip.w, 1);
        }
    EXPECT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({}, 100000, &matrix));
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({}, 0, &matrix));
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({}, -1, &matrix));
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({}, std::numeric_limits<float>::infinity(), &matrix));
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::FrameBounds({NAN, 0, 0}, 1, &matrix));
}

TEST(AssetPreviewPassTest, RecordingCancellationRetainsOwnershipUntilTerminalResult)
{
    ZEngine::Rendering::Renderers::AssetPreviewPass   pass    = {};
    const ZEngine::Rendering::Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Arm(pass, texture);
    EXPECT_EQ(pass.Observe(texture), ZEngine::Rendering::Renderers::AssetPreviewPass::Result::Pending);
    EXPECT_TRUE(pass.Discard(texture));
    EXPECT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Abandoned(pass));
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Empty(pass));
    pass.Forget(texture);
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Empty(pass));
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Cancel(pass);
    EXPECT_EQ(pass.Observe(texture), ZEngine::Rendering::Renderers::AssetPreviewPass::Result::Failed);
    pass.Forget(texture);
    EXPECT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Empty(pass));
}

TEST(AssetPreviewPassTest, SubmissionCarriesExactTimelineAndGeneration)
{
    ZEngine::Rendering::Renderers::AssetPreviewPass   pass    = {};
    const ZEngine::Rendering::Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Arm(pass, texture);
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Submitted(pass, nullptr, 91);
    EXPECT_EQ(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Timeline(pass), 91u);
    EXPECT_EQ(pass.Observe(texture), ZEngine::Rendering::Renderers::AssetPreviewPass::Result::Pending);
    EXPECT_EQ(pass.Observe({.Index = 7, .Generation = 3}), ZEngine::Rendering::Renderers::AssetPreviewPass::Result::Failed);
    EXPECT_FALSE(pass.Discard({.Index = 7, .Generation = 3}));
    pass.Forget(texture);
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Empty(pass));
}

TEST(AssetPreviewPassTest, UsesFixedOffscreenDepthAndVersionedEnvironment)
{
    ZEngine::Rendering::Renderers::AssetPreviewPass pass = {};
    const auto                                      desc = pass.BuildGraphicsPipelineDescription(nullptr);
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_TRUE(desc.EnableDepthWrite);
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "asset_preview");
    EXPECT_FALSE(pass.Publish.RequiresRenderPass());
    EXPECT_EQ(ZEngine::Rendering::Renderers::AssetPreviewPass::Extent, 128u);
    EXPECT_EQ(ZEngine::Rendering::Renderers::AssetPreviewPass::Capacity, 16u);
    EXPECT_NE(ZEngine::Rendering::Renderers::AssetPreviewPass::EnvironmentRevision, 0u);
}

TEST(AssetPreviewPassTest, FullyTransparentRequiresAProvenConstantCutoutNotOpaqueZeroAlphaOrTexturedAlbedo)
{
    ZEngine::Rendering::Meshes::MeshMaterial material{};
    material.AlbedoColor = {.8f, .8f, .8f, 0};
    material.Factors.z   = .5f;
    EXPECT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoColor.w = .5f;
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoColor.w = .49f;
    EXPECT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoMap = 5;
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.AlbedoMap     = UINT32_MAX;
    material.AlbedoColor.w = 0;
    material.Factors.z     = 0;
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.Factors.z = NAN;
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
    material.Factors.z     = .5f;
    material.AlbedoColor.w = NAN;
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::IsFullyTransparent(material));
}

TEST(AssetPreviewPassTest, GraphDeclaresFixedTargetsAndAnExactSampledPublication)
{
    ZEngine::Core::Memory::MemoryManager memory = {};
    memory.Initialize(ZMega(8), {});
    ZEngine::Rendering::Renderers::RenderGraph graph = {};
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
    ZEngine::Rendering::Renderers::RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass                                       = 0;
    ZEngine::Rendering::Renderers::AssetPreviewPass   preview = {};
    const ZEngine::Rendering::Textures::TextureHandle texture = {.Index = 7, .Generation = 2};
    ZEngine::Rendering::Renderers::AssetPreviewPassTestAccess::Arm(preview, texture);
    auto device = std::make_unique<ZEngine::Hardwares::VulkanDevice>();
    ASSERT_TRUE(preview.Register(device.get(), "Preview", {}, &builder, nullptr));
    ASSERT_EQ(graph.Passes[0].Writes.size(), 2u);
    const auto color = graph.Passes[0].Writes[0].Handle;
    const auto depth = graph.Passes[0].Writes[1].Handle;
    EXPECT_EQ(graph.Resources[color.Index].TextureHandle.Index, texture.Index);
    EXPECT_TRUE(graph.Resources[color.Index].External);
    EXPECT_EQ(graph.Resources[depth.Index].Spec.Width, 128u);
    EXPECT_EQ(graph.Resources[depth.Index].Spec.Height, 128u);
    EXPECT_FALSE(graph.Resources[depth.Index].Spec.IsRenderTargetSized);
    EXPECT_EQ(graph.Passes[0].Writes[1].Access, ZEngine::Rendering::Renderers::RGAccess::DepthWrite);
    builder.CurrentPass = 1;
    ASSERT_TRUE(preview.Publish.Register(device.get(), "Publish", {}, &builder, nullptr));
    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Index, color.Index);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Version, color.Version);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, ZEngine::Rendering::Renderers::RGAccess::ShaderRead);
    ASSERT_EQ(graph.ExportedResources.size(), 1u);
    EXPECT_EQ(graph.ExportedResources[0].PassIndex, 0u);
    EXPECT_EQ(preview.Publish.GetPassFlags(), ZEngine::Rendering::Renderers::RGPassFlags::NeverCull);
    graph.FrameArena.Shutdown();
    memory.Shutdown();
}

class AssetPreviewDependencyTest : public ::testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    Memory{};
    ZEngine::Core::VFS::AssetRecordSnapshot Material    = {};
    uuids::uuid                             TextureUUID = *uuids::uuid::from_string("11111111-1111-4111-8111-111111111111");
    void                                    SetUp() override
    {
        Memory.Initialize(ZMega(384), {});
        ZEngine::Managers::AssetManager::Initialize(&Memory.MainArena, nullptr, "/PreviewTest");
        auto* assets                               = ZEngine::Managers::AssetManager::Instance();
        Material.Record.UUID                       = *uuids::uuid::from_string("22222222-2222-4222-8222-222222222222");
        Material.Record.Type                       = ZEngine::Managers::AssetType::MATERIAL;
        Material.Record.State                      = ZEngine::Core::VFS::AssetState::Loaded;
        Material.Record.Path                       = ZEngine::Core::VFS::VFSPath::Parse("/material.zematerial").Value();
        ZEngine::Importers::AssetMaterial material = {};
        material.MaterialUUID                      = Material.Record.UUID;
        material.AlbedoTexUUID                     = TextureUUID;
        assets->Materials.push(material);
        assets->UUIDToMaterialSlot.insert(material.MaterialUUID, 1);
        ZEngine::Core::VFS::MetaFileData meta = {};
        meta.AssetUUID                        = TextureUUID;
        meta.SourceHash                       = 123;
        ASSERT_TRUE(assets->Registry->Register(TextureUUID, ZEngine::Managers::AssetType::TEXTURE, ZEngine::Core::VFS::VFSPath::Parse("/texture.png").Value(), meta).IsOk());
        assets->UUIDToTextureHandle.insert(TextureUUID, {.Index = 5, .Generation = 1});
    }
    void TearDown() override
    {
        ZEngine::Managers::AssetManager::Shutdown();
        Memory.Shutdown();
    }
};

TEST_F(AssetPreviewDependencyTest, TransparencyClassificationUsesTheSameValidatedMaterialSnapshotAndResetsOnFailure)
{
    auto* assets            = ZEngine::Managers::AssetManager::Instance();
    auto& material          = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID  = {};
    material.AlbedoColor[3] = 0;
    material.Factors[2]     = .5f;
    uint64_t hash           = 0;
    bool     transparent    = false;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_TRUE(transparent);
    material.AlbedoTexUUID = TextureUUID;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_FALSE(transparent);
    transparent = true;
    assets->Registry->MarkStale(TextureUUID);
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash, nullptr, &transparent));
    EXPECT_FALSE(transparent);
}

TEST_F(AssetPreviewDependencyTest, ChangesForDependencyReimportAndGpuGeneration)
{
    uint64_t original = 0, changed = 0;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &original));
    auto*                            assets = ZEngine::Managers::AssetManager::Instance();
    ZEngine::Core::VFS::MetaFileData meta   = {};
    meta.AssetUUID                          = TextureUUID;
    meta.SourceHash                         = 456;
    ASSERT_TRUE(assets->Registry->UpdateMeta(TextureUUID, meta));
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &changed));
    EXPECT_NE(original, changed);
    original                                       = changed;
    *assets->UUIDToTextureHandle.find(TextureUUID) = {.Index = 5, .Generation = 2};
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &changed));
    EXPECT_NE(original, changed);
}

TEST_F(AssetPreviewDependencyTest, AlphaModeOpacityAndCutoffInvalidateBothCacheIdentities)
{
    auto* assets                                  = ZEngine::Managers::AssetManager::Instance();
    auto& material                                = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID                        = {};
    material.AlbedoColor[3]                       = 0.f;
    material.Factors[2]                           = .5f;
    uint64_t                                 live = 0, stable = 0, next_live = 0, next_stable = 0;
    bool                                     invisible = false;
    ZEngine::Rendering::Materials::AlphaInfo info      = {};
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &live, &stable, &invisible, &info));
    EXPECT_TRUE(invisible);
    EXPECT_EQ(info.Mode, ZEngine::Rendering::Materials::AlphaMode::Legacy);
    for (auto mode : {ZEngine::Rendering::Materials::AlphaMode::Opaque, ZEngine::Rendering::Materials::AlphaMode::Mask, ZEngine::Rendering::Materials::AlphaMode::Blend})
    {
        material.AlphaMode = mode;
        ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, &invisible, &info));
        EXPECT_NE(live, next_live);
        EXPECT_NE(stable, next_stable);
        EXPECT_EQ(info.Mode, mode);
        EXPECT_EQ(invisible, mode != ZEngine::Rendering::Materials::AlphaMode::Opaque);
        live   = next_live;
        stable = next_stable;
    }
    material.AlbedoColor[3] = .25f;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, &invisible, &info));
    EXPECT_NE(live, next_live);
    EXPECT_NE(stable, next_stable);
    EXPECT_FALSE(invisible);
    EXPECT_FLOAT_EQ(info.BaseOpacity, .25f);
    live                = next_live;
    stable              = next_stable;
    material.Factors[2] = .7f;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &next_live, &next_stable, nullptr, &info));
    EXPECT_NE(live, next_live);
    EXPECT_NE(stable, next_stable);
    EXPECT_FLOAT_EQ(info.Cutoff, .7f);
    Material.Record.UUID = {};
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &next_live, nullptr, &invisible, &info));
    EXPECT_FALSE(invisible);
    EXPECT_EQ(info.Mode, ZEngine::Rendering::Materials::AlphaMode::Legacy);
    EXPECT_FLOAT_EQ(info.BaseOpacity, 1.f);
    EXPECT_FLOAT_EQ(info.Cutoff, 0.f);
}

TEST_F(AssetPreviewDependencyTest, PersistentIdentityIgnoresGpuHandlesAndImportStateButTracksSource)
{
    uint64_t live = 0, stable = 0, changed_live = 0, changed_stable = 0;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &live, &stable));
    auto* assets                                   = ZEngine::Managers::AssetManager::Instance();
    *assets->UUIDToTextureHandle.find(TextureUUID) = {.Index = 17, .Generation = 9};
    assets->Registry->SetState(TextureUUID, ZEngine::Core::VFS::AssetState::Loaded);
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(live, changed_live);
    EXPECT_EQ(stable, changed_stable);
    ZEngine::Core::VFS::MetaFileData meta = {};
    meta.AssetUUID                        = TextureUUID;
    meta.SourceHash                       = 456;
    ASSERT_TRUE(assets->Registry->UpdateMeta(TextureUUID, meta));
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(stable, changed_stable);
}

TEST_F(AssetPreviewDependencyTest, PersistentIdentityIncludesResolvedPathFallbackUuid)
{
    auto* assets           = ZEngine::Managers::AssetManager::Instance();
    auto& material         = assets->Materials[*assets->UUIDToMaterialSlot.find(Material.Record.UUID)];
    material.AlbedoTexUUID = {};
    material.AlbedoTexPath.init(&Memory.MainArena, "/texture.png");
    uint64_t live = 0, stable = 0, changed_live = 0, changed_stable = 0;
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &live, &stable));
    assets->Registry->Remove(TextureUUID);
    const auto                       replacement = *uuids::uuid::from_string("33333333-3333-4333-8333-333333333333");
    ZEngine::Core::VFS::MetaFileData meta        = {};
    meta.AssetUUID                               = replacement;
    meta.SourceHash                              = 123;
    ASSERT_TRUE(assets->Registry->Register(replacement, ZEngine::Managers::AssetType::TEXTURE, ZEngine::Core::VFS::VFSPath::Parse("/texture.png").Value(), meta).IsOk());
    assets->UUIDToTextureHandle.insert(replacement, {.Index = 5, .Generation = 1});
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &changed_live, &changed_stable));
    EXPECT_NE(stable, changed_stable);
}

TEST_F(AssetPreviewDependencyTest, MissingStaleOrUnboundDependenciesUseFallback)
{
    auto*    assets = ZEngine::Managers::AssetManager::Instance();
    uint64_t hash   = 0;
    assets->Registry->MarkStale(TextureUUID);
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash));
    assets->Registry->SetState(TextureUUID, ZEngine::Core::VFS::AssetState::Loaded);
    ASSERT_TRUE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash));
    *assets->UUIDToTextureHandle.find(TextureUUID) = {};
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash));
    assets->Registry->Remove(TextureUUID);
    EXPECT_FALSE(ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash));
}

TEST_F(AssetPreviewDependencyTest, ContendedIngestionNeverBlocksOptionalPreviewWork)
{
    auto*            assets = ZEngine::Managers::AssetManager::Instance();
    std::unique_lock lock(assets->IngestMutex);
    bool             accepted = true;
    std::thread      worker([&] {
        uint64_t hash = 0;
        accepted      = ZEngine::Rendering::Renderers::AssetPreviewPass::DependencyFingerprint(Material, &hash);
    });
    worker.join();
    EXPECT_FALSE(accepted);
}
