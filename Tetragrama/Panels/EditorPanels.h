#pragma once
#include <Tetragrama/Editor.h>
#include <Tetragrama/EditorScene.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
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
            bg->EdgeSoftness = 0.f;

            // ── Toolbar ──────────────────────────────────────────────────────────
            ZUIBeginRow(ctx, "##hier_tb", ZFill(), ZPx(fh));
            ZUISpacer(ctx, 6.f);
            ZUISearchBox(ctx, "##hier_search", m_search, sizeof(m_search), "Search...", ZFill());
            ZUISpacer(ctx, 6.f);
            bool do_add = (ZUISmallButton(ctx, "+##hier").Flags & ZUI_SignalClicked) != 0;
            ZUISpacer(ctx, 6.f);
            ZUIEndRow(ctx);

            // ── 3-column table: Item Label (60%) | Type (25%) | Level (15%) ──────
            // Mirrors ImGui::TableSetupColumn("Item Label", WidthStretch, 0.60f) etc.
            ZUIDataTableColumn cols[3] = {
                {"Item Label", fmaxf(pw * 0.60f, 100.f), false,  true},
                {      "Type", fmaxf(pw * 0.25f,  50.f), false, false},
                {     "Level", fmaxf(pw * 0.15f,  40.f), false, false},
            };

            // ── DFS tree build ────────────────────────────────────────────────────
            uint32_t                  actor_n = eng->ActorManager->Count();

            static constexpr uint32_t kMaxN   = 512;
            uint32_t                  nc      = actor_n < kMaxN ? actor_n : kMaxN;

            struct OutlinerNode
            {
                ActorHandle Handle;
                EntityID    EID;
                EntityID    Parent;
            };
            OutlinerNode* nodes       = ZPushArray(&ctx->FrameArena, OutlinerNode, nc + 1);
            uint32_t*     first_child = ZPushArray(&ctx->FrameArena, uint32_t, nc + 1);
            uint32_t*     next_sib    = ZPushArray(&ctx->FrameArena, uint32_t, nc + 1);
            uint32_t      actual_nc   = 0;
            for (uint32_t i = 0; i <= nc; ++i)
            {
                first_child[i] = UINT32_MAX;
                next_sib[i]    = UINT32_MAX;
            }
            eng->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
                if (actual_nc >= nc)
                    return;
                auto* pc           = actor->GetComponent<ParentComponent>();
                nodes[actual_nc++] = {h, actor->GetEntityID(), (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
            });
            nc = actual_nc;
            for (uint32_t i = 0; i < nc; ++i)
            {
                if (nodes[i].Parent == INVALID_ENTITY)
                    continue;
                for (uint32_t j = 0; j < nc; ++j)
                {
                    if (nodes[j].EID == nodes[i].Parent)
                    {
                        next_sib[i]    = first_child[j];
                        first_child[j] = i;
                        break;
                    }
                }
            }
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

            // Icon colors
            static const float kColLight[4]           = {1.00f, 0.85f, 0.20f, 1.f};
            static const float kColCamera[4]          = {0.45f, 0.85f, 0.55f, 1.f};
            static const float kColMesh[4]            = {0.55f, 0.75f, 0.90f, 1.f};
            static const float kColColl[4]            = {0.85f, 0.65f, 0.15f, 1.f};
            static const float kColActor[4]           = {0.55f, 0.55f, 0.60f, 1.f};
            static const float kWorldIcon[4]          = {0.35f, 0.80f, 0.45f, 1.f};
            static const float kDim[4]                = {0.55f, 0.55f, 0.60f, 1.f};

            // Deferred mutations — applied after DFS to avoid invalidating the tree
            ActorHandle        pending_delete         = {};
            ActorHandle        pending_duplicate      = {};
            ActorHandle        pending_reparent_child = {};
            ActorHandle        pending_reparent_par   = {};
            bool               pending_detach         = false;
            ActorHandle        pending_detach_h       = {};

            // ── Scroll region + DataTable ─────────────────────────────────────────
            ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());
            ZUIBeginDataTable(ctx, "##hier_tbl", 3, cols, ZFit());
            ZUIDataTableHeadersRow(ctx);

            // World root row (always shown, acts as drop target to detach from parent)
            {
                ZUIDataTableNextRow(ctx, false);
                ZUIDataTableSetColumn(ctx, 0);

                // Disclosure chevron
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

                ZUIDataTableSetColumn(ctx, 1);
                ZUILabel(ctx, "World", kDim);

                ZUIDataTableSetColumn(ctx, 2);
                // Root is a drop target: dropping here detaches the actor from its parent
                char drop_root[sizeof(ActorHandle)] = {};
                if (ZUIAcceptDrop(ctx, ctx->DT_RowBox, drop_root, sizeof(drop_root)))
                {
                    ActorHandle dragged = {};
                    secure_memcpy(&dragged, sizeof(dragged), drop_root, sizeof(drop_root));
                    if (dragged.Valid())
                    {
                        pending_detach   = true;
                        pending_detach_h = dragged;
                    }
                }
            }

            // ── Actor rows (DFS) ──────────────────────────────────────────────────
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

                    // Search filter — skip row but still expand children
                    if (m_search[0] && !ContainsCI(label, m_search))
                    {
                        bool has_ch    = (first_child[ni] != UINT32_MAX);
                        bool is_open_s = has_ch && !IsCollapsed(nodes[ni].EID);
                        if (has_ch && is_open_s)
                        {
                            uint32_t c = first_child[ni];
                            while (c != UINT32_MAX)
                            {
                                stk[sp++] = {c, e.depth + 1};
                                c         = next_sib[c];
                            }
                        }
                        continue;
                    }

                    // Type determination
                    const float* type_col;
                    const char*  type_str;
                    float        icon_type;
                    bool         has_ch = (first_child[ni] != UINT32_MAX);
                    if (actor->HasComponent<LightComponent>())
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
                        type_str  = "Mesh";
                        icon_type = ZUI_ICON_MESH;
                    }
                    else if (has_ch)
                    {
                        type_col  = kColColl;
                        type_str  = "Folder";
                        icon_type = ZUI_ICON_FOLDER;
                    }
                    else
                    {
                        type_col  = kColActor;
                        type_str  = "Actor";
                        icon_type = ZUI_ICON_ACTOR;
                    }

                    bool      is_open     = has_ch && !IsCollapsed(nodes[ni].EID);
                    bool      selected    = (scene->SelectedActorHandle.Index == nodes[ni].Handle.Index && scene->SelectedActorHandle.Generation == nodes[ni].Handle.Generation);
                    bool      renaming    = (m_rename_id == nodes[ni].EID && m_rename_id.IsValid());
                    float     indent      = (float) (e.depth + 1) * ctx->Style.IndentSpacing;

                    // Row — ZUIDataTableNextRow handles selection bg, alternating rows, click
                    bool      row_clicked = ZUIDataTableNextRow(ctx, selected);
                    ZUISignal row_sig     = ZUIDataTableRowSignal(ctx);

                    // Col 0: tree content
                    ZUIDataTableSetColumn(ctx, 0);
                    ZUISpacer(ctx, indent);

                    // VS Code chevron (∨/›) or leaf spacer
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
                            ps->UserData = is_open ? 2.f : 3.f;
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

                    // Actor name or inline rename
                    if (renaming)
                    {
                        char tf_key[64];
                        snprintf(tf_key, sizeof(tf_key), "##ren_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                        uint64_t fk_before = ctx->FocusKey;
                        ZUITextField(ctx, tf_key, m_rename_buf, sizeof(m_rename_buf), 150.f);
                        uint64_t fk_after = ctx->FocusKey;
                        if (m_rename_started)
                        {
                            m_rename_started = false;
                            m_rename_fkey    = 0;
                        }
                        else if (m_rename_fkey != 0 && fk_after != m_rename_fkey)
                        {
                            if (nc_comp && m_rename_buf[0])
                                secure_strncpy(nc_comp->Value, sizeof(nc_comp->Value), m_rename_buf, sizeof(m_rename_buf) - 1);
                            m_rename_id   = {};
                            m_rename_fkey = 0;
                        }
                        if (fk_before != fk_after && fk_after != 0)
                            m_rename_fkey = fk_after;
                    }
                    else
                    {
                        ZUILabel(ctx, label);
                    }

                    // Col 1: type
                    ZUIDataTableSetColumn(ctx, 1);
                    ZUILabel(ctx, type_str, kDim);

                    // Col 2: level
                    ZUIDataTableSetColumn(ctx, 2);
                    ZUILabel(ctx, "Default", kDim);

                    // Selection
                    if (row_clicked)
                        scene->SelectedActorHandle = nodes[ni].Handle;

                    // Double-click → inline rename
                    if (!renaming && (row_sig.Flags & ZUI_SignalDoubleClicked))
                    {
                        m_rename_id      = nodes[ni].EID;
                        m_rename_started = true;
                        m_rename_fkey    = 0;
                        secure_strncpy(m_rename_buf, sizeof(m_rename_buf), (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "", sizeof(m_rename_buf) - 1);
                    }

                    // Context menu (right-click on row)
                    if (ZUIBeginPopupContextItem(ctx, "##actor_ctx", row_sig))
                    {
                        if (ZUIMenuItem(ctx, "Rename"))
                        {
                            m_rename_id      = nodes[ni].EID;
                            m_rename_started = true;
                            m_rename_fkey    = 0;
                            secure_strncpy(m_rename_buf, sizeof(m_rename_buf), (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "", sizeof(m_rename_buf) - 1);
                        }
                        if (ZUIMenuItem(ctx, "Duplicate"))
                            pending_duplicate = nodes[ni].Handle;
                        if (ZUIMenuItem(ctx, "Delete"))
                            pending_delete = nodes[ni].Handle;
                        if (nodes[ni].Parent != INVALID_ENTITY && ZUIMenuItem(ctx, "Remove from Parent"))
                        {
                            pending_detach   = true;
                            pending_detach_h = nodes[ni].Handle;
                        }
                        ZUIEndPopup(ctx);
                    }

                    // Drag source + drop target for reparenting
                    ZUIBeginDragSource(ctx, ctx->DT_RowBox, (const char*) &nodes[ni].Handle, sizeof(ActorHandle));
                    char drop_buf[sizeof(ActorHandle)] = {};
                    if (ZUIAcceptDrop(ctx, ctx->DT_RowBox, drop_buf, sizeof(drop_buf)))
                    {
                        ActorHandle dragged = {};
                        secure_memcpy(&dragged, sizeof(dragged), drop_buf, sizeof(drop_buf));
                        if (dragged.Valid() && (dragged.Index != nodes[ni].Handle.Index || dragged.Generation != nodes[ni].Handle.Generation))
                        {
                            pending_reparent_child = dragged;
                            pending_reparent_par   = nodes[ni].Handle;
                        }
                    }

                    // Expand children (DFS)
                    if (has_ch && is_open)
                    {
                        uint32_t c = first_child[ni];
                        while (c != UINT32_MAX)
                        {
                            stk[sp++] = {c, e.depth + 1};
                            c         = next_sib[c];
                        }
                    }
                }
            }

            ZUIEndDataTable(ctx);
            ZUIEndScrollRegion(ctx);

            // ── Deferred mutations ────────────────────────────────────────────────

            auto DeleteActor = [&](ActorHandle h) {
                Actor* a = eng->ActorManager->Access(h);
                if (!a)
                    return;
                auto* mc = a->GetComponent<MeshComponent>();
                if (mc && mc->RenderInstanceId != UINT32_MAX)
                    scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                if (scene->SelectedActorHandle.Index == h.Index && scene->SelectedActorHandle.Generation == h.Generation)
                    scene->SelectedActorHandle = {};
                if (m_rename_id.IsValid())
                {
                    m_rename_id   = {};
                    m_rename_fkey = 0;
                }
                eng->ActorManager->Destroy(h);
            };

            if (pending_reparent_child.Valid() && pending_reparent_par.Valid())
            {
                Actor* child  = eng->ActorManager->Access(pending_reparent_child);
                Actor* parent = eng->ActorManager->Access(pending_reparent_par);
                if (child && parent)
                {
                    EntityID pid = parent->GetEntityID();
                    auto*    pc  = child->GetComponent<ParentComponent>();
                    if (pc)
                        pc->Parent = pid;
                    else
                    {
                        ParentComponent npc = {};
                        npc.Parent          = pid;
                        child->AddComponent<ParentComponent>(npc);
                    }
                }
            }
            if (pending_detach && pending_detach_h.Valid())
            {
                Actor* a = eng->ActorManager->Access(pending_detach_h);
                if (a)
                    a->RemoveComponent<ParentComponent>();
            }
            if (pending_duplicate.Valid())
            {
                Actor* src = eng->ActorManager->Access(pending_duplicate);
                if (src)
                {
                    ActorHandle dh = eng->ActorManager->Create();
                    Actor*      da = eng->ActorManager->Access(dh);
                    if (da)
                    {
                        NameComponent nc_new = {};
                        auto*         nc_src = src->GetComponent<NameComponent>();
                        if (nc_src && nc_src->Value[0])
                        {
                            secure_strncpy(nc_new.Value, sizeof(nc_new.Value), nc_src->Value, sizeof(nc_src->Value));
                            uint32_t nl = (uint32_t) secure_strlen(nc_new.Value);
                            if (nl + 5 < sizeof(nc_new.Value))
                                secure_strncpy(nc_new.Value + nl, sizeof(nc_new.Value) - nl, " Copy", 5);
                        }
                        else
                        {
                            secure_strncpy(nc_new.Value, sizeof(nc_new.Value), "Actor Copy", 10);
                        }
                        da->AddComponent<NameComponent>(nc_new);
                        auto* tc = src->GetComponent<TransformComponent>();
                        da->AddComponent<TransformComponent>(tc ? *tc : TransformComponent{});
                        scene->SelectedActorHandle = dh;
                    }
                }
            }
            if (pending_delete.Valid())
                DeleteActor(pending_delete);

            if (do_add)
            {
                ActorHandle nh = eng->ActorManager->Create();
                Actor*      na = eng->ActorManager->Access(nh);
                if (na)
                {
                    NameComponent nc_new = {};
                    secure_strncpy(nc_new.Value, sizeof(nc_new.Value), "Actor", 5);
                    na->AddComponent<NameComponent>(nc_new);
                    na->AddComponent<TransformComponent>({});
                    scene->SelectedActorHandle = nh;
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
    // Six mini-panel sections in a vertical stack.
    // Each section is a ZUICollapsingHeader (click = collapse/expand, no close).
    // Drag header to reorder using panel-docking visual helpers.
    // Sash (resize) appears only below expanded sections.
    //
    struct InspectorPanel : ZUIPanelView
    {
        // ── Config ─────────────────────────────────────────────────────────
        static constexpr int         N              = 6;
        static constexpr float       kMinH          = 80.f; // minimum expanded content height
        static constexpr float       kSashH         = 6.f;  // resize grip height

        // ── Section display state (indexed by display position) ────────────
        int                          m_order[N]     = {0, 1, 2, 3, 4, 5}; // section type at each display slot
        bool                         m_open[N]      = {true, true, true, false, false, false};
        float                        m_h[N]         = {200.f, 80.f, 100.f, 80.f, 80.f, 80.f};

        // ── Drag-reorder state ─────────────────────────────────────────────
        int                          m_drag_di      = -1;
        bool                         m_drag_active  = false;
        float                        m_drag_acc_y   = 0.f;
        int                          m_drop_slot    = 0;
        bool                         m_drop_is_bot  = false;

        // ── Component data (by section type) ──────────────────────────────
        float                        m_position[3]  = {0.f, 0.f, 0.f};
        float                        m_rotation[3]  = {0.f, 0.f, 0.f};
        float                        m_scale[3]     = {1.f, 1.f, 1.f};
        bool                         m_cast_shadows = true, m_receive_shadows = true;
        float                        m_mass        = 1.f;
        bool                         m_use_gravity = true, m_is_kinematic = false;
        float                        m_light_intensity = 1.f, m_light_range = 10.f;
        float                        m_light_color[3] = {1.f, 1.f, 1.f};
        float                        m_audio_volume   = 1.f;
        bool                         m_audio_loop = false, m_audio_play_awake = true;

        static constexpr const char* kLabels[N] = {"Transform", "Mesh Renderer", "Rigid Body", "Lighting", "Audio Source", "Script"};

        InspectorPanel()
        {
            Title = "Inspector";
        }

        // Content height for section at display position di, clamped to kMinH
        float ContentH(int di) const
        {
            return fmaxf(m_h[di], kMinH);
        }

        // Total scroll-content height for section di (placeholder height when source)
        float SectionRunH(int di, float hdrH) const
        {
            if (di == m_drag_di)
                return hdrH;
            float h    = hdrH + (m_open[di] ? ContentH(di) : 0.f);
            float sash = (m_open[di] && di < N - 1) ? kSashH : 0.f;
            return h + sash;
        }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            using namespace ZEngine::UI;
            (void) rect;

            const float hdrH  = ZUIGetFrameHeight(ctx);
            const float sectW = rect[2] - rect[0];
            const float fw    = sectW - 100.f - 16.f;

            // ── Open scroll region ────────────────────────────────────────
            ZUIBox*     sr    = ZUIBeginScrollRegion(ctx, "##insp_sr", ZFill(), ZFill());
            sr->Flags         = sr->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(sr, ctx->Theme.PanelBg);
            sr->EdgeSoftness         = 0.f;

            ZUIPersistentState* srps = ZUIStateGetOrInsert(&ctx->StateStore, sr->Key);
            const float         scy  = srps ? srps->ScrollY : 0.f;

            // ── Drop slot from cursor ─────────────────────────────────────
            if (m_drag_active)
            {
                float crel    = ctx->MousePos[1] - rect[1] + scy;
                float run     = 0.f;
                m_drop_slot   = 0;
                m_drop_is_bot = false;
                for (int di = 0; di < N; di++)
                {
                    float sh  = SectionRunH(di, hdrH);
                    float top = run, bot = run + sh;
                    if (crel >= top && crel < bot)
                    {
                        if (di != m_drag_di && m_open[di])
                        {
                            bool bot_half = crel >= top + sh * 0.5f;
                            m_drop_slot   = bot_half ? di + 1 : di;
                            m_drop_is_bot = bot_half;
                        }
                        else
                        {
                            m_drop_slot = di;
                        }
                        break;
                    }
                    m_drop_slot = (crel < top) ? di : di + 1;
                    if (crel < top)
                        break;
                    run += sh;
                }
                m_drop_slot = (m_drop_slot < 0) ? 0 : (m_drop_slot > N ? N : m_drop_slot);
            }

            // Cancel drag if cursor exits the inspector panel rect — sections are vertical-only
            if (m_drag_active)
            {
                bool inside = ctx->MousePos[0] >= rect[0] && ctx->MousePos[0] <= rect[2] && ctx->MousePos[1] >= rect[1] && ctx->MousePos[1] <= rect[3];
                if (!inside)
                {
                    m_drag_di     = -1;
                    m_drag_active = false;
                    m_drag_acc_y  = 0.f;
                    m_drop_is_bot = false;
                }
            }

            // ── Commit reorder ────────────────────────────────────────────
            if (ctx->MouseReleased[0] && m_drag_di >= 0)
            {
                if (m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1)
                {
                    int   os = m_order[m_drag_di];
                    float oh = m_h[m_drag_di];
                    bool  oo = m_open[m_drag_di];
                    for (int i = m_drag_di; i < N - 1; i++)
                    {
                        m_order[i] = m_order[i + 1];
                        m_h[i]     = m_h[i + 1];
                        m_open[i]  = m_open[i + 1];
                    }
                    int ins = (m_drop_slot > m_drag_di) ? m_drop_slot - 1 : m_drop_slot;
                    ins     = (ins < 0) ? 0 : (ins >= N ? N - 1 : ins);
                    for (int i = N - 1; i > ins; i--)
                    {
                        m_order[i] = m_order[i - 1];
                        m_h[i]     = m_h[i - 1];
                        m_open[i]  = m_open[i - 1];
                    }
                    m_order[ins] = os;
                    m_h[ins]     = oh;
                    m_open[ins]  = oo;
                }
                m_drag_di     = -1;
                m_drag_active = false;
                m_drag_acc_y  = 0.f;
                m_drop_is_bot = false;
            }

            bool            drop_chg  = m_drag_active && m_drop_slot != m_drag_di && m_drop_slot != m_drag_di + 1;
            const float*    tb        = ctx->Theme.TabActiveBorder;

            // ── Section loop ──────────────────────────────────────────────
            const char*     ghost_lbl = nullptr;
            constexpr float kTopPad   = 6.f;
            float           run_y     = kTopPad;
            ZUISpacer(ctx, kTopPad);

            auto Row = [&](const char* rk) {
                ZUIBeginRow(ctx, rk, ZFill(), ZPx(hdrH));
                ZUISpacer(ctx, 8.f);
            };
            auto EndRow = [&]() { ZUIEndRow(ctx); };

            for (int di = 0; di < N; di++)
            {
                int  s      = m_order[di];
                bool is_src = m_drag_active && (di == m_drag_di);
                char ck[32];

                // Source slot: border placeholder
                if (is_src)
                {
                    ghost_lbl           = kLabels[s];
                    ZUIBox* ph          = ZUIPushBox(ctx, "##ph", 4, ZUI_DrawBorder);
                    ph->Size[0]         = ZFill();
                    ph->Size[1]         = ZPx(hdrH);
                    ph->EdgeSoftness    = 0.f;
                    ph->BorderColor[0]  = tb[0];
                    ph->BorderColor[1]  = tb[1];
                    ph->BorderColor[2]  = tb[2];
                    ph->BorderColor[3]  = 0.30f;
                    ph->BorderThickness = 1.f;
                    ZUIPopBox(ctx);
                    run_y += hdrH;
                    continue;
                }

                // Drop zone visual — before this section
                bool  top_tgt = drop_chg && (m_drop_slot == di) && m_open[di] && !m_drop_is_bot;
                bool  bot_tgt = drop_chg && (m_drop_slot == di + 1) && m_open[di] && m_drop_is_bot;
                bool  col_tgt = drop_chg && (m_drop_slot == di) && !m_open[di] && !m_drop_is_bot;

                float sec_top = run_y - scy;

                if (top_tgt)
                    ZUIDockDividerH(ctx, "##dtop");

                // Header — collapsed target gets teal highlight
                {
                    float        hi[4] = {tb[0], tb[1], tb[2], 0.22f};
                    const float* hcol  = col_tgt ? hi : nullptr;
                    ZUISignal    sig   = ZUICollapsingHeader(ctx, kLabels[s], &m_open[di], hcol);

                    // Drag detection via signal — suppressed naturally when the divider hit zone
                    // owns ctx->ActiveKey, since ZUI_SignalHeld requires ActiveKey == header->Key.
                    if (!m_drag_active && (sig.Flags & ZUI_SignalHeld))
                    {
                        m_drag_acc_y += sig.DragDelta[1];
                        if (fabsf(m_drag_acc_y) > 8.f)
                        {
                            m_drag_active = true;
                            m_drag_di     = di;
                            m_drag_acc_y  = 0.f;
                        }
                    }
                }

                // Content
                if (m_open[di])
                {
                    float cH     = ContentH(di);
                    float half_h = (hdrH + cH) * 0.5f;

                    snprintf(ck, sizeof(ck), "##cc%d", di);
                    ZUIBox* c       = ZUIBeginColumn(ctx, ck, ZFill(), ZPx(cH));
                    c->Flags        = c->Flags | ZUI_ClipChildren;
                    c->EdgeSoftness = 0.f;
                    ZUISpacer(ctx, 2.f);

                    switch (s)
                    {
                        case 0: // Transform
                            Row("##r_pos");
                            ZUILabel(ctx, "Position", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##pos", m_position, 0.1f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_rot");
                            ZUILabel(ctx, "Rotation", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##rot", m_rotation, 0.5f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_scl");
                            ZUILabel(ctx, "Scale", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##scl", m_scale, 0.05f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 1: // Mesh Renderer
                            Row("##r_cs");
                            ZUILabel(ctx, "Cast Shadows", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##cs", &m_cast_shadows);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_rs");
                            ZUILabel(ctx, "Recv Shadows", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##rs", &m_receive_shadows);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 2: // Rigid Body
                            Row("##r_ms");
                            ZUILabel(ctx, "Mass", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##ms", &m_mass, 0.1f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_ug");
                            ZUILabel(ctx, "Use Gravity", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##ug", &m_use_gravity);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_km");
                            ZUILabel(ctx, "Kinematic", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##km", &m_is_kinematic);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 3: // Lighting
                            Row("##r_li");
                            ZUILabel(ctx, "Intensity", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##li", &m_light_intensity, 0.05f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_lr");
                            ZUILabel(ctx, "Range", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##lr", &m_light_range, 0.5f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_lc");
                            ZUILabel(ctx, "Color", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat3(ctx, "##lc", m_light_color, 0.01f, fw / 3.f);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 4: // Audio Source
                            Row("##r_av");
                            ZUILabel(ctx, "Volume", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUIDragFloat(ctx, "##av", &m_audio_volume, 0.02f, fw);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_al");
                            ZUILabel(ctx, "Loop", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##al", &m_audio_loop);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            Row("##r_pa");
                            ZUILabel(ctx, "Play Awake", ctx->Theme.TextDim);
                            ZUISpacer(ctx, 4.f);
                            ZUICheckbox(ctx, "##pa", &m_audio_play_awake);
                            ZUISpacer(ctx, 8.f);
                            EndRow();
                            break;
                        case 5: // Script
                            ZUILabel(ctx, "No script attached", ctx->Theme.TextDim);
                            break;
                    }

                    ZUIEndColumn(ctx);

                    // Sash — after expanded sections only
                    if (di < N - 1)
                    {
                        char sk[16], vk[16];
                        snprintf(sk, sizeof(sk), "##sk%d", di);
                        snprintf(vk, sizeof(vk), "##sv%d", di);

                        ZUIBox* sash       = ZUIPushBox(ctx, sk, (uint32_t) strlen(sk), ZUI_Clickable);
                        sash->Size[0]      = ZFill();
                        sash->Size[1]      = ZPx(kSashH);
                        sash->EdgeSoftness = 0.f;

                        bool shot          = (ctx->HotKey == sash->Key) || (ctx->ActiveKey == sash->Key);
                        if (shot)
                            ctx->ResizeCursor = 2;

                        ZUIBox* vis       = ZUIPushBox(ctx, vk, (uint32_t) strlen(vk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                        vis->Size[0]      = ZFill();
                        vis->Size[1]      = ZPx(2.f);
                        vis->FloatPos[0]  = 0.f;
                        vis->FloatPos[1]  = (kSashH - 2.f) * 0.5f;
                        vis->EdgeSoftness = shot ? (ctx->ActiveKey == sash->Key ? 3.f : 2.f) : 0.f;
                        if (shot)
                            ZUIBoxSetColor(vis, tb[0], tb[1], tb[2], ctx->ActiveKey == sash->Key ? 0.70f : 0.50f);
                        else
                            ZUIBoxSetColor(vis, ctx->Theme.Separator[0], ctx->Theme.Separator[1], ctx->Theme.Separator[2], ctx->Theme.Separator[3]);
                        ZUIPopBox(ctx);

                        ZUISignal ssig = ZUISignalFromBox(ctx, sash);
                        ZUIPopBox(ctx);

                        if ((ssig.Flags & ZUI_SignalHeld) && fabsf(ssig.DragDelta[1]) > 0.05f)
                            m_h[di] = fmaxf(m_h[di] + ssig.DragDelta[1], kMinH);
                    }

                    // Drop zone teal fill — floated sibling after section wrapper
                    if (top_tgt || bot_tgt)
                    {
                        float fill_y = sec_top + (bot_tgt ? half_h : 0.f);
                        char  dzk[16];
                        snprintf(dzk, sizeof(dzk), "##dz%d", di);
                        ZUIDropZoneFill(ctx, dzk, 0.f, fill_y, sectW, half_h);
                    }

                    // 2px divider after section (bottom-half target)
                    if (bot_tgt)
                        ZUIDockDividerH(ctx, "##dbot");
                }

                run_y += SectionRunH(di, hdrH);
            }

            // Divider at end of list
            if (drop_chg && m_drop_slot == N)
                ZUIDockDividerH(ctx, "##dend");

            ZUIEndScrollRegion(ctx);

            // Ghost — floated after scroll region, panel-content coords.
            // cx is centered in the inspector width so the ghost never renders over other panels.
            // cy is clamped to stay within the panel height.
            if (ghost_lbl)
            {
                float panel_h = rect[3] - rect[1];
                float ghost_h = hdrH + ctx->Style.TabGhostContentH;
                float cx      = sectW * 0.5f; // centered — ghost stays inside inspector
                float cy      = ctx->MousePos[1] - rect[1];
                cy            = fmaxf(ghost_h * 0.5f, fminf(cy, panel_h - ghost_h * 0.5f));
                ZUIDockGhostHeader(ctx, "##ghost", ghost_lbl, cx, cy);
            }
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
