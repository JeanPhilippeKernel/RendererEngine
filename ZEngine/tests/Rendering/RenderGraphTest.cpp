#include <ZEngine/Core/Memory/MemoryManager.h>
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

    void AddRead(RGPass& pass, uint32_t resource_index)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, 0};
        pass.Reads.push(pr);
    }

    void AddWrite(RGPass& pass, uint32_t resource_index)
    {
        RGPassResource pr;
        pr.Handle = {resource_index, 0};
        pass.Writes.push(pr);
    }
} // namespace

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

TEST(RenderGraphTopologyTest, FallsBackToDeclarationOrderOnCycle)
{
    MemoryManager manager{};
    manager.Initialize(16384, {});
    auto&         arena = manager.MainArena;

    Array<RGPass> passes;
    passes.init(&arena, 4);

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

    // Mirrors RenderGraph::BuildTopology()'s own fallback behavior on a cycle.
    if (!BuildPassTopology(&arena, passes, order, &cycle_idx))
    {
        order.clear();
        for (uint32_t i = 0; i < passes.size(); ++i)
            order.push(i);
    }

    ASSERT_EQ(order.size(), 2u);
    EXPECT_EQ(order[0], 0u);
    EXPECT_EQ(order[1], 1u);

    manager.Shutdown();
}
