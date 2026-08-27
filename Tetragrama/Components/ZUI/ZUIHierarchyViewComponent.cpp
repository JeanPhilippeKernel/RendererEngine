// clang-format off
#include <Tetragrama/Components/ZUI/ZUIHierarchyViewComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
// clang-format on

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    void ZUIHierarchyViewComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
        parent->LocalArena.CreateSubArena(ZKilo(512), &m_arena);
        m_collapsed.init(&m_arena, 64);
    }

    bool ZUIHierarchyViewComponent::IsCollapsed(EntityID eid) const
    {
        for (uint32_t i = 0; i < m_collapsed.size(); ++i)
            if (m_collapsed[i] == eid)
                return true;
        return false;
    }

    void ZUIHierarchyViewComponent::ToggleCollapsed(EntityID eid)
    {
        for (uint32_t i = 0; i < m_collapsed.size(); ++i)
        {
            if (m_collapsed[i] == eid)
            {
                m_collapsed[i] = INVALID_ENTITY;
                return;
            }
        }
        m_collapsed.push(eid);
    }

    void ZUIHierarchyViewComponent::BuildUI(ZEngine::UI::ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp)
        {
            return;
        }

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        auto* eng           = Engine::GetContext();
        if (!current_scene || !eng || !eng->ActorManager)
        {
            return;
        }

        if (RegionW == 0)
        {
            RegionX = 480.f;
            RegionY = 80.f;
            RegionW = 280.f;
            RegionH = 700.f;
        }

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_hier_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags       = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        ZUIBoxSetColorArr(panel, ctx->Theme.PanelBg);
        panel->BorderColor[0]  = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1]  = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2]  = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3]  = ctx->Theme.PanelBorder[3];
        panel->BorderColor[3]  = 1.0f;
        panel->BorderThickness = 1.f;
        panel->EdgeSoftness    = 0.f;

        // --- Header — draggable ---
        ZUIBox* hdr            = ZUIBeginRow(ctx, "##hier_hdr", ZFill(), ZSPx(ctx, 28.f));
        hdr->Flags             = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        ZUIBoxSetColorArr(hdr, ctx->Theme.TitleBarBg);
        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, Name ? Name : "Hierarchy", ctx->Theme.TextDefault);
        ZUISpacer(ctx, 8.f);
        ZUISignal add_sig = ZUIButton(ctx, "Add##hier");
        ZUISpacer(ctx, 4.f);
        ZUISignal del_sig  = ZUIButton(ctx, "Del##hier");
        ZUISignal drag_sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) && (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX            += drag_sig.DragDelta[0];
            RegionY            += drag_sig.DragDelta[1];
            Detached            = true;
            panel->FloatPos[0]  = RegionX;
            panel->FloatPos[1]  = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked)
        {
            Detached = false;
        }
        ZUISeparator(ctx);

        // --- Scrollable actor list ---
        ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());

        // --- DFS tree build (no ImGui dependency) ---
        auto     scratch = ZGetScratch(&m_arena);
        uint32_t n       = eng->ActorManager->Count();

        struct OutlinerNode
        {
            ActorHandle Handle;
            EntityID    EID;
            EntityID    Parent;
        };
        OutlinerNode* nodes       = ZPushArray(scratch.Arena, OutlinerNode, n + 1);
        uint32_t*     first_child = ZPushArray(scratch.Arena, uint32_t, n + 1);
        uint32_t*     next_sib    = ZPushArray(scratch.Arena, uint32_t, n + 1);
        uint32_t      nc          = 0;

        for (uint32_t i = 0; i < n; ++i)
        {
            first_child[i] = UINT32_MAX;
            next_sib[i]    = UINT32_MAX;
        }

        eng->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
            auto* pc    = actor->GetComponent<ParentComponent>();
            nodes[nc++] = {h, actor->GetEntityID(), (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
        });

        ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx;
        eid_to_idx.init(scratch.Arena, nc * 2 + 1);
        for (uint32_t i = 0; i < nc; ++i)
            eid_to_idx.insert(nodes[i].EID, i);
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

        struct DFSEntry
        {
            uint32_t idx;
            int      depth;
        };
        DFSEntry* stk = ZPushArray(scratch.Arena, DFSEntry, nc * 2 + 1);
        int32_t   sp  = 0;
        for (int32_t i = (int32_t) nc - 1; i >= 0; --i)
            if (nodes[i].Parent == INVALID_ENTITY)
                stk[sp++] = {(uint32_t) i, 0};

        // --- Type icon color tables ---
        static const float k_icon_light[4]   = {1.0f, 0.85f, 0.20f, 1.f};
        static const float k_icon_camera[4]  = {0.45f, 0.85f, 0.55f, 1.f};
        static const float k_icon_mesh[4]    = {0.55f, 0.75f, 0.90f, 1.f};
        static const float k_icon_coll[4]    = {0.85f, 0.65f, 0.15f, 1.f};
        static const float k_icon_default[4] = {0.55f, 0.55f, 0.60f, 1.f};
        (void) k_icon_default;

        // --- Scene root row ---
        {
            static const float k_dim[4] = {0.55f, 0.55f, 0.60f, 1.f};

            ZUIBox*            root_row = ZUIBeginRow(ctx, "##sc_root", ZFill(), ZSPx(ctx, 26.f));
            root_row->Flags             = root_row->Flags | ZUI_DrawBackground | ZUI_Clickable;
            ZUIBoxSetColor(root_row, 0.30f, 0.30f, 0.36f, 0.18f);

            // Disclosure
            const char* root_ind     = m_root_open ? "v##scr" : ">##scr";
            ZUIBox*     root_arrow   = ZUIPushBox(ctx, root_ind, (uint32_t) Helpers::secure_strlen(root_ind), ZUI_DrawText | ZUI_Clickable);
            root_arrow->Size[0]      = ZPx(14.f);
            root_arrow->Size[1]      = ZPx(22.f);
            root_arrow->TextColor[0] = k_dim[0];
            root_arrow->TextColor[1] = k_dim[1];
            root_arrow->TextColor[2] = k_dim[2];
            root_arrow->TextColor[3] = k_dim[3];
            ZUISignal rarrow_sig     = ZUISignalFromBox(ctx, root_arrow);
            ZUIPopBox(ctx);

            // World type icon
            {
                static const float k_icon_world[4] = {0.35f, 0.80f, 0.45f, 1.f};
                ZUIBox*            icon            = ZUIPushBox(ctx, "W##ti_root", 10, ZUI_DrawBackground | ZUI_DrawText);
                icon->Size[0]                      = ZPx(14.f);
                icon->Size[1]                      = ZPx(14.f);
                ZUIBoxSetColorArr(icon, k_icon_world);
                icon->TextColor[0] = 1.f;
                icon->TextColor[1] = 1.f;
                icon->TextColor[2] = 1.f;
                icon->TextColor[3] = 1.f;
                ZUIPopBox(ctx);
            }
            ZUISpacer(ctx, 4.f);

            const char* scene_name = (current_scene->Name && current_scene->Name[0]) ? current_scene->Name : "Scene";
            ZUILabel(ctx, scene_name);

            ZUISignal root_sig = ZUISignalFromBox(ctx, root_row);
            ZUIEndRow(ctx);

            if (rarrow_sig.Flags & ZUI_SignalClicked)
            {
                m_root_open = !m_root_open;
            }
            (void) root_sig; // scene root not selectable
        }

        // --- Actor rows (DFS) ---
        if (m_root_open)
        {
            constexpr float    INDENT_W                = 14.f;
            static const float k_dim[4]                = {0.55f, 0.55f, 0.60f, 1.f};
            static const float k_sel[4]                = {0.26f, 0.44f, 0.70f, 0.50f};

            ActorHandle        pending_delete          = {};
            ActorHandle        pending_duplicate       = {};
            ActorHandle        pending_reparent_child  = {};
            ActorHandle        pending_reparent_parent = {};

            while (sp > 0)
            {
                DFSEntry e     = stk[--sp];
                uint32_t ni    = e.idx;
                Actor*   actor = eng->ActorManager->Access(nodes[ni].Handle);
                if (!actor)
                    continue;

                auto*        nc_comp = actor->GetComponent<NameComponent>();
                const char*  label   = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";

                // Determine type icon character and color
                char         type_char;
                const float* type_bg;
                if (actor->HasComponent<LightComponent>())
                {
                    type_char = 'L';
                    type_bg   = k_icon_light;
                }
                else if (actor->HasComponent<CameraComponent>())
                {
                    type_char = 'C';
                    type_bg   = k_icon_camera;
                }
                else if (actor->HasComponent<MeshComponent>())
                {
                    type_char = 'M';
                    type_bg   = k_icon_mesh;
                }
                else
                {
                    type_char = '+';
                    type_bg   = k_icon_coll;
                }

                bool  has_ch        = (first_child[ni] != UINT32_MAX);
                bool  is_open       = has_ch && !IsCollapsed(nodes[ni].EID);
                bool  selected      = (current_scene->SelectedActorHandle.Index == nodes[ni].Handle.Index && current_scene->SelectedActorHandle.Generation == nodes[ni].Handle.Generation);

                float indent        = (float) (e.depth + 1) * INDENT_W;

                bool  renaming_this = (m_renaming_handle.Valid() && m_renaming_handle.Index == nodes[ni].Handle.Index && m_renaming_handle.Generation == nodes[ni].Handle.Generation);

                // Build row key
                char  row_key[64];
                snprintf(row_key, sizeof(row_key), "##hr_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);

                ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZSPx(ctx, 24.f));
                row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                if (selected)
                {
                    ZUIBoxSetColorArr(row, k_sel);
                }
                else
                {
                    // Neutral base — transparent but non-zero RGB so hover blending
                    // produces a visible highlight rather than dark gray.
                    ZUIBoxSetColor(row, 0.42f, 0.42f, 0.48f, 0.f);
                }

                // Indent spacer
                ZUISpacer(ctx, indent);

                // Disclosure arrow or alignment spacer
                if (has_ch)
                {
                    char        arrow_key[64];
                    const char* ind = is_open ? "v" : ">";
                    snprintf(arrow_key, sizeof(arrow_key), "%s##ar_%u_%u", ind, nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                    ZUIBox* arrow       = ZUIPushBox(ctx, arrow_key, (uint32_t) Helpers::secure_strlen(arrow_key), ZUI_DrawText | ZUI_Clickable);
                    arrow->Size[0]      = ZPx(14.f);
                    arrow->Size[1]      = ZPx(22.f);
                    arrow->TextColor[0] = k_dim[0];
                    arrow->TextColor[1] = k_dim[1];
                    arrow->TextColor[2] = k_dim[2];
                    arrow->TextColor[3] = k_dim[3];
                    ZUISignal asig      = ZUISignalFromBox(ctx, arrow);
                    ZUIPopBox(ctx);
                    if (asig.Flags & ZUI_SignalClicked)
                    {
                        ToggleCollapsed(nodes[ni].EID);
                    }
                }
                else
                {
                    ZUISpacer(ctx, 14.f);
                }

                // Type icon — 14x14 colored box with a 1-char label
                {
                    char icon_key[32];
                    snprintf(icon_key, sizeof(icon_key), "%c##ti_%u_%u", type_char, nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                    ZUIBox* icon  = ZUIPushBox(ctx, icon_key, (uint32_t) Helpers::secure_strlen(icon_key), ZUI_DrawBackground | ZUI_DrawText);
                    icon->Size[0] = ZPx(14.f);
                    icon->Size[1] = ZPx(14.f);
                    ZUIBoxSetColorArr(icon, type_bg);
                    icon->TextColor[0] = 1.f;
                    icon->TextColor[1] = 1.f;
                    icon->TextColor[2] = 1.f;
                    icon->TextColor[3] = 1.f;
                    ZUIPopBox(ctx);
                }
                ZUISpacer(ctx, 4.f);

                // Actor name label or inline rename TextField
                if (renaming_this)
                {
                    char tf_key[64];
                    snprintf(tf_key, sizeof(tf_key), "##ren_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                    uint64_t focus_before = ctx->FocusKey;
                    ZUITextField(ctx, tf_key, m_rename_buf, sizeof(m_rename_buf), 150.f);
                    uint64_t focus_after = ctx->FocusKey;

                    if (m_rename_started)
                    {
                        // First frame after rename triggered: suppress commit check.
                        // User must click the field to focus it.
                        m_rename_started   = false;
                        m_rename_focus_key = 0;
                    }
                    else if (m_rename_focus_key != 0 && focus_after != m_rename_focus_key)
                    {
                        // TextField had focus and FocusKey changed — commit rename
                        auto* nc_ren = actor->GetComponent<NameComponent>();
                        if (nc_ren && m_rename_buf[0])
                        {
                            Helpers::secure_strncpy(nc_ren->Value, sizeof(nc_ren->Value), m_rename_buf, sizeof(m_rename_buf) - 1);
                        }
                        m_renaming_handle  = {};
                        m_rename_focus_key = 0;
                    }
                    // Detect when the TextField first receives focus (user clicks it)
                    if (focus_before != focus_after && focus_after != 0)
                    {
                        m_rename_focus_key = focus_after;
                    }
                }
                else
                {
                    ZUILabel(ctx, label);
                }

                ZUISignal row_sig = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);

                // Single-click: select actor
                if (row_sig.Flags & ZUI_SignalClicked)
                {
                    current_scene->SelectedActorHandle = nodes[ni].Handle;
                }

                // Double-click: begin inline rename
                if (!renaming_this && (row_sig.Flags & ZUI_SignalDoubleClicked))
                {
                    m_renaming_handle  = nodes[ni].Handle;
                    m_rename_started   = true;
                    m_rename_focus_key = 0;
                    if (nc_comp && nc_comp->Value[0])
                    {
                        Helpers::secure_strncpy(m_rename_buf, sizeof(m_rename_buf), nc_comp->Value, sizeof(nc_comp->Value));
                    }
                    else
                    {
                        m_rename_buf[0] = '\0';
                    }
                }

                // Right-click context menu
                if (ZUIBeginPopupContextItem(ctx, "##actor_ctx", row_sig))
                {
                    if (ZUIMenuItem(ctx, "Rename"))
                    {
                        m_renaming_handle  = nodes[ni].Handle;
                        m_rename_started   = true;
                        m_rename_focus_key = 0;
                        if (nc_comp && nc_comp->Value[0])
                        {
                            Helpers::secure_strncpy(m_rename_buf, sizeof(m_rename_buf), nc_comp->Value, sizeof(nc_comp->Value));
                        }
                        else
                        {
                            m_rename_buf[0] = '\0';
                        }
                    }
                    if (ZUIMenuItem(ctx, "Delete"))
                    {
                        pending_delete = nodes[ni].Handle;
                    }
                    if (ZUIMenuItem(ctx, "Duplicate Actor"))
                    {
                        pending_duplicate = nodes[ni].Handle;
                    }
                    ZUIEndPopup(ctx);
                }

                // Drag source: broadcast this actor's handle as the drag payload
                ZUIBeginDragSource(ctx, row, (const char*) &nodes[ni].Handle, sizeof(ActorHandle));

                // Drop target: accept a dragged actor handle for reparenting
                char drop_buf[sizeof(ActorHandle)] = {};
                if (ZUIAcceptDrop(ctx, row, drop_buf, sizeof(drop_buf)))
                {
                    ActorHandle dragged = {};
                    Helpers::secure_memcpy(&dragged, sizeof(dragged), drop_buf, sizeof(drop_buf));
                    if (dragged.Valid() && (dragged.Index != nodes[ni].Handle.Index || dragged.Generation != nodes[ni].Handle.Generation))
                    {
                        pending_reparent_child  = dragged;
                        pending_reparent_parent = nodes[ni].Handle;
                    }
                }

                // Push expanded children (reverse order for correct DFS)
                if (has_ch && is_open)
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

            // --- Deferred mutations (applied after DFS to avoid tree invalidation) ---

            // Reparent: assign new ParentComponent to the dragged actor
            if (pending_reparent_child.Valid() && pending_reparent_parent.Valid())
            {
                Actor* child_actor  = eng->ActorManager->Access(pending_reparent_child);
                Actor* parent_actor = eng->ActorManager->Access(pending_reparent_parent);
                if (child_actor && parent_actor)
                {
                    EntityID new_parent_eid = parent_actor->GetEntityID();
                    auto*    pc             = child_actor->GetComponent<ParentComponent>();
                    if (pc)
                    {
                        pc->Parent = new_parent_eid;
                    }
                    else
                    {
                        ParentComponent new_pc = {};
                        new_pc.Parent          = new_parent_eid;
                        child_actor->AddComponent<ParentComponent>(new_pc);
                    }
                }
            }

            // Duplicate: create a new actor copying Name and Transform from the source
            if (pending_duplicate.Valid())
            {
                Actor* src = eng->ActorManager->Access(pending_duplicate);
                if (src)
                {
                    ActorHandle dup_h = eng->ActorManager->Create();
                    Actor*      dup_a = eng->ActorManager->Access(dup_h);
                    if (dup_a)
                    {
                        auto*         nc_src = src->GetComponent<NameComponent>();
                        NameComponent nc_dup = {};
                        if (nc_src && nc_src->Value[0])
                        {
                            Helpers::secure_strncpy(nc_dup.Value, sizeof(nc_dup.Value), nc_src->Value, sizeof(nc_src->Value));
                            uint32_t name_len = (uint32_t) Helpers::secure_strlen(nc_dup.Value);
                            if (name_len + 5 < sizeof(nc_dup.Value))
                            {
                                Helpers::secure_strncpy(nc_dup.Value + name_len, sizeof(nc_dup.Value) - name_len, " Copy", 5);
                            }
                        }
                        else
                        {
                            Helpers::secure_strncpy(nc_dup.Value, sizeof(nc_dup.Value), "Actor Copy", 10);
                        }
                        dup_a->AddComponent<NameComponent>(nc_dup);
                        auto* tc_src = src->GetComponent<TransformComponent>();
                        if (tc_src)
                        {
                            dup_a->AddComponent<TransformComponent>(*tc_src);
                        }
                        else
                        {
                            dup_a->AddComponent<TransformComponent>({});
                        }
                        current_scene->SelectedActorHandle = dup_h;
                    }
                }
            }

            // Context-menu delete
            if (pending_delete.Valid())
            {
                Actor* del_actor = eng->ActorManager->Access(pending_delete);
                if (del_actor)
                {
                    auto* mc = del_actor->GetComponent<MeshComponent>();
                    if (mc && mc->RenderInstanceId != UINT32_MAX)
                        current_scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                    if (current_scene->SelectedActorHandle.Index == pending_delete.Index && current_scene->SelectedActorHandle.Generation == pending_delete.Generation)
                        current_scene->SelectedActorHandle = {};
                    if (m_renaming_handle.Index == pending_delete.Index && m_renaming_handle.Generation == pending_delete.Generation)
                    {
                        m_renaming_handle  = {};
                        m_rename_focus_key = 0;
                    }
                    eng->ActorManager->Destroy(pending_delete);
                }
            }

            // Header Del button: delete the currently selected actor
            if (del_sig.Flags & ZUI_SignalClicked)
            {
                ActorHandle h     = current_scene->SelectedActorHandle;
                Actor*      actor = h.Valid() ? eng->ActorManager->Access(h) : nullptr;
                if (actor)
                {
                    auto* mc = actor->GetComponent<MeshComponent>();
                    if (mc && mc->RenderInstanceId != UINT32_MAX)
                        current_scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                    if (m_renaming_handle.Index == h.Index && m_renaming_handle.Generation == h.Generation)
                    {
                        m_renaming_handle  = {};
                        m_rename_focus_key = 0;
                    }
                    current_scene->SelectedActorHandle = {};
                    eng->ActorManager->Destroy(h);
                }
            }

            // Add actor
            if (add_sig.Flags & ZUI_SignalClicked)
            {
                ActorHandle new_h = eng->ActorManager->Create();
                Actor*      new_a = eng->ActorManager->Access(new_h);
                if (new_a)
                {
                    NameComponent nc_new = {};
                    Helpers::secure_strncpy(nc_new.Value, sizeof(nc_new.Value), "Actor", 5);
                    new_a->AddComponent<NameComponent>(nc_new);
                    new_a->AddComponent<TransformComponent>({});
                    current_scene->SelectedActorHandle = new_h;
                }
            }
        }

        ZUIEndScrollRegion(ctx); // end scrollable actor list
        ZReleaseScratch(scratch);

        // Status bar — always visible outside the scroll region
        ZUISeparator(ctx);
        {
            char status[64];
            int  total = 0;
            if (eng->ActorManager)
            {
                total = (int) eng->ActorManager->Count();
            }
            snprintf(status, sizeof(status), "%d actor%s", total, total == 1 ? "" : "s");
            ZUILabel(ctx, status, ctx->Theme.TextDim);
        }

        ZUIEndColumn(ctx); // end panel
    }
} // namespace Tetragrama::Components
