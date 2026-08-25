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
    void ZUIHierarchyViewComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                               cstring name, bool visibility)
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
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp) { return; }

        auto* app           = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
        auto* current_scene = reinterpret_cast<EditorScenePtr>(app->CurrentScene);
        auto* eng           = Engine::GetContext();
        if (!current_scene || !eng || !eng->ActorManager) { return; }

        if (RegionW == 0) { RegionX = 480.f; RegionY = 80.f; RegionW = 280.f; RegionH = 700.f; }

        ZUIBox* panel      = ZUIBeginColumn(ctx, "##zui_hier_panel", ZPx(RegionW), ZPx(RegionH));
        panel->Flags = panel->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY;
        panel->FloatPos[0] = RegionX;
        panel->FloatPos[1] = RegionY;
        panel->BgColor[0] = ctx->Theme.PanelBg[0]; panel->BgColor[1] = ctx->Theme.PanelBg[1];
        panel->BgColor[2] = ctx->Theme.PanelBg[2]; panel->BgColor[3] = ctx->Theme.PanelBg[3];
        panel->BorderColor[0] = ctx->Theme.PanelBorder[0];
        panel->BorderColor[1] = ctx->Theme.PanelBorder[1];
        panel->BorderColor[2] = ctx->Theme.PanelBorder[2];
        panel->BorderColor[3] = ctx->Theme.PanelBorder[3];
        panel->BorderThickness = 1.f;

        // --- Header — draggable ---
        ZUIBox* hdr = ZUIBeginRow(ctx, "##hier_hdr", ZFill(), ZPx(28.f));
        hdr->Flags  = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->BgColor[0] = ctx->Theme.HeaderBg[0]; hdr->BgColor[1] = ctx->Theme.HeaderBg[1];
        hdr->BgColor[2] = ctx->Theme.HeaderBg[2]; hdr->BgColor[3] = ctx->Theme.HeaderBg[3];
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, Name ? Name : "Hierarchy", ctx->Theme.TextDefault);
            ZUISpacer(ctx, 8.f);
            ZUISignal add_sig  = ZUIButton(ctx, "Add##hier");
            ZUISpacer(ctx, 4.f);
            ZUISignal del_sig  = ZUIButton(ctx, "Del##hier");
            ZUISignal drag_sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);
        if ((drag_sig.Flags & ZUI_SignalHeld) &&
            (drag_sig.DragDelta[0] != 0.f || drag_sig.DragDelta[1] != 0.f))
        {
            RegionX += drag_sig.DragDelta[0];
            RegionY += drag_sig.DragDelta[1];
            Detached = true;
            panel->FloatPos[0] = RegionX;
            panel->FloatPos[1] = RegionY;
        }
        if (drag_sig.Flags & ZUI_SignalDoubleClicked) { Detached = false; }
        ZUISeparator(ctx);

        // --- Scrollable actor list ---
        ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());

        // --- DFS tree build (no ImGui dependency) ---
        auto     scratch = ZGetScratch(&m_arena);
        uint32_t n       = eng->ActorManager->Count();

        struct OutlinerNode { ActorHandle Handle; EntityID EID; EntityID Parent; };
        OutlinerNode* nodes       = ZPushArray(scratch.Arena, OutlinerNode, n + 1);
        uint32_t*     first_child = ZPushArray(scratch.Arena, uint32_t,     n + 1);
        uint32_t*     next_sib    = ZPushArray(scratch.Arena, uint32_t,     n + 1);
        uint32_t      nc          = 0;

        for (uint32_t i = 0; i < n; ++i) { first_child[i] = UINT32_MAX; next_sib[i] = UINT32_MAX; }

        eng->ActorManager->ForEach([&](ActorHandle h, Actor* actor) {
            auto* pc    = actor->GetComponent<ParentComponent>();
            nodes[nc++] = {h, actor->GetEntityID(),
                           (pc && pc->Parent != INVALID_ENTITY) ? pc->Parent : INVALID_ENTITY};
        });

        ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx;
        eid_to_idx.init(scratch.Arena, nc * 2 + 1);
        for (uint32_t i = 0; i < nc; ++i)
            eid_to_idx.insert(nodes[i].EID, i);
        for (uint32_t i = 0; i < nc; ++i)
        {
            if (nodes[i].Parent == INVALID_ENTITY) continue;
            auto* pidx = eid_to_idx.find(nodes[i].Parent);
            if (!pidx) continue;
            next_sib[i]        = first_child[*pidx];
            first_child[*pidx] = i;
        }

        struct DFSEntry { uint32_t idx; int depth; };
        DFSEntry* stk = ZPushArray(scratch.Arena, DFSEntry, nc * 2 + 1);
        int32_t   sp  = 0;
        for (int32_t i = (int32_t)nc - 1; i >= 0; --i)
            if (nodes[i].Parent == INVALID_ENTITY)
                stk[sp++] = {(uint32_t)i, 0};

        // --- Scene root row ---
        {
            static const float k_sel[4]  = {0.26f, 0.44f, 0.70f, 0.50f};
            static const float k_dim[4]  = {0.55f, 0.55f, 0.60f, 1.f};

            ZUIBox* root_row = ZUIBeginRow(ctx, "##sc_root", ZFill(), ZPx(26.f));
            root_row->Flags  = root_row->Flags | ZUI_DrawBackground | ZUI_Clickable;
            root_row->BgColor[0] = 0.30f; root_row->BgColor[1] = 0.30f;
            root_row->BgColor[2] = 0.36f; root_row->BgColor[3] = 0.18f;

            // Disclosure
            const char* root_ind = m_root_open ? "v##scr" : ">##scr";
            ZUIBox* root_arrow = ZUIPushBox(ctx, root_ind, (uint32_t)Helpers::secure_strlen(root_ind),
                                            ZUI_DrawText | ZUI_Clickable);
            root_arrow->Size[0]  = ZPx(14.f);
            root_arrow->Size[1]  = ZPx(22.f);
            root_arrow->TextColor[0] = k_dim[0]; root_arrow->TextColor[1] = k_dim[1];
            root_arrow->TextColor[2] = k_dim[2]; root_arrow->TextColor[3] = k_dim[3];
            ZUISignal rarrow_sig = ZUISignalFromBox(ctx, root_arrow);
            ZUIPopBox(ctx);

            const char* scene_name = (current_scene->Name && current_scene->Name[0]) ? current_scene->Name : "Scene";
            ZUILabel(ctx, scene_name);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "World", k_dim);

            ZUISignal root_sig = ZUISignalFromBox(ctx, root_row);
            ZUIEndRow(ctx);

            if (rarrow_sig.Flags & ZUI_SignalClicked) { m_root_open = !m_root_open; }
            (void)root_sig; // scene root not selectable
        }

        // --- Actor rows (DFS) ---
        if (m_root_open)
        {
            constexpr float INDENT_W = 14.f;
            static const float k_dim[4] = {0.55f, 0.55f, 0.60f, 1.f};
            static const float k_sel[4] = {0.26f, 0.44f, 0.70f, 0.50f};

            ActorHandle pending_delete = {};
            uint32_t    total_actors   = nc;
            uint32_t    selected_count = 0;

            while (sp > 0)
            {
                DFSEntry e     = stk[--sp];
                uint32_t ni    = e.idx;
                Actor*   actor = eng->ActorManager->Access(nodes[ni].Handle);
                if (!actor) continue;

                auto*       nc_comp    = actor->GetComponent<NameComponent>();
                const char* label      = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";
                bool        is_coll    = !actor->HasComponent<MeshComponent>() &&
                                         !actor->HasComponent<LightComponent>() &&
                                         !actor->HasComponent<CameraComponent>();
                const char* type_str   = is_coll ? "Coll" :
                                         actor->HasComponent<LightComponent>()  ? "Light"  :
                                         actor->HasComponent<CameraComponent>() ? "Camera" :
                                         actor->HasComponent<MeshComponent>()   ? "Mesh"   : "Actor";
                bool        has_ch     = (first_child[ni] != UINT32_MAX);
                bool        is_open    = has_ch && !IsCollapsed(nodes[ni].EID);
                bool        selected   = (current_scene->SelectedActorHandle.Index      == nodes[ni].Handle.Index &&
                                          current_scene->SelectedActorHandle.Generation == nodes[ni].Handle.Generation);
                if (selected) ++selected_count;

                float indent = (float)(e.depth + 1) * INDENT_W;

                // Build row key
                char row_key[64];
                snprintf(row_key, sizeof(row_key), "##hr_%u_%u", nodes[ni].Handle.Index, nodes[ni].Handle.Generation);

                ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(24.f));
                row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
                if (selected) {
                    row->BgColor[0] = k_sel[0]; row->BgColor[1] = k_sel[1];
                    row->BgColor[2] = k_sel[2]; row->BgColor[3] = k_sel[3];
                }
                else {
                    // Neutral base — transparent but non-zero RGB so hover blending
                    // produces a visible highlight rather than dark gray.
                    row->BgColor[0] = 0.42f; row->BgColor[1] = 0.42f;
                    row->BgColor[2] = 0.48f; row->BgColor[3] = 0.f;
                }

                // Indent spacer
                ZUISpacer(ctx, indent);

                // Disclosure arrow or alignment spacer
                if (has_ch) {
                    char arrow_key[64];
                    const char* ind = is_open ? "v" : ">";
                    snprintf(arrow_key, sizeof(arrow_key), "%s##ar_%u_%u", ind,
                             nodes[ni].Handle.Index, nodes[ni].Handle.Generation);
                    ZUIBox* arrow     = ZUIPushBox(ctx, arrow_key, (uint32_t)Helpers::secure_strlen(arrow_key),
                                                   ZUI_DrawText | ZUI_Clickable);
                    arrow->Size[0]    = ZPx(14.f);
                    arrow->Size[1]    = ZPx(22.f);
                    arrow->TextColor[0] = k_dim[0]; arrow->TextColor[1] = k_dim[1];
                    arrow->TextColor[2] = k_dim[2]; arrow->TextColor[3] = k_dim[3];
                    ZUISignal asig    = ZUISignalFromBox(ctx, arrow);
                    ZUIPopBox(ctx);
                    if (asig.Flags & ZUI_SignalClicked) { ToggleCollapsed(nodes[ni].EID); }
                } else {
                    ZUISpacer(ctx, 14.f);
                }

                // Actor name
                ZUILabel(ctx, label);
                ZUISpacer(ctx, 4.f);
                ZUILabel(ctx, type_str, k_dim);

                ZUISignal row_sig = ZUISignalFromBox(ctx, row);
                ZUIEndRow(ctx);

                if (row_sig.Flags & ZUI_SignalClicked) {
                    current_scene->SelectedActorHandle = nodes[ni].Handle;
                }

                // Push expanded children (reverse order for correct DFS)
                if (has_ch && is_open)
                {
                    uint32_t tmp[256]; int tc = 0;
                    uint32_t c = first_child[ni];
                    while (c != UINT32_MAX && tc < 256) { tmp[tc++] = c; c = next_sib[c]; }
                    for (int ci = tc - 1; ci >= 0; --ci)
                        stk[sp++] = {tmp[ci], e.depth + 1};
                }
            }

            // Deferred delete: selected actor
            if (del_sig.Flags & ZUI_SignalClicked)
            {
                ActorHandle h     = current_scene->SelectedActorHandle;
                Actor*      actor = h.Valid() ? eng->ActorManager->Access(h) : nullptr;
                if (actor)
                {
                    auto* mc = actor->GetComponent<MeshComponent>();
                    if (mc && mc->RenderInstanceId != UINT32_MAX)
                        current_scene->RemoveMeshInstance(mc->RenderInstanceId, eng->RenderResourceManager);
                    current_scene->SelectedActorHandle = {};
                    eng->ActorManager->Destroy(h);
                }
            }

            // Add actor
            if (add_sig.Flags & ZUI_SignalClicked)
            {
                ActorHandle  new_h = eng->ActorManager->Create();
                Actor*       new_a = eng->ActorManager->Access(new_h);
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
            int  total = 0, sel = 0;
            // Recount outside scroll for simplicity
            if (eng->ActorManager) { total = (int)eng->ActorManager->Count(); }
            snprintf(status, sizeof(status), "%d actor%s", total, total == 1 ? "" : "s");
            ZUILabel(ctx, status, ctx->Theme.TextDim);
        }

        ZUIEndColumn(ctx); // end panel
    }
} // namespace Tetragrama::Components
