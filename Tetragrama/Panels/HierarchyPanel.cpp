#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Panels/HierarchyPanel.h>
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace Tetragrama::Panels
{
    using namespace ZEngine;
    using namespace ZEngine::ECS;
    using namespace ZEngine::ECS::Components;
    using namespace ZEngine::Helpers;
    using namespace ZEngine::UI;

    static bool ContainsCI(const char* haystack, const char* needle)
    {
        if (!haystack)
            return false;
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

    HierarchyPanel::HierarchyPanel()
    {
        Title = "Hierarchy";
    }

    bool HierarchyPanel::IsCollapsed(ZEngine::ECS::EntityID id) const
    {
        for (int i = 0; i < m_ncollapsed; ++i)
            if (m_collapsed[i] == id)
                return true;
        return false;
    }

    void HierarchyPanel::ToggleCollapsed(ZEngine::ECS::EntityID id)
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

    void HierarchyPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
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
        bg->EdgeSoftness = 0.f;

        // ── Toolbar — row height = fh so search box and button align exactly ─────
        ZUISpacer(ctx, 4.f);
        ZUIBeginRow(ctx, "##hier_tb", ZFill(), ZPx(fh));
        ZUISpacer(ctx, 8.f);
        ZUISearchBox(ctx, "##hier_search", m_search, sizeof(m_search), "Search...", ZFill());
        ZUISpacer(ctx, 4.f);
        // "New Collection" — folder+plus icon (same height as search box = fh)
        {
            ZUIBox* btn       = ZUIPushBox(ctx, "##btn_add", 9, ZUI_Clickable | ZUI_DrawActorIcon);
            btn->Size[0]      = ZPx(fh);
            btn->Size[1]      = ZPx(fh);
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

        // ── 3-column table: Item Label (sortable+resizable) | Type | Level ──────
        ZUIDataTableColumn cols[3] = {
            {"Item Label", fmaxf(pw * 0.55f, 100.f),  true,  true},
            {      "Type", fmaxf(pw * 0.22f,  48.f),  true, false},
            {     "Level", fmaxf(pw * 0.23f,  58.f), false, false},
        };

        // ── O(n) DFS tree build — exact develop algorithm ─────────────────────
        uint32_t actor_total = eng->ActorManager->Count();
        uint32_t cap         = actor_total > 0 ? actor_total : 1; // min 1 to avoid 0-size alloc

        struct OutlinerNode
        {
            ActorHandle Handle;
            EntityID    EID;
            EntityID    Parent;
        };
        OutlinerNode* nodes       = ZPushArray(&ctx->FrameArena, OutlinerNode, cap);
        uint32_t*     first_child = ZPushArray(&ctx->FrameArena, uint32_t, cap);
        uint32_t*     next_sib    = ZPushArray(&ctx->FrameArena, uint32_t, cap);
        uint32_t      node_count  = 0;

        for (uint32_t i = 0; i < cap; ++i)
        {
            first_child[i] = UINT32_MAX;
            next_sib[i]    = UINT32_MAX;
        }
        eng->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
            auto* pc           = actor->GetComponent<ParentComponent>();
            nodes[node_count++] = {h, actor->GetEntityID(), (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
        });

        // O(1) parent lookup — exact develop UnorderedHashMap pattern
        ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx;
        eid_to_idx.init(&ctx->FrameArena, node_count * 2 + 1);
        for (uint32_t i = 0; i < node_count; ++i)
            eid_to_idx.insert(nodes[i].EID, i);

        // Prepend-to-front-of-child-list linkage (exact develop)
        for (uint32_t i = 0; i < node_count; ++i)
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
        DFSEntry* stk = ZPushArray(&ctx->FrameArena, DFSEntry, node_count * 2 + 2);
        int       sp  = 0;
        for (int i = (int) node_count - 1; i >= 0; --i)
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
        bool               drop_handled            = false; // prevents dual-path double-mutation
        // Detach-to-root (World drop + "Remove from Parent" menu) routes through
        // pending_reparent_parent = INVALID_ENTITY — single path, exact develop.

        // ── Scroll region + DataTable ─────────────────────────────────────────
        ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());
        ZUIBeginDataTable(ctx, "##hier_tbl", 3, cols, ZFill());

        // Custom header — UE5 style: bright TextDefault labels, eye icon, sort arrows
        // ctx->DT_ColWidths is populated by ZUIBeginDataTable above.
        {
            static const char* kColNames[] = {"Item Label", "Type", "Level"};
            ZUIBox*            hrow        = ZUIBeginRow(ctx, "##hier_hdr", ZFill(), ZPx(fh));
            hrow->Flags                    = hrow->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(hrow, ctx->Theme.TableHeaderBg);
            hrow->EdgeSoftness = 0.f;

            // Column header cells — each cell is ZPx(cw) wide starting at x=0,
            // exactly matching data column cell boundaries (no prefix offset).
            // Eye icon lives INSIDE column 0 (same as develop branch).
            for (int ci = 0; ci < 3; ++ci)
            {
                float       cw    = ctx->DT_ColWidths ? ctx->DT_ColWidths[ci] : cols[ci].InitWidth;
                const char* lbl   = kColNames[ci];
                uint32_t    llen  = (uint32_t) strlen(lbl);
                bool        is_sc = (ctx->DT_SortCol == ci);

                // Cell container (X-axis row inside the header row)
                char  cellk[32]; snprintf(cellk, sizeof(cellk), "##hcell_%d", ci);
                ZUIBox* cell_row = ZUIBeginRow(ctx, cellk, ZPx(cw), ZPx(fh));
                (void) cell_row;

                // Column 0: decorative eye icon (no ZUI_Clickable — not yet interactive).
                // ZUIStateGetOrInsert is required to set UserData for the icon renderer.
                if (ci == 0)
                {
                    ZUISpacer(ctx, 4.f);
                    ZUIBox* eye  = ZUIPushBox(ctx, "##hdr_eye", 9, ZUI_DrawActorIcon);
                    eye->Size[0] = ZPx(fh * 0.7f);
                    eye->Size[1] = ZPx(fh);
                    eye->TextColor[0] = ctx->Theme.TextDim[0]; eye->TextColor[1] = ctx->Theme.TextDim[1];
                    eye->TextColor[2] = ctx->Theme.TextDim[2]; eye->TextColor[3] = ctx->Theme.TextDim[3];
                    auto* eps = ZUIStateGetOrInsert(&ctx->StateStore, eye->Key);
                    if (eps) eps->UserData = ZUI_ICON_WORLD;
                    ZUIPopBox(ctx);
                    ZUISpacer(ctx, 4.f);
                }
                else
                {
                    ZUISpacer(ctx, 6.f); // same left indent as data Padding[0]
                }

                // Label — teal if sort column, bright otherwise
                ZUIBox* lbox     = ZUIPushBox(ctx, lbl, llen, ZUI_DrawText | ZUI_Clickable);
                lbox->Size[0]    = ZText();
                lbox->Size[1]    = ZPx(fh);
                lbox->Label      = ZUIPushStr(&ctx->FrameArena, lbl, llen);
                bool lhov = !is_sc && (ctx->HotKey == lbox->Key);
                lbox->TextColor[0] = is_sc ? ctx->Theme.TabActiveBorder[0] : lhov ? 1.f : ctx->Theme.TextDefault[0];
                lbox->TextColor[1] = is_sc ? ctx->Theme.TabActiveBorder[1] : lhov ? 1.f : ctx->Theme.TextDefault[1];
                lbox->TextColor[2] = is_sc ? ctx->Theme.TabActiveBorder[2] : lhov ? 1.f : ctx->Theme.TextDefault[2];
                lbox->TextColor[3] = 1.f;
                ZUISignal csig   = ZUISignalFromBox(ctx, lbox);
                ZUIPopBox(ctx);

                ZUIEndRow(ctx); // cell_row

                if (cols[ci].Sortable && (csig.Flags & ZUI_SignalClicked))
                {
                    if (ctx->DT_SortCol == ci) ctx->DT_SortAsc = !ctx->DT_SortAsc;
                    else { ctx->DT_SortCol = ci; ctx->DT_SortAsc = true; }
                }
            }
            ZUIEndRow(ctx); // hrow
        }

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
                bool arr_hov      = (ctx->HotKey == arr->Key);
                arr->TextColor[0] = arr_hov ? ctx->Theme.TextDefault[0] : kDim[0];
                arr->TextColor[1] = arr_hov ? ctx->Theme.TextDefault[1] : kDim[1];
                arr->TextColor[2] = arr_hov ? ctx->Theme.TextDefault[2] : kDim[2];
                arr->TextColor[3] = arr_hov ? ctx->Theme.TextDefault[3] : kDim[3];
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
            if (!drop_handled && ZUIAcceptDrop(ctx, ctx->DT_RowBox, drop_root, sizeof(drop_root)))
            {
                ActorHandle dragged = {};
                secure_memcpy(&dragged, sizeof(dragged), drop_root, sizeof(drop_root));
                if (dragged.Valid())
                {
                    pending_reparent_child  = dragged;
                    pending_reparent_parent = INVALID_ENTITY;
                    drop_handled            = true;
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

                auto*       nc_comp     = actor->GetComponent<NameComponent>();
                const char* label       = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";
                bool        label_match = !m_search[0] || ContainsCI(label, m_search);

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

                // Render the row only when it matches the search filter;
                // always push children so descendant matches are reachable.
                if (label_match)
                {

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
                    bool arr_hov      = (ctx->HotKey == arr->Key);
                    arr->TextColor[0] = arr_hov ? ctx->Theme.TextDefault[0] : kDim[0];
                    arr->TextColor[1] = arr_hov ? ctx->Theme.TextDefault[1] : kDim[1];
                    arr->TextColor[2] = arr_hov ? ctx->Theme.TextDefault[2] : kDim[2];
                    arr->TextColor[3] = arr_hov ? ctx->Theme.TextDefault[3] : kDim[3];
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
                        {
                            if (secure_strncpy(nc_comp->Value, sizeof(nc_comp->Value), m_rename_buf, sizeof(m_rename_buf) - 1) >= 0)
                            {
                                m_rename_id   = {};
                                m_rename_fkey = 0;
                            }
                            // On copy failure keep rename active so user can retry
                        }
                        else
                        {
                            m_rename_id   = {};
                            m_rename_fkey = 0;
                        }
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
                    double now                 = (double) ctx->Time;
                    if (m_last_click_idx == nodes[ni].Handle.Index && m_last_click_gen == nodes[ni].Handle.Generation && now - m_last_click_time < 0.30)
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
                if (!drop_handled && ctx->DragDropFired && m_dragging_actor.Valid() && ctx->DT_RowBox)
                {
                    auto* drps   = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                    bool  in_row = drps && drps->ScreenMaxX > drps->ScreenMinX && ctx->MousePos[0] >= drps->ScreenMinX && ctx->MousePos[0] <= drps->ScreenMaxX && ctx->MousePos[1] >= drps->ScreenMinY && ctx->MousePos[1] <= drps->ScreenMaxY;
                    if (in_row && (m_dragging_actor.Index != nodes[ni].Handle.Index || m_dragging_actor.Generation != nodes[ni].Handle.Generation))
                    {
                        pending_reparent_child  = m_dragging_actor;
                        pending_reparent_parent = nodes[ni].EID;
                        drop_handled            = true;
                    }
                }

                } // end if (label_match)

                // Push children — always unconditional so filtered subtrees remain reachable.
                if (has_ch && !collapsed)
                {
                    uint32_t tmp[1024];
                    int      tc = 0;
                    uint32_t c  = first_child[ni];
                    while (c != UINT32_MAX && tc < 1024)
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
            if (!drop_handled && ctx->DragDropFired && m_dragging_actor.Valid() && ctx->DT_RowBox)
            {
                auto* rzps    = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                bool  in_zone = rzps && rzps->ScreenMaxX > rzps->ScreenMinX && ctx->MousePos[0] >= rzps->ScreenMinX && ctx->MousePos[0] <= rzps->ScreenMaxX && ctx->MousePos[1] >= rzps->ScreenMinY && ctx->MousePos[1] <= rzps->ScreenMaxY;
                if (in_zone)
                {
                    pending_reparent_parent = INVALID_ENTITY;
                    pending_reparent_child  = m_dragging_actor;
                    drop_handled            = true;
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
            float    tip_y   = fmaxf(0.f, ctx->MousePos[1] - rect[1] - ZUIGetFrameHeight(ctx) - 4.f);
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
                // Cycle guard: abort if the target is a descendant of the child being moved.
                bool cycle = false;
                if (pending_reparent_parent != INVALID_ENTITY)
                {
                    EntityID child_eid = child->GetEntityID();
                    EntityID walk      = pending_reparent_parent;
                    uint32_t steps     = 0;
                    while (walk != INVALID_ENTITY && steps < node_count)
                    {
                        if (walk == child_eid) { cycle = true; break; }
                        auto* widx = eid_to_idx.find(walk);
                        if (!widx) break;
                        walk = nodes[*widx].Parent;
                        ++steps;
                    }
                }

                if (!cycle)
                {
                    if (pending_reparent_parent == INVALID_ENTITY)
                    {
                        if (child->HasComponent<ParentComponent>())
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
        }

        // Delete actor — re-root children first to prevent orphaned subtrees.
        if (pending_delete.Valid())
        {
            Actor* a = eng->ActorManager->Access(pending_delete);
            if (a)
            {
                EntityID delete_eid = a->GetEntityID();
                // Detach all direct children before destroying the parent.
                for (uint32_t ci = 0; ci < node_count; ++ci)
                {
                    if (nodes[ci].Parent == delete_eid)
                    {
                        Actor* child_a = eng->ActorManager->Access(nodes[ci].Handle);
                        if (child_a && child_a->HasComponent<ParentComponent>())
                            child_a->RemoveComponent<ParentComponent>();
                    }
                }
                auto* mc = a->GetComponent<MeshComponent>();
                if (mc && mc->RenderInstanceId != UINT32_MAX)
                    scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                if (scene->SelectedActorHandle.Index == pending_delete.Index && scene->SelectedActorHandle.Generation == pending_delete.Generation)
                    scene->SelectedActorHandle = {};
                if (m_rename_id.IsValid() && m_rename_id == delete_eid)
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
} // namespace Tetragrama::Panels
