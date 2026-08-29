#pragma once
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Reflection/BuiltInComponentReflection.h>
#include <ZEngine/ECS/Reflection/ComponentMeta.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Logging/Logger.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ── Shared helpers ────────────────────────────────────────────────────────

    static void EmptyPanelBg(ZUIContext* ctx, const char* key, const float col[4], const char* msg)
    {
        ZUIBox* bg = ZUIBeginColumn(ctx, key, ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, col);
        bg->EdgeSoftness = 0.f;
        if (msg && msg[0])
        {
            {
                char fk[48];
                snprintf(fk, sizeof(fk), "##ept_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
            {
                char lk[48];
                snprintf(lk, sizeof(lk), "##epl_%s", key);
                uint32_t mlen     = (uint32_t) strlen(msg);
                ZUIBox*  lbl      = ZUIPushBox(ctx, lk, (uint32_t) strlen(lk), ZUI_DrawText);
                lbl->Size[0]      = ZFill();
                lbl->Size[1]      = ZText();
                lbl->TextAlign    = ZUITextAlign::Center;
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, msg, mlen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }
            {
                char fk[48];
                snprintf(fk, sizeof(fk), "##epb_%s", key);
                ZUIBox* f  = ZUIPushBox(ctx, fk, (uint32_t) strlen(fk), ZUI_None);
                f->Size[0] = ZFill();
                f->Size[1] = ZFill();
                ZUIPopBox(ctx);
            }
        }
        ZUIEndColumn(ctx);
    }

    // ── Hierarchy panel ───────────────────────────────────────────────────────
    //
    // Real ECS actor tree with DFS traversal, inline rename, context menu,
    // drag-and-drop reparenting, search filter, and two-column layout.
    // VS Code chevrons (∨/›) via ZUI_DrawTriArrow + UserData 2.f/3.f.
    //
    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel()
        {
            Title = "Hierarchy";
        }

        // Set by ZUIPanelManagerComponent::Initialize before first BuildContent call
        Tetragrama::Layers::ZUILayer* m_layer                    = nullptr;

        // Search bar buffer
        char                          m_search[128]              = {};

        // Inline rename state
        ZEngine::ECS::EntityID        m_rename_id                = {};
        char                          m_rename_buf[128]          = {};
        bool                          m_rename_started           = false;
        uint64_t                      m_rename_fkey              = 0;

        // Collapsed-entity set (linear scan, sufficient for small-medium scenes)
        static constexpr int          kMaxCollapsed              = 256;
        ZEngine::ECS::EntityID        m_collapsed[kMaxCollapsed] = {};
        int                           m_ncollapsed               = 0;
        bool                          m_root_open                = true;

        // Drag-reparent: panel owns drag state because ZUIInteractionPass clears
        // DragPayloadLen=0 on mouse release (before BuildContent sees DragDropFired=true)
        ZEngine::ECS::ActorHandle     m_dragging_actor           = {};
        char                          m_drag_label[128]          = {};

        // Panel-level double-click tracking (ZUISignalFromBox is called twice per row,
        // so we track in the panel to avoid double-firing)
        uint32_t                      m_last_click_idx           = UINT32_MAX;
        uint32_t                      m_last_click_gen           = 0;
        float                         m_last_click_time          = -1.f;

        bool                          IsCollapsed(ZEngine::ECS::EntityID id) const
        {
            for (int i = 0; i < m_ncollapsed; ++i)
                if (m_collapsed[i] == id)
                    return true;
            return false;
        }

        void ToggleCollapsed(ZEngine::ECS::EntityID id)
        {
            for (int i = 0; i < m_ncollapsed; ++i)
            {
                if (m_collapsed[i] == id)
                {
                    m_collapsed[i] = m_collapsed[--m_ncollapsed];
                    return;
                }
            }
            if (m_ncollapsed < kMaxCollapsed)
                m_collapsed[m_ncollapsed++] = id;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine;
            using namespace ZEngine::ECS;
            using namespace ZEngine::ECS::Components;
            using namespace ZEngine::Helpers;
            using namespace ZEngine::UI;

            // Guard — need app, scene, and actor manager
            if (!m_layer || !m_layer->CurrentApp)
            {
                EmptyPanelBg(ctx, "##hier_empty", ctx->Theme.PanelBg, nullptr);
                return;
            }
            auto* app   = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
            auto* scene = reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene);
            auto* eng   = Engine::GetContext();
            if (!scene || !eng || !eng->ActorManager)
            {
                EmptyPanelBg(ctx, "##hier_empty", ctx->Theme.PanelBg, nullptr);
                return;
            }

            float   fh = ZUIGetFrameHeight(ctx);
            float   pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 200.f;

            ZUIBox* bg = ZUIBeginColumn(ctx, "##hier_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness              = 0.f;

            // ── Toolbar ──────────────────────────────────────────────────────────
            static constexpr float kBtnSz = 22.f;
            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##hier_tb", ZFill(), ZPx(kBtnSz));
            ZUISpacer(ctx, 8.f);
            ZUISearchBox(ctx, "##hier_search", m_search, sizeof(m_search), "Search...", ZFill());
            ZUISpacer(ctx, 4.f);
            // "New Collection" — folder+plus icon, matches develop button
            {
                ZUIBox* btn       = ZUIPushBox(ctx, "##btn_add", 9, ZUI_Clickable | ZUI_DrawActorIcon);
                btn->Size[0]      = ZPx(kBtnSz);
                btn->Size[1]      = ZPx(kBtnSz);
                bool hov          = (ctx->HotKey == btn->Key);
                btn->TextColor[0] = hov ? 0.85f : 0.55f;
                btn->TextColor[1] = hov ? 0.85f : 0.58f;
                btn->TextColor[2] = hov ? 0.90f : 0.65f;
                btn->TextColor[3] = 1.f;
                auto* bps         = ZUIStateGetOrInsert(&ctx->StateStore, btn->Key);
                if (bps)
                    bps->UserData = ZUI_ICON_COLLECTION_ADD;
                ZUISignal bsig = ZUISignalFromBox(ctx, btn);
                ZUIPopBox(ctx);
                if (bsig.Flags & ZUI_SignalClicked)
                {
                    ActorHandle nh = eng->ActorManager->Create();
                    Actor*      na = eng->ActorManager->Access(nh);
                    if (na)
                    {
                        NameComponent nc_new = {};
                        secure_strncpy(nc_new.Value, sizeof(nc_new.Value), "Collection", 10);
                        na->AddComponent<NameComponent>(nc_new);
                        na->AddComponent<TransformComponent>({});
                        scene->SelectedActorHandle = nh;
                    }
                }
            }
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
            ZUISpacer(ctx, 4.f);

            // ── 3-column table: Item Label (60%) | Type (25%) | Level (15%) ──────
            ZUIDataTableColumn cols[3] = {
                {"Item Label", fmaxf(pw * 0.60f, 100.f), false,  true},
                {      "Type", fmaxf(pw * 0.25f,  50.f), false, false},
                {     "Level", fmaxf(pw * 0.15f,  40.f), false, false},
            };

            // ── O(n) DFS tree build — exact develop algorithm ─────────────────────
            uint32_t actor_total = eng->ActorManager->Count();
            if (actor_total == 0)
            {
                ZUIEndColumn(ctx);
                return;
            }
            static constexpr uint32_t kMaxN = 512;
            uint32_t                  cap   = actor_total < kMaxN ? actor_total : kMaxN;

            struct OutlinerNode
            {
                ActorHandle Handle;
                EntityID    EID;
                EntityID    Parent;
            };
            OutlinerNode* nodes       = ZPushArray(&ctx->FrameArena, OutlinerNode, cap);
            uint32_t*     first_child = ZPushArray(&ctx->FrameArena, uint32_t, cap);
            uint32_t*     next_sib    = ZPushArray(&ctx->FrameArena, uint32_t, cap);
            uint32_t      nc          = 0;

            for (uint32_t i = 0; i < cap; ++i)
            {
                first_child[i] = UINT32_MAX;
                next_sib[i]    = UINT32_MAX;
            }
            eng->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
                if (nc >= cap)
                    return;
                auto* pc    = actor->GetComponent<ParentComponent>();
                nodes[nc++] = {h, actor->GetEntityID(), (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
            });

            // O(1) parent lookup — exact develop UnorderedHashMap pattern
            ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx;
            eid_to_idx.init(&ctx->FrameArena, nc * 2 + 1);
            for (uint32_t i = 0; i < nc; ++i)
                eid_to_idx.insert(nodes[i].EID, i);

            // Prepend-to-front-of-child-list linkage (exact develop)
            for (uint32_t i = 0; i < nc; ++i)
            {
                if (nodes[i].Parent == INVALID_ENTITY)
                    continue;
                auto* pidx = eid_to_idx.find(nodes[i].Parent);
                if (!pidx)
                    continue;
                next_sib[i]        = first_child[*pidx];
                first_child[*pidx] = i;
            }

            // DFS stack — roots pushed backward so first root is processed first (exact develop)
            struct DFSEntry
            {
                uint32_t idx;
                int      depth;
            };
            DFSEntry* stk = ZPushArray(&ctx->FrameArena, DFSEntry, nc * 2 + 2);
            int       sp  = 0;
            for (int i = (int) nc - 1; i >= 0; --i)
                if (nodes[i].Parent == INVALID_ENTITY)
                    stk[sp++] = {(uint32_t) i, 0};

            // Icon / type colors
            static const float kColLight[4]            = {1.00f, 0.85f, 0.20f, 1.f};
            static const float kColCamera[4]           = {0.45f, 0.85f, 0.55f, 1.f};
            static const float kColMesh[4]             = {0.55f, 0.75f, 0.90f, 1.f};
            static const float kColColl[4]             = {0.85f, 0.65f, 0.15f, 1.f};
            static const float kColActor[4]            = {0.55f, 0.55f, 0.60f, 1.f};
            static const float kWorldIcon[4]           = {0.35f, 0.80f, 0.45f, 1.f};
            static const float kDim[4]                 = {0.55f, 0.55f, 0.60f, 1.f};

            // Deferred mutations — applied after DFS to avoid tree invalidation
            ActorHandle        pending_delete          = {};
            ActorHandle        pending_reparent_child  = {};
            EntityID           pending_reparent_parent = INVALID_ENTITY;
            // Detach-to-root (World drop + "Remove from Parent" menu) routes through
            // pending_reparent_parent = INVALID_ENTITY — single path, exact develop.

            // ── Scroll region + DataTable ─────────────────────────────────────────
            ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());
            ZUIBeginDataTable(ctx, "##hier_tbl", 3, cols, ZFit());
            ZUIDataTableHeadersRow(ctx);

            // ── World root row ────────────────────────────────────────────────────
            {
                ZUIDataTableNextRow(ctx, false);
                ZUIDataTableSetColumn(ctx, 0);
                ZUIBeginRow(ctx, "##tr0_root", ZFill(), ZFill());

                // Chevron
                {
                    ZUIBox* arr       = ZUIPushBox(ctx, "##arr_root", 10, ZUI_DrawTriArrow | ZUI_Clickable);
                    arr->Size[0]      = ZPx(fh);
                    arr->Size[1]      = ZPx(fh);
                    arr->TextColor[0] = kDim[0];
                    arr->TextColor[1] = kDim[1];
                    arr->TextColor[2] = kDim[2];
                    arr->TextColor[3] = kDim[3];
                    auto* ps          = ZUIStateGetOrInsert(&ctx->StateStore, arr->Key);
                    if (ps)
                        ps->UserData = m_root_open ? 2.f : 3.f;
                    ZUISignal asig = ZUISignalFromBox(ctx, arr);
                    ZUIPopBox(ctx);
                    if (asig.Flags & ZUI_SignalClicked)
                        m_root_open = !m_root_open;
                }
                // World icon
                {
                    ZUIBox* icon       = ZUIPushBox(ctx, "##ti_root", 9, ZUI_DrawActorIcon);
                    icon->Size[0]      = ZPx(14.f);
                    icon->Size[1]      = ZPx(14.f);
                    icon->TextColor[0] = kWorldIcon[0];
                    icon->TextColor[1] = kWorldIcon[1];
                    icon->TextColor[2] = kWorldIcon[2];
                    icon->TextColor[3] = kWorldIcon[3];
                    auto* ps           = ZUIStateGetOrInsert(&ctx->StateStore, icon->Key);
                    if (ps)
                        ps->UserData = ZUI_ICON_WORLD;
                    ZUIPopBox(ctx);
                }
                ZUISpacer(ctx, 4.f);
                const char* sname = (scene->Name && scene->Name[0]) ? scene->Name : "Scene";
                ZUILabel(ctx, sname);
                ZUIEndRow(ctx);

                ZUIDataTableSetColumn(ctx, 1);
                ZUILabel(ctx, "World", kDim);
                ZUIDataTableSetColumn(ctx, 2);
                // World root accepts drops to detach an actor from its parent
                char drop_root[sizeof(ActorHandle)] = {};
                if (ZUIAcceptDrop(ctx, ctx->DT_RowBox, drop_root, sizeof(drop_root)))
                {
                    ActorHandle dragged = {};
                    secure_memcpy(&dragged, sizeof(dragged), drop_root, sizeof(drop_root));
                    if (dragged.Valid())
                    {
                        pending_reparent_child  = dragged;
                        pending_reparent_parent = INVALID_ENTITY;
                    }
                }
            }

            // ── Actor DFS rows ────────────────────────────────────────────────────
            if (m_root_open)
            {
                while (sp > 0)
                {
                    DFSEntry e     = stk[--sp];
                    uint32_t ni    = e.idx;
                    Actor*   actor = eng->ActorManager->Access(nodes[ni].Handle);
                    if (!actor)
                        continue;

                    auto*       nc_comp = actor->GetComponent<NameComponent>();
                    const char* label   = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";

                    // Search filter: skip node entirely (develop: plain continue, no subtree push)
                    if (m_search[0] && !ContainsCI(label, m_search))
                        continue;

                    // Type — exact develop: is_collection = !Mesh && !Light && !Camera
                    bool         is_coll = !actor->HasComponent<MeshComponent>() && !actor->HasComponent<LightComponent>() && !actor->HasComponent<CameraComponent>();
                    const float* type_col;
                    const char*  type_str;
                    float        icon_type;
                    if (is_coll)
                    {
                        type_col  = kColColl;
                        type_str  = "Collection";
                        icon_type = ZUI_ICON_FOLDER;
                    }
                    else if (actor->HasComponent<LightComponent>())
                    {
                        type_col  = kColLight;
                        type_str  = "Light";
                        icon_type = ZUI_ICON_LIGHT;
                    }
                    else if (actor->HasComponent<CameraComponent>())
                    {
                        type_col  = kColCamera;
                        type_str  = "Camera";
                        icon_type = ZUI_ICON_CAMERA;
                    }
                    else if (actor->HasComponent<MeshComponent>())
                    {
                        type_col  = kColMesh;
                        type_str  = "Static Mesh";
                        icon_type = ZUI_ICON_MESH;
                    }
                    else
                    {
                        type_col  = kColActor;
                        type_str  = "Actor";
                        icon_type = ZUI_ICON_ACTOR;
                    }

                    bool      has_ch      = (first_child[ni] != UINT32_MAX);
                    bool      collapsed   = has_ch && IsCollapsed(nodes[ni].EID); // exact develop naming
                    bool      selected    = (scene->SelectedActorHandle.Index == nodes[ni].Handle.Index && scene->SelectedActorHandle.Generation == nodes[ni].Handle.Generation);
                    bool      renaming    = (m_rename_id == nodes[ni].EID && m_rename_id.IsValid());
                    float     indent      = (float) (e.depth + 1) * ctx->Style.IndentSpacing;

                    // Row
                    bool      row_clicked = ZUIDataTableNextRow(ctx, selected);
                    ZUISignal row_sig     = ZUIDataTableRowSignal(ctx);

                    // Drop-target teal border — use prev-frame ScreenRect since HotKey frozen during drag
                    if (ctx->DragSourceKey != 0 && ctx->DT_RowBox)
                    {
                        auto* rps = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                        if (rps && rps->ScreenMaxX > rps->ScreenMinX && ctx->MousePos[0] >= rps->ScreenMinX && ctx->MousePos[0] <= rps->ScreenMaxX && ctx->MousePos[1] >= rps->ScreenMinY && ctx->MousePos[1] <= rps->ScreenMaxY)
                        {
                            ctx->DT_RowBox->Flags           = ctx->DT_RowBox->Flags | ZUI_DrawBorder;
                            ctx->DT_RowBox->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                            ctx->DT_RowBox->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                            ctx->DT_RowBox->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                            ctx->DT_RowBox->BorderColor[3]  = 0.85f;
                            ctx->DT_RowBox->BorderThickness = 1.f;
                        }
                    }

                    // Col 0: [indent][chevron or spacer][icon][name or rename field]
                    ZUIDataTableSetColumn(ctx, 0);
                    {
                        char rk0[64];
                        snprintf(rk0, sizeof(rk0), "##tr0_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                        ZUIBeginRow(ctx, rk0, ZFill(), ZFill());
                    }
                    ZUISpacer(ctx, indent);

                    if (has_ch)
                    {
                        char arr_key[64];
                        snprintf(arr_key, sizeof(arr_key), "##arr_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                        ZUIBox* arr       = ZUIPushBox(ctx, arr_key, (uint32_t) strlen(arr_key), ZUI_DrawTriArrow | ZUI_Clickable);
                        arr->Size[0]      = ZPx(fh);
                        arr->Size[1]      = ZPx(fh);
                        arr->TextColor[0] = kDim[0];
                        arr->TextColor[1] = kDim[1];
                        arr->TextColor[2] = kDim[2];
                        arr->TextColor[3] = kDim[3];
                        auto* ps          = ZUIStateGetOrInsert(&ctx->StateStore, arr->Key);
                        if (ps)
                            ps->UserData = !collapsed ? 2.f : 3.f; // ∨ expanded / › collapsed
                        ZUISignal asig = ZUISignalFromBox(ctx, arr);
                        ZUIPopBox(ctx);
                        if (asig.Flags & ZUI_SignalClicked)
                            ToggleCollapsed(nodes[ni].EID);
                    }
                    else
                    {
                        ZUISpacer(ctx, fh);
                    }

                    // Type icon
                    {
                        char ik[32];
                        snprintf(ik, sizeof(ik), "##ti_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                        ZUIBox* icon       = ZUIPushBox(ctx, ik, (uint32_t) strlen(ik), ZUI_DrawActorIcon);
                        icon->Size[0]      = ZPx(14.f);
                        icon->Size[1]      = ZPx(14.f);
                        icon->TextColor[0] = type_col[0];
                        icon->TextColor[1] = type_col[1];
                        icon->TextColor[2] = type_col[2];
                        icon->TextColor[3] = type_col[3];
                        auto* ps           = ZUIStateGetOrInsert(&ctx->StateStore, icon->Key);
                        if (ps)
                            ps->UserData = icon_type;
                        ZUIPopBox(ctx);
                    }
                    ZUISpacer(ctx, 4.f);

                    // Name or inline rename
                    if (renaming)
                    {
                        char tf_key[64];
                        snprintf(tf_key, sizeof(tf_key), "##ren_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);

                        // On the first frame: auto-focus the field immediately.
                        // Previous approach waited for the user to click — if they clicked
                        // elsewhere instead, m_rename_fkey stayed 0 and rename never committed.
                        if (m_rename_started)
                        {
                            m_rename_fkey    = ZUIHashStr(tf_key, (uint32_t) strlen(tf_key));
                            ctx->FocusKey    = m_rename_fkey;
                            m_rename_started = false;
                        }

                        ZUITextField(ctx, tf_key, m_rename_buf, sizeof(m_rename_buf), 150.f);

                        // Commit when focus leaves the field (Enter clears FocusKey via ZUIEndFrame)
                        if (m_rename_fkey != 0 && ctx->FocusKey != m_rename_fkey)
                        {
                            if (nc_comp && m_rename_buf[0])
                                secure_strncpy(nc_comp->Value, sizeof(nc_comp->Value), m_rename_buf, sizeof(m_rename_buf) - 1);
                            m_rename_id   = {};
                            m_rename_fkey = 0;
                        }
                    }
                    else
                    {
                        ZUILabel(ctx, label);
                    }
                    ZUIEndRow(ctx); // close horizontal layout

                    // Col 1: type
                    ZUIDataTableSetColumn(ctx, 1);
                    ZUILabel(ctx, type_str, kDim);

                    // Col 2: level
                    ZUIDataTableSetColumn(ctx, 2);
                    ZUILabel(ctx, "Default", kDim);

                    // Selection + panel-level double-click tracking.
                    // ZUISignalFromBox is called twice per row (DataTableNextRow + DataTableRowSignal)
                    // so double-click must be tracked here, not in ZUISignalFromBox.
                    if (row_clicked)
                    {
                        scene->SelectedActorHandle = nodes[ni].Handle;
                        float now                  = ctx->Time;
                        if (m_last_click_idx == nodes[ni].Handle.Index && m_last_click_gen == nodes[ni].Handle.Generation && now - m_last_click_time < 0.30f)
                        {
                            // Double-click on name/icon area → start rename
                            if (!renaming)
                            {
                                m_rename_id      = nodes[ni].EID;
                                m_rename_started = true;
                                m_rename_fkey    = 0;
                                secure_strncpy(m_rename_buf, sizeof(m_rename_buf), (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "", sizeof(m_rename_buf) - 1);
                            }
                            m_last_click_idx = UINT32_MAX; // consume
                        }
                        else
                        {
                            m_last_click_idx  = nodes[ni].Handle.Index;
                            m_last_click_gen  = nodes[ni].Handle.Generation;
                            m_last_click_time = now;
                        }
                    }

                    // Context menu
                    if (ZUIBeginPopupContextItem(ctx, "##actor_ctx", row_sig))
                    {
                        if (ZUIMenuItem(ctx, "Rename"))
                        {
                            m_rename_id      = nodes[ni].EID;
                            m_rename_started = true;
                            m_rename_fkey    = 0;
                            secure_strncpy(m_rename_buf, sizeof(m_rename_buf), (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "", sizeof(m_rename_buf) - 1);
                        }
                        if (nodes[ni].Parent != INVALID_ENTITY && ZUIMenuItem(ctx, "Remove from Parent"))
                        {
                            pending_reparent_child  = nodes[ni].Handle;
                            pending_reparent_parent = INVALID_ENTITY;
                        }
                        if (ZUIMenuItem(ctx, "Delete"))
                            pending_delete = nodes[ni].Handle;
                        ZUIEndPopup(ctx);
                    }

                    // Drag source — store handle in panel state (DragPayloadLen is cleared by
                    // ZUIInteractionPass on release, before the next BuildContent sees DragDropFired)
                    ZUIBeginDragSource(ctx, ctx->DT_RowBox, (const char*) &nodes[ni].Handle, sizeof(ActorHandle));
                    if (ctx->DragSourceKey != 0 && ctx->DT_RowBox && ctx->DragSourceKey == ctx->DT_RowBox->Key)
                    {
                        m_dragging_actor = nodes[ni].Handle;
                        secure_strncpy(m_drag_label, sizeof(m_drag_label), label, sizeof(m_drag_label) - 1);
                    }

                    // Drop target — bounds-based: DragTargetKey = deepest clickable child (chevron),
                    // not the outer row. Use prev-frame ScreenRect + panel-owned m_dragging_actor.
                    if (ctx->DragDropFired && m_dragging_actor.Valid() && ctx->DT_RowBox)
                    {
                        auto* drps   = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                        bool  in_row = drps && drps->ScreenMaxX > drps->ScreenMinX && ctx->MousePos[0] >= drps->ScreenMinX && ctx->MousePos[0] <= drps->ScreenMaxX && ctx->MousePos[1] >= drps->ScreenMinY && ctx->MousePos[1] <= drps->ScreenMaxY;
                        if (in_row && (m_dragging_actor.Index != nodes[ni].Handle.Index || m_dragging_actor.Generation != nodes[ni].Handle.Generation))
                        {
                            pending_reparent_child  = m_dragging_actor;
                            pending_reparent_parent = nodes[ni].EID;
                        }
                    }

                    // Push children — collect forward then reverse-push (exact develop tmp[] pattern)
                    if (has_ch && !collapsed)
                    {
                        uint32_t tmp[256];
                        int      tc = 0;
                        uint32_t c  = first_child[ni];
                        while (c != UINT32_MAX && tc < 256)
                        {
                            tmp[tc++] = c;
                            c         = next_sib[c];
                        }
                        for (int ci = tc - 1; ci >= 0; --ci)
                            stk[sp++] = {tmp[ci], e.depth + 1};
                    }
                }
            }

            // ── Root drop zone row — exact develop 8px InvisibleButton equivalent ──
            {
                ZUIDataTableNextRow(ctx, false);
                ZUIDataTableSetColumn(ctx, 0);
                ZUIBeginRow(ctx, "##root_dz_r", ZFill(), ZFill());
                // Highlight when drag is hovering this zone
                if (ctx->DragSourceKey != 0 && ctx->DT_RowBox)
                {
                    auto* rps = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                    if (rps && rps->ScreenMaxX > rps->ScreenMinX && ctx->MousePos[0] >= rps->ScreenMinX && ctx->MousePos[0] <= rps->ScreenMaxX && ctx->MousePos[1] >= rps->ScreenMinY && ctx->MousePos[1] <= rps->ScreenMaxY)
                    {
                        ctx->DT_RowBox->Flags           = ctx->DT_RowBox->Flags | ZUI_DrawBorder;
                        ctx->DT_RowBox->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                        ctx->DT_RowBox->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                        ctx->DT_RowBox->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                        ctx->DT_RowBox->BorderColor[3]  = 0.85f;
                        ctx->DT_RowBox->BorderThickness = 1.f;
                    }
                }
                ZUILabel(ctx, "Drop here to detach from parent", kDim);
                ZUIEndRow(ctx);
                ZUIDataTableSetColumn(ctx, 1);
                ZUIDataTableSetColumn(ctx, 2);
                // Root drop zone — use m_dragging_actor (DragPayloadLen=0 on drop frame)
                if (ctx->DragDropFired && m_dragging_actor.Valid() && ctx->DT_RowBox)
                {
                    auto* rzps    = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                    bool  in_zone = rzps && rzps->ScreenMaxX > rzps->ScreenMinX && ctx->MousePos[0] >= rzps->ScreenMinX && ctx->MousePos[0] <= rzps->ScreenMaxX && ctx->MousePos[1] >= rzps->ScreenMinY && ctx->MousePos[1] <= rzps->ScreenMaxY;
                    if (in_zone)
                    {
                        pending_reparent_parent = INVALID_ENTITY;
                        pending_reparent_child  = m_dragging_actor;
                    }
                }
            }

            ZUIEndDataTable(ctx);
            ZUIEndScrollRegion(ctx);

            // ── Drag tooltip — "Move: [name]" near cursor (matches develop ImGui::Text) ─
            if (ctx->DragSourceKey != 0 && m_drag_label[0])
            {
                char tip_buf[140];
                snprintf(tip_buf, sizeof(tip_buf), "Move: %s", m_drag_label);
                uint32_t tlen    = (uint32_t) strlen(tip_buf);
                float    tip_x   = ctx->MousePos[0] - rect[0] + 12.f;
                float    tip_y   = ctx->MousePos[1] - rect[1] - ZUIGetFrameHeight(ctx) - 4.f;
                ZUIBox*  tip     = ZUIPushBox(ctx, "##drag_tip", 10, ZUI_DrawBackground | ZUI_DrawText | ZUI_FloatX | ZUI_FloatY | ZUI_DropShadow);
                tip->FloatPos[0] = tip_x;
                tip->FloatPos[1] = tip_y;
                tip->Size[0]     = ZText();
                tip->Size[1]     = ZText();
                tip->Label       = ZUIPushStr(&ctx->FrameArena, tip_buf, tlen);
                tip->Padding[0]  = 6.f;
                tip->Padding[2]  = 6.f;
                ZUIBoxSetColorArr(tip, ctx->Theme.MenuBarBg);
                tip->TextColor[0] = ctx->Theme.TextDefault[0];
                tip->TextColor[1] = ctx->Theme.TextDefault[1];
                tip->TextColor[2] = ctx->Theme.TextDefault[2];
                tip->TextColor[3] = ctx->Theme.TextDefault[3];
                ZUIPopBox(ctx);
            }
            else if (ctx->DragSourceKey == 0)
            {
                m_drag_label[0] = '\0';
            }
            // DragDropFired fires on every mouse release during a drag (hit or miss).
            // Consume m_dragging_actor so it doesn't match next frame.
            if (ctx->DragDropFired)
                m_dragging_actor = {};

            // ── Deferred mutations — exact develop pattern ────────────────────────

            // Reparent (also handles detach-to-root via INVALID_ENTITY)
            if (pending_reparent_child.Valid())
            {
                Actor* child = eng->ActorManager->Access(pending_reparent_child);
                if (child)
                {
                    if (pending_reparent_parent == INVALID_ENTITY)
                    {
                        child->RemoveComponent<ParentComponent>();
                    }
                    else
                    {
                        ParentComponent pc_new = {};
                        pc_new.Parent          = pending_reparent_parent;
                        if (child->HasComponent<ParentComponent>())
                            child->GetComponent<ParentComponent>()->Parent = pending_reparent_parent;
                        else
                            child->AddComponent<ParentComponent>(pc_new);
                    }
                }
            }

            // Delete actor
            if (pending_delete.Valid())
            {
                Actor* a = eng->ActorManager->Access(pending_delete);
                if (a)
                {
                    auto* mc = a->GetComponent<MeshComponent>();
                    if (mc && mc->RenderInstanceId != UINT32_MAX)
                        scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                    if (scene->SelectedActorHandle.Index == pending_delete.Index && scene->SelectedActorHandle.Generation == pending_delete.Generation)
                        scene->SelectedActorHandle = {};
                    if (m_rename_id.IsValid())
                    {
                        m_rename_id   = {};
                        m_rename_fkey = 0;
                    }
                    eng->ActorManager->Destroy(pending_delete);
                }
            }

            // ── Status bar ───────────────────────────────────────────────────────
            ZUISeparator(ctx);
            ZUIBox* sb = ZUIBeginRow(ctx, "##hier_sb", ZFill(), ZPx(fh));
            sb->Flags  = sb->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sb, ctx->Theme.TitleBarBg);
            ZUISpacer(ctx, 6.f);
            {
                char status[64];
                int  total = (int) eng->ActorManager->Count();
                if (scene->SelectedActorHandle.Valid())
                    snprintf(status, sizeof(status), "%d actor%s (1 selected)", total, total == 1 ? "" : "s");
                else
                    snprintf(status, sizeof(status), "%d actor%s", total, total == 1 ? "" : "s");
                ZUILabel(ctx, status, kDim);
            }
            ZUIEndRow(ctx);

            ZUIEndColumn(ctx);
        }

    private:
        static bool ContainsCI(const char* haystack, const char* needle)
        {
            if (!needle[0])
                return true;
            for (; *haystack; ++haystack)
            {
                const char* h = haystack;
                const char* n = needle;
                while (*h && *n && tolower((unsigned char) *h) == tolower((unsigned char) *n))
                {
                    ++h;
                    ++n;
                }
                if (!*n)
                    return true;
            }
            return false;
        }
    };

    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, "Viewport");
        }
    };

    // ── Inspector panel ───────────────────────────────────────────────────────
    //
    // Reflection-driven: iterates ComponentReflectionRegistry::ForEach for the
    // selected actor, draws every registered component with ZUI widgets.
    // Matches develop InspectorViewUIComponent architecture translated to ZUI.
    //
    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel()
        {
            Title = "Inspector";
        }

        Tetragrama::Layers::ZUILayer* m_layer        = nullptr;
        char                          m_search[128]  = {};
        char                          m_category[64] = "All"; // active category pill ("All" = no filter)
        bool                          m_sec_open[64] = {};    // per TypeID, initialized to true
        bool                          m_sec_init     = false;

        static constexpr float        kLabelW        = 96.f;

        // ── ZUIVec3Row ────────────────────────────────────────────────────────
        // Three DragFloats with colored 3px left-edge bars (R=red, G=green, B=blue).
        // Matches develop Vec3Row() + ImDrawList colored bar approach.
        static bool                   Vec3Row(ZEngine::UI::ZUIContext* ctx, const char* row_key, const char* label, float* v, float speed, float pw)
        {
            using namespace ZEngine::UI;
            // Axis bar colors — matches develop: R(215,90,80) G(100,200,110) B(90,140,230)
            static const float kBarR[4] = {0.84f, 0.35f, 0.31f, 1.f};
            static const float kBarG[4] = {0.39f, 0.78f, 0.43f, 1.f};
            static const float kBarB[4] = {0.35f, 0.55f, 0.90f, 1.f};

            const float        fh       = ZUIGetFrameHeight(ctx);
            const float        w3       = fmaxf((pw - kLabelW - 36.f) / 3.f, 32.f);
            bool               any      = false;

            ZUIBeginRow(ctx, row_key, ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f); // leading indent — fields are inset from section edge

            // Label
            {
                ZUIBox* lbl       = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
                lbl->Size[0]      = ZPx(kLabelW);
                lbl->Size[1]      = ZPx(fh);
                uint32_t llen     = (uint32_t) strlen(label);
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, label, llen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }

            // Helper: axis pill (colored bg + "X/Y/Z" letter) + DragFloat
            struct AxisPill
            {
                static void Draw(ZUIContext* c, const char* pill_key, const char* drag_key, const float col[4], float* val, float sp, float w, float h)
                {
                    // Colored pill badge "X/Y/Z"
                    ZUIBox* pill        = ZUIPushBox(c, pill_key, (uint32_t) strlen(pill_key), ZUI_DrawBackground | ZUI_DrawText);
                    pill->Size[0]       = ZPx(14.f);
                    pill->Size[1]       = ZPx(h);
                    // Uppercase axis letter: "##x_…" → 'X', "##y_…" → 'Y', "##z_…" → 'Z'
                    char axis_letter[2] = {(char) (drag_key[2] - 32), '\0'};
                    pill->Label         = ZUIPushStr(&c->FrameArena, axis_letter, 1);
                    pill->TextAlign     = ZUITextAlign::Center;
                    ZUIBoxSetColorArr(pill, col);
                    ZUIBoxSetCornerRadius(pill, 2.f);
                    pill->TextColor[0] = pill->TextColor[1] = pill->TextColor[2] = 1.f;
                    pill->TextColor[3]                                           = 1.f;
                    ZUIPopBox(c);
                    ZUIDragFloat(c, drag_key, val, sp, w);
                }
            };
            char fxk[48], fyk[48], fzk[48];
            snprintf(fxk, sizeof(fxk), "##x_%s", row_key + 2);
            snprintf(fyk, sizeof(fyk), "##y_%s", row_key + 2);
            snprintf(fzk, sizeof(fzk), "##z_%s", row_key + 2);
            char pxk[48], pyk[48], pzk[48];
            snprintf(pxk, sizeof(pxk), "##px_%s", row_key + 2);
            snprintf(pyk, sizeof(pyk), "##py_%s", row_key + 2);
            snprintf(pzk, sizeof(pzk), "##pz_%s", row_key + 2);
            AxisPill::Draw(ctx, pxk, fxk, kBarR, &v[0], speed, w3, fh);
            AxisPill::Draw(ctx, pyk, fyk, kBarG, &v[1], speed, w3, fh);
            AxisPill::Draw(ctx, pzk, fzk, kBarB, &v[2], speed, w3, fh);
            ZUISpacer(ctx, 8.f); // trailing — matches leading

            ZUIEndRow(ctx);
            return any;
        }

        // ── DrawZUIField ──────────────────────────────────────────────────────
        // Widget dispatch by FieldType. Two-column row: dim label (kLabelW) + widget.
        // Matches develop DrawField() translated to ZUI widget calls.
        static void DrawZUIField(ZEngine::UI::ZUIContext* ctx, const ZEngine::ECS::FieldDescriptor& fd, void* comp_data, float pw, uint32_t comp_idx, uint32_t field_idx)
        {
            using namespace ZEngine::UI;
            using namespace ZEngine::ECS;

            if (fd.Hidden)
                return;

            void* ptr = static_cast<char*>(comp_data) + fd.Offset;
            float fh  = ZUIGetFrameHeight(ctx);
            float wid = fmaxf(pw - kLabelW - 24.f, 36.f); // 8px leading + 8px trailing

            // Vertical breathing room — same 3px gap above every field row,
            // current and future types all get it for free here.
            ZUISpacer(ctx, 3.f);

            char row_key[48];
            snprintf(row_key, sizeof(row_key), "##frow_%u_%u", comp_idx, field_idx);

            // Vec3f is handled by Vec3Row (full-row layout with colored bars)
            if (fd.Type == FieldType::Vec3f)
            {
                float* fv         = static_cast<float*>(ptr);

                // Rotation fields stored in radians — display in degrees
                bool   is_radians = (fd.Tooltip && strstr(fd.Tooltip, "radians") != nullptr);
                float  display[3] = {fv[0], fv[1], fv[2]};
                if (is_radians)
                {
                    display[0] = fv[0] * 57.2957795f;
                    display[1] = fv[1] * 57.2957795f;
                    display[2] = fv[2] * 57.2957795f;
                }

                if (Vec3Row(ctx, row_key, fd.Name, display, is_radians ? 1.0f : 0.05f, pw) && is_radians)
                {
                    fv[0] = display[0] * 0.01745329f; // deg → rad
                    fv[1] = display[1] * 0.01745329f;
                    fv[2] = display[2] * 0.01745329f;
                }
                else if (!is_radians)
                {
                    fv[0] = display[0];
                    fv[1] = display[1];
                    fv[2] = display[2];
                }
                return;
            }

            // All other types: two-column row (label | widget)
            ZUIBeginRow(ctx, row_key, ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f); // leading indent — aligns with Vec3Row

            {
                ZUIBox* lbl       = ZUIPushBox(ctx, "##lbl", 5, ZUI_DrawText);
                lbl->Size[0]      = ZPx(kLabelW);
                lbl->Size[1]      = ZPx(fh);
                uint32_t llen     = (uint32_t) strlen(fd.Name);
                lbl->Label        = ZUIPushStr(&ctx->FrameArena, fd.Name, llen);
                lbl->TextColor[0] = ctx->Theme.TextDim[0];
                lbl->TextColor[1] = ctx->Theme.TextDim[1];
                lbl->TextColor[2] = ctx->Theme.TextDim[2];
                lbl->TextColor[3] = ctx->Theme.TextDim[3];
                ZUIPopBox(ctx);
            }

            char wkey[48];
            snprintf(wkey, sizeof(wkey), "##fv_%u_%u", comp_idx, field_idx);

            bool disabled = fd.ReadOnly;
            (void) disabled; // TODO: ZUIBeginDisabled when implemented

            switch (fd.Type)
            {
                case FieldType::Bool:
                    ZUICheckbox(ctx, wkey, static_cast<bool*>(ptr));
                    break;

                case FieldType::Int8:
                case FieldType::Int16:
                case FieldType::Int32:
                {
                    int32_t val = 0;
                    if (fd.Type == FieldType::Int8)
                        val = *static_cast<int8_t*>(ptr);
                    else if (fd.Type == FieldType::Int16)
                        val = *static_cast<int16_t*>(ptr);
                    else
                        val = *static_cast<int32_t*>(ptr);
                    if (ZUIDragInt(ctx, wkey, &val, 1.f, wid))
                    {
                        if (fd.Type == FieldType::Int8)
                            *static_cast<int8_t*>(ptr) = (int8_t) val;
                        else if (fd.Type == FieldType::Int16)
                            *static_cast<int16_t*>(ptr) = (int16_t) val;
                        else
                            *static_cast<int32_t*>(ptr) = val;
                    }
                    break;
                }

                case FieldType::Int64:
                case FieldType::UInt8:
                case FieldType::UInt16:
                case FieldType::UInt32:
                case FieldType::UInt64:
                {
                    // Display as read-only formatted string for now
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%lld", (long long) *(int64_t*) ptr);
                    ZUILabel(ctx, buf, ctx->Theme.TextDefault);
                    break;
                }

                case FieldType::Float:
                {
                    ZUIDragFloat(ctx, wkey, static_cast<float*>(ptr), 0.05f, wid);
                    break;
                }

                case FieldType::Double:
                {
                    float f = (float) *static_cast<double*>(ptr);
                    if (ZUIDragFloat(ctx, wkey, &f, 0.05f, wid))
                        *static_cast<double*>(ptr) = (double) f;
                    break;
                }

                case FieldType::Vec2f:
                {
                    float* fv = static_cast<float*>(ptr);
                    float  w2 = fmaxf(wid * 0.5f, 36.f);
                    char   xk[48], yk[48];
                    snprintf(xk, sizeof(xk), "##v2x_%u_%u", comp_idx, field_idx);
                    snprintf(yk, sizeof(yk), "##v2y_%u_%u", comp_idx, field_idx);
                    ZUIDragFloat(ctx, xk, &fv[0], 0.05f, w2);
                    ZUIDragFloat(ctx, yk, &fv[1], 0.05f, w2);
                    break;
                }

                case FieldType::Vec4f:
                {
                    float* fv = static_cast<float*>(ptr);
                    float  w4 = fmaxf(wid * 0.25f, 28.f);
                    for (int ax = 0; ax < 4; ++ax)
                    {
                        char ak[48];
                        snprintf(ak, sizeof(ak), "##v4_%d_%u_%u", ax, comp_idx, field_idx);
                        ZUIDragFloat(ctx, ak, &fv[ax], 0.05f, w4);
                    }
                    break;
                }

                case FieldType::String:
                {
                    uint32_t cap = fd.StringCap > 0 ? fd.StringCap : 256u;
                    ZUITextField(ctx, wkey, static_cast<char*>(ptr), cap, wid);
                    break;
                }

                case FieldType::AssetUUID:
                {
                    // Read-only UUID string
                    const char* uuid_str = static_cast<const char*>(ptr);
                    ZUILabel(ctx, uuid_str && uuid_str[0] ? uuid_str : "(none)", ctx->Theme.TextDefault);
                    break;
                }

                case FieldType::Enum:
                {
                    // Read current value (size-dispatched)
                    int64_t cur = 0;
                    switch (fd.Size)
                    {
                        case 1:
                            cur = (int64_t) *static_cast<uint8_t*>(ptr);
                            break;
                        case 2:
                            cur = (int64_t) *static_cast<uint16_t*>(ptr);
                            break;
                        case 4:
                            cur = (int64_t) *static_cast<uint32_t*>(ptr);
                            break;
                        case 8:
                            cur = *static_cast<int64_t*>(ptr);
                            break;
                    }
                    // Find preview label
                    const char* preview = "?";
                    for (uint32_t ei = 0; ei < fd.EnumCount; ++ei)
                        if (fd.EnumValues[ei].Value == cur)
                        {
                            preview = fd.EnumValues[ei].Name;
                            break;
                        }
                    if (ZUIBeginCombo(ctx, wkey, preview, ZPx(wid)))
                    {
                        for (uint32_t ei = 0; ei < fd.EnumCount; ++ei)
                        {
                            bool sel = (fd.EnumValues[ei].Value == cur);
                            if (ZUIComboItem(ctx, fd.EnumValues[ei].Name, sel))
                            {
                                int64_t nv = fd.EnumValues[ei].Value;
                                switch (fd.Size)
                                {
                                    case 1:
                                        *static_cast<uint8_t*>(ptr) = (uint8_t) nv;
                                        break;
                                    case 2:
                                        *static_cast<uint16_t*>(ptr) = (uint16_t) nv;
                                        break;
                                    case 4:
                                        *static_cast<uint32_t*>(ptr) = (uint32_t) nv;
                                        break;
                                    case 8:
                                        *static_cast<int64_t*>(ptr) = nv;
                                        break;
                                }
                            }
                        }
                        ZUIEndCombo(ctx);
                    }
                    break;
                }

                default:
                    ZUILabel(ctx, "(unsupported)", ctx->Theme.TextDim);
                    break;
            }

            ZUISpacer(ctx, 8.f); // trailing — matches leading
            ZUIEndRow(ctx);
        }

        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine;
            using namespace ZEngine::ECS;
            using namespace ZEngine::ECS::Components;
            using namespace ZEngine::Helpers;
            using namespace ZEngine::UI;

            // Guard
            if (!m_layer || !m_layer->CurrentApp)
            {
                EmptyPanelBg(ctx, "##insp_empty", ctx->Theme.PanelBg, nullptr);
                return;
            }
            auto* app   = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
            auto* scene = reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene);
            auto* eng   = Engine::GetContext();
            if (!scene || !eng || !eng->ActorManager)
            {
                EmptyPanelBg(ctx, "##insp_empty", ctx->Theme.PanelBg, nullptr);
                return;
            }

            // Initialize section-open state to all-true on first call
            if (!m_sec_init)
            {
                for (int i = 0; i < 64; ++i)
                    m_sec_open[i] = true;
                m_sec_init = true;
            }

            float   fh = ZUIGetFrameHeight(ctx);
            float   pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 220.f;

            ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;

            // ── No-selection guard ────────────────────────────────────────────────
            Actor* actor     = scene->SelectedActorHandle.Valid() ? eng->ActorManager->Access(scene->SelectedActorHandle) : nullptr;
            if (!actor)
            {
                ZUIBeginScrollRegion(ctx, "##insp_empty_scroll", ZFill(), ZFill());
                ZUISpacer(ctx, 12.f);
                {
                    ZUIBox* lbl       = ZUIPushBox(ctx, "##no_sel", 8, ZUI_DrawText);
                    lbl->Size[0]      = ZFill();
                    lbl->Size[1]      = ZText();
                    lbl->TextAlign    = ZUITextAlign::Center;
                    uint32_t tlen     = 17;
                    lbl->Label        = ZUIPushStr(&ctx->FrameArena, "No actor selected", tlen);
                    lbl->TextColor[0] = ctx->Theme.TextDim[0];
                    lbl->TextColor[1] = ctx->Theme.TextDim[1];
                    lbl->TextColor[2] = ctx->Theme.TextDim[2];
                    lbl->TextColor[3] = ctx->Theme.TextDim[3];
                    ZUIPopBox(ctx);
                }
                ZUIEndScrollRegion(ctx);
                ZUIEndColumn(ctx);
                return;
            }

            // ── Actor header — name editing ───────────────────────────────────────
            {
                ZUIBox* hdr = ZUIBeginColumn(ctx, "##insp_hdr", ZFill(), ZPx(fh + 16.f));
                hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
                ZUIBoxSetColor(hdr, 0.18f, 0.18f, 0.22f, 1.f);
                hdr->EdgeSoftness = 0.f;
                hdr->Padding[1]   = 6.f; // top padding — vertically centers the field
                hdr->Padding[3]   = 6.f; // bottom padding

                auto* nc_comp     = actor->GetComponent<NameComponent>();
                ZUIBeginRow(ctx, "##insp_hdr_r", ZFill(), ZPx(fh));
                ZUISpacer(ctx, 8.f);
                if (nc_comp)
                    ZUITextField(ctx, "##actor_name", nc_comp->Value, sizeof(nc_comp->Value), fmaxf(pw - 24.f, 80.f));
                else
                    ZUILabel(ctx, "Actor", ctx->Theme.TextDefault);
                ZUISpacer(ctx, 8.f);
                ZUIEndRow(ctx);

                ZUIEndColumn(ctx);
            }

            // ── Search bar ────────────────────────────────────────────────────────
            ZUISpacer(ctx, 4.f);
            ZUIBeginRow(ctx, "##insp_search_row", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUISearchBox(ctx, "##insp_search", m_search, sizeof(m_search), "Search Details...", ZFill());
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);
            ZUISpacer(ctx, 4.f);

            // ── Category pill buttons (UE5-style filter row) ──────────────────────
            // Collect unique categories for components on this actor
            {
                static constexpr int kMaxCats       = 16;
                const char*          cats[kMaxCats] = {};
                int                  cat_n          = 0;
                const ArchetypeMask  cat_mask       = actor->GetComponentMask();
                const auto&          cat_reg        = ComponentReflectionRegistry::Get();

                cat_reg.ForEach([&](const ComponentMeta& m) {
                    if (!MaskHas(cat_mask, m.TypeID) || !m.Category)
                        return;
                    // Check not already in list
                    for (int ci = 0; ci < cat_n; ++ci)
                        if (cats[ci] && strcmp(cats[ci], m.Category) == 0)
                            return;
                    if (cat_n < kMaxCats)
                        cats[cat_n++] = m.Category;
                });

                if (cat_n > 0)
                {
                    ZUISpacer(ctx, 4.f);
                    // Pill button colors
                    static const float kPillAct[4]  = {0.22f, 0.63f, 0.69f, 1.f}; // teal — active (matches "All" blue in UE5)
                    static const float kPillRest[4] = {0.20f, 0.20f, 0.24f, 1.f}; // dark — inactive

                    ZUIBeginRow(ctx, "##cat_pills", ZFill(), ZPx(fh + 4.f));
                    ZUISpacer(ctx, 6.f);

                    // "All" pill
                    {
                        bool    is_all = (strcmp(m_category, "All") == 0);
                        ZUIBox* btn    = ZUIPushBox(ctx, "##cpAll", 7, ZUI_Clickable | ZUI_DrawBackground | ZUI_DrawText);
                        float   tw     = 28.f;
                        btn->Size[0]   = ZPx(tw);
                        btn->Size[1]   = ZPx(fh);
                        btn->Label     = ZUIPushStr(&ctx->FrameArena, "All", 3);
                        btn->TextAlign = ZUITextAlign::Center;
                        ZUIBoxSetColorArr(btn, is_all ? kPillAct : kPillRest);
                        ZUIBoxSetCornerRadius(btn, 3.f);
                        btn->TextColor[0] = btn->TextColor[1] = btn->TextColor[2] = 1.f;
                        btn->TextColor[3]                                         = 1.f;
                        ZUISignal sig                                             = ZUISignalFromBox(ctx, btn);
                        ZUIPopBox(ctx);
                        if (sig.Flags & ZUI_SignalClicked)
                            ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), "All", 3);
                        ZUISpacer(ctx, 4.f);
                    }

                    // One pill per category
                    for (int ci = 0; ci < cat_n; ++ci)
                    {
                        if (!cats[ci])
                            continue;
                        bool is_active = (strcmp(m_category, cats[ci]) == 0);
                        char pill_key[48];
                        snprintf(pill_key, sizeof(pill_key), "##cp_%d", ci);
                        uint32_t clen   = (uint32_t) strlen(cats[ci]);
                        // Approximate pill width from name length
                        float    pill_w = fmaxf((float) clen * 7.f + 10.f, 40.f);

                        ZUIBox*  btn    = ZUIPushBox(ctx, pill_key, (uint32_t) strlen(pill_key), ZUI_Clickable | ZUI_DrawBackground | ZUI_DrawText);
                        btn->Size[0]    = ZPx(pill_w);
                        btn->Size[1]    = ZPx(fh);
                        btn->Label      = ZUIPushStr(&ctx->FrameArena, cats[ci], clen);
                        btn->TextAlign  = ZUITextAlign::Center;
                        ZUIBoxSetColorArr(btn, is_active ? kPillAct : kPillRest);
                        ZUIBoxSetCornerRadius(btn, 3.f);
                        btn->TextColor[0] = btn->TextColor[1] = btn->TextColor[2] = 1.f;
                        btn->TextColor[3]                                         = 1.f;
                        ZUISignal sig                                             = ZUISignalFromBox(ctx, btn);
                        ZUIPopBox(ctx);
                        if (sig.Flags & ZUI_SignalClicked)
                            ZEngine::Helpers::secure_strncpy(m_category, sizeof(m_category), cats[ci], sizeof(m_category) - 1);
                        ZUISpacer(ctx, 4.f);
                    }
                    ZUIEndRow(ctx);
                    ZUISpacer(ctx, 4.f);
                }
            }

            ZUISeparator(ctx);

            // ── Scroll region ─────────────────────────────────────────────────────
            ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##insp_scroll", ZFill(), ZFill());
            ZUIPaddingXY(scroll, 0.f, 4.f); // 4px top/bottom breathing room

            // ── Reflection-driven component sections ──────────────────────────────
            ArchetypeMask mask     = actor->GetComponentMask();
            uint32_t      comp_idx = 0;
            const auto&   registry = ComponentReflectionRegistry::Get();

            registry.ForEach([&](const ComponentMeta& meta) {
                if (!MaskHas(mask, meta.TypeID))
                    return;

                // Category pill filter
                bool all_cats = (strcmp(m_category, "All") == 0);
                if (!all_cats && (!meta.Category || strcmp(meta.Category, m_category) != 0))
                {
                    ++comp_idx;
                    return;
                }

                // Search filter on component name
                if (m_search[0])
                {
                    bool name_matches = (strstr(meta.TypeName, m_search) != nullptr);
                    // Also check if any field name matches
                    bool field_match  = false;
                    for (uint32_t fi = 0; fi < meta.FieldCount && !field_match; ++fi)
                        if (!meta.Fields[fi].Hidden && strstr(meta.Fields[fi].Name, m_search))
                            field_match = true;
                    if (!name_matches && !field_match)
                    {
                        ++comp_idx;
                        return;
                    }
                }

                void* comp_data = actor->GetComponentRaw(meta.TypeID);
                if (!comp_data)
                {
                    ++comp_idx;
                    return;
                }

                uint32_t open_idx = meta.TypeID < 64u ? meta.TypeID : 0u;
                ZUICollapsingHeader(ctx, meta.TypeName, &m_sec_open[open_idx]);

                if (m_sec_open[open_idx])
                {
                    ZUISpacer(ctx, 6.f);
                    for (uint32_t fi = 0; fi < meta.FieldCount; ++fi)
                        DrawZUIField(ctx, meta.Fields[fi], comp_data, pw, comp_idx, fi);
                    ZUISpacer(ctx, 6.f);
                    ZUISeparator(ctx);
                }
                ZUISpacer(ctx, 2.f); // small gap between sections

                ++comp_idx;
            });

            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx);
        }
    };

    // ── Console panel ─────────────────────────────────────────────────────────

    struct ConsolePanel : ZUIPanelView
    {
        ConsolePanel()
        {
            Title = "Console";
        }

        ~ConsolePanel()
        {
            if (m_cookie)
                ZEngine::Logging::Logger::RemoveEventHandler(m_cookie);
        }

        struct LogEntry
        {
            char    Text[256] = {};
            float   Color[4]  = {0.9f, 0.9f, 0.9f, 1.f};
            uint8_t Level     = 0;
        };

        // kLevels[0] = "All" (show everything); kLevels[1..5] map to LogLevel 0..4.
        // Putting "All" first matches standard log panel convention.
        static constexpr int         kMaxEntries         = 512;
        static constexpr const char* kLevels[]           = {"All", "Trace", "Info", "Warn", "Error", "Critical"};

        LogEntry                     m_ring[kMaxEntries] = {};
        int                          m_head              = 0;
        int                          m_count             = 0;
        std::mutex                   m_mutex;
        uint32_t                     m_cookie       = 0;
        bool                         m_initialized  = false;
        char                         m_search[128]  = {};
        int                          m_filter_level = 0;  // 0 = All; 1..5 = exact LogLevel 0..4
        std::atomic<bool>            m_auto_scroll{true}; // atomic: read on logger thread, written on UI thread
        bool                         m_scroll_pending = false;

        void                         PushEntry(const LogEntry& e)
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_ring[m_head] = e;
            m_head         = (m_head + 1) % kMaxEntries;
            if (m_count < kMaxEntries)
                ++m_count;
            if (m_auto_scroll.load(std::memory_order_relaxed))
                m_scroll_pending = true;
        }

        static void OnLogEntry(void* user, const ZEngine::Logging::LogMessage& msg)
        {
            auto*    self = static_cast<ConsolePanel*>(user);
            LogEntry e    = {};
            int      len  = msg.Message ? (int) strlen(msg.Message) : 0;
            if (len > (int) sizeof(e.Text) - 1)
                len = (int) sizeof(e.Text) - 1;
            if (msg.Message)
                memcpy(e.Text, msg.Message, len);
            e.Text[len] = '\0';
            e.Color[0]  = msg.Color[0];
            e.Color[1]  = msg.Color[1];
            e.Color[2]  = msg.Color[2];
            e.Color[3]  = msg.Color[3];
            e.Level     = static_cast<uint8_t>(msg.Level);
            self->PushEntry(e);
        }

        // Case-insensitive substring search
        static bool ContainsCI(const char* haystack, const char* needle)
        {
            if (!needle[0])
                return true;
            for (; *haystack; ++haystack)
            {
                const char* h = haystack;
                const char* n = needle;
                while (*h && *n && tolower((unsigned char) *h) == tolower((unsigned char) *n))
                {
                    ++h;
                    ++n;
                }
                if (!*n)
                    return true;
            }
            return false;
        }

        bool PassesFilter(const LogEntry& e) const
        {
            if (m_filter_level > 0 && e.Level != (uint8_t) (m_filter_level - 1))
                return false;
            if (m_search[0] && !ContainsCI(e.Text, m_search))
                return false;
            return true;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            if (!m_initialized)
            {
                m_cookie      = ZEngine::Logging::Logger::AddEventHandler({OnLogEntry, this});
                m_initialized = true;
            }

            float   pw = rect[2] - rect[0];
            float   fh = ZUIGetFrameHeight(ctx);

            ZUIBox* bg = ZUIBeginColumn(ctx, "##con_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;

            ZUISpacer(ctx, 6.f);

            // ── Toolbar: search | Filters popup | Clear | auto-scroll ─────────────
            ZUIBeginRow(ctx, "##con_tb", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 8.f);
            ZUISearchBox(ctx, "##con_search", m_search, sizeof(m_search), "Search Log...", ZPx(fmaxf(pw * 0.50f, 120.f)));
            ZUISpacer(ctx, 8.f);

            // Filters dropdown — shows "Filters" when All, level name when filtered
            const char* flt_preview = (m_filter_level == 0) ? "Filters" : kLevels[m_filter_level];
            if (ZUIBeginCombo(ctx, "##con_flt", flt_preview, ZPx(100.f)))
            {
                for (int i = 0; i < 6; ++i)
                    if (ZUIComboItem(ctx, kLevels[i], m_filter_level == i))
                        m_filter_level = i;
                ZUIEndCombo(ctx);
            }

            ZUISpacer(ctx, 8.f);
            bool do_clear = (ZUISmallButton(ctx, "Clear##con").Flags & ZUI_SignalClicked) != 0;
            ZUISpacer(ctx, 8.f);
            // atomic<bool> — load to local, pass pointer, store result back
            bool as = m_auto_scroll.load(std::memory_order_relaxed);
            ZUICheckbox(ctx, "##con_as", &as);
            m_auto_scroll.store(as, std::memory_order_relaxed);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "Auto-scroll", ctx->Theme.TextDim);
            ZUISpacer(ctx, 8.f);
            ZUIEndRow(ctx);

            ZUISpacer(ctx, 4.f);
            ZUISeparator(ctx);

            // ── Log entries ──────────────────────────────────────────────────────
            bool    pending;
            ZUIBox* scroll = ZUIBeginScrollRegion(ctx, "##con_scroll", ZFill(), ZFill());
            ZUIPaddingXY(scroll, 4.f, 2.f);
            {
                std::lock_guard<std::mutex> lk(m_mutex);
                pending          = m_scroll_pending;
                m_scroll_pending = false;

                if (do_clear)
                {
                    m_count     = 0;
                    m_head      = 0;
                    m_search[0] = '\0';
                }

                int total = (m_count >= kMaxEntries) ? kMaxEntries : m_count;
                int start = (m_count >= kMaxEntries) ? m_head : 0;

                for (int i = 0; i < total; ++i)
                {
                    const LogEntry& e = m_ring[(start + i) % kMaxEntries];
                    if (!PassesFilter(e))
                        continue;
                    ZUILabel(ctx, e.Text, e.Color);
                }
            }
            if (pending)
                ZUIScrollToBottom(ctx, "##con_scroll");

            ZUIEndScrollRegion(ctx);
            ZUIEndColumn(ctx);
        }
    };

} // namespace Tetragrama::Panels
