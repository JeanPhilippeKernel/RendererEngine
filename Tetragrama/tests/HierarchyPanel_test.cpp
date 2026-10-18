#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/HierarchyPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <cstdio>
#include <memory>
#include <string_view>
#include <utility>

using namespace ZEngine::UI;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Helpers;

namespace Tetragrama::Panels
{
    struct HierarchyPanelTestAccess
    {
        static void Draw(HierarchyPanel& panel, ZUIContext& ctx, float width, Editor& editor, EditorScene& scene, ZEngine::EngineContext& engine)
        {
            panel.BuildHierarchy(&ctx, width, &editor, &scene, &engine);
        }
    };
} // namespace Tetragrama::Panels

class HierarchyPanelTest : public testing::Test
{
protected:
    ZEngine::Core::Memory::MemoryManager    memory     = {};
    Scene                                   ecs        = {};
    ActorManager                            actors     = {};
    Tetragrama::EditorScene                 scene      = {};
    std::unique_ptr<Tetragrama::Editor>     editor     = {};
    std::unique_ptr<ZEngine::EngineContext> engine     = {};
    Tetragrama::Panels::HierarchyPanel      panel      = {};
    ZUIContext                              ctx        = {};
    ZUIGlyph                                glyphs[96] = {};
    ZUIFont                                 font       = {};
    ZUIFontAtlas                            atlas      = {};
    ActorHandle                             light      = {};
    ActorHandle                             camera     = {};

    void                                    SetUp() override
    {
        memory.Initialize(ZMega(64), {});
        ecs.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, ecs);
        editor               = std::make_unique<Tetragrama::Editor>();
        engine               = std::make_unique<ZEngine::EngineContext>();
        engine->ActorManager = &actors;
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(4), ZMega(1), 2048, 2048);
        ctx.ScreenW     = 800;
        ctx.ScreenH     = 600;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
            glyph.AdvanceX = 5.5f;
        font.Glyphs     = glyphs;
        font.GlyphCount = 96;
        font.LineHeight = 14.f;
        atlas.Body = atlas.Small = atlas.Large = atlas.Header = atlas.Heading = atlas.Tiny = &font;
        ctx.Atlas                                                                          = &atlas;
        light                                                                              = scene.CreateActor(actors, "AlphaLight");
        actors.Access(light)->AddComponent<LightComponent>({});
        camera = scene.CreateActor(actors, "BetaCamera");
        actors.Access(camera)->AddComponent<CameraComponent>({});
        scene.MarkDirty(false);
        Frame();
    }
    void TearDown() override
    {
        ZUIContextDestroy(&ctx);
        actors.Shutdown();
        ecs.Shutdown();
    }
    void Frame(float width = 380.f, float height = 500.f)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(height));
        Tetragrama::Panels::HierarchyPanelTestAccess::Draw(panel, ctx, width, *editor, scene, *engine);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    ZUIBox* FindText(std::string_view text, ZUIBox* root = nullptr)
    {
        if (!root)
            root = ctx.Root;
        if ((root->Flags & ZUI_DrawText) && root->Label.Ptr && std::string_view(root->Label.Ptr, root->Label.Len) == text)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = FindText(text, child))
                return found;
        return nullptr;
    }
    ZUIBox* Find(uint64_t key, ZUIBox* root = nullptr)
    {
        if (!root)
            root = ctx.Root;
        if (root->Key == key)
            return root;
        for (auto* child = root->FirstChild; child; child = child->NextSib)
            if (auto* found = Find(key, child))
                return found;
        return nullptr;
    }
    ZUIPersistentState* ScrollState()
    {
        return ZUIStateGetOrInsert(&ctx.StateStore, ZUIHashStr("##hier_scroll", 13));
    }
    ZUIBox* Scrollbar()
    {
        char key[64] = {};
        snprintf(key, sizeof(key), "##sbthm_%llu", static_cast<unsigned long long>(ZUIHashStr("##hier_scroll", 13)));
        return Find(ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key))));
    }
    void AddOverflowActors()
    {
        for (uint32_t i = 0; i < 40; ++i)
        {
            char name[32] = {};
            snprintf(name, sizeof(name), "Overflow_%03u", i);
            ASSERT_TRUE(scene.CreateActor(actors, name).Valid());
        }
        scene.MarkDirty(false);
    }
    NameComponent* Name(ActorHandle handle)
    {
        return actors.Access(handle)->GetComponent<NameComponent>();
    }
    void Click(float x, float y)
    {
        ctx.MousePos[0] = x;
        ctx.MousePos[1] = y;
        // Resolve hover, then press/release through the real interaction pass.
        Frame();
        ctx.MouseDown[0] = ctx.MousePressed[0] = true;
        Frame();
        ctx.MouseDown[0]     = false;
        ctx.MouseReleased[0] = true;
        Frame();
    }
    void BeginRename(ActorHandle handle)
    {
        const auto* label = FindText(Name(handle)->Value);
        ASSERT_NE(label, nullptr);
        const float x = (label->ScreenMin[0] + label->ScreenMax[0]) * .5f;
        const float y = (label->ScreenMin[1] + label->ScreenMax[1]) * .5f;
        Click(x, y);
        Click(x, y);
        Frame();
        ASSERT_EQ(panel.m_rename_id, actors.Access(handle)->GetEntityID());
        ASSERT_NE(panel.m_rename_fkey, 0u);
        ASSERT_EQ(ctx.FocusKey, panel.m_rename_fkey);
    }
    void Input(const char* text)
    {
        secure_strcpy(ctx.TextInput, sizeof(ctx.TextInput), text);
        ctx.TextInputLen = static_cast<uint32_t>(secure_strlen(text));
        Frame();
    }
};

TEST_F(HierarchyPanelTest, TypeValuesMatchHeaderInsetAndActorLabelVerticalCenter)
{
    for (float width : {306.f, 380.f, 600.f})
    {
        Frame(width);
        const auto* header = FindText("Type");
        ASSERT_NE(header, nullptr);
        const float header_text_x = header->ScreenMin[0] + ctx.Style.FramePadding[0];
        for (auto pair : {
             std::pair{     "Scene",  "World"},
             std::pair{"AlphaLight",  "Light"},
             std::pair{"BetaCamera", "Camera"}
        })
        {
            const auto* label = FindText(pair.first);
            const auto* type  = FindText(pair.second);
            ASSERT_NE(label, nullptr);
            ASSERT_NE(type, nullptr);
            EXPECT_FLOAT_EQ(type->ScreenMin[0] + type->Padding[0], header_text_x);
            EXPECT_FLOAT_EQ((type->ScreenMin[1] + type->ScreenMax[1]) * .5f, (label->ScreenMin[1] + label->ScreenMax[1]) * .5f);
            EXPECT_EQ(type->FontSize, label->FontSize);
            // One fill-sized text box, without a wrapper or measuring child.
            EXPECT_EQ(type->FirstChild, nullptr);
            EXPECT_EQ(type->Parent->FirstChild, type);
            EXPECT_EQ(type->Parent->LastChild, type);
            for (int channel = 0; channel < 4; ++channel)
                EXPECT_FLOAT_EQ(type->TextColor[channel], label->TextColor[channel]);
        }
    }
}

TEST_F(HierarchyPanelTest, NestedSiblingsRetainAscendingAndDescendingSortOrder)
{
    const auto parent     = scene.CreateActor(actors, "Parent");
    const auto child_b    = scene.CreateActor(actors, "Child_B");
    const auto child_a    = scene.CreateActor(actors, "Child_A");
    const auto grandchild = scene.CreateActor(actors, "Grandchild");
    const auto parent_id  = actors.Access(parent)->GetEntityID();
    actors.Access(child_b)->AddComponent<ParentComponent>({parent_id});
    actors.Access(child_a)->AddComponent<ParentComponent>({parent_id});
    actors.Access(grandchild)->AddComponent<ParentComponent>({actors.Access(child_a)->GetEntityID()});
    for (bool ascending : {true, false})
    {
        panel.m_sort_asc = ascending;
        Frame();
        const auto* p = FindText("Parent");
        const auto* a = FindText("Child_A");
        const auto* b = FindText("Child_B");
        const auto* g = FindText("Grandchild");
        ASSERT_NE(p, nullptr);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        ASSERT_NE(g, nullptr);
        EXPECT_LT(p->ScreenMin[1], a->ScreenMin[1]);
        EXPECT_LT(p->ScreenMin[1], b->ScreenMin[1]);
        EXPECT_LT(a->ScreenMin[1], g->ScreenMin[1]);
        if (ascending)
            EXPECT_LT(g->ScreenMin[1], b->ScreenMin[1]);
        else
            EXPECT_LT(b->ScreenMin[1], a->ScreenMin[1]);
    }
}

TEST_F(HierarchyPanelTest, MaximumActorCountTraversesAllChildrenWithBoundedReusableScratch)
{
    const auto     parent      = scene.CreateActor(actors, "Parent");
    const auto     parent_id   = actors.Access(parent)->GetEntityID();
    const uint32_t child_count = ActorManager::MAX_ACTORS - actors.Count();
    for (uint32_t i = 0; i < child_count; ++i)
    {
        char name[32] = {};
        snprintf(name, sizeof(name), "Child_%04u", i);
        const auto child = scene.CreateActor(actors, name);
        actors.Access(child)->AddComponent<ParentComponent>({parent_id});
    }
    ASSERT_EQ(actors.Count(), ActorManager::MAX_ACTORS);
    // Hide most rows so the test exercises maximum traversal breadth without
    // exceeding this fixture's intentionally small box/state budgets.
    ASSERT_GT(child_count, 0u);
    char last_name[32] = {};
    snprintf(last_name, sizeof(last_name), "Child_%04u", child_count - 1);
    secure_strcpy(panel.m_search, sizeof(panel.m_search), last_name);
    Frame();
    ASSERT_NE(FindText(last_name), nullptr);
    const auto frame_bytes      = ctx.FrameArena.m_current_offset;
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    for (int i = 0; i < 8; ++i)
    {
        panel.m_sort_asc = !panel.m_sort_asc;
        Frame();
        EXPECT_NE(FindText(last_name), nullptr);
        EXPECT_EQ(ctx.FrameArena.m_current_offset, frame_bytes);
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
    }
}

TEST_F(HierarchyPanelTest, PowerOfTwoActorCountsDoNotDoubleParentLookupScratch)
{
    panel.m_root_open = false;
    while (actors.Count() < ActorManager::MAX_ACTORS - 1)
        ASSERT_TRUE(scene.CreateActor(actors, "Actor").Valid());
    Frame();
    const auto previous_bytes = ctx.FrameArena.m_current_offset;
    ASSERT_TRUE(scene.CreateActor(actors, "Actor").Valid());
    Frame();
    // One extra actor needs only a node, child links and a DFS entry. Both
    // counts fit the same half-full parent lookup table; no extra 32 KiB slab.
    EXPECT_GE(ctx.FrameArena.m_current_offset, previous_bytes);
    EXPECT_LT(ctx.FrameArena.m_current_offset - previous_bytes, 256u);
}

TEST_F(HierarchyPanelTest, DoubleClickRenameCommitsWithEnterAndMarksSceneDirty)
{
    BeginRename(light);
    Input("RenamedLight");
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
    ctx.EnterPressed = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_STREQ(Name(light)->Value, "RenamedLight");
    EXPECT_STREQ(Name(camera)->Value, "BetaCamera");
    EXPECT_TRUE(scene.IsDirty());
    Frame();
    EXPECT_NE(FindText("RenamedLight"), nullptr);
}

TEST_F(HierarchyPanelTest, ClickingEmptyScrollableSpaceCommitsAndExitsRename)
{
    BeginRename(light);
    Input("ClickedAway");
    Click(350.f, 350.f);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "ClickedAway");
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, EscapeCancelsWithoutChangingActorOrDirtyState)
{
    BeginRename(light);
    Input("DiscardMe");
    ctx.EscapePressed = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
    EXPECT_FALSE(scene.IsDirty());
    Frame();
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
}

TEST_F(HierarchyPanelTest, ClickingAnotherActorCommitsOriginalTargetAndPreservesSelection)
{
    BeginRename(light);
    Input("NewLight");
    const auto* label = FindText("BetaCamera");
    ASSERT_NE(label, nullptr);
    Click(label->ScreenMin[0] + 2.f, (label->ScreenMin[1] + label->ScreenMax[1]) * .5f);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "NewLight");
    EXPECT_STREQ(Name(camera)->Value, "BetaCamera");
    EXPECT_EQ(scene.SelectedActorHandle, camera);
}

TEST_F(HierarchyPanelTest, ClickingInsideRenameFieldKeepsSessionActive)
{
    BeginRename(light);
    Input("StillEditing");
    const auto* state = ZUIStateGetOrInsert(&ctx.StateStore, panel.m_rename_fkey);
    Click(state->ScreenMinX + 10.f, (state->ScreenMinY + state->ScreenMaxY) * .5f);
    EXPECT_TRUE(panel.m_rename_id.IsValid());
    EXPECT_EQ(ctx.FocusKey, panel.m_rename_fkey);
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, RightClickOutsideCommitsAndExitsRename)
{
    BeginRename(light);
    Input("RightClickedAway");
    ctx.MousePos[0]     = 350.f;
    ctx.MousePos[1]     = 350.f;
    ctx.MousePressed[1] = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "RightClickedAway");
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, ClickingSearchCommitsAndPreservesNewTextFieldFocus)
{
    BeginRename(light);
    Input("SearchClickedAway");
    Click(80.f, 20.f);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "SearchClickedAway");
    EXPECT_EQ(ctx.FocusKey, ZUIHashStr("##hier_search", 13));
}

TEST_F(HierarchyPanelTest, ReopenedRenameDoesNotReuseCancelledTextOrUndoHistory)
{
    BeginRename(light);
    Input("DiscardMe");
    ctx.EscapePressed = true;
    Frame();
    Frame();
    BeginRename(light);
    EXPECT_STREQ(panel.m_rename_buf, "AlphaLight");
    EXPECT_EQ(ctx.UndoTop, 0u);
    ctx.CtrlZPressed = true;
    Frame();
    EXPECT_STREQ(panel.m_rename_buf, "AlphaLight");
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, EmptyAndUnchangedRenamesExitWithoutDirtyingScene)
{
    BeginRename(light);
    ctx.EnterPressed = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(scene.IsDirty());
    BeginRename(light);
    ctx.BackspacePressed = true;
    Frame();
    EXPECT_STREQ(panel.m_rename_buf, "");
    ctx.EnterPressed = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, FilteredOrCollapsedRenameTargetDoesNotLeaveEditingStuck)
{
    BeginRename(light);
    Input("FilteredName");
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "BetaCamera");
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(light)->Value, "FilteredName");
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "");
    Frame();
    BeginRename(camera);
    Input("CollapsedName");
    panel.m_root_open = false;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(camera)->Value, "CollapsedName");
}

TEST_F(HierarchyPanelTest, RemovedRenameTargetCannotRenameAnotherActor)
{
    BeginRename(light);
    Input("StaleName");
    actors.Destroy(light);
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(Name(camera)->Value, "BetaCamera");
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, HidingTargetBeforeFirstEditingFrameDoesNotLeavePendingRename)
{
    panel.m_rename_id      = actors.Access(light)->GetEntityID();
    panel.m_rename_started = true;
    secure_strcpy(panel.m_rename_buf, sizeof(panel.m_rename_buf), "AlphaLight");
    panel.m_root_open = false;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(panel.m_rename_started);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, OverflowingRowsProduceScrollbarInsideListViewport)
{
    AddOverflowActors();
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    const auto* viewport = Find(ZUIHashStr("##hier_scroll", 13));
    const auto* table    = Find(ZUIHashStr("##hier_tbl", 10));
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(table, nullptr);
    EXPECT_GT(table->ComputedSize[1], viewport->ComputedSize[1]);
    EXPECT_GT(ScrollState()->MaxScrollY, 0.f);
    const auto* bar = Scrollbar();
    ASSERT_NE(bar, nullptr);
    EXPECT_TRUE(bar->Flags & ZUI_Clickable);
    EXPECT_GE(bar->ScreenMin[0], viewport->ScreenMin[0]);
    EXPECT_LE(bar->ScreenMax[0], viewport->ScreenMax[0]);
    EXPECT_GE(bar->ScreenMin[1], viewport->ScreenMin[1]);
    EXPECT_LE(bar->ScreenMax[1], viewport->ScreenMax[1]);
    ASSERT_NE(bar->FirstChild, nullptr);
    EXPECT_GT(bar->FirstChild->Colors[0][3], 0.f);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, WheelAndEndScrollingReachLastActorWithoutMovingSearchOrStatus)
{
    AddOverflowActors();
    ctx.Style.ReducedMotion = true;
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    const float search_y = Find(ZUIHashStr("##hier_tb", 9))->ScreenMin[1];
    const float status_y = Find(ZUIHashStr("##hier_sb", 9))->ScreenMin[1];
    ctx.MousePos[0]      = 120.f;
    ctx.MousePos[1]      = 100.f;
    ctx.FocusKey         = 0;
    ctx.ScrollDelta      = -1.f;
    Frame(380.f, 220.f);
    EXPECT_GT(ScrollState()->ScrollYTarget, 0.f);
    Frame(380.f, 220.f);
    EXPECT_GT(ScrollState()->ScrollY, 0.f);
    ctx.EndPressed = true;
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    EXPECT_FLOAT_EQ(ScrollState()->ScrollY, ScrollState()->MaxScrollY);
    const auto* last     = FindText("Overflow_039");
    const auto* viewport = Find(ZUIHashStr("##hier_scroll", 13));
    ASSERT_NE(last, nullptr);
    ASSERT_NE(viewport, nullptr);
    EXPECT_GE(last->ScreenMin[1], viewport->ScreenMin[1]);
    EXPECT_LE(last->ScreenMax[1], viewport->ScreenMax[1]);
    EXPECT_FLOAT_EQ(Find(ZUIHashStr("##hier_tb", 9))->ScreenMin[1], search_y);
    EXPECT_FLOAT_EQ(Find(ZUIHashStr("##hier_sb", 9))->ScreenMin[1], status_y);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, DraggingScrollbarMovesListWithoutRenamingOrSelectingActors)
{
    AddOverflowActors();
    const auto selection = scene.SelectedActorHandle;
    // A taller viewport makes the thumb overlap clickable actor rows, not
    // just the non-clickable header at the top of the list.
    Frame();
    Frame();
    const auto* bar = Scrollbar();
    ASSERT_NE(bar, nullptr);
    const uint64_t bar_key = bar->Key;
    ctx.MousePos[0]        = (bar->ScreenMin[0] + bar->ScreenMax[0]) * .5f;
    ctx.MousePos[1]        = (bar->ScreenMin[1] + bar->ScreenMax[1]) * .5f;
    Frame();
    ctx.MouseDown[0] = ctx.MousePressed[0] = true;
    Frame();
    EXPECT_EQ(ctx.ActiveKey, bar_key);
    ctx.MousePos[1] += 35.f;
    Frame();
    EXPECT_GT(ScrollState()->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(ScrollState()->ScrollY, ScrollState()->ScrollYTarget);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Frame();
    EXPECT_EQ(scene.SelectedActorHandle, selection);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, SmallListsDoNotHaveScrollbars)
{
    Frame();
    Frame();
    EXPECT_FLOAT_EQ(ScrollState()->MaxScrollY, 0.f);
    EXPECT_EQ(Scrollbar(), nullptr);
}

TEST_F(HierarchyPanelTest, FilteringAndCollapsingRemoveScrollbarAndClampStaleScrollTargets)
{
    AddOverflowActors();
    ctx.Style.ReducedMotion = true;
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    auto* state = ScrollState();
    ASSERT_GT(state->MaxScrollY, 0.f);
    state->ScrollY = state->ScrollYTarget = state->MaxScrollY;
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "BetaCamera");
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    EXPECT_EQ(Scrollbar(), nullptr);
    EXPECT_FLOAT_EQ(state->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(state->ScrollYTarget, 0.f);
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "");
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    EXPECT_FLOAT_EQ(state->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(state->ScrollYTarget, 0.f);
    ASSERT_GT(state->MaxScrollY, 0.f);
    state->ScrollY = state->ScrollYTarget = state->MaxScrollY;
    panel.m_root_open                     = false;
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    EXPECT_EQ(Scrollbar(), nullptr);
    EXPECT_FLOAT_EQ(state->ScrollY, 0.f);
    EXPECT_FLOAT_EQ(state->ScrollYTarget, 0.f);
}
