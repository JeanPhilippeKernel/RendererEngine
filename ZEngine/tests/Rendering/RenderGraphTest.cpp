#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Rendering/Renderers/Base/IInlineComputePass.h>
#include <ZEngine/Rendering/Renderers/Base/ITransferPass.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/Graphics/GridPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace ZEngine;
using namespace ZEngine::Core::Memory;
using namespace ZEngine::Core::Containers;
using namespace ZEngine::Rendering::Renderers;

namespace
{
    RGPass MakePass(ArenaAllocator* arena, cstring name)
    {
        RGPass pass;
        pass.Name = name;
        pass.Reads.init(arena, 4);
        pass.Writes.init(arena, 4);
        pass.QueryWrites.init(arena, 2);
        pass.QueryResets.init(arena, 2);
        return pass;
    }

    void AddRead(RGPass& pass, uint32_t resource_index, uint32_t version = 0)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, version};
        pass.Reads.push(pr);
    }

    void AddWrite(RGPass& pass, uint32_t resource_index, uint32_t version = 0)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, version};
        pass.Writes.push(pr);
    }

    Array<RGResource> MakeResources(ArenaAllocator* arena, uint32_t count)
    {
        Array<RGResource> resources;
        resources.init(arena, count, count);
        return resources;
    }

    struct TestTransferPass final : ITransferPass
    {
        void RegisterTransfer(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const /*res_builder*/) override {}
        void RecordTransfer(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    struct TestComputePass final : IInlineComputePass
    {
        cstring GetShaderName() const override
        {
            return "test_compute";
        }
        uint32_t GetPushConstantSize() const override
        {
            return 16;
        }
        void RegisterCompute(Hardwares::VulkanDevicePtr const /*device*/, const RenderGraphFrameContext& /*frame_context*/, RenderGraphResourceBuilderPtr const /*res_builder*/) override {}
        void ExecuteCompute(Hardwares::VulkanDevicePtr const /*device*/, RenderGraphResourceInspectorPtr /*res_inspector*/, Rendering::Scenes::SceneDataPtr /*scene*/, VkPipeline /*pipeline*/, VkPipelineLayout /*layout*/, Hardwares::CommandBufferPtr const /*command_buffer*/) override {}
    };

    void TestReadbackCallback(const void* /*data*/, size_t /*size*/, void* /*context*/) {}
} // namespace

TEST(RenderGraphTransientPoolTest, RequiresUsageSupersetForAliasing)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification base{};
    base.Width  = 128;
    base.Height = 128;
    base.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({1, 0}, base, 0);

    auto storage           = base;
    storage.IsUsageStorage = true;
    EXPECT_FALSE(pool.TryAlias(storage, 1).Valid());

    auto transfer_source                  = base;
    transfer_source.IsUsageTransferSource = true;
    EXPECT_FALSE(pool.TryAlias(transfer_source, 1).Valid());

    EXPECT_TRUE(pool.TryAlias(base, 1).Valid());
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesOnlyCompatibleBufferStorage)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView buffer = {};
    buffer.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&buffer, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0);

    EXPECT_EQ(pool.TryAlias(512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1), &buffer);
    EXPECT_EQ(pool.TryAlias(1024, VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, 1), nullptr);
    EXPECT_EQ(pool.TryAlias(2048, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1), nullptr);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesBackingAllocationOnlyForNonOverlappingImageObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);

    ZEngine::Rendering::Specifications::TextureSpecification spec{};
    spec.Width  = 128;
    spec.Height = 128;
    spec.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({1, 0}, spec, 2);
    pool.RegisterAlias(&pool.Slots[0], "A", {1, 0}, spec, 0, 2);

    pool.BeginFrame();
    EXPECT_TRUE(pool.FindNamedAlias("A", spec, 0, 2).Valid());
    EXPECT_EQ(pool.FindAliasingSlot(spec, 1, 3), nullptr);
    EXPECT_EQ(pool.FindAliasingSlot(spec, 3, 4), &pool.Slots[0]);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, ReusesBackingAllocationOnlyForNonOverlappingBufferObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView backing = {};
    backing.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
    pool.RegisterAlias(&pool.Slots[0], "A", &backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2);

    pool.BeginFrame();
    EXPECT_EQ(pool.FindNamedAlias("A", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2), &backing);
    EXPECT_EQ(pool.FindAliasingSlot(1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, 3), nullptr);
    EXPECT_EQ(pool.FindAliasingSlot(512, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 3, 4), &pool.Slots[0]);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, RetainsNamedAliasingObjectsAcrossFrames)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&           arena = manager.MainArena;

    RGTransientPool pool;
    pool.Initialize(&arena);
    ZEngine::Rendering::Specifications::TextureSpecification spec{};
    spec.Width  = 64;
    spec.Height = 64;
    spec.Format = ZEngine::Rendering::Specifications::ImageFormat::R8G8B8A8_UNORM;
    pool.Register({7, 3}, spec, 1);
    pool.RegisterAlias(&pool.Slots[0], "History", {7, 3}, spec, 0, 1);

    pool.BeginFrame();
    const auto first = pool.FindNamedAlias("History", spec, 0, 1);
    ASSERT_TRUE(first.Valid());

    pool.BeginFrame();
    const auto second = pool.FindNamedAlias("History", spec, 0, 1);
    EXPECT_EQ(second.Index, first.Index);
    EXPECT_EQ(second.Generation, first.Generation);
    manager.Shutdown();
}

TEST(RenderGraphTransientPoolTest, RejectsOverlappingNamedAliasingObjects)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                 arena = manager.MainArena;

    RGTransientBufferPool pool;
    pool.Initialize(&arena);
    BufferView backing = {};
    backing.Handle     = reinterpret_cast<VkBuffer>(1);
    pool.Register(&backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 2);
    pool.RegisterAlias(&pool.Slots[0], "Scratch", &backing, 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2);

    pool.BeginFrame();
    EXPECT_EQ(pool.FindNamedAlias("Scratch", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 0, 2), &backing);
    EXPECT_EQ(pool.FindNamedAlias("Scratch", 1024, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, 1, 3), nullptr);
    manager.Shutdown();
}

TEST(RenderGraphTransientStatisticsTest, CalculatesNonNegativeAliasingSavings)
{
    RGTransientStatistics statistics = {};
    statistics.VirtualImageBytes     = 1024;
    statistics.VirtualBufferBytes    = 512;
    statistics.PhysicalImageBytes    = 768;
    statistics.PhysicalBufferBytes   = 256;
    EXPECT_EQ(statistics.EstimatedAliasingSavings(), 512u);

    statistics.PhysicalImageBytes = 2048;
    EXPECT_EQ(statistics.EstimatedAliasingSavings(), 0u);
}

TEST(RenderGraphTimestampTest, ConvertsWrappedTimestampPairsToMilliseconds)
{
    // An eight-bit GPU timestamp wraps from 255 to zero. 250 -> 5 is 11 ticks.
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(250, 5, 8, 1000000.0f), 11.0f);
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(10, 15, 64, 200000.0f), 1.0f);
    EXPECT_FLOAT_EQ(RGTimestampDurationMilliseconds(10, 15, 0, 1000000.0f), 0.0f);
}

TEST(RenderGraphDebugDumpTest, ProducesDotAndJsonWithoutStdContainers)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Resources.init(&arena, 1);
    graph.Resources.push({.Name = "FrameColor", .Kind = RGResourceKind::Attachment, .FirstPassIndex = 0, .LastPassIndex = 0});
    graph.Passes.init(&arena, 1);
    RGPass pass = MakePass(&arena, "Composite");
    pass.Queue  = ZEngine::Rendering::QueueType::GRAPHIC_QUEUE;
    AddRead(pass, 0);
    AddWrite(pass, 0, 1);
    graph.Passes.push(std::move(pass));
    graph.SortedPassIndices.init(&arena, 1);
    graph.SortedPassIndices.push(0);
    graph.QueueBatches.init(&arena, 1);
    graph.QueueBatches.push({.Queue = ZEngine::Rendering::QueueType::GRAPHIC_QUEUE, .FirstPassOrder = 0, .PassCount = 1});
    graph.TransientStatistics.VirtualImageBytes  = 1024;
    graph.TransientStatistics.PhysicalImageBytes = 512;

    String dump;
    dump.init(&arena, 512);
    graph.WriteDebugDump(dump, RGDebugDumpFormat::Dot);
    EXPECT_NE(std::strstr(dump.c_str(), "digraph RenderGraph"), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "FrameColor"), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "Composite"), nullptr);

    graph.WriteDebugDump(dump, RGDebugDumpFormat::Json);
    EXPECT_NE(std::strstr(dump.c_str(), "\"statistics\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"resources\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"batches\""), nullptr);
    EXPECT_NE(std::strstr(dump.c_str(), "\"timings\""), nullptr);
    manager.Shutdown();
}

TEST(GridPassTest, BuildsStaticPipelineDescription)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&      arena = manager.MainArena;

    GridPass   pass  = {};
    const auto desc  = pass.BuildGraphicsPipelineDescription(&arena);

    EXPECT_STREQ(desc.DebugName, "Infinite-Grid-Pipeline");
    EXPECT_STREQ(desc.ShaderSpecificationValue.Name, "infinite_grid");
    EXPECT_TRUE(desc.EnableDepthTest);
    EXPECT_FALSE(desc.EnableDepthWrite);
    EXPECT_TRUE(desc.EnableBlending);
    ASSERT_EQ(desc.VertexInputBindingSpecifications.size(), 1u);
    EXPECT_EQ(desc.VertexInputBindingSpecifications[0].Stride, sizeof(float) * 8);
    EXPECT_EQ(desc.VertexInputBindingSpecifications[0].Rate, VK_VERTEX_INPUT_RATE_VERTEX);
    ASSERT_EQ(desc.VertexInputAttributeSpecifications.size(), 1u);
    EXPECT_EQ(desc.VertexInputAttributeSpecifications[0].Format, ZEngine::Rendering::Specifications::ImageFormat::R32G32B32_SFLOAT);

    manager.Shutdown();
}

TEST(RenderGraphCallbackPassTest, DescribesComputePipelineWithoutGraphicsPassBuilder)
{
    TestComputePass pass = {};

    EXPECT_EQ(pass.GetPipelineType(), ZEngine::Rendering::Specifications::RenderPassType::COMPUTE);
    EXPECT_STREQ(pass.GetComputeShaderName(), "test_compute");
    EXPECT_EQ(pass.GetComputePushConstantSize(), 16u);
}

TEST(RenderGraphDeclarationValidationTest, RejectsReadWithoutProducer)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "Reader");
    AddRead(reader, 0, 1);
    passes.push(std::move(reader));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::MissingProducer);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, AcceptsImportedInitialVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "Reader");
    AddRead(reader, 0);
    passes.push(std::move(reader));
    auto resources             = MakeResources(&arena, 1);
    resources[0].External      = true;
    resources[0].TextureHandle = {1, 0};

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, AcceptsImportedBufferInitialVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 1);
    RGPass reader = MakePass(&arena, "BufferReader");
    AddRead(reader, 0);
    passes.push(std::move(reader));

    Core::Memory::BufferView buffer = {};
    buffer.Handle                   = reinterpret_cast<VkBuffer>(1);
    auto resources                  = MakeResources(&arena, 1);
    resources[0].Kind               = RGResourceKind::Buffer;
    resources[0].External           = true;
    resources[0].Buffer             = &buffer;

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, OrdersFrameColorBeforeSwapchainWriter)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass scene = MakePass(&arena, "Scene");
    AddWrite(scene, 0, 1);
    passes.push(std::move(scene));

    RGPass ui = MakePass(&arena, "ZUI");
    AddRead(ui, 0, 1);
    AddWrite(ui, 1, 1);
    passes.push(std::move(ui));

    auto resources             = MakeResources(&arena, 2);
    resources[0].TextureHandle = {1, 0};
    resources[1].Kind          = RGResourceKind::Swapchain;
    resources[1].External      = true;

    EXPECT_TRUE(ValidatePassDeclarations(&arena, passes, resources));

    Array<uint32_t> order;
    order.init(&arena, 2);
    uint32_t cycle_pass = UINT32_MAX;
    EXPECT_TRUE(BuildPassTopology(&arena, passes, order, &cycle_pass));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    manager.Shutdown();
}

TEST(RenderGraphImportedBufferTest, RebindsTheActiveFrameBufferOnly)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                    arena       = manager.MainArena;

    Core::Memory::BufferView frame_zero  = {};
    frame_zero.Handle                    = reinterpret_cast<VkBuffer>(1);
    Core::Memory::BufferView frame_one   = {};
    frame_one.Handle                     = reinterpret_cast<VkBuffer>(2);
    Core::Memory::BufferView null_buffer = {};

    RenderGraph              graph;
    graph.Resources.init(&arena, 2);
    graph.ResourceIndex.init(&arena, 2);
    auto& resource_zero                        = graph.Resources.push_use({});
    resource_zero.Name                         = "CullingInput_Frame0";
    resource_zero.Kind                         = RGResourceKind::Buffer;
    resource_zero.External                     = true;
    resource_zero.Buffer                       = &frame_zero;
    auto& resource_one                         = graph.Resources.push_use({});
    resource_one.Name                          = "CullingInput_Frame1";
    resource_one.Kind                          = RGResourceKind::Buffer;
    resource_one.External                      = true;
    resource_one.Buffer                        = &frame_zero;
    graph.ResourceIndex["CullingInput_Frame0"] = 0;
    graph.ResourceIndex["CullingInput_Frame1"] = 1;

    EXPECT_TRUE(graph.UpdateImportedBuffer("CullingInput_Frame1", &frame_one));
    EXPECT_EQ(graph.Resources[0].Buffer, &frame_zero);
    EXPECT_EQ(graph.Resources[1].Buffer, &frame_one);
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame2", &frame_one));
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame1", nullptr));
    EXPECT_FALSE(graph.UpdateImportedBuffer("CullingInput_Frame1", &null_buffer));
    EXPECT_EQ(graph.Resources[1].Buffer, &frame_one);
    manager.Shutdown();
}

TEST(RenderGraphSynchronizationTest, IndirectReadUsesDrawIndirectAccess)
{
    const RGResourceState state = GetRGAccessState(RGAccess::IndirectRead);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_UNDEFINED);
}

TEST(RenderGraphSynchronizationTest, StorageWriteUsesGeneralWriteOnlyState)
{
    const RGResourceState state = GetRGAccessState(RGAccess::StorageWrite);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_SHADER_WRITE_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_GENERAL);
}

TEST(RenderGraphSynchronizationTest, ColorAttachmentLoadUsesReadWriteState)
{
    const RGResourceState state = GetRGAccessState(RGAccess::ColorReadWrite);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
}

TEST(RenderGraphSynchronizationTest, DepthReadPreservesStoredAttachmentState)
{
    const RGResourceState state = GetRGAccessState(RGAccess::DepthRead);

    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);
    EXPECT_EQ(state.Layout, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
}

TEST(RenderGraphDeclarationTest, ExactBindlessReadKeepsVersionAndShaderStages)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ResourceIndex.init(&arena, 1);

    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    graph.Passes.push(std::move(producer));
    graph.Passes.push(MakePass(&arena, "BindlessConsumer"));

    auto& resource                     = graph.Resources.push_use({});
    resource.Name                      = "LookupTable";
    resource.Kind                      = RGResourceKind::Texture;
    resource.LatestVersion             = 1;
    graph.ResourceIndex[resource.Name] = 0;

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass = 1;
    builder.ReadBindless({0, 1}, 17, RGShaderStages::Vertex | RGShaderStages::Fragment);

    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    const RGPassResource& read = graph.Passes[1].Reads[0];
    EXPECT_EQ(read.Handle.Index, 0u);
    EXPECT_EQ(read.Handle.Version, 1u);
    EXPECT_EQ(read.Access, RGAccess::ShaderRead);
    EXPECT_TRUE(read.HasStateOverride);
    EXPECT_EQ(read.BindlessSlot, 17u);
    EXPECT_EQ(read.StateOverride.Stage, VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT);
    EXPECT_EQ(read.StateOverride.Access, VK_ACCESS_2_SHADER_READ_BIT);
    EXPECT_EQ(read.StateOverride.Layout, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

    Array<uint32_t> order;
    order.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ConditionalReadUsesExactVersionAndExplicitFallback)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 3);
    graph.Resources.init(&arena, 1);
    auto& resource         = graph.Resources.push_use({});
    resource.Kind          = RGResourceKind::Buffer;
    resource.LatestVersion = 1;
    graph.Passes.push(MakePass(&arena, "PredicateProducer"));
    graph.Passes.push(MakePass(&arena, "ConditionalConsumer"));
    graph.Passes.push(MakePass(&arena, "FallbackConsumer"));

    Hardwares::VulkanDevice device     = {};
    graph.Device                       = &device;
    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);

    device.PhysicalDeviceSupportConditionalRendering = true;
    builder.CurrentPass                              = 1;
    builder.UseConditional({0, 1});

    ASSERT_TRUE(graph.Passes[1].Conditional.Enabled);
    EXPECT_FALSE(graph.Passes[1].Conditional.UsesFallback);
    ASSERT_EQ(graph.Passes[1].Reads.size(), 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Handle.Version, 1u);
    EXPECT_EQ(graph.Passes[1].Reads[0].Access, RGAccess::ConditionalRead);

    const RGResourceState state = GetRGAccessState(RGAccess::ConditionalRead);
    EXPECT_EQ(state.Stage, VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT);
    EXPECT_EQ(state.Access, VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT);

    device.PhysicalDeviceSupportConditionalRendering = false;
    builder.CurrentPass                              = 2;
    builder.UseConditional({0, 1}, {.Fallback = RGConditionalFallback::Unconditional});
    EXPECT_TRUE(graph.Passes[2].Conditional.Enabled);
    EXPECT_TRUE(graph.Passes[2].Conditional.UsesFallback);
    EXPECT_TRUE(graph.Passes[2].Reads.empty());
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, ReadbackKeepsExactBufferVersionAndRequiresTransferSink)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 2);
    graph.Resources.init(&arena, 1);
    graph.ReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "Producer"));
    auto& resource         = graph.Resources.push_use({});
    resource.Name          = "ExposureHistogram";
    resource.Kind          = RGResourceKind::Buffer;
    resource.LatestVersion = 2;
    AddWrite(graph.Passes[0], 0, 2);

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass           = 0;
    const RGReadbackHandle handle = builder.DeclareReadback("ExposureReadback", {0, 2}, &TestReadbackCallback, nullptr, 64, 16);

    ASSERT_TRUE(handle.Valid());
    ASSERT_EQ(graph.ReadbackRequests.size(), 1u);
    const RGReadbackRequest& request = graph.ReadbackRequests[handle.Index];
    EXPECT_EQ(request.Source.Index, 0u);
    EXPECT_EQ(request.Source.Version, 2u);
    EXPECT_EQ(request.Offset, 16u);
    EXPECT_EQ(request.Size, 64u);
    EXPECT_EQ(resource.BufferUsage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);

    RGPass sink             = MakePass(&arena, "ExposureReadback");
    sink.Flags              = RGPassFlags::NeverCull;
    sink.RequiresRenderPass = false;
    sink.InternalOperation  = RGInternalPassOperation::Readback;
    sink.RequestedQueue     = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    AddRead(sink, 0, 2);
    graph.Passes.push(std::move(sink));

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    Array<RGExportedResource> exports;
    exports.init(&arena, 0);
    CullPasses(&arena, graph.Passes, graph.Resources, dependencies, exports);
    EXPECT_TRUE(graph.Passes[1].IsActive());

    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);
    BuildQueueBatches(graph.Passes, order, true, false, batches);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationTest, QueryReadbackDependsOnEveryDeclaredQueryWriter)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&       arena = manager.MainArena;

    RenderGraph graph = {};
    graph.Passes.init(&arena, 3);
    graph.Resources.init(&arena, 0);
    graph.QueryPools.init(&arena, 1);
    graph.QueryPoolIndex.init(&arena, 2);
    graph.QueryReadbackRequests.init(&arena, 1);
    graph.Passes.push(MakePass(&arena, "OcclusionWriter"));

    RenderGraphResourceBuilder builder = {};
    builder.Initialize(&graph);
    builder.CurrentPass           = 0;
    const RGQueryHandle    pool   = builder.DeclareOcclusionQueryPool("SceneOcclusion", 8);
    const RGReadbackHandle result = builder.DeclareQueryReadback(pool, &TestReadbackCallback, nullptr);
    builder.WriteQueryPool(pool, 2, 3);

    ASSERT_TRUE(pool.Valid());
    ASSERT_TRUE(result.Valid());
    ASSERT_EQ(graph.QueryPools.size(), 1u);
    EXPECT_EQ(graph.QueryPools[pool.Index].QueryCount, 8u);
    ASSERT_EQ(graph.Passes[0].QueryWrites.size(), 1u);
    EXPECT_EQ(graph.Passes[0].QueryWrites[0].FirstQuery, 2u);
    EXPECT_EQ(graph.Passes[0].QueryWrites[0].Count, 3u);

    RGPass resolve               = MakePass(&arena, "OcclusionQueryReadback");
    resolve.Flags                = RGPassFlags::NeverCull;
    resolve.RequiresRenderPass   = false;
    resolve.InternalOperation    = RGInternalPassOperation::QueryReadback;
    resolve.InternalRequestIndex = result.Index;
    resolve.InternalQueryPool    = pool;
    resolve.RequestedQueue       = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    graph.Passes.push(std::move(resolve));

    Array<uint32_t> order;
    order.init(&arena, 2);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 2);
    ASSERT_TRUE(BuildPassTopology(&arena, graph.Passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    ASSERT_EQ(dependencies.size(), 1u);
    EXPECT_EQ(dependencies[0].From, 0u);
    EXPECT_EQ(dependencies[0].To, 1u);

    Array<RGExportedResource> exports;
    exports.init(&arena, 0);
    CullPasses(&arena, graph.Passes, graph.Resources, dependencies, exports);
    EXPECT_TRUE(graph.Passes[0].IsActive());

    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);
    BuildQueueBatches(graph.Passes, order, true, false, batches);
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphTransferPassTest, RequestsTransferQueueWithoutRenderPass)
{
    TestTransferPass pass = {};
    EXPECT_EQ(pass.GetRequestedQueue(), ZEngine::Rendering::QueueType::TRANSFER_QUEUE);
    EXPECT_FALSE(pass.RequiresRenderPass());
}

TEST(RenderGraphDeclarationValidationTest, RejectsDuplicateProducers)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass first = MakePass(&arena, "First");
    AddWrite(first, 0, 1);
    passes.push(std::move(first));
    RGPass second = MakePass(&arena, "Second");
    AddWrite(second, 0, 1);
    passes.push(std::move(second));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::DuplicateProducer);
    manager.Shutdown();
}

TEST(RenderGraphDeclarationValidationTest, RejectsEnabledConsumerOfDisabledProducer)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    RGPass producer  = MakePass(&arena, "DisabledProducer");
    producer.Enabled = false;
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));
    RGPass consumer = MakePass(&arena, "Consumer");
    AddRead(consumer, 0, 1);
    passes.push(std::move(consumer));
    auto                          resources = MakeResources(&arena, 1);

    RGDeclarationValidationResult result;
    EXPECT_FALSE(ValidatePassDeclarations(&arena, passes, resources, &result));
    EXPECT_EQ(result.Error, RGDeclarationError::MissingProducer);
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, SortsReadAfterWrite)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // Declared as [B reads X, A writes X] — B must still end up after A.
    RGPass b = MakePass(&arena, "B");
    AddRead(b, /*resource=*/0);
    passes.push(std::move(b));

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, /*resource=*/0);
    passes.push(std::move(a));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 1u); // A (declared second, index 1) must come first
    EXPECT_EQ(order[1], 0u); // B (declared first, index 0) must come second

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, BindsReadToItsDeclaredVersion)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // Declared out of order. B consumes X@1; C produces X@2. Versioning
    // must bind B to A rather than the later producer C.
    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 0, 2);
    passes.push(std::move(c));

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0, 1);
    passes.push(std::move(a));

    Array<uint32_t> order;
    order.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 2u); // A
    EXPECT_EQ(order[1], 0u); // B
    EXPECT_EQ(order[2], 1u); // C

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, CullsPassesUnreachableFromSwapchain)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    RGPass source = MakePass(&arena, "Source");
    AddWrite(source, 0, 1);
    passes.push(std::move(source));
    RGPass composite = MakePass(&arena, "Composite");
    AddRead(composite, 0, 1);
    AddWrite(composite, 1, 1);
    passes.push(std::move(composite));
    RGPass present = MakePass(&arena, "Present");
    AddRead(present, 1, 1);
    AddWrite(present, 2, 1);
    passes.push(std::move(present));
    RGPass dead = MakePass(&arena, "Dead");
    AddWrite(dead, 3, 1);
    passes.push(std::move(dead));

    auto resources        = MakeResources(&arena, 4);
    resources[2].Kind     = RGResourceKind::Swapchain;
    resources[2].External = true;

    Array<uint32_t> order;
    order.init(&arena, 4);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));

    Array<RGExportedResource> exports;
    exports.init(&arena, 1);
    CullPasses(&arena, passes, resources, dependencies, exports);

    EXPECT_TRUE(passes[0].IsActive());
    EXPECT_TRUE(passes[1].IsActive());
    EXPECT_TRUE(passes[2].IsActive());
    EXPECT_TRUE(passes[3].Culled);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, RetainsExplicitExportsAndNeverCullPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));
    RGPass exporter = MakePass(&arena, "Exporter");
    AddRead(exporter, 0, 1);
    AddWrite(exporter, 1, 1);
    passes.push(std::move(exporter));
    RGPass side_effect = MakePass(&arena, "SideEffect");
    side_effect.Flags  = RGPassFlags::NeverCull;
    passes.push(std::move(side_effect));
    RGPass dead = MakePass(&arena, "Dead");
    AddWrite(dead, 2, 1);
    passes.push(std::move(dead));

    auto            resources = MakeResources(&arena, 3);
    Array<uint32_t> order;
    order.init(&arena, 4);
    Array<RGPassDependency> dependencies;
    dependencies.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));

    Array<RGExportedResource> exports;
    exports.init(&arena, 1);
    exports.push({
        {1, 1},
        1
    });
    CullPasses(&arena, passes, resources, dependencies, exports);

    EXPECT_TRUE(passes[0].IsActive());
    EXPECT_TRUE(passes[1].IsActive());
    EXPECT_TRUE(passes[2].IsActive());
    EXPECT_TRUE(passes[3].Culled);
    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, PreservesDeclarationOrderForIndependentPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddWrite(b, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 2);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, GroupsIndependentPassesIntoOneRecordingLevel)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass producer = MakePass(&arena, "Producer");
    AddWrite(producer, 0, 1);
    passes.push(std::move(producer));

    RGPass left = MakePass(&arena, "Left");
    AddRead(left, 0, 1);
    AddWrite(left, 1, 1);
    passes.push(std::move(left));

    RGPass right = MakePass(&arena, "Right");
    AddRead(right, 0, 1);
    AddWrite(right, 2, 1);
    passes.push(std::move(right));

    RGPass consumer = MakePass(&arena, "Consumer");
    AddRead(consumer, 1, 1);
    AddRead(consumer, 2, 1);
    passes.push(std::move(consumer));

    Array<uint32_t>         order;
    Array<RGPassDependency> dependencies;
    Array<RGTopologyLevel>  levels;
    order.init(&arena, 4);
    dependencies.init(&arena, 8);
    levels.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr, &dependencies));
    BuildTopologyLevels(&arena, &arena, order, dependencies, static_cast<uint32_t>(passes.size()), levels);

    ASSERT_EQ(levels.size(), 3u);
    ASSERT_EQ(levels[0].PassIndices.size(), 1u);
    ASSERT_EQ(levels[1].PassIndices.size(), 2u);
    ASSERT_EQ(levels[2].PassIndices.size(), 1u);
    EXPECT_EQ(levels[0].PassIndices[0], 0u);
    EXPECT_EQ(levels[1].PassIndices[0], 1u);
    EXPECT_EQ(levels[1].PassIndices[1], 2u);
    EXPECT_EQ(levels[2].PassIndices[0], 3u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, ExcludesDisabledPasses)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    RGPass disabled  = MakePass(&arena, "Disabled");
    disabled.Enabled = false;
    AddWrite(disabled, 0);
    passes.push(std::move(disabled));

    RGPass enabled = MakePass(&arena, "Enabled");
    AddWrite(enabled, 1);
    passes.push(std::move(enabled));

    Array<uint32_t> order;
    order.init(&arena, 4);
    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, HandlesWriteAfterWrite)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // [A writes X, B writes X], no intervening read — A must stay before B.
    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddWrite(b, 0);
    passes.push(std::move(b));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, HandlesWriteAfterRead)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // [A writes X, B reads X, C writes X] — order must be A, B, C.
    RGPass a = MakePass(&arena, "A");
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddWrite(c, 0);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);

    ASSERT_TRUE(BuildPassTopology(&arena, passes, order, nullptr));
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);
    EXPECT_EQ(order[2], 2u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, DetectsSimpleCycle)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // A reads Y, writes X. B reads X, writes Y — a 2-node cycle.
    RGPass a = MakePass(&arena, "A");
    AddRead(a, 1);
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    AddWrite(b, 1);
    passes.push(std::move(b));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_FALSE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    EXPECT_EQ(order.size(), 0u);
    EXPECT_TRUE(cycle_idx == 0u || cycle_idx == 1u);

    manager.Shutdown();
}

TEST(RenderGraphTopologyTest, DetectsLongerCycleWithoutHanging)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

    // A -> B -> C -> A via read/write chains on resources 0,1,2.
    RGPass a = MakePass(&arena, "A");
    AddRead(a, 2);
    AddWrite(a, 0);
    passes.push(std::move(a));

    RGPass b = MakePass(&arena, "B");
    AddRead(b, 0);
    AddWrite(b, 1);
    passes.push(std::move(b));

    RGPass c = MakePass(&arena, "C");
    AddRead(c, 1);
    AddWrite(c, 2);
    passes.push(std::move(c));

    Array<uint32_t> order;
    order.init(&arena, 4);
    uint32_t cycle_idx = UINT32_MAX;

    ASSERT_FALSE(BuildPassTopology(&arena, passes, order, &cycle_idx));
    EXPECT_EQ(order.size(), 0u);
    EXPECT_LT(cycle_idx, 3u);

    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, GroupsContiguousPassesByResolvedQueue)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);
    passes.push(MakePass(&arena, "Graphics A"));
    passes.push(MakePass(&arena, "Compute A"));
    passes.push(MakePass(&arena, "Compute B"));
    passes.push(MakePass(&arena, "Graphics B"));
    passes[1].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes[2].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;

    Array<uint32_t> order;
    order.init(&arena, 4);
    order.push(0);
    order.push(1);
    order.push(2);
    order.push(3);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 4);

    BuildQueueBatches(passes, order, false, true, batches);

    ASSERT_EQ(batches.size(), 3u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[0].FirstPassOrder, 0u);
    EXPECT_EQ(batches[0].PassCount, 1u);
    EXPECT_EQ(batches[1].Queue, ZEngine::Rendering::QueueType::COMPUTE_QUEUE);
    EXPECT_EQ(batches[1].FirstPassOrder, 1u);
    EXPECT_EQ(batches[1].PassCount, 2u);
    EXPECT_EQ(batches[2].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[2].FirstPassOrder, 3u);
    EXPECT_EQ(batches[2].PassCount, 1u);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, FallsBackToGraphicsWithoutDedicatedFamily)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 2);
    passes.push(MakePass(&arena, "Compute"));
    passes.push(MakePass(&arena, "Transfer"));
    passes[0].RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes[1].RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;

    Array<uint32_t> order;
    order.init(&arena, 2);
    order.push(0);
    order.push(1);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 2);

    BuildQueueBatches(passes, order, false, false, batches);

    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(batches[0].PassCount, 2u);
    EXPECT_EQ(passes[0].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    EXPECT_EQ(passes[1].Queue, ZEngine::Rendering::QueueType::GRAPHIC_QUEUE);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, RecordsOneDependencyPerCrossQueueResourceHazard)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 3);
    RGPass compute = MakePass(&arena, "Compute");
    AddWrite(compute, 0, 1);
    compute.RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes.push(std::move(compute));
    RGPass graphics = MakePass(&arena, "Graphics");
    AddRead(graphics, 0, 1);
    AddWrite(graphics, 0, 2);
    passes.push(std::move(graphics));
    RGPass transfer = MakePass(&arena, "Transfer");
    AddRead(transfer, 0, 2);
    transfer.RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    passes.push(std::move(transfer));

    Array<uint32_t> order;
    order.init(&arena, 3);
    order.push(0);
    order.push(1);
    order.push(2);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 3);
    BuildQueueBatches(passes, order, true, true, batches);
    Array<RGQueueDependency> dependencies;
    dependencies.init(&arena, 3);
    Array<RGPassDependency> pass_dependencies;
    pass_dependencies.init(&arena, 2);
    pass_dependencies.push({0, 1});
    pass_dependencies.push({1, 2});

    BuildQueueDependencies(&arena, passes, order, batches, pass_dependencies, dependencies);

    ASSERT_EQ(batches.size(), 3u);
    ASSERT_EQ(dependencies.size(), 2u);
    EXPECT_EQ(dependencies[0].FromBatch, 0u);
    EXPECT_EQ(dependencies[0].ToBatch, 1u);
    EXPECT_EQ(dependencies[1].FromBatch, 1u);
    EXPECT_EQ(dependencies[1].ToBatch, 2u);
    manager.Shutdown();
}

TEST(RenderGraphQueueScheduleTest, ProducesOwnershipTransferForEachQueueHandOff)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 3);
    RGPass compute = MakePass(&arena, "Compute");
    AddWrite(compute, 0, 1);
    compute.RequestedQueue = ZEngine::Rendering::QueueType::COMPUTE_QUEUE;
    passes.push(std::move(compute));
    RGPass graphics = MakePass(&arena, "Graphics");
    AddRead(graphics, 0, 1);
    AddWrite(graphics, 0, 2);
    passes.push(std::move(graphics));
    RGPass transfer = MakePass(&arena, "Transfer");
    AddRead(transfer, 0, 2);
    transfer.RequestedQueue = ZEngine::Rendering::QueueType::TRANSFER_QUEUE;
    passes.push(std::move(transfer));

    Array<uint32_t> order;
    order.init(&arena, 3);
    order.push(0);
    order.push(1);
    order.push(2);
    Array<RGQueueBatch> batches;
    batches.init(&arena, 3);
    BuildQueueBatches(passes, order, true, true, batches);
    Array<RGQueueOwnershipTransfer> transfers;
    transfers.init(&arena, 3);

    BuildQueueOwnershipTransfers(&arena, passes, order, batches, 1, transfers);

    ASSERT_EQ(transfers.size(), 2u);
    EXPECT_EQ(transfers[0].ResourceIndex, 0u);
    EXPECT_EQ(transfers[0].FromBatch, 0u);
    EXPECT_EQ(transfers[0].ToBatch, 1u);
    EXPECT_EQ(transfers[1].ResourceIndex, 0u);
    EXPECT_EQ(transfers[1].FromBatch, 1u);
    EXPECT_EQ(transfers[1].ToBatch, 2u);
    manager.Shutdown();
}
