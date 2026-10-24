#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/HierarchyPanel.h>
#include <ZEngine/Core/Memory/MemoryManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <gtest/gtest.h>
#include <array>
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
        static void Draw(HierarchyPanel& panel, ZUIContext& ctx, float width, float height, Editor& editor, EditorScene& scene, ZEngine::EngineContext& engine)
        {
            panel.BuildHierarchy(&ctx, width, height, &editor, &scene, &engine);
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
        memory.Initialize(ZMega(128), {});
        ecs.Initialize(&memory.MainArena);
        actors.Initialize(&memory.MainArena, ecs);
        editor               = std::make_unique<Tetragrama::Editor>();
        engine               = std::make_unique<ZEngine::EngineContext>();
        engine->ActorManager = &actors;
        engine->Scene        = &ecs;
        ZUIContextInit(&ctx, &memory.MainArena, ZMega(32), ZMega(1), 2048, 2048);
        ctx.ScreenW     = 800;
        ctx.ScreenH     = 600;
        ctx.MousePos[0] = ctx.MousePos[1] = -100.f;
        for (auto& glyph : glyphs)
        {
            glyph.AdvanceX = 5.5f;
        }
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
        editor->Session.Shutdown();
        scene.Session = nullptr;
        ZUIContextDestroy(&ctx);
        actors.Shutdown();
        ecs.Shutdown();
    }
    void Frame(float width = 380.f, float height = 500.f)
    {
        ZUIBeginFrame(&ctx, 1.f / 60.f);
        ZUIBeginColumn(&ctx, "root", ZPx(width), ZPx(height));
        Tetragrama::Panels::HierarchyPanelTestAccess::Draw(panel, ctx, width, height, *editor, scene, *engine);
        ZUIEndColumn(&ctx);
        ZUIEndFrame(&ctx);
    }
    ZUIBox* FindText(std::string_view text, ZUIBox* root = nullptr)
    {
        if (!root)
        {
            root = ctx.Root;
        }
        if ((root->Flags & ZUI_DrawText) && root->Label.Ptr && std::string_view(root->Label.Ptr, root->Label.Len) == text)
        {
            return root;
        }
        for (auto* child = root->FirstChild; child; child = child->NextSib)
        {
            if (auto* found = FindText(text, child))
            {
                return found;
            }
        }
        return nullptr;
    }
    ZUIBox* Find(uint64_t key, ZUIBox* root = nullptr)
    {
        if (!root)
        {
            root = ctx.Root;
        }
        if (root->Key == key)
        {
            return root;
        }
        for (auto* child = root->FirstChild; child; child = child->NextSib)
        {
            if (auto* found = Find(key, child))
            {
                return found;
            }
        }
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
    EntityID PureEntity(const char* label, bool transform = true)
    {
        const auto    entity = ecs.CreateEntity();
        NameComponent name   = {};
        secure_strcpy(name.Value, sizeof(name.Value), label);
        ecs.AddComponent<NameComponent>(entity, name);
        if (transform)
        {
            ecs.AddComponent<TransformComponent>(entity, {});
        }
        std::array<uint8_t, 16> identity = {};
        identity[0]                      = 0xff;
        for (uint32_t i = 0; i < 4; ++i)
        {
            identity[8 + i]  = static_cast<uint8_t>(entity.Index >> (i * 8));
            identity[12 + i] = static_cast<uint8_t>(entity.Generation >> (i * 8));
        }
        ecs.AddComponent<UUIDComponent>(entity, {uuids::uuid(identity)});
        return entity;
    }
    void AttachSession(uint32_t max_targets = 8)
    {
        ASSERT_TRUE(editor->Session.Initialize(&memory.MainArena, {.MaxTargets = max_targets, .HistoryEntries = 8, .HistoryBytes = 4096}));
        ASSERT_TRUE(editor->Session.BeginMutationPhase());
        ASSERT_TRUE(editor->Session.BindScene(&ecs));
        ASSERT_TRUE(scene.AttachSession(editor->Session, *engine));
        editor->Session.EndMutationPhase();
        Frame();
    }
    void Drain()
    {
        ASSERT_TRUE(editor->Session.BeginMutationPhase());
        ASSERT_TRUE(editor->Session.ProcessRequests());
        editor->Session.EndMutationPhase();
    }
    ZUIBox* Row(const char* label)
    {
        auto* box = FindText(label);
        while (box && !(box->Flags & ZUI_Clickable))
        {
            box = box->Parent;
        }
        return box;
    }
    void Drop(const Tetragrama::EntityTarget& source, const char* destination)
    {
        const auto* row = Row(destination);
        ASSERT_NE(row, nullptr);
        ctx.MousePos[0]         = (row->ScreenMin[0] + row->ScreenMax[0]) * .5f;
        ctx.MousePos[1]         = (row->ScreenMin[1] + row->ScreenMax[1]) * .5f;
        panel.m_dragging_target = source;
        ctx.DragDropFired       = true;
        Frame();
    }
    void Click(float x, float y, float width = 380.f, float height = 500.f)
    {
        ctx.MousePos[0] = x;
        ctx.MousePos[1] = y;
        // Resolve hover, then press/release through the real interaction pass.
        Frame(width, height);
        ctx.MouseDown[0] = ctx.MousePressed[0] = true;
        Frame(width, height);
        ctx.MouseDown[0]     = false;
        ctx.MouseReleased[0] = true;
        Frame(width, height);
    }
    void ChooseRow(const char* label, bool modifier = false, bool shift = false, float height = 500.f)
    {
        const auto* box = FindText(label);
        ASSERT_NE(box, nullptr);
        ctx.CtrlDown  = modifier;
        ctx.ShiftDown = shift;
        Click(box->ScreenMin[0] + 2.f, (box->ScreenMin[1] + box->ScreenMax[1]) * .5f, 380.f, height);
        ctx.CtrlDown = ctx.ShiftDown = false;
        Drain();
        Frame(380.f, height);
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

TEST_F(HierarchyPanelTest, PureEntitiesAreVisibleSelectableAndDoNotRequireAnActorManager)
{
    const auto entity = PureEntity("PureLight");
    ecs.AddComponent<LightComponent>(entity, {.LightType = LightComponent::Type::Point});
    AttachSession();
    engine->ActorManager = nullptr;
    Frame();
    const auto* label = FindText("PureLight");
    ASSERT_NE(label, nullptr);
    Click(label->ScreenMin[0] + 2.f, (label->ScreenMin[1] + label->ScreenMax[1]) * .5f);
    EXPECT_TRUE(editor->Session.Selection().Entities().empty());
    EXPECT_GT(editor->Session.PendingRequestCount(), 0u);
    Drain();
    EXPECT_EQ(scene.SelectedEntity(), entity);
    EXPECT_TRUE(scene.IsSelected(entity));
    EXPECT_FALSE(scene.IsDirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
}

TEST_F(HierarchyPanelTest, ActorChildrenFollowPureOrganizationalParents)
{
    const auto parent = PureEntity("PureGroup");
    ecs.RemoveComponent<TransformComponent>(parent);
    ASSERT_TRUE(ecs.SetParent(actors.Access(light)->GetEntityID(), parent));
    Frame();
    const auto* parent_label = FindText("PureGroup");
    const auto* child_label  = FindText("AlphaLight");
    ASSERT_NE(parent_label, nullptr);
    ASSERT_NE(child_label, nullptr);
    EXPECT_LT(parent_label->ScreenMin[1], child_label->ScreenMin[1]);
    EXPECT_GT(child_label->ScreenMin[0], parent_label->ScreenMin[0]);
    panel.ToggleCollapsed(parent);
    Frame();
    EXPECT_EQ(FindText("AlphaLight"), nullptr);
    EXPECT_NE(FindText("BetaCamera"), nullptr);
}

TEST_F(HierarchyPanelTest, MalformedHierarchyStaysVisibleWithoutRewritingAuthoredParents)
{
    const auto     tail   = PureEntity("A_Tail");
    const auto     a      = PureEntity("B_CycleA");
    const auto     b      = PureEntity("C_CycleB");
    const auto     orphan = PureEntity("Orphan");
    const auto     self   = PureEntity("Self");
    const EntityID stale  = {60000, 9};
    ecs.AddComponent<ParentComponent>(tail, {a});
    ecs.AddComponent<ParentComponent>(a, {b});
    ecs.AddComponent<ParentComponent>(b, {a});
    ecs.AddComponent<ParentComponent>(orphan, {stale});
    ecs.AddComponent<ParentComponent>(self, {self});
    Frame();
    for (const char* label : {"A_Tail", "B_CycleA", "C_CycleB", "Orphan", "Self"})
    {
        EXPECT_NE(FindText(label), nullptr);
    }
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(tail)->Parent, a);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(a)->Parent, b);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(b)->Parent, a);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(orphan)->Parent, stale);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(self)->Parent, self);
    const auto bytes = ctx.FrameArena.m_current_offset;
    for (uint32_t i = 0; i < 8; ++i)
    {
        panel.m_sort_asc = !panel.m_sort_asc;
        Frame();
        EXPECT_EQ(ctx.FrameArena.m_current_offset, bytes);
        EXPECT_NE(FindText("A_Tail"), nullptr);
    }
}

TEST_F(HierarchyPanelTest, RowInteractionIdentitySurvivesSortReordering)
{
    PureEntity("PureGroup");
    Frame();
    const auto* row = Row("AlphaLight");
    ASSERT_NE(row, nullptr);
    const auto key   = row->Key;
    const auto y     = row->ScreenMin[1];
    panel.m_sort_asc = false;
    Frame();
    row = Row("AlphaLight");
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(row->Key, key);
    EXPECT_NE(row->ScreenMin[1], y);
}

TEST_F(HierarchyPanelTest, PureEntityDropQueuesReparentAndRestoresPreviewBeforeChangingParent)
{
    const auto child  = PureEntity("PureChild");
    const auto parent = PureEntity("PureParent");
    AttachSession();
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(child);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 4.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    Drop(target, "PureParent");
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(child), nullptr);
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(child)->Position.x, 4.f);
    Drain();
    ASSERT_NE(ecs.GetComponent<ParentComponent>(child), nullptr);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(child)->Parent, parent);
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(child)->Position.x, 0.f);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_TRUE(editor->Session.Dirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
}

TEST_F(HierarchyPanelTest, DropRejectsRecycledEntityEvenWhenItReusesTheAuthoredUUID)
{
    const auto child = PureEntity("PureChild");
    PureEntity("PureParent");
    AttachSession();
    const auto target = editor->Session.TargetFor(child);
    ecs.DestroyEntity(child);
    const auto replacement                              = PureEntity("Replacement");
    ecs.GetComponent<UUIDComponent>(replacement)->Value = target.UUID;
    Frame();
    Drop(target, "PureParent");
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(replacement), nullptr);
}

TEST_F(HierarchyPanelTest, SceneEpochReplacementRetiresRenameAndDragState)
{
    const auto entity = PureEntity("PureGroup");
    AttachSession();
    panel.m_rename_id      = entity;
    panel.m_rename_target  = editor->Session.TargetFor(entity);
    panel.m_rename_started = true;
    panel.m_rename_fkey    = 123;
    ctx.FocusKey           = 123;
    secure_strcpy(panel.m_rename_buf, sizeof(panel.m_rename_buf), "OldSceneName");
    panel.m_dragging_target = panel.m_rename_target;
    panel.ToggleCollapsed(entity);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.BindScene(&ecs));
    editor->Session.EndMutationPhase();
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_FALSE(panel.m_dragging_target.CachedEntity.IsValid());
    EXPECT_FALSE(panel.IsCollapsed(entity));
    EXPECT_STREQ(ecs.GetComponent<NameComponent>(entity)->Value, "PureGroup");
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
}

TEST_F(HierarchyPanelTest, PureEntityRenameQueuesAuthoredChangesOutsideTheUIBuild)
{
    const auto entity = PureEntity("PureGroup");
    AttachSession();
    const auto* label = FindText("PureGroup");
    ASSERT_NE(label, nullptr);
    const auto x = label->ScreenMin[0] + 2.f;
    const auto y = (label->ScreenMin[1] + label->ScreenMax[1]) * .5f;
    Click(x, y);
    Click(x, y);
    Frame();
    ASSERT_EQ(panel.m_rename_id, entity);
    Drain();
    Input("RenamedPure");
    ctx.EnterPressed = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_STREQ(ecs.GetComponent<NameComponent>(entity)->Value, "PureGroup");
    Drain();
    EXPECT_STREQ(ecs.GetComponent<NameComponent>(entity)->Value, "RenamedPure");
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, CollectionButtonQueuesCreationAndCancelsTheActivePreviewBeforeApplying)
{
    AttachSession();
    const auto entity = actors.Access(light)->GetEntityID();
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 4.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    const auto* button = Find(ZUIHashStr("##hier_new_collection", 21));
    ASSERT_NE(button, nullptr);
    const auto count = ecs.AliveCount();
    Click((button->ScreenMin[0] + button->ScreenMax[0]) * .5f, (button->ScreenMin[1] + button->ScreenMax[1]) * .5f);
    EXPECT_EQ(ecs.AliveCount(), count);
    EXPECT_TRUE(editor->Session.Active());
    Drain();
    EXPECT_EQ(ecs.AliveCount(), count + 1);
    EXPECT_EQ(actors.Count(), count + 1);
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(entity)->Position.x, 0.f);
    const auto selected = scene.SelectedEntity();
    ASSERT_TRUE(ecs.IsAlive(selected));
    EXPECT_STREQ(ecs.GetComponent<NameComponent>(selected)->Value, "Collection");
    EXPECT_TRUE(scene.IsDirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
}

TEST_F(HierarchyPanelTest, PureEntityDeleteMenuQueuesTheOriginalTargetUntilTheMutationPhase)
{
    const auto entity = PureEntity("PureGroup");
    AttachSession();
    char key[48] = {};
    snprintf(key, sizeof(key), "##ctx_%u_%u", entity.Index, entity.Generation);
    ctx.MousePos[0] = ctx.MousePos[1] = 100.f;
    ZUIOpenPopup(&ctx, key);
    Frame();
    Frame();
    const auto* item = FindText("Delete");
    ASSERT_NE(item, nullptr);
    Click((item->ScreenMin[0] + item->ScreenMax[0]) * .5f, (item->ScreenMin[1] + item->ScreenMax[1]) * .5f);
    EXPECT_TRUE(ecs.IsAlive(entity));
    EXPECT_GT(editor->Session.PendingRequestCount(), 0u);
    Drain();
    EXPECT_FALSE(ecs.IsAlive(entity));
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, DeletingPureParentRestoresAndDetachesAllKindsOfChildren)
{
    const auto parent      = PureEntity("PureParent");
    const auto child       = PureEntity("PureChild");
    const auto actor_child = actors.Access(light)->GetEntityID();
    ASSERT_TRUE(ecs.SetParent(child, parent));
    ASSERT_TRUE(ecs.SetParent(actor_child, parent));
    AttachSession();
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.Select(parent));
    const auto target = editor->Session.TargetFor(child);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 4.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    EXPECT_FALSE(scene.DeleteEntity(*engine, parent)); // UI cannot bypass the session.
    ASSERT_TRUE(editor->Session.RequestDelete(parent));
    EXPECT_TRUE(ecs.IsAlive(parent));
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(child)->Position.x, 4.f);
    const auto actor_count = actors.Count();
    Drain();
    EXPECT_FALSE(ecs.IsAlive(parent));
    EXPECT_TRUE(ecs.IsAlive(child));
    EXPECT_EQ(actors.Count(), actor_count);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(child), nullptr);
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(actor_child), nullptr);
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(child)->Position.x, 0.f);
    EXPECT_TRUE(editor->Session.Selection().Entities().empty());
    EXPECT_FALSE(editor->Session.Active());
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, DeletingActorAlsoDetachesPureChildrenAndRetiresTheActorHandle)
{
    const auto parent = actors.Access(light)->GetEntityID();
    const auto child  = PureEntity("PureChild");
    ASSERT_TRUE(ecs.SetParent(child, parent));
    AttachSession();
    ASSERT_TRUE(editor->Session.RequestDelete(parent));
    Drain();
    EXPECT_FALSE(ecs.IsAlive(parent));
    EXPECT_FALSE(actors.IsLive(light));
    EXPECT_TRUE(ecs.IsAlive(child));
    EXPECT_EQ(ecs.GetComponent<ParentComponent>(child), nullptr);
}

TEST_F(HierarchyPanelTest, QueuedDeleteCannotDestroyAReplacementWithTheSameUUID)
{
    const auto original = PureEntity("Original");
    AttachSession();
    const auto identity = ecs.GetComponent<UUIDComponent>(original)->Value;
    ASSERT_TRUE(editor->Session.RequestDelete(original));
    ecs.DestroyEntity(original);
    const auto replacement                              = PureEntity("Replacement");
    ecs.GetComponent<UUIDComponent>(replacement)->Value = identity;
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    EXPECT_FALSE(editor->Session.ProcessRequests());
    editor->Session.EndMutationPhase();
    EXPECT_TRUE(ecs.IsAlive(replacement));
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, SceneRebindRetiresOwnerMutationHooksUntilExplicitlyReattached)
{
    const auto entity = PureEntity("PureGroup");
    AttachSession();
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.BindScene(&ecs));
    ASSERT_TRUE(editor->Session.RequestDelete(entity));
    EXPECT_FALSE(editor->Session.ProcessRequests());
    EXPECT_TRUE(ecs.IsAlive(entity));
    EXPECT_FALSE(editor->Session.Dirty());
    ASSERT_TRUE(scene.AttachSession(editor->Session, *engine));
    ASSERT_TRUE(editor->Session.RequestDelete(entity));
    ASSERT_TRUE(editor->Session.ProcessRequests());
    EXPECT_FALSE(ecs.IsAlive(entity));
    editor->Session.EndMutationPhase();
}

TEST_F(HierarchyPanelTest, MultipleQueuedDeletesAreAppliedWithoutDroppingIndependentCommands)
{
    const auto first  = PureEntity("First");
    const auto second = PureEntity("Second");
    AttachSession();
    ASSERT_TRUE(editor->Session.RequestDelete(first));
    ASSERT_TRUE(editor->Session.RequestDelete(second));
    Drain();
    EXPECT_FALSE(ecs.IsAlive(first));
    EXPECT_FALSE(ecs.IsAlive(second));
    EXPECT_EQ(actors.Count(), 2u);
}

TEST_F(HierarchyPanelTest, FullActorCapacityRejectsCreationWithoutCancellingTheActivePreview)
{
    panel.m_root_open = false;
    while (actors.Count() < ActorManager::MAX_ACTORS)
    {
        ASSERT_TRUE(scene.CreateActor(actors, "Collection").Valid());
    }
    scene.MarkDirty(false);
    AttachSession();
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto entity = actors.Access(light)->GetEntityID();
    const auto target = editor->Session.TargetFor(entity);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 4.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    ASSERT_TRUE(editor->Session.RequestCollection("TooMany"));
    EXPECT_FALSE(editor->Session.ProcessRequests());
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(entity)->Position.x, 4.f);
    EXPECT_EQ(actors.Count(), ActorManager::MAX_ACTORS);
    editor->Session.Cancel();
    EXPECT_FALSE(scene.IsDirty());
    editor->Session.EndMutationPhase();
}

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
            {
                EXPECT_FLOAT_EQ(type->TextColor[channel], label->TextColor[channel]);
            }
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
        {
            EXPECT_LT(g->ScreenMin[1], b->ScreenMin[1]);
        }
        else
        {
            EXPECT_LT(b->ScreenMin[1], a->ScreenMin[1]);
        }
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
    {
        ASSERT_TRUE(scene.CreateActor(actors, "Actor").Valid());
    }
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

TEST_F(HierarchyPanelTest, FullEntityCapacityEmitsOnlyViewportRowsAndRetainsTheCompleteScrollExtent)
{
    EntityID last_entity = {};
    for (uint32_t i = 0; ecs.AliveCount() < EntityRegistry::MAX_ENTITIES; ++i)
    {
        char name[32] = {};
        snprintf(name, sizeof(name), "Pure_%05u", i);
        last_entity = PureEntity(name, false);
    }
    AttachSession();
    ctx.Style.ReducedMotion = true;
    Frame();
    Frame();
    const auto* viewport = Find(ZUIHashStr("##hier_scroll", 13));
    const auto* table    = Find(ZUIHashStr("##hier_tbl", 10));
    ASSERT_NE(viewport, nullptr);
    ASSERT_NE(table, nullptr);
    const auto expected = 24.f + (float(EntityRegistry::MAX_ENTITIES) + 2.f) * ZUIDataTableRowHeight();
    EXPECT_FLOAT_EQ(table->ComputedSize[1], expected);
    EXPECT_FLOAT_EQ(ScrollState()->MaxScrollY, expected - viewport->ComputedSize[1]);
    EXPECT_NE(FindText("Pure_00000"), nullptr);
    EXPECT_EQ(FindText("Pure_40000"), nullptr);
    uint32_t   boxes = 0;
    const auto count = [&](const auto& self, const ZUIBox* box) -> void {
        ++boxes;
        for (auto* child = box->FirstChild; child; child = child->NextSib)
        {
            self(self, child);
        }
    };
    count(count, ctx.Root);
    EXPECT_LT(boxes, 700u);
    EXPECT_LT(ctx.StateStore.Count, 200u);
    EXPECT_LT(ctx.FrameArena.m_current_offset, ZMega(16));
    ScrollState()->ScrollY = ScrollState()->ScrollYTarget = ScrollState()->MaxScrollY;
    Frame();
    viewport         = Find(ZUIHashStr("##hier_scroll", 13));
    const auto* last = FindText("Pure_65533");
    ASSERT_NE(last, nullptr);
    EXPECT_GE(last->ScreenMin[1], viewport->ScreenMin[1]);
    EXPECT_LE(last->ScreenMax[1], viewport->ScreenMax[1]);
    EXPECT_EQ(FindText("Pure_00000"), nullptr);
    EXPECT_LT(ctx.StateStore.Count, 200u);
    Click(last->ScreenMin[0] + 2.f, (last->ScreenMin[1] + last->ScreenMax[1]) * .5f);
    Drain();
    EXPECT_EQ(scene.SelectedEntity(), last_entity);
    EXPECT_TRUE(scene.IsSelected(last_entity));
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, ScrollingThroughThousandsOfRowsReclaimsOnlyOwnedTransientState)
{
    for (uint32_t i = 0; i < 4096; ++i)
    {
        char name[32] = {};
        snprintf(name, sizeof(name), "Pure_%05u", i);
        (void) PureEntity(name, false);
    }
    const uint64_t unrelated                                  = ZUIHashStr("other-panel-state", 17);
    ZUIStateGetOrInsert(&ctx.StateStore, unrelated)->UserData = 123.f;
    ctx.Style.ReducedMotion                                   = true;
    Frame();
    const auto persistent_bytes = ctx.PersistentArena.m_current_offset;
    for (uint32_t row = 0; row < 4096; row += 24)
    {
        auto* state    = ScrollState();
        state->ScrollY = state->ScrollYTarget = float(row) * ZUIDataTableRowHeight();
        Frame();
        EXPECT_LT(ctx.StateStore.Count, 200u) << row;
        EXPECT_EQ(ctx.PersistentArena.m_current_offset, persistent_bytes);
        EXPECT_FLOAT_EQ(ZUIStateGetOrInsert(&ctx.StateStore, unrelated)->UserData, 123.f);
    }
    auto* state    = ScrollState();
    state->ScrollY = state->ScrollYTarget = 0.f;
    Frame();
    EXPECT_NE(FindText("Pure_00000"), nullptr);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, KeyboardNavigationCrossesVirtualWindowsWithoutWrappingOrSelecting)
{
    AddOverflowActors();
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    const auto selected = scene.SelectedActorHandle;
    ASSERT_NE(Row("AlphaLight"), nullptr);
    ctx.FocusKey = Row("AlphaLight")->Key;
    for (uint32_t step = 0; step < 31; ++step)
    {
        ctx.ArrowDownPressed = true;
        Frame(380.f, 220.f);
    }
    ASSERT_NE(Row("Overflow_029"), nullptr);
    EXPECT_EQ(ctx.FocusKey, Row("Overflow_029")->Key);
    EXPECT_GT(ScrollState()->ScrollY, 0.f);
    EXPECT_EQ(scene.SelectedActorHandle, selected);
    ctx.EndPressed = true;
    Frame(380.f, 220.f);
    ASSERT_NE(Row("Overflow_039"), nullptr);
    EXPECT_EQ(ctx.FocusKey, Row("Overflow_039")->Key);
    ctx.HomePressed = true;
    Frame(380.f, 220.f);
    ASSERT_NE(Row("World"), nullptr);
    EXPECT_EQ(ctx.FocusKey, Row("World")->Key);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, ScrollingARenameOutOfTheWindowQueuesItsOriginalTargetAndExits)
{
    AddOverflowActors();
    AttachSession();
    BeginRename(light);
    Input("RenamedBeforeScroll");
    auto* state    = ScrollState();
    state->ScrollY = state->ScrollYTarget = state->MaxScrollY;
    ctx.Style.ReducedMotion               = true;
    Frame();
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_EQ(ctx.FocusKey, 0u);
    EXPECT_STREQ(Name(light)->Value, "AlphaLight");
    Drain();
    EXPECT_STREQ(Name(light)->Value, "RenamedBeforeScroll");
    EXPECT_TRUE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, ReleaseAfterScrollingCannotRetargetAPressToANewRow)
{
    AddOverflowActors();
    Frame(380.f, 220.f);
    const auto  selection = scene.SelectedActorHandle;
    const auto* original  = Row("AlphaLight");
    ASSERT_NE(original, nullptr);
    ctx.ActiveKey = ctx.HotKey = original->Key;
    ctx.MouseDown[0]           = true;
    ctx.Style.ReducedMotion    = true;
    ScrollState()->ScrollY = ScrollState()->ScrollYTarget = ScrollState()->MaxScrollY;
    Frame(380.f, 220.f);
    EXPECT_EQ(ctx.ActiveKey, 0u);
    ctx.MouseDown[0]     = false;
    ctx.MouseReleased[0] = true;
    Frame(380.f, 220.f);
    EXPECT_EQ(scene.SelectedActorHandle, selection);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, FilteringADeepExpandedTreePreservesOrderAndCompleteVisibleHeight)
{
    EntityID parent = {};
    for (uint32_t i = 0; i < 4096; ++i)
    {
        char name[32] = {};
        snprintf(name, sizeof(name), "Deep_%05u", i);
        const auto entity = PureEntity(name, false);
        if (parent.IsValid())
        {
            ecs.AddComponent<ParentComponent>(entity, {parent});
        }
        parent = entity;
    }
    Frame();
    Frame();
    EXPECT_EQ(FindText("Deep_04095"), nullptr);
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "Deep_04095");
    Frame();
    const auto* table = Find(ZUIHashStr("##hier_tbl", 10));
    ASSERT_NE(table, nullptr);
    EXPECT_FLOAT_EQ(table->ComputedSize[1], 24.f + 3.f * ZUIDataTableRowHeight());
    EXPECT_NE(FindText("Deep_04095"), nullptr);
    EXPECT_FLOAT_EQ(ScrollState()->MaxScrollY, 0.f);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, DragSourceIdentitySurvivesLeavingTheVirtualWindow)
{
    AddOverflowActors();
    AttachSession();
    const auto id           = actors.Access(light)->GetEntityID();
    const auto source       = editor->Session.TargetFor(id);
    panel.m_dragging_target = source;
    const auto* row         = Row("AlphaLight");
    ASSERT_NE(row, nullptr);
    ctx.DragSourceKey = ctx.ActiveKey = row->Key;
    ctx.MouseDown[0]                  = true;
    ctx.Style.ReducedMotion           = true;
    ScrollState()->ScrollY = ScrollState()->ScrollYTarget = ScrollState()->MaxScrollY;
    Frame();
    EXPECT_EQ(FindText("AlphaLight"), nullptr);
    EXPECT_NE(ctx.DragSourceKey, 0u);
    EXPECT_EQ(panel.m_dragging_target.UUID, source.UUID);
    Drop(source, "Overflow_039");
    Drain();
    const auto* parent = ecs.GetComponent<ParentComponent>(id);
    ASSERT_NE(parent, nullptr);
    const auto* parent_name = ecs.GetComponent<NameComponent>(parent->Parent);
    ASSERT_NE(parent_name, nullptr);
    EXPECT_STREQ(parent_name->Value, "Overflow_039");
}

TEST_F(HierarchyPanelTest, LargeScrollbarDragBuildsTheDestinationWindowInTheSameFrame)
{
    AddOverflowActors();
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    const auto* bar = Scrollbar();
    ASSERT_NE(bar, nullptr);
    ctx.ActiveKey    = bar->Key;
    ctx.MouseDown[0] = true;
    ctx.MousePos[1]  = ctx.PrevMousePos[1] + 1000.f;
    Frame(380.f, 220.f);
    EXPECT_FLOAT_EQ(ScrollState()->ScrollY, ScrollState()->MaxScrollY);
    const auto* last     = FindText("Overflow_039");
    const auto* viewport = Find(ZUIHashStr("##hier_scroll", 13));
    ASSERT_NE(last, nullptr);
    ASSERT_NE(viewport, nullptr);
    EXPECT_GE(last->ScreenMin[1], viewport->ScreenMin[1]);
    EXPECT_LE(last->ScreenMax[1], viewport->ScreenMax[1]);
    EXPECT_EQ(FindText("AlphaLight"), nullptr);
    EXPECT_FALSE(scene.IsDirty());
}

TEST_F(HierarchyPanelTest, ModifierClicksToggleOrderedMembershipWithoutTriggeringRename)
{
    AttachSession();
    const auto first = actors.Access(light)->GetEntityID(), second = actors.Access(camera)->GetEntityID();
    ChooseRow("AlphaLight");
    ChooseRow("BetaCamera", true);
    ASSERT_EQ(editor->Session.Selection().Entities().size(), 2u);
    EXPECT_EQ(editor->Session.Selection().Entities()[0].CachedEntity, first);
    EXPECT_EQ(editor->Session.Selection().Entities()[1].CachedEntity, second);
    EXPECT_EQ(scene.SelectedEntity(), second);
    ChooseRow("BetaCamera", true);
    ASSERT_EQ(editor->Session.Selection().Entities().size(), 1u);
    EXPECT_EQ(scene.SelectedEntity(), first);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    EXPECT_FALSE(scene.IsDirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
}

TEST_F(HierarchyPanelTest, ReverseShiftRangeUsesDisplayOrderAndShrinksFromTheOriginalAnchor)
{
    const auto a = PureEntity("Range_A"), b = PureEntity("Range_B"), c = PureEntity("Range_C");
    AttachSession();
    ChooseRow("Range_C");
    ChooseRow("Range_A", false, true);
    const auto& selection = editor->Session.Selection();
    ASSERT_EQ(selection.Entities().size(), 3u);
    EXPECT_EQ(selection.Entities()[0].CachedEntity, a);
    EXPECT_EQ(selection.Entities()[1].CachedEntity, b);
    EXPECT_EQ(selection.Entities()[2].CachedEntity, c);
    EXPECT_EQ(scene.SelectedEntity(), a);
    EXPECT_EQ(panel.m_selection_anchor.CachedEntity, c);
    ChooseRow("Range_B", false, true);
    ASSERT_EQ(selection.Entities().size(), 2u);
    EXPECT_EQ(selection.Entities()[0].CachedEntity, b);
    EXPECT_EQ(selection.Entities()[1].CachedEntity, c);
    EXPECT_EQ(scene.SelectedEntity(), b);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
}

TEST_F(HierarchyPanelTest, ModifierShiftAddsTheRangeWithoutDuplicatesOrReorderingExistingTargets)
{
    const auto a = PureEntity("Range_A"), b = PureEntity("Range_B"), c = PureEntity("Range_C");
    AttachSession();
    ChooseRow("AlphaLight");
    ChooseRow("Range_A", true);
    ChooseRow("Range_C", true, true);
    const auto selected = editor->Session.Selection().Entities();
    ASSERT_EQ(selected.size(), 4u);
    EXPECT_EQ(selected[0].CachedEntity, actors.Access(light)->GetEntityID());
    EXPECT_EQ(selected[1].CachedEntity, a);
    EXPECT_EQ(selected[2].CachedEntity, b);
    EXPECT_EQ(selected[3].CachedEntity, c);
    EXPECT_EQ(scene.SelectedEntity(), c);
}

TEST_F(HierarchyPanelTest, ShiftRangesUseTheCurrentSiblingSortOrder)
{
    const auto a = PureEntity("Range_A"), b = PureEntity("Range_B"), c = PureEntity("Range_C");
    AttachSession();
    ChooseRow("Range_A");
    panel.m_sort_asc = false;
    Frame();
    ChooseRow("Range_C", false, true);
    const auto selected = editor->Session.Selection().Entities();
    ASSERT_EQ(selected.size(), 3u);
    EXPECT_EQ(selected[0].CachedEntity, c);
    EXPECT_EQ(selected[1].CachedEntity, b);
    EXPECT_EQ(selected[2].CachedEntity, a);
    EXPECT_EQ(scene.SelectedEntity(), c);
}

TEST_F(HierarchyPanelTest, FilteredRangesExcludeHiddenRowsAndFallBackToTheVisibleActiveTarget)
{
    const auto anchor = PureEntity("HiddenAnchor"), a = PureEntity("Range_A"), b = PureEntity("Range_B"), c = PureEntity("Range_C");
    AttachSession();
    ChooseRow("HiddenAnchor");
    const ZEngine::Core::Containers::FixedArray<Tetragrama::EntityTarget, 2> selected = {
        {editor->Session.TargetFor(anchor), editor->Session.TargetFor(a)}
    };
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    ASSERT_TRUE(editor->Session.SetSelection({selected.data(), selected.size()}, selected[1].UUID));
    editor->Session.EndMutationPhase();
    secure_strcpy(panel.m_search, sizeof(panel.m_search), "Range_");
    Frame();
    ChooseRow("Range_C", false, true);
    const auto range = editor->Session.Selection().Entities();
    ASSERT_EQ(range.size(), 3u);
    EXPECT_EQ(range[0].CachedEntity, a);
    EXPECT_EQ(range[1].CachedEntity, b);
    EXPECT_EQ(range[2].CachedEntity, c);
    EXPECT_FALSE(scene.IsSelected(anchor));
}

TEST_F(HierarchyPanelTest, CollapsedDescendantsAreNotIncludedInTheDisplayedRange)
{
    const auto parent = PureEntity("Range_AParent"), child = PureEntity("Range_BChild"), end = PureEntity("Range_C");
    ASSERT_TRUE(ecs.SetParent(child, parent));
    AttachSession();
    ChooseRow("Range_AParent");
    panel.ToggleCollapsed(parent);
    Frame();
    ASSERT_EQ(FindText("Range_BChild"), nullptr);
    ChooseRow("Range_C", false, true);
    const auto selected = editor->Session.Selection().Entities();
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0].CachedEntity, parent);
    EXPECT_EQ(selected[1].CachedEntity, end);
    EXPECT_FALSE(scene.IsSelected(child));
}

TEST_F(HierarchyPanelTest, ARangeAcrossVirtualizedRowsIsOneAtomicRequestAndUsesTheFullDisplayedOrder)
{
    constexpr uint32_t                                     count = 512;
    ZEngine::Core::Containers::FixedArray<EntityID, count> ids   = {};
    for (uint32_t i = 0; i < count; ++i)
    {
        char label[32] = {};
        snprintf(label, sizeof(label), "Range_%04u", i);
        ids[i] = PureEntity(label);
    }
    AttachSession(1024);
    ctx.Style.ReducedMotion = true;
    Frame(380.f, 220.f);
    Frame(380.f, 220.f);
    ChooseRow("Range_0000", false, false, 220.f);
    ctx.EndPressed = true;
    Frame(380.f, 220.f);
    ASSERT_EQ(FindText("Range_0000"), nullptr);
    const auto* label = FindText("Range_0511");
    ASSERT_NE(label, nullptr);
    const auto offset = memory.MainArena.m_current_offset;
    ctx.ShiftDown     = true;
    Click(label->ScreenMin[0] + 2.f, (label->ScreenMin[1] + label->ScreenMax[1]) * .5f, 380.f, 220.f);
    ctx.ShiftDown = false;
    EXPECT_EQ(editor->Session.PendingRequestCount(), 1u);
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 1u);
    Drain();
    ASSERT_EQ(editor->Session.Selection().Entities().size(), count);
    for (uint32_t i = 0; i < count; ++i)
    {
        EXPECT_EQ(editor->Session.Selection().Entities()[i].CachedEntity, ids[i]);
    }
    EXPECT_EQ(scene.SelectedEntity(), ids[count - 1]);
    EXPECT_EQ(memory.MainArena.m_current_offset, offset);
    EXPECT_LT(ctx.StateStore.Count, 200u);
    EXPECT_FALSE(scene.IsDirty());
    EXPECT_EQ(editor->Session.History().UndoCount(), 0u);
}

TEST_F(HierarchyPanelTest, OverCapacityRangeDoesNotQueuePartialSelectionOrCancelTheLivePreview)
{
    EntityID first = {};
    for (uint32_t i = 0; i < 10; ++i)
    {
        char label[32] = {};
        snprintf(label, sizeof(label), "Range_%02u", i);
        const auto id = PureEntity(label);
        if (!i)
        {
            first = id;
        }
    }
    AttachSession();
    ChooseRow("Range_00");
    ASSERT_TRUE(editor->Session.BeginMutationPhase());
    const auto target = editor->Session.TargetFor(first);
    ASSERT_TRUE(editor->Session.BeginTransform({&target, 1}));
    LocalTransformValue value = {};
    value.Position.x          = 7.f;
    ASSERT_TRUE(editor->Session.PreviewTransforms({&value, 1}));
    editor->Session.EndMutationPhase();
    ChooseRow("Range_09", false, true);
    EXPECT_TRUE(editor->Session.Active());
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    ASSERT_EQ(editor->Session.Selection().Entities().size(), 1u);
    EXPECT_EQ(scene.SelectedEntity(), first);
    EXPECT_FLOAT_EQ(ecs.GetComponent<TransformComponent>(first)->Position.x, 7.f);
}

TEST_F(HierarchyPanelTest, KeyboardShiftNavigationExtendsAndShrinksRangesWithoutSelectingTheWorldRow)
{
    const auto a = PureEntity("Range_A"), b = PureEntity("Range_B"), c = PureEntity("Range_C");
    AttachSession();
    ChooseRow("Range_A");
    ctx.ShiftDown = ctx.ArrowDownPressed = true;
    Frame();
    Drain();
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 2u);
    EXPECT_TRUE(scene.IsSelected(a));
    EXPECT_EQ(scene.SelectedEntity(), b);
    ctx.EndPressed = true;
    Frame();
    Drain();
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 3u);
    EXPECT_EQ(scene.SelectedEntity(), c);
    ctx.HomePressed = true;
    Frame();
    Drain();
    EXPECT_EQ(scene.SelectedEntity(), actors.Access(light)->GetEntityID());
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 3u);
    EXPECT_EQ(ctx.FocusKey, Row("AlphaLight")->Key);
    EXPECT_FALSE(scene.IsSelected(b));
    EXPECT_FALSE(scene.IsSelected(c));
    ctx.ShiftDown = false;
}

TEST_F(HierarchyPanelTest, KeyboardModifierActivationTogglesMembershipWithoutStartingRename)
{
    AttachSession();
    ChooseRow("AlphaLight");
    ctx.FocusKey      = Row("BetaCamera")->Key;
    ctx.KeyboardFocus = true;
    ctx.CtrlDown = ctx.EnterPressed = true;
    Frame();
    Drain();
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 2u);
    EXPECT_EQ(scene.SelectedEntity(), actors.Access(camera)->GetEntityID());
    ctx.EnterPressed = true;
    Frame();
    Drain();
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 1u);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
    ctx.CtrlDown = false;
}

TEST_F(HierarchyPanelTest, MouseSelectionUsesPressTimeModifiersEvenWhenReleasedBeforeMouseUp)
{
    PureEntity("Range_A");
    const auto end = PureEntity("Range_B");
    AttachSession();
    ChooseRow("AlphaLight");
    const auto physical_click = [&](const char* text, bool modifier, bool shift) {
        const auto* label = FindText(text);
        ASSERT_NE(label, nullptr);
        ZUIFeedMousePos(&ctx, label->ScreenMin[0] + 2.f, (label->ScreenMin[1] + label->ScreenMax[1]) * .5f);
        Frame();
        ctx.CtrlDown  = modifier;
        ctx.ShiftDown = shift;
        ZUIFeedMouseButton(&ctx, 0, true);
        Frame();
        ctx.CtrlDown = ctx.ShiftDown = false;
        ZUIFeedMouseButton(&ctx, 0, false);
        Frame();
        Drain();
        Frame();
    };
    physical_click("BetaCamera", true, false);
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 2u);
    physical_click("Range_B", false, true);
    EXPECT_EQ(editor->Session.Selection().Entities().size(), 3u);
    EXPECT_EQ(scene.SelectedEntity(), end);
    EXPECT_FALSE(scene.IsSelected(actors.Access(light)->GetEntityID()));
    EXPECT_FALSE(panel.m_rename_id.IsValid());
}

TEST_F(HierarchyPanelTest, ClearingSelectionRetiresTheOldAnchorAndModifiedClicksCannotRename)
{
    const auto a = PureEntity("Range_A"), b = PureEntity("Range_B");
    AttachSession();
    ChooseRow("Range_A");
    ASSERT_TRUE(editor->Session.Request(Tetragrama::EditorCommand::ClearSelection));
    Drain();
    ChooseRow("Range_B", false, true);
    ASSERT_EQ(editor->Session.Selection().Entities().size(), 1u);
    EXPECT_EQ(scene.SelectedEntity(), b);
    EXPECT_FALSE(scene.IsSelected(a));
    ChooseRow("Range_B", true);
    ChooseRow("Range_B", true);
    EXPECT_FALSE(panel.m_rename_id.IsValid());
}

TEST_F(HierarchyPanelTest, ModalKeyboardInputCannotQueueOutlinerSelection)
{
    AttachSession();
    ChooseRow("AlphaLight");
    ctx.FocusKey       = Row("BetaCamera")->Key;
    ctx.KeyboardFocus  = true;
    ctx.ActiveModalKey = 12345;
    ctx.ShiftDown = ctx.ArrowDownPressed = ctx.EnterPressed = true;
    Frame();
    EXPECT_EQ(editor->Session.PendingRequestCount(), 0u);
    EXPECT_EQ(scene.SelectedEntity(), actors.Access(light)->GetEntityID());
    ctx.ActiveModalKey = 0;
    ctx.ShiftDown      = false;
}

TEST_F(HierarchyPanelTest, EverySelectedRowReusesTheExistingFigmaSelectionFill)
{
    AttachSession();
    ChooseRow("AlphaLight");
    ChooseRow("BetaCamera", true);
    for (const char* name : {"AlphaLight", "BetaCamera"})
    {
        const auto* row = Row(name);
        ASSERT_NE(row, nullptr);
        for (uint32_t corner = 0; corner < 4; ++corner)
        {
            for (uint32_t channel = 0; channel < 4; ++channel)
            {
                EXPECT_FLOAT_EQ(row->Colors[corner][channel], ctx.Theme.RowSelectedBg[channel]);
            }
        }
    }
    EXPECT_NE(FindText("2 actors (2 selected)"), nullptr);
}
