#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <cstring>

using namespace ZEngine::UI;

namespace
{
    // Owns a memory manager + ZUI context for one test; the box tree is built with
    // ZUIPushBox/ZUIPopBox exactly like the widget layer does, then solved directly.
    struct LayoutFixture
    {
        ZEngine::Core::Memory::MemoryManager memory = {};
        ZUIContext                           ctx    = {};

        LayoutFixture()
        {
            memory.Initialize(ZMega(1), {});
            ZUIContextInit(&ctx, &memory.MainArena, ZKilo(64), ZKilo(16), 64, 64);
        }

        ~LayoutFixture()
        {
            ZUIContextDestroy(&ctx);
            memory.Shutdown();
        }

        ZUIBox* Push(const char* key, ZUISize w, ZUISize h, ZUIAxis axis = ZUIAxis::Y)
        {
            ZUIBox* box     = ZUIPushBox(&ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_None);
            box->Size[0]    = w;
            box->Size[1]    = h;
            box->LayoutAxis = axis;
            return box;
        }

        ZUIBox* Leaf(const char* key, ZUISize w, ZUISize h)
        {
            ZUIBox* box = Push(key, w, h);
            ZUIPopBox(&ctx);
            return box;
        }

        void Solve()
        {
            ZUILayoutSolve(&ctx);
        }
    };
} // namespace

TEST(ZUILayoutTest, DefaultsKeepChildrenPackedWithoutSpacing)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    ZUIBox*       a    = f.Leaf("a", ZPx(40), ZPx(10));
    ZUIBox*       b    = f.Leaf("b", ZPx(40), ZPx(10));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[1], 10.f);
    EXPECT_FLOAT_EQ(a->ScreenMin[0], 0.f);
    EXPECT_FLOAT_EQ(root->FlowOffset, 0.f);
}

TEST(ZUILayoutTest, GapSeparatesConsecutiveChildren)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->Gap          = 4.f;
    ZUIBox* a          = f.Leaf("a", ZPx(40), ZPx(10));
    ZUIBox* b          = f.Leaf("b", ZPx(40), ZPx(10));
    ZUIBox* c          = f.Leaf("c", ZPx(40), ZPx(10));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[1], 14.f);
    EXPECT_FLOAT_EQ(c->ScreenMin[1], 28.f);
}

TEST(ZUILayoutTest, FitContainerIncludesGapsButNoTrailingGap)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZFit());
    root->Gap          = 4.f;
    f.Leaf("a", ZPx(40), ZPx(10));
    f.Leaf("b", ZPx(40), ZPx(10));
    f.Leaf("c", ZPx(40), ZPx(10));
    f.Solve();

    EXPECT_FLOAT_EQ(root->ComputedSize[1], 38.f); // 3 * 10 + 2 gaps of 4
}

TEST(ZUILayoutTest, FillChildrenShareSpaceLeftAfterGaps)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->Gap          = 10.f;
    ZUIBox* fixed      = f.Leaf("fixed", ZFill(), ZPx(20));
    ZUIBox* fill_a     = f.Leaf("fill_a", ZFill(), ZFill());
    ZUIBox* fill_b     = f.Leaf("fill_b", ZFill(), ZFill());
    f.Solve();

    // 100 - 20 fixed - 2 gaps of 10 = 60, split across two Fill children.
    EXPECT_FLOAT_EQ(fill_a->ComputedSize[1], 30.f);
    EXPECT_FLOAT_EQ(fill_b->ComputedSize[1], 30.f);
    EXPECT_FLOAT_EQ(fixed->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(fill_a->ScreenMin[1], 30.f);
    EXPECT_FLOAT_EQ(fill_b->ScreenMin[1], 70.f);
    EXPECT_FLOAT_EQ(fill_b->ScreenMax[1], 100.f);
}

TEST(ZUILayoutTest, GapAppliesAlongRowAxis)
{
    LayoutFixture f   = {};
    ZUIBox*       row = f.Push("row", ZPx(200), ZPx(20), ZUIAxis::X);
    row->Gap          = 8.f;
    ZUIBox* a         = f.Leaf("a", ZPx(30), ZPx(20));
    ZUIBox* b         = f.Leaf("b", ZPx(30), ZPx(20));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[0], 0.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[0], 38.f);
}

TEST(ZUILayoutTest, FloatedChildrenTakeNeitherGapNorFlowSpace)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->Gap          = 6.f;
    ZUIBox* a          = f.Leaf("a", ZPx(40), ZPx(10));
    ZUIBox* floated    = f.Leaf("floated", ZPx(10), ZPx(10));
    floated->Flags     = floated->Flags | ZUI_FloatY;
    ZUIBox* b          = f.Leaf("b", ZPx(40), ZPx(10));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[1], 16.f); // a (10) + one gap (6); floated child ignored
}

TEST(ZUILayoutTest, MainAlignCenterAndEndUseFreeSpace)
{
    {
        LayoutFixture f    = {};
        ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
        root->Gap          = 10.f;
        root->MainAlign    = ZUIAlign::Center;
        ZUIBox* a          = f.Leaf("a", ZPx(40), ZPx(20));
        ZUIBox* b          = f.Leaf("b", ZPx(40), ZPx(20));
        f.Solve();

        // content = 20 + 10 + 20 = 50 -> 25 free on each side
        EXPECT_FLOAT_EQ(a->ScreenMin[1], 25.f);
        EXPECT_FLOAT_EQ(b->ScreenMin[1], 55.f);
    }
    {
        LayoutFixture f    = {};
        ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
        root->MainAlign    = ZUIAlign::End;
        ZUIBox* a          = f.Leaf("a", ZPx(40), ZPx(20));
        f.Solve();

        EXPECT_FLOAT_EQ(a->ScreenMin[1], 80.f);
    }
}

TEST(ZUILayoutTest, SpaceBetweenPinsFirstAndLastChild)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->MainAlign    = ZUIAlign::SpaceBetween;
    ZUIBox* a          = f.Leaf("a", ZPx(40), ZPx(10));
    ZUIBox* b          = f.Leaf("b", ZPx(40), ZPx(10));
    ZUIBox* c          = f.Leaf("c", ZPx(40), ZPx(10));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[1], 45.f);
    EXPECT_FLOAT_EQ(c->ScreenMax[1], 100.f);
}

TEST(ZUILayoutTest, MainAlignIsNoOpWhenFillConsumesFreeSpace)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->MainAlign    = ZUIAlign::Center;
    ZUIBox* fill       = f.Leaf("fill", ZFill(), ZFill());
    f.Solve();

    EXPECT_FLOAT_EQ(fill->ScreenMin[1], 0.f);
    EXPECT_FLOAT_EQ(fill->ComputedSize[1], 100.f);
}

TEST(ZUILayoutTest, CrossAlignCenterAndEnd)
{
    LayoutFixture f    = {};
    ZUIBox*       root = f.Push("root", ZPx(100), ZPx(100));
    root->CrossAlign   = ZUIAlign::Center;
    ZUIBox* narrow     = f.Leaf("narrow", ZPx(40), ZPx(10));
    f.Solve();
    EXPECT_FLOAT_EQ(narrow->ScreenMin[0], 30.f);

    LayoutFixture g     = {};
    ZUIBox*       root2 = g.Push("root", ZPx(100), ZPx(100));
    root2->CrossAlign   = ZUIAlign::End;
    ZUIBox* end_child   = g.Leaf("end_child", ZPx(40), ZPx(10));
    ZUIBox* full        = g.Leaf("full", ZFill(), ZPx(10));
    g.Solve();
    EXPECT_FLOAT_EQ(end_child->ScreenMin[0], 60.f);
    EXPECT_FLOAT_EQ(full->ScreenMin[0], 0.f); // already spans the cross axis
}

TEST(ZUILayoutTest, PaddingComposesWithGapAndAlign)
{
    LayoutFixture f   = {};
    ZUIBox*       row = f.Push("row", ZPx(200), ZPx(40), ZUIAxis::X);
    ZUIPadding(row, 10.f);
    row->Gap        = 5.f;
    row->CrossAlign = ZUIAlign::Center;
    ZUIBox* a       = f.Leaf("a", ZPx(30), ZPx(10));
    ZUIBox* b       = f.Leaf("b", ZPx(30), ZPx(20));
    f.Solve();

    EXPECT_FLOAT_EQ(a->ScreenMin[0], 10.f);
    EXPECT_FLOAT_EQ(b->ScreenMin[0], 45.f);
    EXPECT_FLOAT_EQ(a->ScreenMin[1], 10.f + (20.f - 10.f) * 0.5f); // inner height 20, child 10
    EXPECT_FLOAT_EQ(b->ScreenMin[1], 10.f);
}
