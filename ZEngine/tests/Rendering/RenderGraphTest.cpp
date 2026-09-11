#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Rendering/Renderers/Base/RenderPass.h>
#include <ZEngine/Rendering/Renderers/RenderGraphTopology.h>
#include <gtest/gtest.h>

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

TEST(RenderPassBuilderTest, KeepsLoadOperationPerExternalOutput)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&                                                          arena = manager.MainArena;

    ZEngine::Rendering::Renderers::RenderPasses::RenderPassBuilder builder;
    builder.Initialize(&arena);
    auto spec = builder.UseRenderTarget({1, 0}, ZEngine::Rendering::Specifications::LoadOperation::CLEAR).UseRenderTarget({2, 0}, ZEngine::Rendering::Specifications::LoadOperation::LOAD).Detach();

    ASSERT_EQ(spec.ExternalOutputs.size(), 2u);
    ASSERT_EQ(spec.ExternalOutputLoadOps.size(), 2u);
    EXPECT_EQ(spec.ExternalOutputLoadOps[0], ZEngine::Rendering::Specifications::LoadOperation::CLEAR);
    EXPECT_EQ(spec.ExternalOutputLoadOps[1], ZEngine::Rendering::Specifications::LoadOperation::LOAD);

    manager.Shutdown();
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

    BuildQueueDependencies(&arena, passes, order, batches, 1, dependencies);

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
