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
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/Rendering/Cameras/FlyCamera.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>

using namespace ZEngine;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Helpers;
using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    // Case-insensitive three-way compare used by the sibling sort.
    static int ZUICompareCI(const char* a, const char* b)
    {
        while (*a && *b)
        {
            const int d = tolower((unsigned char) *a) - tolower((unsigned char) *b);
            if (d != 0)
            {
                return d;
            }
            ++a;
            ++b;
        }
        return tolower((unsigned char) *a) - tolower((unsigned char) *b);
    }

    static bool ContainsCI(const char* haystack, const char* needle)
    {
        if (!haystack)
        {
            return false;
        }
        if (!needle[0])
        {
            return true;
        }
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
            {
                return true;
            }
        }
        return false;
    }

    static uint64_t RowIdentity(uint64_t epoch, EntityID entity)
    {
        char key[80] = {};
        snprintf(key, sizeof(key), "##entity_%llu_%u_%u", static_cast<unsigned long long>(epoch), entity.Index, entity.Generation);
        return ZUIHashStr(key, static_cast<uint32_t>(secure_strlen(key)));
    }

    static void TypeLabel(ZUIContext* ctx, const char* text)
    {
        // Match the header's text inset and the actor label's row center.
        // Text is vertically centered by the renderer; no per-row wrapper or
        // text measurement is needed for this fill-sized label.
        auto* label    = ZUIPushBox(ctx, "##hier_type", 11, ZUI_DrawText);
        label->Size[0] = label->Size[1] = ZFill();
        label->Padding[0]               = ctx->Style.FramePadding[0];
        label->Label                    = ZUIPushStr(&ctx->FrameArena, text, static_cast<uint32_t>(secure_strlen(text)));
        secure_memcpy(label->TextColor, sizeof(label->TextColor), ctx->Theme.TextDefault, sizeof(label->TextColor));
        ZUIPopBox(ctx);
    }

    HierarchyPanel::HierarchyPanel()
    {
        Title = "Outliner";
        Icon  = (int) ZEngine::UI::ZUIIcon::ListTree;
    }

    bool HierarchyPanel::IsCollapsed(ZEngine::ECS::EntityID id) const
    {
        for (int i = 0; i < m_ncollapsed; ++i)
        {
            if (m_collapsed[i] == id)
            {
                return true;
            }
        }
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
        {
            m_collapsed[m_ncollapsed++] = id;
        }
    }

    void HierarchyPanel::BuildContent(ZUIContext* ctx, float rect[4])
    {
        // Actor wrappers are optional; all authored entities come from the ECS.
        if (!m_layer || !m_layer->CurrentApp)
        {
            EmptyPanelBg(ctx, "##hier_empty", ctx->Theme.PanelBg, nullptr);
            return;
        }
        auto* app   = reinterpret_cast<Tetragrama::EditorPtr>(m_layer->CurrentApp);
        auto* scene = reinterpret_cast<Tetragrama::EditorScenePtr>(app->CurrentScene);
        auto* eng   = Engine::GetContext();
        if (!scene || !eng || !(scene->Session ? scene->Session->Scene() : eng->Scene))
        {
            EmptyPanelBg(ctx, "##hier_empty", ctx->Theme.PanelBg, nullptr);
            return;
        }

        const float pw = (rect[2] - rect[0] > 1.f) ? rect[2] - rect[0] : 200.f;
        BuildHierarchy(ctx, pw, rect[3] - rect[1], app, scene, eng);
    }

    void HierarchyPanel::BuildHierarchy(ZUIContext* ctx, float pw, float height, Editor* app, EditorScene* scene, EngineContext* eng)
    {
        auto* ecs = scene->Session ? scene->Session->Scene() : eng->Scene;
        if (!ecs)
        {
            return;
        }
        const uint64_t epoch        = scene->Session ? scene->Session->SceneEpoch() : 0;
        const bool     view_changed = m_view_scene != ecs || m_view_epoch != epoch;
        if (view_changed)
        {
            // A replacement scene can reuse every runtime ID. Retire panel-local
            // interaction state rather than applying old rename/drag state to it.
            if (m_rename_fkey && ctx->FocusKey == m_rename_fkey)
            {
                ctx->FocusKey = 0;
            }
            m_rename_id        = {};
            m_rename_target    = {};
            m_rename_fkey      = 0;
            m_rename_started   = false;
            m_dragging_target  = {};
            m_drag_label[0]    = '\0';
            m_ncollapsed       = 0;
            m_last_click_idx   = UINT64_MAX;
            m_selection_anchor = {};
            m_view_scene       = ecs;
            m_view_epoch       = epoch;
        }
        ZUIBox* bg = ZUIBeginColumn(ctx, "##hier_bg", ZFill(), ZFill());
        bg->Flags  = bg->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
        bg->EdgeSoftness = 0.f;

        // Figma "Outliner search": 41 px band, 8 px padding, search field then a 16 px filter icon.
        {
            ZUIBox* tb     = ZUIBeginRow(ctx, "##hier_tb", ZFill(), ZPx(41.f));
            tb->Padding[0] = tb->Padding[2] = 8.f;
            tb->Gap                         = 8.f;
            tb->CrossAlign                  = ZUIAlign::Center;
            ZUISearchBox(ctx, "##hier_search", m_search, sizeof(m_search), "Search actors", ZFill());
            // "New collection": 25 px bordered icon button (Figma node 39:980).
            ZUIBox* nb          = ZUIBeginRow(ctx, "##hier_new_collection", ZPx(25.f), ZPx(25.f));
            nb->Flags           = nb->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_KeyboardActivate;
            nb->MainAlign       = ZUIAlign::Center;
            nb->CrossAlign      = ZUIAlign::Center;
            nb->BorderThickness = 1.f;
            ZUIBoxSetCornerRadius(nb, 4.f);
            ZUIBoxSetColorArr(nb, ctx->Theme.InputBg);
            ZEngine::Helpers::secure_memcpy(nb->BorderColor, sizeof(nb->BorderColor), ctx->Theme.PanelBorder, sizeof(nb->BorderColor));
            ZUIIconBox(ctx, ZUIIcon::FolderPlus, 13.f, ctx->Theme.TextDim);
            ZUISignal nsig = ZUISignalFromBox(ctx, nb);
            ZUIBoxSetColorArr(nb, ctx->HotKey == nb->Key ? ctx->Theme.ButtonHoveredBg : ctx->Theme.InputBg);
            ZUIEndRow(ctx);
            if (nsig.Flags & ZUI_SignalClicked)
            {
                if (scene->Session)
                {
                    if (!scene->Session->RequestCollection("Collection"))
                        ZENGINE_CORE_WARN("[Outliner] The collection request could not be queued.")
                }
                else if (eng->ActorManager)
                {
                    scene->CreateActor(*eng->ActorManager, "Collection");
                }
            }
            ZUIIconBox(ctx, ZUIIcon::ListFilter, 16.f);
            ZUIEndRow(ctx);
        }

        // Two-column table — Type is fixed; Actor Label stretches.
        static constexpr float kTypeW     = 126.f; // Figma: Type text starts 258 px into the 380 px panel (4 px cell padding)
        static constexpr float kLabelMinW = 180.f; // Item Label never compresses below this
        ZUIDataTableColumn     cols[2]    = {
            {"Actor Label", fmaxf(pw - kTypeW, kLabelMinW), kLabelMinW, true, false},
            {"Type", kTypeW, 0.f, true, false},
        };

        // O(n) tree construction and traversal, plus O(n log n) sorting.
        const uint32_t entity_total = ecs->AliveCount();
        const uint32_t cap          = std::max(entity_total, 1u);

        struct OutlinerNode
        {
            ActorHandle Handle     = {};
            EntityID    EID        = {};
            EntityID    Parent     = INVALID_ENTITY;
            uint32_t    TreeParent = UINT32_MAX; // display-only fallback for malformed graphs
            const char* Name       = "Actor";    // sort key (column 0)
            int         TypeRank   = {};         // sort key (column 1): 0 Collection, 1 Light, 2 Camera, 3 Static Mesh, 4 Actor
            bool        Emitted    = false;
        };
        OutlinerNode* nodes       = ZPushArray<OutlinerNode>(&ctx->FrameArena, cap);
        uint32_t*     first_child = ZPushArray<uint32_t>(&ctx->FrameArena, cap);
        uint32_t*     next_sib    = ZPushArray<uint32_t>(&ctx->FrameArena, cap);
        uint32_t      node_count  = 0;

        for (uint32_t i = 0; i < cap; ++i)
        {
            first_child[i] = UINT32_MAX;
            next_sib[i]    = UINT32_MAX;
        }
        ecs->ForEachEntity([&](EntityID entity) {
            const auto* pc        = ecs->GetComponent<ParentComponent>(entity);
            const auto* nm        = ecs->GetComponent<NameComponent>(entity);
            const bool  is_light  = ecs->HasComponent<LightComponent>(entity);
            const bool  is_cam    = ecs->HasComponent<CameraComponent>(entity);
            const bool  is_mesh   = ecs->HasComponent<MeshComponent>(entity);
            const int   type_rank = (!is_mesh && !is_light && !is_cam) ? 0 : (is_light ? 1 : (is_cam ? 2 : 3));
            nodes[node_count++]   = {{}, entity, pc ? pc->Parent : INVALID_ENTITY, UINT32_MAX, (nm && nm->Value[0]) ? nm->Value : "Actor", type_rank};
        });

        // Icon / type colors
        static const float kDim[4]                 = {0.55f, 0.55f, 0.60f, 1.f};

        // Deferred mutations — applied after DFS to avoid tree invalidation
        EntityID           pending_delete          = {};
        EntityID           pending_reset_xform     = {};
        EntityID           pending_reparent_child  = {};
        EntityID           pending_reparent_parent = INVALID_ENTITY;
        bool               drop_handled            = false; // prevents dual-path double-mutation
        // Detach-to-root (World drop + "Remove from Parent" menu) routes through
        // pending_reparent_parent = INVALID_ENTITY — single path, exact develop.

        // Scroll region + DataTable
        auto*              hierarchy_list          = ZUIBeginScrollRegion(ctx, "##hier_scroll", ZFill(), ZFill());
        hierarchy_list->Flags                      = hierarchy_list->Flags | ZUI_NavList;
        // Filtering/collapsing can shrink the list below its previous scroll
        // destination. Do not resume that stale destination when rows return.
        if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, hierarchy_list->Key))
        {
            state->ScrollYTarget = std::clamp(state->ScrollYTarget, 0.f, fmaxf(state->MaxScrollY, 0.f));
        }
        // The scroll viewport fills the panel; its table must retain the full
        // row height so overflow is measurable instead of clipped internally.
        ZUIBeginDataTable(ctx, "##hier_tbl", 2, cols, ZFit());
        // Item Label always fills remaining space — override stored width so
        // panel resize never squeezes all columns simultaneously.
        if (ctx->DT_ColWidths && ctx->DT_ColCount >= 2)
        {
            ctx->DT_ColWidths[0] = fmaxf(pw - ctx->DT_ColWidths[1], cols[0].MinWidth);
        }

        // Sibling sort (Figma "Actor Label ↑"): unsorted state behaves as Actor Label ascending.
        // The table state is loaded by ZUIBeginDataTable, so the tree is linked after it.
        const int  sort_col = m_sort_col;
        const bool sort_asc = m_sort_asc;
        // Actor iteration is handle-index ordered. An explicit tie-break retains
        // that order without stable_sort's per-frame temporary heap allocation.
        std::sort(nodes, nodes + node_count, [&](const OutlinerNode& l, const OutlinerNode& r) {
            int c = (sort_col == 1) ? (l.TypeRank - r.TypeRank) : 0;
            if (c == 0)
            {
                c = ZUICompareCI(l.Name, r.Name);
            }
            if (c == 0)
            {
                return l.EID.Index < r.EID.Index;
            }
            return sort_asc ? c < 0 : c > 0;
        });

        // O(1) parent lookup — exact develop UnorderedHashMap pattern
        ZEngine::Core::Containers::UnorderedHashMap<EntityID, uint32_t> eid_to_idx = {};
        // init rounds up to a power of two (minimum 16). Exactly twice the
        // live count already satisfies its <= 50% load contract; adding one
        // doubles the allocation whenever the actor count is a power of two.
        eid_to_idx.init(&ctx->FrameArena, node_count * 2);
        for (uint32_t i = 0; i < node_count; ++i)
        {
            eid_to_idx.insert(nodes[i].EID, i);
        }
        // Compatibility adapters are optional and never determine membership,
        // widget identity, traversal order or the session's selected target.
        if (eng->ActorManager)
        {
            eng->ActorManager->ForEach([&](ActorHandle handle, Actor* actor) {
                if (const auto* index = eid_to_idx.find(actor->GetEntityID()))
                {
                    nodes[*index].Handle = handle;
                }
            });
        }
        const auto select_node  = [&](const OutlinerNode& node, SelectionMode mode = SelectionMode::Replace) { return scene->Session ? scene->Session->RequestSelection(node.EID, mode) : eng->ActorManager && scene->SelectActor(*eng->ActorManager, node.Handle); };
        const auto drag_target  = [&](EntityID entity) { return scene->Session ? scene->Session->TargetFor(entity) : EntityTarget{{}, entity, 0}; };
        const auto resolve_drag = [&](const EntityTarget& target) {
            const auto entity = scene->Session ? scene->Session->Resolve(target) : target.SceneEpoch == 0 ? target.CachedEntity : INVALID_ENTITY;
            return entity == target.CachedEntity && ecs->IsAlive(entity) ? entity : INVALID_ENTITY;
        };

        // Resolve the original target even when it is filtered/collapsed or no
        // longer selected. Never apply its buffer to another actor.
        const auto finish_rename = [&](bool commit) {
            const auto* index = commit && m_rename_buf[0] ? eid_to_idx.find(m_rename_id) : nullptr;
            auto*       name  = index ? ecs->GetComponent<NameComponent>(nodes[*index].EID) : nullptr;
            if (name && secure_strcmp(name->Value, m_rename_buf) != 0)
            {
                if (scene->Session)
                {
                    if (!scene->Session->RequestRename(m_rename_target, m_rename_original, m_rename_buf))
                        ZENGINE_CORE_WARN("[Outliner] The rename request could not be queued.")
                }
                else if (secure_strcpy(name->Value, sizeof(name->Value), m_rename_buf) >= 0)
                {
                    scene->MarkDirty(true);
                }
            }
            if (m_rename_fkey && ctx->FocusKey == m_rename_fkey)
            {
                ctx->FocusKey = 0;
            }
            m_rename_id      = {};
            m_rename_target  = {};
            m_rename_fkey    = 0;
            m_rename_started = false;
        };
        const auto begin_rename = [&](EntityID id, const NameComponent* name) {
            if (m_rename_id.IsValid())
            {
                finish_rename(true);
            }
            secure_strcpy(m_rename_buf, sizeof(m_rename_buf), name ? name->Value : "");
            secure_strcpy(m_rename_original, sizeof(m_rename_original), m_rename_buf);
            m_rename_target  = drag_target(id);
            m_rename_id      = id;
            m_rename_started = true;
        };
        if (m_rename_id.IsValid() && !eid_to_idx.find(m_rename_id))
        {
            finish_rename(false);
        }
        const EntityID rename_on_entry = m_rename_id;
        bool           rename_drawn    = false;

        // Iterative three-color parent walks visit each entity at most twice.
        // Missing/stale links display at the root; break one display edge per
        // cycle so malformed authored data remains visible and repairable. Never
        // rewrite authored parents merely to make the Outliner a forest.
        auto*          topology_state  = ZPushArray<uint8_t>(&ctx->FrameArena, cap);
        std::fill_n(topology_state, node_count, uint8_t{0});
        for (uint32_t i = 0; i < node_count; ++i)
        {
            if (const auto* parent = eid_to_idx.find(nodes[i].Parent))
            {
                nodes[i].TreeParent = *parent;
            }
        }
        for (uint32_t i = 0; i < node_count; ++i)
        {
            uint32_t walk = i;
            while (walk != UINT32_MAX && topology_state[walk] == 0)
            {
                topology_state[walk] = 1;
                walk                 = nodes[walk].TreeParent;
            }
            const uint32_t cycle = walk != UINT32_MAX && topology_state[walk] == 1 ? walk : UINT32_MAX;
            for (walk = i; walk != UINT32_MAX && topology_state[walk] == 1; walk = nodes[walk].TreeParent)
            {
                topology_state[walk] = 2;
            }
            if (cycle != UINT32_MAX)
            {
                nodes[cycle].TreeParent = UINT32_MAX;
            }
        }

        // Reverse-sorted child links can be pushed directly onto the LIFO
        // traversal stack: they are then visited in sorted order without a
        // fixed-size temporary child array or a second reversal pass.
        for (uint32_t i = 0; i < node_count; ++i)
        {
            const uint32_t parent = nodes[i].TreeParent;
            if (parent == UINT32_MAX)
            {
                continue;
            }
            next_sib[i]         = first_child[parent];
            first_child[parent] = i;
        }

        // DFS stack — roots pushed backward so first root is processed first (exact develop)
        struct DFSEntry
        {
            uint32_t idx   = {};
            int      depth = {};
        };
        // Each actor has at most one parent, so pending entries never exceed
        // the actor count. Entries are fully assigned before they are read.
        DFSEntry* stk = ZPushArray<DFSEntry>(&ctx->FrameArena, cap);
        int       sp  = 0;
        for (int i = (int) node_count - 1; i >= 0; --i)
        {
            if (nodes[i].TreeParent == UINT32_MAX)
            {
                stk[sp++] = {(uint32_t) i, 0};
            }
        }

        // Custom header — UE5 style: bright TextDefault labels, eye icon, sort arrows
        // ctx->DT_ColWidths is populated by ZUIBeginDataTable above.
        {
            static const char* kColNames[] = {"Actor Label", "Type"};
            (void) 0;
            ZUIBox* hrow     = ZUIBeginRow(ctx, "##hier_hdr", ZFill(), ZPx(24.f));
            hrow->CrossAlign = ZUIAlign::Center;
            hrow->Flags      = hrow->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(hrow, ctx->Theme.TableHeaderBg);
            hrow->EdgeSoftness = 0.f;

            // Column header cells — each cell is ZPx(cw) wide starting at x=0,
            // exactly matching data column cell boundaries (no prefix offset).
            // Eye icon lives INSIDE column 0 (same as develop branch).
            for (int ci = 0; ci < 2; ++ci)
            {
                float       cw        = ctx->DT_ColWidths ? ctx->DT_ColWidths[ci] : cols[ci].InitWidth;
                const char* lbl       = kColNames[ci];
                uint32_t    llen      = (uint32_t) ZEngine::Helpers::secure_strlen(lbl);
                bool        is_sc     = (m_sort_col == ci);

                // Cell container (X-axis row inside the header row)
                char        cellk[32] = {};
                snprintf(cellk, sizeof(cellk), "##hcell_%d", ci);
                ZUIBox* cell_row     = ZUIBeginRow(ctx, cellk, ZPx(cw), ZPx(24.f));
                cell_row->CrossAlign = ZUIAlign::Center;

                // Column 0: decorative eye icon (no ZUI_Clickable — not yet interactive).
                // ZUIStateGetOrInsert is required to set UserData for the icon renderer.
                if (ci == 0)
                {
                    ZUISpacer(ctx, 10.f);
                    ZUIIconBox(ctx, ZUIIcon::Eye, 12.f, ctx->Theme.TextDim);
                    ZUISpacer(ctx, 9.f);
                }
                else
                {
                    ZUISpacer(ctx, 4.f);
                }

                // Label — blue if sort column, bright otherwise
                ZUIBox* lbox  = ZUIPushBox(ctx, lbl, llen, ZUI_DrawText | ZUI_Clickable | ZUI_KeyboardActivate);
                lbox->Size[0] = ZText();
                lbox->Size[1] = ZPx(24.f);
                {
                    // The active sort column carries an arrow (Figma "Actor Label ↑").
                    const bool active_col = is_sc;
                    const bool asc        = m_sort_asc;
                    char       shown[48]  = {};
                    if (active_col)
                    {
                        snprintf(shown, sizeof(shown), "%s %s", lbl, asc ? "\xe2\x86\x91" : "\xe2\x86\x93");
                    }
                    else
                    {
                        snprintf(shown, sizeof(shown), "%s", lbl);
                    }
                    lbox->Label = ZUIPushStr(&ctx->FrameArena, shown, (uint32_t) ZEngine::Helpers::secure_strlen(shown));
                }
                bool lhov          = !is_sc && (ctx->HotKey == lbox->Key);
                lbox->TextColor[0] = (is_sc || lhov) ? ctx->Theme.TextDefault[0] : ctx->Theme.TextDim[0];
                lbox->TextColor[1] = (is_sc || lhov) ? ctx->Theme.TextDefault[1] : ctx->Theme.TextDim[1];
                lbox->TextColor[2] = (is_sc || lhov) ? ctx->Theme.TextDefault[2] : ctx->Theme.TextDim[2];
                lbox->TextColor[3] = 1.f;
                ZUISignal csig     = ZUISignalFromBox(ctx, lbox);
                ZUIPopBox(ctx);

                ZUIEndRow(ctx); // cell_row

                if (cols[ci].Sortable && (csig.Flags & ZUI_SignalClicked))
                {
                    if (m_sort_col == ci)
                    {
                        m_sort_asc = !m_sort_asc;
                    }
                    else
                    {
                        m_sort_col = ci;
                        m_sort_asc = true;
                    }
                }
            }
            ZUIEndRow(ctx); // hrow
        }

        // World root row
        {
            ZUIDataTableNextRow(ctx, false);
            if (ctx->FocusKey == ctx->DT_RowBox->Key)
            {
                if (ctx->SpacePressed || ctx->EnterPressed)
                {
                    m_root_open = !m_root_open;
                }
                if (ctx->ArrowRightPressed)
                {
                    m_root_open = true;
                }
                if (ctx->ArrowLeftPressed)
                {
                    m_root_open = false;
                }
            }
            ZUIDataTableSetColumn(ctx, 0);
            ZUIBox* root_row     = ZUIBeginRow(ctx, "##tr0_root", ZFill(), ZFill());
            root_row->CrossAlign = ZUIAlign::Center;

            // Figma "Actor row": [10][eye 11][6][chevron 9][5][glyph 12][5][label]
            ZUISpacer(ctx, 10.f);
            ZUIIconBox(ctx, ZUIIcon::Eye, 11.f, ctx->Theme.TextDim);
            ZUISpacer(ctx, 6.f);
            {
                ZUIBox* arr  = ZUIPushBox(ctx, "##arr_root", 10, ZUI_DrawIcon | ZUI_Clickable | ZUI_NoKeyboardFocus);
                arr->Size[0] = ZPx(9.f);
                arr->Size[1] = ZPx(9.f);
                arr->IconId  = (uint16_t) (m_root_open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight);
                for (int k = 0; k < 4; ++k)
                {
                    arr->TextColor[k] = ctx->Theme.TextDim[k];
                }
                ZUISignal asig = ZUISignalFromBox(ctx, arr);
                ZUIPopBox(ctx);
                if (asig.Flags & ZUI_SignalClicked)
                {
                    m_root_open = !m_root_open;
                }
            }
            ZUISpacer(ctx, 5.f);
            ZUIIconBox(ctx, ZUIIcon::Map, 12.f, ctx->Theme.TextDim);
            ZUISpacer(ctx, 5.f);
            const char* sname = (scene->Name && scene->Name[0]) ? scene->Name : "Scene";
            ZUILabelTight(ctx, sname, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUIEndRow(ctx);

            ZUIDataTableSetColumn(ctx, 1);
            TypeLabel(ctx, "World");
            // World root accepts drops to detach an actor from its parent
            char               drop_root[sizeof(EntityTarget)] = {};
            ZUIDragPayloadKind drop_kind                       = ZUIDragPayloadKind::Opaque;
            if (!drop_handled && ZUIAcceptDrop(ctx, ctx->DT_RowBox, drop_root, sizeof(drop_root), &drop_kind) && drop_kind == ZUIDragPayloadKind::Opaque && ctx->DragPayloadLen == sizeof(EntityTarget))
            {
                EntityTarget dragged = {};
                secure_memcpy(&dragged, sizeof(dragged), drop_root, sizeof(drop_root));
                if (const auto entity = resolve_drag(dragged); entity.IsValid())
                {
                    pending_reparent_child  = entity;
                    pending_reparent_parent = INVALID_ENTITY;
                    drop_handled            = true;
                }
            }
        }

        // Flatten the complete displayed order before emitting boxes. Filtering
        // still visits expanded descendants; hidden rows occupy no scroll space.
        auto*    visible       = ZPushArray<DFSEntry>(&ctx->FrameArena, cap);
        uint32_t visible_count = 0;
        if (m_root_open)
        {
            while (sp > 0)
            {
                const auto e  = stk[--sp];
                const auto ni = e.idx;
                if (!m_search[0] || ContainsCI(nodes[ni].Name, m_search))
                {
                    visible[visible_count++] = e;
                }
                if (!IsCollapsed(nodes[ni].EID))
                {
                    for (uint32_t child = first_child[ni]; child != UINT32_MAX; child = next_sib[child])
                    {
                        stk[sp++] = {child, e.depth + 1};
                    }
                }
            }
        }
        const auto select_row = [&](uint32_t row, bool range, bool additive, EntityID previous_focus = INVALID_ENTITY) {
            auto*       session = scene->Session;
            const auto& node    = nodes[visible[row].idx];
            if (!session || !range)
            {
                const bool accepted = select_node(node, additive ? SelectionMode::Toggle : SelectionMode::Replace);
                if (accepted && session)
                {
                    m_selection_anchor = session->TargetFor(node.EID);
                }
                return accepted;
            }
            EntityID anchor = INVALID_ENTITY;
            if (session->Selection().Contains(m_selection_anchor.UUID) && session->Resolve(m_selection_anchor) == m_selection_anchor.CachedEntity)
            {
                anchor = m_selection_anchor.CachedEntity;
            }
            if (!anchor.IsValid())
            {
                anchor = previous_focus.IsValid() ? previous_focus : session->Resolve(session->Selection().ActiveUUID());
            }
            const auto displayed_row = [&](EntityID entity) {
                for (uint32_t i = 0; i < visible_count; ++i)
                {
                    if (nodes[visible[i].idx].EID == entity)
                    {
                        return i;
                    }
                }
                return UINT32_MAX;
            };
            uint32_t anchor_row = displayed_row(anchor);
            if (anchor_row == UINT32_MAX)
            {
                anchor_row = displayed_row(session->Resolve(session->Selection().ActiveUUID()));
            }
            if (anchor_row == UINT32_MAX)
            {
                anchor_row = row;
            }
            const uint32_t begin = std::min(row, anchor_row), count = std::max(row, anchor_row) - begin + 1;
            if (count > session->Selection().Capacity())
            {
                return false;
            }
            auto* targets = static_cast<EntityTarget*>(ctx->FrameArena.AllocateNoZero(sizeof(EntityTarget) * count, alignof(EntityTarget)));
            if (!targets)
            {
                return false;
            }
            for (uint32_t i = 0; i < count; ++i)
            {
                std::construct_at(targets + i, session->TargetFor(nodes[visible[begin + i].idx].EID));
            }
            const auto endpoint = session->TargetFor(node.EID);
            if (!session->RequestSelectionRange({targets, count}, endpoint.UUID, additive))
            {
                return false;
            }
            m_selection_anchor = session->TargetFor(nodes[visible[anchor_row].idx].EID);
            return true;
        };
        const float row_height      = ZUIDataTableRowHeight();
        const float leading_height  = 24.f + row_height; // custom header + World
        const float viewport_height = std::isfinite(height) ? fmaxf(0.f, height - 41.f - 25.f - 2.f) : 0.f;
        const float max_scroll      = fmaxf(0.f, leading_height + (float(visible_count) + 1.f) * row_height - viewport_height);
        float       scroll          = 0.f;
        if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, hierarchy_list->Key))
        {
            state->ScrollY       = std::clamp(std::isfinite(state->ScrollY) ? state->ScrollY : 0.f, 0.f, max_scroll);
            state->ScrollYTarget = std::clamp(std::isfinite(state->ScrollYTarget) ? state->ScrollYTarget : 0.f, 0.f, max_scroll);
            scroll               = state->ScrollY;
        }
        // Navigate the full displayed order, not only this frame's emitted boxes.
        // Consuming these edges prevents the generic clipped-box navigator from
        // wrapping focus to the top when the next logical row is off screen.
        if (!ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && (ctx->ArrowUpPressed || ctx->ArrowDownPressed || ctx->HomePressed || ctx->EndPressed))
        {
            const auto root_key = ctx->DT_RowBox->Key;
            int        focused  = ctx->FocusKey == root_key ? 0 : -1;
            for (uint32_t i = 0; focused < 0 && i < m_row_count && m_rows_epoch == epoch; ++i)
            {
                if (ctx->FocusKey == m_rows[i].Row)
                {
                    if (const auto* node = eid_to_idx.find(m_rows[i].Entity))
                    {
                        for (uint32_t row = 0; row < visible_count; ++row)
                        {
                            if (visible[row].idx == *node)
                            {
                                focused = int(row) + 1;
                                break;
                            }
                        }
                    }
                }
            }
            if (focused >= 0)
            {
                int next = ctx->HomePressed ? 0 : ctx->EndPressed ? int(visible_count) : std::clamp(focused + (ctx->ArrowDownPressed ? 1 : -1), 0, int(visible_count));
                if (ctx->ShiftDown && !next && visible_count)
                {
                    next = 1; // World is a display row, never an entity-range endpoint.
                }
                if (ctx->ShiftDown && next > 0 && next != focused)
                {
                    const auto previous = focused > 0 ? nodes[visible[uint32_t(focused - 1)].idx].EID : INVALID_ENTITY;
                    if (!select_row(uint32_t(next - 1), true, ctx->CtrlDown, previous))
                        ZENGINE_CORE_WARN("[Outliner] The complete keyboard selection range could not be queued.")
                    m_last_click_idx = UINT64_MAX;
                }
                ctx->FocusKey   = next ? ZUIDataTableRowKey(ctx, RowIdentity(epoch, nodes[visible[uint32_t(next - 1)].idx].EID)) : root_key;
                const float top = 24.f + float(next) * row_height;
                if (top < scroll)
                {
                    scroll = top;
                }
                else if (top + row_height > scroll + viewport_height)
                {
                    scroll = top + row_height - viewport_height;
                }
                scroll = std::clamp(scroll, 0.f, max_scroll);
                if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, hierarchy_list->Key))
                {
                    state->ScrollY = state->ScrollYTarget = scroll;
                }
                ctx->ArrowUpPressed = ctx->ArrowDownPressed = ctx->HomePressed = ctx->EndPressed = false;
                ctx->KeyboardFocus                                                               = true;
            }
        }
        const uint32_t first = uint32_t(std::clamp(std::floor((scroll - leading_height) / row_height) - 2.f, 0.f, float(visible_count)));
        const uint32_t end   = uint32_t(std::clamp(std::ceil((scroll + viewport_height - leading_height) / row_height) + 2.f, float(first), float(visible_count)));
        ZENGINE_VALIDATE_ASSERT(end - first <= MaxRenderedRows, "Outliner viewport exceeds its bounded row workspace")
        for (uint32_t i = first; i < end; ++i)
        {
            nodes[visible[i].idx].Emitted = true;
        }
        // Reclaim only this panel's retired transient row states. Keep stable
        // identity keys (never recycled row-slot keys), and do not touch another
        // panel's persistent editing/scroll state. No state pointers cross erase.
        const auto retire = [&](uint64_t key) {
            if (!key)
            {
                return;
            }
            if (ctx->HotKey == key)
            {
                ctx->HotKey = 0;
            }
            if (ctx->ActiveKey == key)
            {
                ctx->ActiveKey = 0;
            }
            if (ctx->FocusKey == key)
            {
                ctx->FocusKey = 0;
            }
            (void) ZUIStateEraseTransient(&ctx->StateStore, key);
        };
        for (uint32_t i = 0; i < m_row_count; ++i)
        {
            const auto& row   = m_rows[i];
            const auto* index = eid_to_idx.find(row.Entity);
            if (view_changed || m_rows_epoch != epoch || !index || !nodes[*index].Emitted)
            {
                retire(row.Row);
                retire(row.Arrow);
                retire(row.Rename);
            }
        }
        m_row_count  = 0;
        m_rows_epoch = epoch;
        ZUIDataTableSkipRows(ctx, first);
        // Only viewport rows + two rows of overscan on each side create UI boxes.
        for (uint32_t row_index = first; row_index < end; ++row_index)
        {
            const auto     e      = visible[row_index];
            const auto     ni     = e.idx;
            const EntityID entity = nodes[ni].EID;
            if (!ecs->IsAlive(entity))
            {
                continue;
            }

            auto*       nc_comp  = ecs->GetComponent<NameComponent>(entity);
            const char* label    = (nc_comp && nc_comp->Value[0]) ? nc_comp->Value : "Actor";

            // Type — exact develop: is_collection = !Mesh && !Light && !Camera
            bool        is_coll  = nodes[ni].TypeRank == 0;
            const char* type_str = {};
            if (is_coll)
            {
                type_str = "Collection";
            }
            else if (nodes[ni].TypeRank == 1)
            {
                type_str = "Light";
            }
            else if (nodes[ni].TypeRank == 2)
            {
                type_str = "Camera";
            }
            else if (nodes[ni].TypeRank == 3)
            {
                type_str = "Static Mesh";
            }
            else
            {
                type_str = "Actor";
            }

            bool  has_ch    = (first_child[ni] != UINT32_MAX);
            bool  collapsed = has_ch && IsCollapsed(nodes[ni].EID); // exact develop naming
            bool  selected  = scene->IsSelected(nodes[ni].EID, eng->ActorManager);
            bool  renaming  = (m_rename_id == nodes[ni].EID && m_rename_id.IsValid());
            float indent    = (float) (e.depth + 1) * 10.f; // Figma: 10 px per level

            // Display order and filtering were resolved before virtualization;
            // build interaction and content only for this emitted row.
            {

                // Row
                bool  row_clicked   = ZUIDataTableNextRow(ctx, selected, RowIdentity(epoch, entity));
                auto& row_state     = m_rows[m_row_count++];
                char  arrow_key[64] = {}, rename_key[64] = {};
                snprintf(arrow_key, sizeof(arrow_key), "##arr_%u_%u", entity.Index, entity.Generation);
                snprintf(rename_key, sizeof(rename_key), "##ren_%u_%u", entity.Index, entity.Generation);
                row_state         = {.Entity = entity, .Row = ctx->DT_RowBox->Key, .Arrow = ZUIHashStr(arrow_key, static_cast<uint32_t>(secure_strlen(arrow_key))), .Rename = ZUIHashStr(rename_key, static_cast<uint32_t>(secure_strlen(rename_key)))};
                ZUISignal row_sig = ZUIDataTableRowSignal(ctx);
                if (has_ch && ctx->FocusKey == ctx->DT_RowBox->Key && ((ctx->ArrowLeftPressed && !collapsed) || (ctx->ArrowRightPressed && collapsed)))
                {
                    ToggleCollapsed(nodes[ni].EID);
                }

                // Drop-target blue border — use prev-frame ScreenRect since HotKey frozen during drag
                if (ctx->DragSourceKey != 0 && ctx->DT_RowBox)
                {
                    auto* rps = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                    if (rps && rps->ScreenMaxX > rps->ScreenMinX && ctx->MousePos[0] >= rps->ScreenMinX && ctx->MousePos[0] <= rps->ScreenMaxX && ctx->MousePos[1] >= rps->ScreenMinY && ctx->MousePos[1] <= rps->ScreenMaxY)
                    {
                        ctx->DT_RowBox->Flags           = ctx->DT_RowBox->Flags | ZUI_DrawBorder;
                        ctx->DT_RowBox->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
                        ctx->DT_RowBox->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
                        ctx->DT_RowBox->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
                        ctx->DT_RowBox->BorderColor[3]  = ctx->Theme.TabActiveBorder[3];
                        ctx->DT_RowBox->BorderThickness = 1.f;
                    }
                }

                // Col 0: [indent][chevron or spacer][icon][name or rename field]
                ZUIDataTableSetColumn(ctx, 0);
                {
                    char rk0[64] = {};
                    snprintf(rk0, sizeof(rk0), "##tr0_%u_%u", entity.Index, entity.Generation);
                    ZUIBox* arow     = ZUIBeginRow(ctx, rk0, ZFill(), ZFill());
                    arow->CrossAlign = ZUIAlign::Center;
                }
                ZUISpacer(ctx, 10.f);
                ZUIIconBox(ctx, ZUIIcon::Eye, 11.f, ctx->Theme.TextDim);
                ZUISpacer(ctx, 6.f + indent);

                if (has_ch)
                {
                    char arr_key[64] = {};
                    snprintf(arr_key, sizeof(arr_key), "##arr_%u_%u", entity.Index, entity.Generation);
                    ZUIBox* arr     = ZUIPushBox(ctx, arr_key, (uint32_t) ZEngine::Helpers::secure_strlen(arr_key), ZUI_DrawIcon | ZUI_Clickable | ZUI_NoKeyboardFocus);
                    row_state.Arrow = arr->Key;
                    arr->Size[0]    = ZPx(9.f);
                    arr->Size[1]    = ZPx(9.f);
                    arr->IconId     = (uint16_t) (!collapsed ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight);
                    bool arr_hov    = (ctx->HotKey == arr->Key);
                    for (int k = 0; k < 4; ++k)
                    {
                        arr->TextColor[k] = arr_hov ? ctx->Theme.TextDefault[k] : ctx->Theme.TextDim[k];
                    }
                    ZUISignal asig = ZUISignalFromBox(ctx, arr);
                    ZUIPopBox(ctx);
                    if (asig.Flags & ZUI_SignalClicked)
                    {
                        ToggleCollapsed(nodes[ni].EID);
                    }
                }
                else
                {
                    ZUIIconBox(ctx, ZUIIcon::Minus, 9.f, ctx->Theme.TextDim);
                }
                ZUISpacer(ctx, 5.f);

                // Type glyph
                {
                    ZUIIcon glyph = ZUIIcon::Box;
                    if (is_coll)
                    {
                        glyph = (has_ch && !collapsed) ? ZUIIcon::FolderOpen : ZUIIcon::Folder;
                    }
                    else if (nodes[ni].TypeRank == 1)
                    {
                        glyph = ZUIIcon::Sun;
                    }
                    else if (nodes[ni].TypeRank == 2)
                    {
                        glyph = ZUIIcon::Camera;
                    }
                    ZUIIconBox(ctx, glyph, 12.f, ctx->Theme.TextDim);
                }
                ZUISpacer(ctx, 5.f);

                // Name or inline rename
                if (renaming)
                {
                    rename_drawn    = true;
                    char tf_key[64] = {};
                    snprintf(tf_key, sizeof(tf_key), "##ren_%u_%u", entity.Index, entity.Generation);
                    row_state.Rename = ZUIHashStr(tf_key, static_cast<uint32_t>(secure_strlen(tf_key)));

                    // Begin a fresh editing session, without stale cursor,
                    // selection or undo history from a previous rename.
                    if (m_rename_started)
                    {
                        m_rename_fkey    = ZUIHashStr(tf_key, (uint32_t) ZEngine::Helpers::secure_strlen(tf_key));
                        ctx->FocusKey    = m_rename_fkey;
                        m_rename_started = false;
                        if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, m_rename_fkey))
                        {
                            state->UserData    = static_cast<float>(secure_strlen(m_rename_buf));
                            state->SelectStart = 0;
                        }
                        ctx->UndoFieldKey = m_rename_fkey;
                        ctx->UndoTop = ctx->RedoTop = 0;
                    }

                    const bool focused = ctx->FocusKey == m_rename_fkey;
                    if (focused && ctx->EscapePressed)
                    {
                        finish_rename(false);
                        ctx->EscapePressed = false;
                        ZUILabelTight(ctx, label, ctx->Theme.TextDefault, ZUIFontSize::Body);
                    }
                    else
                    {
                        // Scrollable empty space deliberately retains focus;
                        // detect outside presses directly instead of waiting
                        // for the global interaction pass to blur the field.
                        const auto* state   = ZUIStateGetOrInsert(&ctx->StateStore, m_rename_fkey);
                        const bool  outside = (ctx->MousePressed[0] || ctx->MousePressed[1]) && (!state || ctx->MousePos[0] < state->ScreenMinX || ctx->MousePos[0] >= state->ScreenMaxX || ctx->MousePos[1] < state->ScreenMinY || ctx->MousePos[1] >= state->ScreenMaxY);
                        ZUITextField(ctx, tf_key, m_rename_buf, sizeof(m_rename_buf), 150.f);
                        if (!focused || outside || ctx->EnterPressed)
                        {
                            finish_rename(true);
                            if (focused)
                            {
                                ctx->EnterPressed = false;
                            }
                        }
                    }
                }
                else
                {
                    ZUILabelTight(ctx, label, ctx->Theme.TextDefault, ZUIFontSize::Body);
                }
                ZUIEndRow(ctx); // close horizontal layout

                // Col 1: type
                ZUIDataTableSetColumn(ctx, 1);
                TypeLabel(ctx, type_str);

                // Selection + panel-level double-click tracking.
                // ZUISignalFromBox is called twice per row (DataTableNextRow + DataTableRowSignal)
                // so double-click must be tracked here, not in ZUISignalFromBox.
                if (row_clicked && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && (ctx->MouseReleased[0] || !ctx->TextInputActive))
                {
                    const bool latched  = ctx->MouseReleased[0] && ctx->MousePressSerial[0] != 0 && ctx->MouseReleaseSerial[0] == ctx->MousePressSerial[0];
                    const bool modifier = latched ? ctx->MousePressCtrl[0] : ctx->CtrlDown;
                    const bool range    = latched ? ctx->MousePressShift[0] : ctx->ShiftDown;
                    const bool accepted = select_row(row_index, range, modifier);
                    if (!accepted)
                        ZENGINE_CORE_WARN("[Editor] Could not queue the hierarchy selection")
                    double now = (double) ctx->Time;
                    if (!accepted || modifier || range || !ctx->MouseReleased[0])
                    {
                        m_last_click_idx = UINT64_MAX;
                    }
                    else if (m_last_click_idx == entity.Index && m_last_click_gen == entity.Generation && now - m_last_click_time < 0.30)
                    {
                        // Double-click on name/icon area → start rename
                        if (!renaming)
                        {
                            begin_rename(nodes[ni].EID, nc_comp);
                        }
                        m_last_click_idx = UINT64_MAX; // consume
                    }
                    else
                    {
                        m_last_click_idx  = entity.Index;
                        m_last_click_gen  = entity.Generation;
                        m_last_click_time = now;
                    }
                }

                // Context menu — unique key per actor so the popup only opens
                // for the right-clicked row (shared key causes all rows to match
                // the open popup in the same frame, overwriting pending_delete).
                char ctx_key[48] = {};
                snprintf(ctx_key, sizeof(ctx_key), "##ctx_%u_%u", entity.Index, entity.Generation);
                ctx->PopupDesiredW = 250.f;
                if (ZUIBeginPopupContextItem(ctx, ctx_key, row_sig))
                {
                    if (ZUIMenuItemEx(ctx, "Rename", "F2"))
                    {
                        begin_rename(nodes[ni].EID, nc_comp);
                    }
                    if (ZUIMenuItemEx(ctx, "Focus Selected", "F"))
                    {
                        const auto* transform = ecs->GetComponent<TransformComponent>(entity);
                        if (transform && app->CameraController)
                        {
                            if (auto* fly = static_cast<ZEngine::Rendering::Cameras::FlyCamera*>(app->CameraController->GetCamera()))
                            {
                                fly->FocusOn({transform->WorldTransform(0, 3), transform->WorldTransform(1, 3), transform->WorldTransform(2, 3)});
                            }
                        }
                    }
                    ZUISeparator(ctx);
                    if (ZUIBeginSubMenu(ctx, "Transform", true, (int) ZUIIcon::Move))
                    {
                        if (ZUIMenuItemEx(ctx, "Reset Transform", nullptr))
                        {
                            pending_reset_xform = entity;
                        }
                        ZUIEndSubMenu(ctx);
                    }
                    ZUIBeginSubMenu(ctx, "Pivot", false, (int) ZUIIcon::Maximize);
                    if (ZUIBeginSubMenu(ctx, "Attach To", node_count > 1, (int) ZUIIcon::Layers))
                    {
                        for (uint32_t ai = 0, shown = 0; ai < node_count && shown < 12; ++ai)
                        {
                            if (ai == ni)
                            {
                                continue;
                            }
                            char        at_label[96] = {};
                            const auto* at_name      = ecs->GetComponent<NameComponent>(nodes[ai].EID);
                            snprintf(at_label, sizeof(at_label), "%s##att%u", at_name ? at_name->Value : "Actor", ai);
                            if (ZUIMenuItemEx(ctx, at_label, nullptr))
                            {
                                pending_reparent_child  = entity;
                                pending_reparent_parent = nodes[ai].EID;
                            }
                            ++shown;
                        }
                        ZUIEndSubMenu(ctx);
                    }
                    ZUISeparator(ctx);
                    if (ZUIMenuItemEx(ctx, "Properties...", "Alt+Enter", false, true, (int) ZUIIcon::Settings))
                        if (!select_node(nodes[ni]))
                            ZENGINE_CORE_WARN("[Editor] Could not queue the hierarchy selection")
                    ZUISeparator(ctx);
                    if (nodes[ni].Parent != INVALID_ENTITY && ZUIMenuItem(ctx, "Remove from Parent"))
                    {
                        pending_reparent_child  = entity;
                        pending_reparent_parent = INVALID_ENTITY;
                    }
                    if (ZUIMenuItemEx(ctx, "Delete", "Del", false, true, (int) ZUIIcon::X))
                    {
                        pending_delete = entity;
                    }
                    ZUIEndPopup(ctx);
                }

                // Preserve authored identity across filtering, reordering,
                // entity recycling and scene replacement during a drag.
                const auto source = drag_target(entity);
                ZUIBeginDragSource(ctx, ctx->DT_RowBox, reinterpret_cast<const char*>(&source), sizeof(source));
                if (ctx->DragSourceKey != 0 && ctx->DT_RowBox && ctx->DragSourceKey == ctx->DT_RowBox->Key)
                {
                    m_dragging_target = source;
                    secure_strncpy(m_drag_label, sizeof(m_drag_label), label, sizeof(m_drag_label) - 1);
                }

                // Drop target — bounds-based: DragTargetKey = deepest clickable child (chevron),
                // not the outer row. Validate the panel-owned source identity.
                if (!drop_handled && ctx->DragDropFired && resolve_drag(m_dragging_target).IsValid() && ctx->DT_RowBox)
                {
                    auto* drps   = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                    bool  in_row = drps && drps->ScreenMaxX > drps->ScreenMinX && ctx->MousePos[0] >= drps->ScreenMinX && ctx->MousePos[0] <= drps->ScreenMaxX && ctx->MousePos[1] >= drps->ScreenMinY && ctx->MousePos[1] <= drps->ScreenMaxY;
                    if (in_row && m_dragging_target.CachedEntity != entity)
                    {
                        pending_reparent_child  = m_dragging_target.CachedEntity;
                        pending_reparent_parent = nodes[ni].EID;
                        drop_handled            = true;
                    }
                }
            }
        }
        ZUIDataTableSkipRows(ctx, visible_count - end);

        // Root drop zone row — exact develop 8px InvisibleButton equivalent
        {
            ZUIDataTableNextRow(ctx, false, ZUIHashStr("##hier_detach", 13));
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
                    ctx->DT_RowBox->BorderColor[3]  = ctx->Theme.TabActiveBorder[3];
                    ctx->DT_RowBox->BorderThickness = 1.f;
                }
            }
            ZUILabel(ctx, "Drop here to detach from parent", kDim);
            ZUIEndRow(ctx);
            ZUIDataTableSetColumn(ctx, 1);
            // Root drop zone — use the actor owned by this panel's drag.
            if (!drop_handled && ctx->DragDropFired && resolve_drag(m_dragging_target).IsValid() && ctx->DT_RowBox)
            {
                auto* rzps    = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_RowBox->Key);
                bool  in_zone = rzps && rzps->ScreenMaxX > rzps->ScreenMinX && ctx->MousePos[0] >= rzps->ScreenMinX && ctx->MousePos[0] <= rzps->ScreenMaxX && ctx->MousePos[1] >= rzps->ScreenMinY && ctx->MousePos[1] <= rzps->ScreenMaxY;
                if (in_zone)
                {
                    pending_reparent_parent = INVALID_ENTITY;
                    pending_reparent_child  = m_dragging_target.CachedEntity;
                    drop_handled            = true;
                }
            }
        }

        ZUIEndDataTable(ctx);
        ZUIEndScrollRegion(ctx);

        // Hidden rows cannot observe focus loss. Finish an existing session,
        // but let a newly requested rename build its field next frame.
        if (rename_on_entry.IsValid() && m_rename_id == rename_on_entry && !rename_drawn)
        {
            finish_rename(!ctx->EscapePressed);
        }

        // Foreground Tooltip recipe, 12 px right of and below the cursor.
        if (ctx->DragSourceKey != 0 && m_drag_label[0])
        {
            char tip_buf[140] = {};
            snprintf(tip_buf, sizeof(tip_buf), "Move: %s", m_drag_label);
            ZUIDragLabel(ctx, tip_buf);
        }
        else if (ctx->DragSourceKey == 0)
        {
            m_drag_label[0] = '\0';
        }
        // DragDropFired fires on every mouse release during a drag (hit or miss).
        // Consume the source so it cannot match another mouse release.
        if (ctx->DragDropFired)
        {
            m_dragging_target = {};
        }

        // Deferred mutations — exact develop pattern

        // Reparent (also handles detach-to-root via INVALID_ENTITY)
        if (ecs->IsAlive(pending_reparent_child))
        {
            if (scene->Session)
            {
                if (!scene->Session->RequestReparent(pending_reparent_child, pending_reparent_parent))
                    ZENGINE_CORE_WARN("[Outliner] The reparent request could not be queued.")
            }
            else
            {
                const auto* previous = ecs->GetComponent<ParentComponent>(pending_reparent_child);
                if ((previous ? previous->Parent : INVALID_ENTITY) != pending_reparent_parent && ecs->SetParent(pending_reparent_child, pending_reparent_parent))
                {
                    scene->MarkDirty(true);
                }
            }
        }

        if (pending_reset_xform.IsValid())
        {
            if (scene->Session)
            {
                if (!scene->Session->RequestTransformValue(pending_reset_xform, {}))
                    ZENGINE_CORE_WARN("[Outliner] The transform reset could not be queued.")
            }
            else if (auto* transform = ecs->GetComponent<TransformComponent>(pending_reset_xform); transform && !SameLocalTransform(CaptureLocalTransform(*transform), {}))
            {
                ApplyLocalTransform(*transform, {});
                scene->MarkDirty(true);
            }
        }

        // Removal also updates actor/render bindings, but only after the session
        // has restored the active transaction at its exclusive mutation point.
        if (pending_delete.IsValid())
        {
            if (m_rename_id == pending_delete)
            {
                finish_rename(false);
            }
            if (scene->Session)
            {
                if (!scene->Session->RequestDelete(pending_delete))
                    ZENGINE_CORE_WARN("[Outliner] The delete request could not be queued.")
            }
            else
            {
                (void) scene->DeleteEntity(*eng, pending_delete);
            }
        }

        // Status bar
        ZUISeparator(ctx);
        ZUIBox* sb = ZUIBeginRow(ctx, "##hier_sb", ZFill(), ZPx(25.f));
        sb->Flags  = sb->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(sb, ctx->Theme.PanelBg);
        sb->CrossAlign = ZUIAlign::Center;
        ZUISpacer(ctx, 10.f);
        {
            char       status[64] = {};
            int        total      = (int) ecs->AliveCount();
            const auto selected   = scene->Session ? scene->Session->Selection().Entities().size() : (scene->SelectedActorHandle.Valid() ? 1u : 0u);
            if (selected)
            {
                snprintf(status, sizeof(status), "%d actor%s (%u selected)", total, total == 1 ? "" : "s", static_cast<unsigned>(selected));
            }
            else
            {
                snprintf(status, sizeof(status), "%d actor%s", total, total == 1 ? "" : "s");
            }
            ZUILabelTight(ctx, status, ctx->Theme.TextDim, ZUIFontSize::Body);
        }
        ZUIEndRow(ctx);

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Panels
