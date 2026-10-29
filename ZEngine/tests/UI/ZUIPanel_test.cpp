#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Renderers/ZUIPass.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <limits>

using namespace ZEngine::UI;

namespace
{
    struct View : ZUIPanelView
    {
        View() = default;
        explicit View(const char* title)
        {
            Title = title;
        }
        void BuildContent(ZUIContext*, float[4]) override {}
    };

    class ZUIPanelTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager memory  = {};
        ZUIPanelManager                      manager = {};
        View                                 browser{"Content Browser"};
        View                                 console{"Console"};
        std::string                          path  = {};
        ZUIPanel*                            panel = nullptr;

        void                                 SetUp() override
        {
            memory.Initialize(ZMega(8), {});
            manager.Init(&memory.MainArena);
            const auto key                     = ZUIDockHashName(browser.Title);
            manager.DockTree->Root->ContentKey = key;
            panel                              = manager.AddPanel(key);
            manager.AddView(panel, &browser);
            manager.AddView(panel, &console);
            path = (std::filesystem::temp_directory_path() / ("zui-layout-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".ini")).string();
        }
        void TearDown() override
        {
            std::error_code error = {};
            std::filesystem::remove(path, error);
        }
        bool Load()
        {
            ZUIPanelView* views[] = {&browser, &console};
            return ZUIDockLoad(&manager, path.c_str(), views, 2);
        }
    };
} // namespace

TEST_F(ZUIPanelTest, LayoutRoundtripPreservesSpacedTitlesVisibilityAndActiveTab)
{
    browser.Visible  = false;
    panel->ActiveTab = 1;
    ZUIDockSave(&manager, path.c_str());
    browser.Visible = true;
    ASSERT_TRUE(Load());
    ASSERT_EQ(panel->ViewCount, 2u);
    EXPECT_EQ(panel->Views[0], &browser);
    EXPECT_EQ(panel->Views[1], &console);
    EXPECT_FALSE(browser.Visible);
    EXPECT_TRUE(console.Visible);
    EXPECT_EQ(panel->ActiveTab, 1u);
    ZUIDockSave(&manager, path.c_str());
    ASSERT_TRUE(Load());
    EXPECT_EQ(panel->ViewCount, 2u);
}

TEST_F(ZUIPanelTest, OldSingleWordAndTitleOnlyRecordsStillLoad)
{
    FILE* file = fopen(path.c_str(), "w");
    ASSERT_NE(file, nullptr);
    fprintf(file, "# ZUI Layout v4\nnode 0 -1 1 0 1 %016llx 0 0\npanel %016llx 0 0 2\nview Console 0\nview Content Browser\n", (unsigned long long) panel->DockKey, (unsigned long long) panel->DockKey);
    fclose(file);
    ASSERT_TRUE(Load());
    ASSERT_EQ(panel->ViewCount, 2u);
    EXPECT_EQ(panel->Views[0], &console);
    EXPECT_FALSE(console.Visible);
    EXPECT_EQ(panel->Views[1], &browser);
    EXPECT_TRUE(browser.Visible);
}

TEST_F(ZUIPanelTest, TruncatedViewRecordsDoNotReadUninitializedTitles)
{
    FILE* file = fopen(path.c_str(), "w");
    ASSERT_NE(file, nullptr);
    fprintf(file, "# ZUI Layout v4\nnode 0 -1 1 0 1 %016llx 0 0\npanel %016llx 0 0 4294967295\nview Content Browser 1\n", (unsigned long long) panel->DockKey, (unsigned long long) panel->DockKey);
    fclose(file);
    ASSERT_TRUE(Load());
    ASSERT_EQ(panel->ViewCount, 2u); // Unspecified registered views remain available.
    EXPECT_EQ(panel->Views[0], &browser);
    EXPECT_EQ(panel->Views[1], &console);
}

TEST_F(ZUIPanelTest, OlderLayoutCannotUnregisterAnOmittedHiddenImporter)
{
    View importer{"Importer"};
    importer.Visible = false;
    manager.AddView(panel, &importer);
    FILE* file = fopen(path.c_str(), "w");
    ASSERT_NE(file, nullptr);
    fprintf(file, "# ZUI Layout v4\nnode 0 -1 1 0 1 %016llx 0 0\npanel %016llx 0 0 1\nview Content Browser 1\n", (unsigned long long) panel->DockKey, (unsigned long long) panel->DockKey);
    fclose(file);
    ZUIPanelView* views[] = {&browser, &console, &importer};
    ASSERT_TRUE(ZUIDockLoad(&manager, path.c_str(), views, 3));
    ASSERT_EQ(panel->ViewCount, 3u);
    EXPECT_FALSE(importer.Visible);
    manager.SetPanelVisible("Importer", true, true);
    EXPECT_TRUE(manager.IsPanelVisible("Importer"));
    EXPECT_EQ(panel->Views[panel->ActiveTab], &importer);
    ZUIDockSave(&manager, path.c_str());
    ASSERT_TRUE(ZUIDockLoad(&manager, path.c_str(), views, 3));
    EXPECT_TRUE(manager.IsPanelVisible("Importer"));
}

TEST_F(ZUIPanelTest, RenamedViewRestoresItsOldTitleWithoutLosingVisibilityOrTabOrder)
{
    View importer{"Importer"};
    importer.Visible = false;
    manager.AddView(panel, &importer);
    panel->ActiveTab = 2;
    ZUIDockSave(&manager, path.c_str());
    importer.Title        = "Asset Importer";
    importer.LegacyTitle  = "Importer";
    importer.Visible      = true;
    ZUIPanelView* views[] = {&browser, &console, &importer};
    ASSERT_TRUE(ZUIDockLoad(&manager, path.c_str(), views, 3));
    ASSERT_EQ(panel->ViewCount, 3u);
    EXPECT_EQ(panel->Views[2], &importer);
    EXPECT_FALSE(importer.Visible);
    manager.SetPanelVisible("Importer", true, true);
    EXPECT_TRUE(manager.IsPanelVisible("Asset Importer"));
    EXPECT_TRUE(manager.IsPanelVisible("Importer"));
    EXPECT_EQ(panel->ActiveTab, 2u);
    ZUIDockSave(&manager, path.c_str());
    ASSERT_TRUE(ZUIDockLoad(&manager, path.c_str(), views, 3));
    EXPECT_EQ(panel->ViewCount, 3u);
    EXPECT_EQ(panel->Views[2], &importer);
}

TEST_F(ZUIPanelTest, LayoutRestoresMovedSharedViewsWithoutDuplicatingThem)
{
    constexpr uint64_t moved_key = 0xd0c400000001ull;
    FILE*              file      = fopen(path.c_str(), "w");
    ASSERT_NE(file, nullptr);
    fprintf(file, "# ZUI Layout v4\nnode 0 -1 0 0 1\nnode 1 0 1 0 0.5 %016llx 0 0\nnode 2 0 1 0 0.5 %016llx 0 0\npanel %016llx 0 1 0\npanel %016llx 1 0 2\nview Console 1\nview Content Browser 1\n", (unsigned long long) panel->DockKey, (unsigned long long) moved_key, (unsigned long long) panel->DockKey, (unsigned long long) moved_key);
    fclose(file);
    ASSERT_TRUE(Load());
    const auto* moved = manager.FindPanel(moved_key);
    ASSERT_NE(moved, nullptr);
    EXPECT_EQ(moved->ViewCount, 2u);
    EXPECT_EQ(moved->Views[0], &console);
    EXPECT_EQ(moved->Views[1], &browser);
    EXPECT_EQ(moved->ActiveTab, 1u);
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, moved_key), nullptr);
    EXPECT_EQ(panel->ViewCount, 0u);
    EXPECT_TRUE(panel->Hidden);
    ZUIDockSave(&manager, path.c_str());
    ASSERT_TRUE(Load());
    EXPECT_EQ(manager.FindPanel(moved_key)->ViewCount, 2u);
    EXPECT_EQ(panel->ViewCount, 0u);
}

TEST_F(ZUIPanelTest, FocusRequestActivatesAnAlreadyVisibleTab)
{
    panel->ActiveTab    = 0;
    manager.LayoutDirty = false;
    manager.SetPanelVisible(console.Title, true);
    EXPECT_EQ(panel->ActiveTab, 0u);
    EXPECT_FALSE(manager.LayoutDirty);
    manager.SetPanelVisible(console.Title, true, true);
    EXPECT_EQ(panel->ActiveTab, 1u);
    EXPECT_TRUE(manager.LayoutDirty);
    manager.LayoutDirty = false;
    manager.SetPanelVisible(console.Title, true, true);
    EXPECT_FALSE(manager.LayoutDirty);
}

TEST_F(ZUIPanelTest, ReopeningCancelsPendingCloseWithoutDuplicatingDockLeaf)
{
    manager.SetPanelVisible(browser.Title, false);
    manager.SetPanelVisible(console.Title, false);
    ASSERT_TRUE(panel->Hidden);
    ASSERT_EQ(manager.PendingCloseCount, 1u);
    manager.SetPanelVisible(browser.Title, true, true);
    EXPECT_FALSE(panel->Hidden);
    EXPECT_EQ(manager.PendingCloseCount, 0u);
    EXPECT_EQ(manager.DockTree->Root->ContentKey, panel->DockKey);
    EXPECT_EQ(manager.DockTree->Root->First, nullptr);
    EXPECT_TRUE(manager.IsPanelVisible(browser.Title));
    EXPECT_FALSE(manager.IsPanelVisible(console.Title));
}

TEST_F(ZUIPanelTest, HiddenPanelWithVisibleViewIsRedocked)
{
    const auto second_key              = ZUIDockHashName("Other");
    manager.DockTree->Root->ContentKey = second_key;
    panel->Hidden                      = true;
    manager.SetPanelVisible(browser.Title, true, true);
    EXPECT_FALSE(panel->Hidden);
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
    manager.SetPanelVisible(nullptr, true, true);
}

TEST_F(ZUIPanelTest, ResetRestoresDefaultTabsAndCancelsPendingCloseWithoutDuplicatingLeaves)
{
    View importer{"Importer"};
    importer.Visible = false;
    manager.AddView(panel, &importer);
    manager.SetPanelVisible(browser.Title, false);
    manager.SetPanelVisible(console.Title, false);
    ASSERT_TRUE(panel->Hidden);
    ASSERT_EQ(manager.PendingCloseCount, 1u);
    manager.Drag.Active = true;
    manager.ResetLayout();
    EXPECT_TRUE(manager.IsPanelVisible(browser.Title));
    EXPECT_TRUE(manager.IsPanelVisible(console.Title));
    EXPECT_FALSE(manager.IsPanelVisible(importer.Title));
    EXPECT_EQ(manager.PendingCloseCount, 0u);
    EXPECT_FALSE(manager.Drag.Active);
    EXPECT_EQ(panel->ActiveTab, 0u);
    EXPECT_EQ(manager.DockTree->Root->First, nullptr);
    EXPECT_EQ(manager.DockTree->Root->ContentKey, panel->DockKey);
    manager.SetPanelVisible(importer.Title, true, true);
    manager.ResetLayout();
    EXPECT_FALSE(importer.Visible);
    EXPECT_EQ(panel->ActiveTab, 0u);
    EXPECT_EQ(manager.DockTree->Root->First, nullptr);
}

TEST_F(ZUIPanelTest, ResetRedocksOnlyMissingDefaultVisiblePanelsAndRemovesSavedLayout)
{
    const auto other_key = ZUIDockHashName("Other");
    View       optional{"Optional"};
    optional.Visible = false;
    auto* other      = manager.AddPanel(other_key);
    manager.AddView(other, &optional);
    manager.DockTree->Root->ContentKey = other_key;
    panel->Hidden                      = true;
    optional.Visible                   = true;
    manager.SetLayoutPath(path.c_str());
    ZUIDockSave(&manager, path.c_str());
    ASSERT_TRUE(std::filesystem::exists(path));
    manager.ResetLayout();
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
    EXPECT_EQ(ZUIDockFindLeaf(manager.DockTree, other_key), nullptr);
    EXPECT_TRUE(other->Hidden);
    EXPECT_FALSE(optional.Visible);
    EXPECT_FALSE(std::filesystem::exists(path));
    EXPECT_TRUE(manager.LayoutDirty);
    manager.ResetLayout();
    EXPECT_EQ(manager.DockTree->Root->First, nullptr);
    EXPECT_EQ(manager.DockTree->Root->ContentKey, panel->DockKey);
}

namespace
{
    class DocumentTabsTest : public testing::Test
    {
    protected:
        ZEngine::Core::Memory::MemoryManager memory  = {};
        ZUIContext                           ctx     = {};
        ZUIPanelManager                      manager = {};
        View                                 viewport{"Viewport"};
        View                                 hidden{"Hidden"};
        View                                 importer{"Importer"};
        View                                 profiler{"Profiler"};
        ZUIPanel*                            panel = nullptr;

        void                                 SetUp() override
        {
            memory.Initialize(ZMega(16), {});
            ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(2), 2048, 2048);
            manager.Init(&memory.MainArena);
            manager.DrawMenuBar                = false;
            manager.DrawBuiltinStatusBar       = false;
            viewport.Key                       = 1;
            viewport.Chromeless                = true;
            viewport.Closeable                 = false;
            hidden.Key                         = 2;
            hidden.Visible                     = false;
            importer.Key                       = 3;
            profiler.Key                       = 4;
            panel                              = manager.AddPanel(100);
            manager.DockTree->Root->ContentKey = panel->DockKey;
            manager.AddView(panel, &viewport);
            manager.AddView(panel, &hidden);
            manager.AddView(panel, &importer);
            manager.AddView(panel, &profiler);
            ctx.ScreenW = 400;
            ctx.ScreenH = 600;
        }
        void TearDown() override
        {
            ZUIContextDestroy(&ctx);
        }
        uint64_t Key(const char* format, uint64_t identity = 0)
        {
            char key[80] = {};
            snprintf(key, sizeof(key), format, (unsigned long long) panel->DockKey, (unsigned long long) identity);
            return ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        }
        ZUIBox* Find(uint64_t key)
        {
            ZUIBox*  stack[2048] = {};
            uint32_t count       = 0;
            if (ctx.Root)
                stack[count++] = ctx.Root;
            while (count)
            {
                auto* box = stack[--count];
                if (box->Key == key)
                    return box;
                for (auto* child = box->FirstChild; child; child = child->NextSib)
                    if (count < 2048)
                        stack[count++] = child;
            }
            return nullptr;
        }
        ZUIPersistentState* Bar()
        {
            return ZUIStateGetOrInsert(&ctx.StateStore, Key("##tabbar_%llx"));
        }
        void Build(bool docked = false, const char* label = "Scene")
        {
            ZUIBeginFrame(&ctx, 1.f / 60.f);
            ZUIBeginColumn(&ctx, "root", ZPx((float) ctx.ScreenW), ZPx((float) ctx.ScreenH));
            if (docked)
                manager.BuildUI(&ctx, 100.f, 0.f);
            manager.BuildDocumentTabBar(&ctx, "Viewport", label);
            ZUIEndColumn(&ctx);
            ZUILayoutSolve(&ctx);
        }
        ZUIPanel* AddDestination(View* view)
        {
            auto* destination = manager.AddPanel(200);
            manager.AddView(destination, view);
            ZUIDockSplitH(manager.DockTree, manager.DockTree->Root, 0.5f, panel->DockKey, destination->DockKey);
            return destination;
        }
        void Drop(ZUIPanel* source, uint32_t tab, float x, float y)
        {
            manager.Drag.Active    = true;
            manager.Drag.SrcPanel  = source;
            manager.Drag.SrcTabIdx = tab;
            ctx.MousePos[0]        = x;
            ctx.MousePos[1]        = y;
            ctx.MouseReleased[0]   = true;
            ctx.MouseDown[0]       = false;
            Build(true);
            ctx.MouseReleased[0] = false;
        }
    };
} // namespace

TEST_F(DocumentTabsTest, LongDocumentLabelsGrowTabsAndCloseHitTargetsRemainSixteenPixels)
{
    ZUIGlyph glyphs[96] = {};
    for (auto& glyph : glyphs)
        glyph.AdvanceX = 7.f * 1.3f;
    ZUIFont font       = {};
    font.Glyphs        = glyphs;
    font.GlyphCount    = 96;
    font.LineHeight    = 13.f;
    ZUIFontAtlas atlas = {};
    atlas.Body = atlas.Large = &font;
    ctx.Atlas                = &atlas;
    ctx.ScreenW              = 1000;
    Build(false, "A deliberately long scene label");
    const auto* tab = Find(Key("##tab_%llx_%llx", viewport.Key));
    ASSERT_NE(tab, nullptr);
    EXPECT_GT(tab->ComputedSize[0], 224.f);
    const auto* close = Find(Key("x##x_%llx_%llx", importer.Key));
    ASSERT_NE(close, nullptr);
    EXPECT_FLOAT_EQ(close->ComputedSize[0], 10.f);
    EXPECT_FLOAT_EQ(close->ComputedSize[1], 10.f);
    EXPECT_FLOAT_EQ(close->MinHitSize[0], 16.f);
    EXPECT_FLOAT_EQ(close->MinHitSize[1], 16.f);
    EXPECT_TRUE(close->Flags & ZUI_KeyboardActivate);
}

namespace
{
    class DockSplittersTest : public DocumentTabsTest
    {
    protected:
        View destination{"Other"};
        View nested{"Nested"};

        void SetUp() override
        {
            DocumentTabsTest::SetUp();
            viewport.Chromeless = false;
            ctx.ScreenW         = 1001;
            ctx.ScreenH         = 801;
            AddDestination(&destination);
        }
        ZUIBox* Divider(uint32_t index = 0, bool hit = false)
        {
            char key[32] = {};
            snprintf(key, sizeof(key), hit ? "##sdivh_%u" : "##sdiv_%u", index);
            return Find(ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key)));
        }
        ZUIBox* Panel(uint64_t key)
        {
            char name[32] = {};
            snprintf(name, sizeof(name), "##panel_%llx", (unsigned long long) key);
            return Find(ZUIHashStr(name, (uint32_t) ZEngine::Helpers::secure_strlen(name)));
        }
        void Hover(uint32_t index = 0)
        {
            const auto* hit = Divider(index, true);
            ASSERT_NE(hit, nullptr);
            for (uint32_t axis = 0; axis < 2; ++axis)
                ctx.MousePos[axis] = (hit->ScreenMin[axis] + hit->ScreenMax[axis]) * 0.5f;
            Build(true);
        }
        void AddNestedSplit(ZUIAxis root_axis)
        {
            manager.DockTree->Root->SplitAxis = root_axis;
            auto* parent                      = manager.DockTree->Root->First;
            if (root_axis == ZUIAxis::X)
                ZUIDockSplitV(manager.DockTree, parent, 0.5f, panel->DockKey, 300);
            else
                ZUIDockSplitH(manager.DockTree, parent, 0.5f, panel->DockKey, 300);
            manager.AddView(manager.AddPanel(300), &nested);
        }
    };
} // namespace

TEST_F(DockSplittersTest, IdleDocksHaveOnlyABackdropGapAndAnInvisibleEightPixelHitLane)
{
    Build(true);
    const auto* hit = Divider(0, true);
    ASSERT_NE(hit, nullptr);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_FLOAT_EQ(hit->ComputedSize[0], 8.f);
    EXPECT_TRUE(hit->Flags & ZUI_NoKeyboardFocus);
    EXPECT_TRUE(hit->Flags & ZUI_DrawBackground); // Suppress the generic clickable hover overlay.
    EXPECT_FLOAT_EQ(hit->MinHitSize[0], 0.f);
    EXPECT_FLOAT_EQ(hit->MinHitSize[1], 0.f);
    for (const auto& color : hit->Colors)
        EXPECT_FLOAT_EQ(color[3], 0.f);
    const auto* first  = Panel(100);
    const auto* second = Panel(200);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_FALSE(first->Flags & ZUI_DrawBorder);
    EXPECT_FALSE(second->Flags & ZUI_DrawBorder);
    EXPECT_FLOAT_EQ(first->BorderThickness, 0.f);
    EXPECT_FLOAT_EQ(second->ScreenMin[0] - first->ScreenMax[0], 4.f);
    EXPECT_FLOAT_EQ((hit->ScreenMin[0] + hit->ScreenMax[0]) * 0.5f, (first->ScreenMax[0] + second->ScreenMin[0]) * 0.5f);
}

TEST_F(DockSplittersTest, HoverIndicatorIsTwoPixelsCenteredInsideTheGapOnBothAxes)
{
    for (ZUIAxis axis : {ZUIAxis::X, ZUIAxis::Y})
    {
        SCOPED_TRACE((int) axis);
        manager.DockTree->Root->SplitAxis = axis;
        ctx.MousePos[0] = ctx.MousePos[1] = 0.f;
        Build(true);
        Hover();
        const auto* visual = Divider();
        const auto* first  = Panel(100);
        const auto* second = Panel(200);
        ASSERT_NE(visual, nullptr);
        const auto a = (uint32_t) axis;
        EXPECT_FLOAT_EQ(visual->ComputedSize[a], 2.f);
        EXPECT_FLOAT_EQ(visual->ScreenMin[a] - first->ScreenMax[a], 1.f);
        EXPECT_FLOAT_EQ(second->ScreenMin[a] - visual->ScreenMax[a], 1.f);
        EXPECT_FALSE(visual->Flags & ZUI_Clickable);
        for (uint32_t channel = 0; channel < 4; ++channel)
            EXPECT_FLOAT_EQ(visual->Colors[0][channel], ctx.Theme.TabActiveBorder[channel]);
    }
}

TEST_F(DockSplittersTest, HoverBandIsNarrowerThanTheGrabLaneAndClearsOnMouseLeave)
{
    Build(true);
    Hover();
    ASSERT_NE(Divider(), nullptr);
    const auto* hit = Divider(0, true);
    ctx.MousePos[0] = (hit->ScreenMin[0] + hit->ScreenMax[0]) * 0.5f + 3.5f;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_EQ(ctx.Cursor, ZUICursor::HorizontalResize); // Still inside the invisible 8 px lane.
    ctx.MousePos[0] += 10.f;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_EQ(ctx.Cursor, ZUICursor::Default);
}

TEST_F(DockSplittersTest, DockAndIndicatorEdgesAlignToFramebufferPixelsAtFractionalScales)
{
    for (ZUIAxis axis : {ZUIAxis::X, ZUIAxis::Y})
        for (float scale : {1.f, 1.25f, 1.5f, 2.f})
        {
            SCOPED_TRACE((int) axis);
            SCOPED_TRACE(scale);
            manager.DockTree->Root->SplitAxis = axis;
            ctx.UIScale                       = scale;
            Build(true);
            Hover();
            const auto* visual = Divider();
            const auto* first  = Panel(100);
            const auto* second = Panel(200);
            ASSERT_NE(visual, nullptr);
            const auto a = (uint32_t) axis;
            EXPECT_NEAR((second->ScreenMin[a] - first->ScreenMax[a]) * scale, roundf(ctx.Style.DockGap * scale), 0.001f);
            EXPECT_NEAR(visual->ComputedSize[a] * scale, roundf(ctx.Style.DockingSeparatorSize * scale), 0.001f);
            EXPECT_GE(visual->ScreenMin[a], first->ScreenMax[a]);
            EXPECT_LE(visual->ScreenMax[a], second->ScreenMin[a]);
            EXPECT_NEAR((visual->ScreenMin[a] + visual->ScreenMax[a]) * 0.5f, (first->ScreenMax[a] + second->ScreenMin[a]) * 0.5f, 0.5f / scale + 0.001f);
            for (const auto* box : {first, second, visual})
                for (uint32_t coordinate = 0; coordinate < 2; ++coordinate)
                {
                    EXPECT_NEAR(box->ScreenMin[coordinate] * scale, roundf(box->ScreenMin[coordinate] * scale), 0.001f);
                    EXPECT_NEAR(box->ScreenMax[coordinate] * scale, roundf(box->ScreenMax[coordinate] * scale), 0.001f);
                }
        }
}

TEST_F(DockSplittersTest, NestedIndicatorsNeverOverlapAtEitherJunctionOrientation)
{
    AddNestedSplit(ZUIAxis::X);
    ctx.Style.DockingSeparatorSizeRest = 2.f; // Exercise both geometries together, including theme overrides.
    for (ZUIAxis axis : {ZUIAxis::X, ZUIAxis::Y})
    {
        manager.DockTree->Root->SplitAxis        = axis;
        manager.DockTree->Root->First->SplitAxis = axis == ZUIAxis::X ? ZUIAxis::Y : ZUIAxis::X;
        Build(true);
        const auto* parent = Divider();
        const auto* child  = Divider(1);
        const auto* first  = Panel(100);
        ASSERT_NE(parent, nullptr);
        ASSERT_NE(child, nullptr);
        ASSERT_NE(first, nullptr);
        const auto a = (uint32_t) axis;
        EXPECT_FLOAT_EQ(child->ScreenMax[a], first->ScreenMax[a]);
        EXPECT_LT(child->ScreenMax[a], parent->ScreenMin[a]);
    }
}

TEST_F(DockSplittersTest, JunctionHighlightsOnlyTheNearestDivider)
{
    AddNestedSplit(ZUIAxis::X);
    Build(true);
    const auto* parent = Divider(0, true);
    const auto* child  = Divider(1, true);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    const float center = (parent->ScreenMin[0] + parent->ScreenMax[0]) * 0.5f;
    ctx.MousePos[0]    = center - 2.5f;
    ctx.MousePos[1]    = (child->ScreenMin[1] + child->ScreenMax[1]) * 0.5f;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_NE(Divider(1), nullptr);
    ctx.MousePos[0] = center;
    Build(true);
    EXPECT_NE(Divider(), nullptr);
    EXPECT_EQ(Divider(1), nullptr);
}

TEST_F(DockSplittersTest, OversizedIndicatorsAreClampedAndZeroGapKeepsTheGrabLaneUsable)
{
    ctx.Style.DockingSeparatorSize = 50.f;
    for (float gap : {4.f, 2.f, 0.f, -4.f})
    {
        ctx.Style.DockGap = gap;
        Build(true);
        Hover();
        const auto* hit = Divider(0, true);
        ASSERT_NE(hit, nullptr);
        EXPECT_FLOAT_EQ(hit->ComputedSize[0], 8.f);
        const auto* visual = Divider();
        if (gap > 0.f)
        {
            ASSERT_NE(visual, nullptr);
            EXPECT_FLOAT_EQ(visual->ScreenMin[0], Panel(100)->ScreenMax[0]);
            EXPECT_FLOAT_EQ(visual->ScreenMax[0], Panel(200)->ScreenMin[0]);
        }
        else
            EXPECT_EQ(visual, nullptr);
    }
}

TEST_F(DockSplittersTest, InvalidDisplayScaleFallsBackToLogicalPixels)
{
    for (float scale : {0.f, -1.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    {
        ctx.UIScale = scale;
        Build(true);
        Hover();
        const auto* visual = Divider();
        ASSERT_NE(visual, nullptr);
        EXPECT_FLOAT_EQ(visual->ComputedSize[0], 2.f);
        EXPECT_FLOAT_EQ(Panel(200)->ScreenMin[0] - Panel(100)->ScreenMax[0], 4.f);
    }
}

TEST_F(DockSplittersTest, DragIndicatorStaysVisibleOutsideTheLaneAndClearsAfterRelease)
{
    Build(true);
    Hover();
    const auto* hit = Divider(0, true);
    ASSERT_NE(hit, nullptr);
    ctx.HotKey = ctx.ActiveKey = hit->Key;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ctx.PrevMousePos[0]                    = ctx.MousePos[0];
    ctx.PrevMousePos[1]                    = ctx.MousePos[1];
    Build(true);
    ctx.MousePressed[0]   = false;
    ctx.MousePos[0]      += 15.f;
    ctx.MousePos[1]       = 0.f;
    const float old_size  = manager.DockTree->Root->First->SizePx;
    Build(true);
    EXPECT_FLOAT_EQ(manager.DockTree->Root->First->SizePx, old_size + 15.f);
    EXPECT_NE(Divider(), nullptr);
    EXPECT_EQ(ctx.Cursor, ZUICursor::HorizontalResize);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_TRUE(manager.LayoutDirty);
}

TEST_F(DockSplittersTest, InvisibleGrabLaneTakesPrecedenceOverAdjacentClickableContent)
{
    struct ClickableView : View
    {
        void BuildContent(ZUIContext* context, float[4]) override
        {
            auto* body    = ZUIPushBox(context, "##dock_content", 14, ZUI_Clickable | ZUI_DrawBackground);
            body->Size[0] = body->Size[1] = ZFill();
            ZUIPopBox(context);
        }
    } content       = {};
    content.Title   = viewport.Title;
    panel->Views[0] = &content;
    Build(true);
    const auto* first = Panel(100);
    const auto* hit   = Divider(0, true);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(hit, nullptr);
    ctx.MousePos[0] = first->ScreenMax[0] - 1.f;
    ctx.MousePos[1] = (hit->ScreenMin[1] + hit->ScreenMax[1]) * 0.5f;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, hit->Key);
}

TEST_F(DockSplittersTest, JunctionHoverAndClickTargetChooseTheSameNearestDivider)
{
    AddNestedSplit(ZUIAxis::X);
    Build(true);
    const auto* parent = Divider(0, true);
    const auto* child  = Divider(1, true);
    ASSERT_NE(parent, nullptr);
    ASSERT_NE(child, nullptr);
    const float center_x = (parent->ScreenMin[0] + parent->ScreenMax[0]) * 0.5f;
    const float center_y = (child->ScreenMin[1] + child->ScreenMax[1]) * 0.5f;
    for (float delta_y : {0.f, 2.9f})
    {
        ctx.MousePos[0] = center_x - 2.5f;
        ctx.MousePos[1] = center_y + delta_y;
        Build(true);
        ZUIInteractionPass(&ctx);
        const uint32_t expected = delta_y == 0.f ? 1u : 0u;
        EXPECT_NE(Divider(expected), nullptr);
        EXPECT_EQ(Divider(1u - expected), nullptr);
        EXPECT_EQ(ctx.HotKey, Divider(expected, true)->Key);
    }
}

TEST_F(DockSplittersTest, LosingPointerCaptureClearsTheDragIndicatorWithoutAReleaseEvent)
{
    Build(true);
    Hover();
    const auto* hit = Divider(0, true);
    ASSERT_NE(hit, nullptr);
    ctx.HotKey = ctx.ActiveKey = hit->Key;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ctx.PrevMousePos[0]                    = ctx.MousePos[0];
    ctx.PrevMousePos[1]                    = ctx.MousePos[1];
    Build(true);
    ctx.MousePressed[0] = ctx.MouseDown[0] = false;
    ctx.ActiveKey                          = 0;
    ctx.MousePos[0] = ctx.MousePos[1] = 0.f;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_EQ(ctx.Cursor, ZUICursor::Default);
    EXPECT_TRUE(manager.LayoutDirty);
}

TEST_F(DockSplittersTest, LosingActiveKeyWhileMouseIsDownCancelsResizeCapture)
{
    Build(true);
    Hover();
    const auto* hit = Divider(0, true);
    ASSERT_NE(hit, nullptr);
    ctx.HotKey = ctx.ActiveKey = hit->Key;
    ctx.MousePressed[0] = ctx.MouseDown[0] = true;
    ctx.PrevMousePos[0]                    = ctx.MousePos[0];
    ctx.PrevMousePos[1]                    = ctx.MousePos[1];
    Build(true);
    ASSERT_NE(Divider(), nullptr);
    ctx.MousePressed[0] = false;
    ctx.ActiveKey       = 0; // Another control or focus change takes pointer capture without a mouse-up.
    ctx.MousePos[0] = ctx.MousePos[1] = 0.f;
    Build(true);
    EXPECT_EQ(Divider(), nullptr);
    EXPECT_EQ(ctx.Cursor, ZUICursor::Default);
    EXPECT_TRUE(manager.LayoutDirty);
}

TEST_F(DockSplittersTest, OversizedTreesBoundTrackingAndDiscardStaleSlotsAfterCollapse)
{
    auto* leaf = manager.DockTree->Root->First;
    for (uint64_t i = 0; i < 40; ++i)
        leaf = ZUIDockSplitH(manager.DockTree, leaf, .5f, panel->DockKey, 1000 + i);
    Build(true);
    for (uint32_t i = 0; i < 32; ++i)
        EXPECT_NE(Divider(i, true), nullptr);
    EXPECT_EQ(Divider(32, true), nullptr);
    for (uint64_t i = 0; i < 40; ++i)
    {
        auto* extra = ZUIDockFindLeaf(manager.DockTree, 1000 + i);
        ASSERT_NE(extra, nullptr);
        ZUIDockCollapseLeaf(manager.DockTree, extra);
    }
    Build(true);
    EXPECT_NE(Divider(0, true), nullptr);
    for (uint32_t i = 1; i < 32; ++i)
        EXPECT_EQ(Divider(i, true), nullptr);
}

TEST_F(DockSplittersTest, RepeatedHoverAndIdleFramesDoNotGrowPersistentStorage)
{
    Build(true);
    Hover(); // Warm both idle and indicator widget state.
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    const auto tree_bytes       = memory.MainArena.m_current_offset;
    for (uint32_t frame = 0; frame < 60; ++frame)
    {
        if (frame % 2)
            Hover();
        else
        {
            ctx.MousePos[0] = ctx.MousePos[1] = 0.f;
            Build(true);
        }
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
        EXPECT_EQ(memory.MainArena.m_current_offset, tree_bytes);
    }
}

TEST_F(DockSplittersTest, HoverAddsOnlyOneFlatIndicatorQuadAndNoGrabLaneOverlay)
{
    using namespace ZEngine::Rendering::Renderers;
    ZEngine::Core::Memory::ArenaAllocator arena = {};
    memory.MainArena.CreateSubArena(ZMega(4), &arena, "DockSplitter/Payload");
    ZUIPass          pass    = {};
    ZUIRenderPayload payload = {};
    Build(true);
    pass.PreparePayload(&ctx, &payload, &arena);
    const uint32_t idle_vertices = payload.VtxCount;
    const uint32_t idle_indices  = payload.IdxCount;
    Hover();
    ZUIInteractionPass(&ctx);
    Build(true); // Animate the hit lane's hot state, as in a real editor frame.
    arena.Clear();
    pass.PreparePayload(&ctx, &payload, &arena);
    EXPECT_EQ(payload.VtxCount, idle_vertices + 4);
    EXPECT_EQ(payload.IdxCount, idle_indices + 6);
    arena.Shutdown();
}

TEST_F(DocumentTabsTest, DockedAndCentralViewportHaveOnlyTheExternalStrip)
{
    for (bool central : {false, true})
    {
        manager.DockTree->Root->IsCentral = central;
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(400), ZPx(600));
        manager.BuildUI(&ctx, 100.f, 0.f);
        EXPECT_EQ(Find(Key("##tabbar_%llx")), nullptr);
        manager.BuildDocumentTabBar(&ctx, "Viewport", "Scene*");
        EXPECT_NE(Find(Key("##tabbar_%llx")), nullptr);
        EXPECT_NE(Find(Key("##tab_%llx_%llx", viewport.Key)), nullptr);
        EXPECT_EQ(Find(Key("##tab_%llx_%llx", hidden.Key)), nullptr);
        ZUIEndColumn(&ctx);
    }
}

TEST_F(DocumentTabsTest, OverflowArrowsScrollWithoutOverlappingOrLosingHitTests)
{
    Build();
    ASSERT_GT(Bar()->MaxScrollX, 0.f);
    auto* arrow = Find(Key("##tsr_%llx"));
    auto* bar   = Find(Key("##tabbar_%llx"));
    ASSERT_NE(arrow, nullptr);
    ASSERT_NE(bar, nullptr);
    EXPECT_LE(bar->ScreenMax[0], arrow->ScreenMin[0]);
    ctx.MousePos[0] = (arrow->ScreenMin[0] + arrow->ScreenMax[0]) * 0.5f;
    ctx.MousePos[1] = 15.f;
    ZUIInteractionPass(&ctx);
    EXPECT_EQ(ctx.HotKey, arrow->Key);
    ctx.ActiveKey        = arrow->Key;
    ctx.MouseReleased[0] = true;
    Build();
    EXPECT_GT(Bar()->ScrollXTarget, 0.f);
    ctx.MouseReleased[0] = false;
    ctx.ActiveKey        = 0;
    Build();
    EXPECT_GT(Bar()->ScrollX, 0.f);
    EXPECT_LE(Bar()->ScrollX, Bar()->MaxScrollX);
}

TEST_F(DocumentTabsTest, ActivationAndResizeRevealTheActiveTabWithStableIdentity)
{
    Build();
    manager.SetPanelVisible(profiler.Title, true, true);
    for (int frame = 0; frame < 60; ++frame)
        Build(false, "Renamed*");
    auto* tab = Find(Key("##tab_%llx_%llx", profiler.Key));
    auto* bar = Find(Key("##tabbar_%llx"));
    ASSERT_NE(tab, nullptr);
    ASSERT_NE(bar, nullptr);
    EXPECT_GE(tab->ScreenMin[0], bar->ScreenMin[0]);
    EXPECT_LE(tab->ScreenMax[0], bar->ScreenMax[0] + 0.01f);
    const float before = Bar()->ScrollXTarget;
    ctx.ScreenW        = 280;
    Build();
    EXPECT_GT(Bar()->ScrollXTarget, before);
    EXPECT_LE(Bar()->ScrollXTarget, Bar()->MaxScrollX);
}

TEST_F(DocumentTabsTest, HorizontalReorderSkipsHiddenViewsAndPreservesActiveIdentity)
{
    Build();
    const auto held_key = Key("##tab_%llx_%llx", viewport.Key);
    ctx.ActiveKey       = held_key;
    ctx.HotKey          = held_key;
    ctx.MouseDown[0]    = true;
    ctx.MousePos[0]     = 200.f;
    ctx.PrevMousePos[0] = 0.f;
    manager.LayoutDirty = false;
    Build();
    EXPECT_EQ(panel->Views[0], &importer);
    EXPECT_EQ(panel->Views[1], &hidden);
    EXPECT_EQ(panel->Views[2], &viewport);
    EXPECT_EQ(panel->ActiveTab, 2u);
    EXPECT_EQ(panel->ReorderTabIdx, 2u);
    EXPECT_TRUE(manager.LayoutDirty);
    EXPECT_FALSE(manager.Drag.Active);
    ctx.PrevMousePos[0] = ctx.MousePos[0];
    Build();
    EXPECT_NE(Find(held_key), nullptr);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Build();
    EXPECT_FALSE(panel->ReorderActive);
    EXPECT_EQ(panel->ActiveTab, 2u);
}

TEST_F(DocumentTabsTest, VerticalDocumentDragUsesTheCorrectSourceAndViewportCannotClose)
{
    Build();
    ctx.ActiveKey    = Key("##tab_%llx_%llx", importer.Key);
    ctx.HotKey       = ctx.ActiveKey;
    ctx.MouseDown[0] = true;
    ctx.MousePos[1]  = 80.f;
    Build();
    EXPECT_TRUE(manager.Drag.Active);
    EXPECT_EQ(manager.Drag.SrcPanel, panel);
    EXPECT_EQ(manager.Drag.SrcTabIdx, 2u);
    EXPECT_FALSE(panel->ReorderActive);
    manager.Drag         = {};
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    ctx.ActiveKey        = Key("x##x_%llx_%llx", viewport.Key);
    ctx.HotKey           = ctx.ActiveKey;
    Build(true);
    EXPECT_TRUE(viewport.Visible);
}

TEST_F(DocumentTabsTest, FullDestinationRejectsSingleTabAndWholePanelAtomically)
{
    View  destination_view{"Destination"};
    View  extra[kMaxTabsPerPanel] = {};
    auto* destination             = AddDestination(&destination_view);
    for (uint32_t i = 1; i < kMaxTabsPerPanel; ++i)
    {
        extra[i].Key = 1000 + i;
        manager.AddView(destination, &extra[i]);
    }
    for (uint32_t tab : {2u, kWholePanel})
    {
        manager.LayoutDirty = false;
        Drop(panel, tab, 300.f, 350.f);
        EXPECT_EQ(destination->ViewCount, kMaxTabsPerPanel);
        EXPECT_EQ(panel->ViewCount, 4u);
        EXPECT_EQ(panel->Views[2], &importer);
        EXPECT_FALSE(panel->Hidden);
        EXPECT_FALSE(manager.LayoutDirty);
        EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
    }
}

TEST_F(DocumentTabsTest, TabMergePreservesRemainingActiveViewAndReusesEmptySlots)
{
    View  destination_view{"Destination"};
    auto* destination = AddDestination(&destination_view);
    panel->ActiveTab  = 3;
    Drop(panel, 2, 300.f, 350.f);
    EXPECT_EQ(panel->Views[panel->ActiveTab], &profiler);
    EXPECT_EQ(destination->Views[destination->ActiveTab], &importer);
    Drop(panel, kWholePanel, 300.f, 350.f);
    EXPECT_EQ(panel->ViewCount, 0u);
    EXPECT_TRUE(panel->Hidden);
    EXPECT_EQ(destination->ViewCount, 5u);
    EXPECT_EQ(destination->Views[destination->ActiveTab], &profiler);
    EXPECT_EQ(manager.PanelCount, 2u);
    auto* reused = manager.AddPanel(300);
    EXPECT_EQ(reused, panel);
    EXPECT_EQ(manager.PanelCount, 2u);
    EXPECT_EQ(reused->LastActiveView, nullptr);
}

TEST_F(DocumentTabsTest, DroppingOnExternalDocumentStripMergesBeforeDragCleanup)
{
    View  destination_view{"Destination"};
    auto* other = AddDestination(&destination_view);
    Build(true);
    Drop(other, 0, 100.f, 15.f);
    EXPECT_EQ(panel->ViewCount, 5u);
    EXPECT_EQ(panel->Views[panel->ActiveTab], &destination_view);
    EXPECT_EQ(other->ViewCount, 0u);
    EXPECT_TRUE(other->Hidden);
    EXPECT_FALSE(manager.Drag.Active);
}

TEST_F(DocumentTabsTest, DragGhostAndDocumentInsertionMarkerShareTheForegroundLayer)
{
    View  destination_view{"Destination"};
    auto* other = AddDestination(&destination_view);
    Build(true);
    manager.Drag.Active    = true;
    manager.Drag.SrcPanel  = other;
    manager.Drag.SrcTabIdx = 0;
    ctx.MouseDown[0]       = true;
    ctx.MousePos[0]        = 100.f;
    ctx.MousePos[1]        = 15.f;
    Build(true);
    auto*      marker       = Find(Key("##dz_ins_%llx"));
    const char header_key[] = "####drag_ghost_h";
    auto*      header       = Find(ZUIHashStr(header_key, sizeof(header_key) - 1));
    ASSERT_NE(marker, nullptr);
    ASSERT_NE(header, nullptr);
    ASSERT_NE(ctx.ForegroundRoot, nullptr);
    EXPECT_EQ(marker->Parent, ctx.ForegroundRoot);
    EXPECT_EQ(header->Parent->Parent, ctx.ForegroundRoot);
    EXPECT_FLOAT_EQ(header->Size[1].Value, 30.f);
    EXPECT_FLOAT_EQ(marker->Colors[0][0], Tokens::AccentBlue.R);
    EXPECT_FLOAT_EQ(marker->Colors[0][1], Tokens::AccentBlue.G);
    EXPECT_FLOAT_EQ(marker->Colors[0][2], Tokens::AccentBlue.B);
    EXPECT_FLOAT_EQ(marker->Colors[0][3], 1.f);
    EXPECT_EQ(ctx.ForegroundRoot->Parent, ctx.Root);
    ZUIEndFrame(&ctx);
    EXPECT_EQ(ctx.Root->LastChild, ctx.ForegroundRoot);
    EXPECT_TRUE(manager.Drag.Active);
    EXPECT_EQ(manager.Drag.DropZone, ZUIDropZone::Center);
}

TEST_F(DocumentTabsTest, WholePanelEdgeMoveKeepsBothSiblingPanelsReachable)
{
    View  destination_view{"Destination"};
    auto* other = AddDestination(&destination_view);
    Drop(panel, kWholePanel, 395.f, 350.f);
    EXPECT_EQ(panel->ViewCount, 4u);
    EXPECT_EQ(other->ViewCount, 1u);
    EXPECT_FALSE(panel->Hidden);
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, other->DockKey), nullptr);
}

TEST_F(DocumentTabsTest, SplittingAnOptionalTabDoesNotChangeItsResetVisibility)
{
    View optional{"Optional"};
    optional.Key     = 5;
    optional.Visible = false;
    manager.AddView(panel, &optional);
    View destination_view{"Destination"};
    AddDestination(&destination_view);
    manager.SetPanelVisible(optional.Title, true, true);
    Drop(panel, 4, 395.f, 350.f);
    ASSERT_TRUE(manager.IsPanelVisible(optional.Title));
    manager.ResetLayout();
    EXPECT_FALSE(optional.Visible);
    EXPECT_FALSE(manager.IsPanelVisible(optional.Title));
    EXPECT_TRUE(manager.IsPanelVisible(viewport.Title));
    EXPECT_TRUE(manager.IsPanelVisible(destination_view.Title));
    EXPECT_EQ(manager.PendingCloseCount, 0u);
}

TEST_F(DocumentTabsTest, NearFullDestinationRejectsWholePanelWithoutPartialInsertion)
{
    View  destination_view{"Destination"};
    View  extra[kMaxTabsPerPanel] = {};
    auto* destination             = AddDestination(&destination_view);
    for (uint32_t i = 1; i < kMaxTabsPerPanel - 1; ++i)
    {
        extra[i].Key = 1000 + i;
        manager.AddView(destination, &extra[i]);
    }
    Drop(panel, kWholePanel, 300.f, 350.f);
    EXPECT_EQ(destination->ViewCount, kMaxTabsPerPanel - 1);
    EXPECT_EQ(panel->ViewCount, 4u);
    EXPECT_EQ(panel->Views[0], &viewport);
    EXPECT_FALSE(panel->Hidden);
}

TEST_F(DocumentTabsTest, ExhaustedPanelPoolRejectsSplitWithoutMutatingDockTree)
{
    View  destination_view{"Destination"};
    auto* destination = AddDestination(&destination_view);
    while (manager.PanelCount < kMaxPanels)
        ASSERT_NE(manager.AddPanel(1000 + manager.PanelCount), nullptr);
    auto* root          = manager.DockTree->Root;
    manager.LayoutDirty = false;
    Drop(panel, 2, 395.f, 350.f);
    EXPECT_EQ(manager.DockTree->Root, root);
    EXPECT_EQ(panel->ViewCount, 4u);
    EXPECT_EQ(destination->ViewCount, 1u);
    EXPECT_EQ(manager.PanelCount, kMaxPanels);
    EXPECT_FALSE(manager.LayoutDirty);
}

TEST_F(DocumentTabsTest, RemovingTheOnlyVisibleSourceTabKeepsHiddenViewsRestorable)
{
    View destination_view{"Destination"};
    AddDestination(&destination_view);
    viewport.Visible = false;
    profiler.Visible = false;
    panel->ActiveTab = 2;
    Drop(panel, 2, 300.f, 350.f);
    EXPECT_EQ(panel->ViewCount, 3u);
    EXPECT_TRUE(panel->Hidden);
    EXPECT_EQ(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
    manager.SetPanelVisible(viewport.Title, true, true);
    EXPECT_FALSE(panel->Hidden);
    EXPECT_EQ(panel->Views[panel->ActiveTab], &viewport);
    EXPECT_NE(ZUIDockFindLeaf(manager.DockTree, panel->DockKey), nullptr);
}

TEST_F(DocumentTabsTest, RepeatedSplitAndMergeReusesPanelSlotsWithoutLosingViews)
{
    View       destination_view{"Destination"};
    auto*      destination = AddDestination(&destination_view);
    const auto index_of    = [](const ZUIPanel* p, const ZUIPanelView* view) {
        for (uint32_t i = 0; i < p->ViewCount; ++i)
            if (p->Views[i] == view)
                return i;
        return kWholePanel;
    };
    const auto drop_at = [&](ZUIPanel* source, uint32_t tab, ZUIPanel* target, bool edge) {
        const float root_rect[4] = {0.f, 100.f, 400.f, 600.f};
        ZUIDockLayout(manager.DockTree, root_rect);
        float r[4] = {};
        ASSERT_TRUE(ZUIDockRectForKey(manager.DockTree, target->DockKey, r));
        Drop(source, tab, edge ? r[2] - 1.f : (r[0] + r[2]) * 0.5f, (r[1] + r[3]) * 0.5f);
    };
    for (uint32_t iteration = 0; iteration < 40; ++iteration)
    {
        drop_at(panel, index_of(panel, &importer), destination, true);
        ZUIPanel* detached = nullptr;
        for (uint32_t i = 0; i < manager.PanelCount; ++i)
            if (!manager.Panels[i].Hidden && &manager.Panels[i] != panel && index_of(&manager.Panels[i], &importer) != kWholePanel)
                detached = &manager.Panels[i];
        ASSERT_NE(detached, nullptr);
        ASSERT_NE(detached, destination);
        drop_at(detached, 0, destination, false);
        drop_at(destination, index_of(destination, &importer), panel, false);
        ASSERT_EQ(panel->ViewCount, 4u);
        EXPECT_EQ(destination->ViewCount, 1u);
        EXPECT_EQ(manager.PanelCount, 3u);
        EXPECT_NE(index_of(panel, &viewport), kWholePanel);
        EXPECT_NE(index_of(panel, &hidden), kWholePanel);
        EXPECT_NE(index_of(panel, &importer), kWholePanel);
        EXPECT_NE(index_of(panel, &profiler), kWholePanel);
    }
}

TEST_F(DocumentTabsTest, CloseAfterReorderingStillTargetsTheSameDocument)
{
    panel->Views[0] = &importer;
    panel->Views[2] = &viewport;
    Build(true);
    ctx.ActiveKey        = Key("x##x_%llx_%llx", importer.Key);
    ctx.HotKey           = ctx.ActiveKey;
    ctx.MouseReleased[0] = true;
    Build(true);
    EXPECT_FALSE(importer.Visible);
    EXPECT_TRUE(viewport.Visible);
    EXPECT_TRUE(profiler.Visible);
    EXPECT_EQ(panel->Views[panel->ActiveTab], &viewport);
}
