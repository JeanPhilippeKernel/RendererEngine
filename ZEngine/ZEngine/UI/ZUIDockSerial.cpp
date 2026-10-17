#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <cstdio>
#include <cstring>

using namespace ZEngine::Core::Memory;

namespace ZEngine::UI
{
    // Node collection helpers (DFS, parent-before-children)

    static constexpr uint32_t kMaxSerialNodes = 128;

    static bool               ViewIsAssigned(const ZUIPanelManager* manager, const ZUIPanelView* view)
    {
        for (uint32_t pi = 0; pi < manager->PanelCount; ++pi)
            for (uint32_t vi = 0; vi < manager->Panels[pi].ViewCount; ++vi)
                if (manager->Panels[pi].Views[vi] == view)
                    return true;
        return false;
    }

    struct NodeRecord
    {
        ZUIDockNode* Ptr      = nullptr;
        int          ParentId = -1; // -1 = root
    };

    static void CollectNodes(ZUIDockNode* node, int parent_id, NodeRecord records[], uint32_t* count)
    {
        if (!node || *count >= kMaxSerialNodes)
        {
            return;
        }
        int my_id      = (int) (*count)++;
        records[my_id] = {node, parent_id};
        for (ZUIDockNode* c = node->First; c; c = c->Next)
            CollectNodes(c, my_id, records, count);
    }

    // ZUIDockSave  (v4 format — adds per-view Visible; incompatible with v3)

    void ZUIDockSave(ZUIPanelManager* manager, const char* path)
    {
        if (!manager || !path || !path[0])
        {
            return;
        }
        FILE* f = fopen(path, "w");
        if (!f)
        {
            return;
        }

        fprintf(f, "# ZUI Layout v4\n");

        // Dock tree
        // node <id> <parent_id> <is_leaf> <axis:0=X,1=Y> <pct> [<content_key_hex> <is_central> <auto_hide_tab_bar>]
        if (manager->DockTree && manager->DockTree->Root)
        {
            NodeRecord records[kMaxSerialNodes] = {};
            uint32_t   count                    = 0;
            CollectNodes(manager->DockTree->Root, -1, records, &count);

            for (uint32_t i = 0; i < count; ++i)
            {
                ZUIDockNode* n        = records[i].Ptr;
                bool         is_leaf  = (n->ContentKey != 0);
                // Compute save fraction from SizePx (runtime source of truth).
                // Falls back to PctOfParent if SizePx not yet initialized.
                float        save_pct = n->PctOfParent;
                if (n->Parent && n->SizePx > 0.f)
                {
                    float sib_sum = 0.f;
                    for (ZUIDockNode* s = n->Parent->First; s; s = s->Next)
                        sib_sum += s->SizePx;
                    if (sib_sum > 1e-6f)
                        save_pct = n->SizePx / sib_sum;
                }
                fprintf(f, "node %u %d %d %d %f", i, records[i].ParentId, is_leaf ? 1 : 0, (n->SplitAxis == ZUIAxis::X) ? 0 : 1, (double) save_pct);
                if (is_leaf)
                    fprintf(f, " %016llx %d %d", (unsigned long long) n->ContentKey, n->IsCentral ? 1 : 0, n->AutoHideTabBar ? 1 : 0);
                fprintf(f, "\n");
            }
        }

        // Panels
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            // panel <dock_key_hex> <active_tab> <hidden> <view_count>
            // followed by one  `view <title> <visible>`  line per view (v4)
            fprintf(f, "panel %016llx %u %d %u\n", (unsigned long long) p->DockKey, p->ActiveTab, p->Hidden ? 1 : 0, p->ViewCount);
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            {
                const ZUIPanelView* v     = p->Views[vi];
                const char*         title = (v && v->Title) ? v->Title : "";
                int                 vis   = (v && v->Visible) ? 1 : 0;
                fprintf(f, "view %s %d\n", title, vis);
            }
        }

        fclose(f);
    }

    // ZUIDockLoad  (v4 format only — v3 files are intentionally incompatible)

    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path, ZUIPanelView** views, uint32_t view_count)
    {
        if (!manager || !path || !path[0])
        {
            return false;
        }
        FILE* f = fopen(path, "r");
        if (!f)
        {
            return false;
        }

        // Version check (v4 only; older formats are discarded, not migrated)
        char line[512] = {};
        if (!fgets(line, sizeof(line), f) || ZEngine::Helpers::secure_memcmp(line, sizeof(line), "# ZUI Layout v4", sizeof("# ZUI Layout v4"), 15) != 0)
        {
            fclose(f);
            return false;
        }

        // Pass 1: parse all node records
        struct LoadNode
        {
            int      parent_id         = -1;
            int      is_leaf           = {};
            int      axis              = {}; // 0=X 1=Y
            float    pct               = 1.f;
            uint64_t content_key       = {}; // 0 if split
            int      is_central        = {};
            int      auto_hide_tab_bar = {};
        };

        LoadNode load_nodes[kMaxSerialNodes] = {};
        uint32_t node_count                  = 0;

        // Also buffer panel lines for pass 2
        struct LoadPanel
        {
            uint64_t key        = {};
            uint32_t active_tab = {};
            int      hidden     = {};
            uint32_t view_count = {};
        };
        static constexpr uint32_t kMaxPanels                                    = 32;
        LoadPanel                 load_panels[kMaxPanels]                       = {};
        char                      panel_views[kMaxPanels][kMaxTabsPerPanel][64] = {}; // title buffers
        int                       panel_view_vis[kMaxPanels][kMaxTabsPerPanel]  = {}; // per-view visible (v4)
        uint32_t                  panel_count                                   = 0;
        int                       pending_panel                                 = -1; // index of panel we're reading views for
        uint32_t                  panel_view_idx                                = 0;

        while (fgets(line, sizeof(line), f))
        {
            // Strip trailing newline
            uint32_t len = (uint32_t) ZEngine::Helpers::secure_strlen(line);
            while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            {
                line[--len] = '\0';
            }

            if (line[0] == '#' || line[0] == '\0')
            {
                continue;
            }

            if (ZEngine::Helpers::secure_memcmp(line, sizeof(line), "node ", sizeof("node "), 5) == 0 && node_count < kMaxSerialNodes)
            {
                pending_panel             = -1; // end any ongoing panel read
                LoadNode& n               = load_nodes[node_count];
                n.content_key             = 0;
                n.is_central              = 0;
                n.auto_hide_tab_bar       = 0;
                unsigned long long ck     = 0;
                int                parsed = sscanf(line + 5, "%*u %d %d %d %f %llx %d %d", &n.parent_id, &n.is_leaf, &n.axis, &n.pct, &ck, &n.is_central, &n.auto_hide_tab_bar);
                n.content_key             = (uint64_t) ck;
                if (parsed >= 4)
                {
                    ++node_count;
                }
            }
            else if (ZEngine::Helpers::secure_memcmp(line, sizeof(line), "panel ", sizeof("panel "), 6) == 0 && panel_count < kMaxPanels)
            {
                LoadPanel&         p = load_panels[panel_count];
                unsigned long long k = 0;
                if (sscanf(line + 6, "%llx %u %d %u", &k, &p.active_tab, &p.hidden, &p.view_count) == 4)
                {
                    p.key = (uint64_t) k;
                    if (p.view_count > kMaxTabsPerPanel)
                        p.view_count = kMaxTabsPerPanel;
                    pending_panel  = (int) panel_count;
                    panel_view_idx = 0;
                    ++panel_count;
                }
            }
            else if (ZEngine::Helpers::secure_memcmp(line, sizeof(line), "view ", sizeof("view "), 5) == 0 && pending_panel >= 0)
            {
                if (panel_view_idx < kMaxTabsPerPanel)
                {
                    char* title  = line + 5;
                    int   vis    = 1;
                    // Read visibility from the right: titles can contain spaces. Keep
                    // accepting old title-only records, without changing the v4 format.
                    char* suffix = strrchr(title, ' ');
                    if (suffix && (ZEngine::Helpers::secure_strcmp(suffix + 1, "0") == 0 || ZEngine::Helpers::secure_strcmp(suffix + 1, "1") == 0))
                    {
                        vis     = suffix[1] - '0';
                        *suffix = '\0';
                    }
                    // Reject overlong titles rather than silently matching a different view.
                    if (ZEngine::Helpers::secure_strlen(title) < sizeof(panel_views[0][0]))
                        ZEngine::Helpers::secure_memcpy(panel_views[pending_panel][panel_view_idx], sizeof(panel_views[pending_panel][panel_view_idx]), title, ZEngine::Helpers::secure_strlen(title) + 1);
                    panel_view_vis[pending_panel][panel_view_idx] = vis;
                    ++panel_view_idx;
                }
            }
        }
        fclose(f);

        if (node_count == 0)
        {
            return false;
        }

        // Rebuild dock tree
        if (!manager->DockTree || !manager->DockTree->Arena)
        {
            return false;
        }
        ArenaAllocator* arena                      = manager->DockTree->Arena;

        // Allocate all nodes upfront
        ZUIDockNode*    new_nodes[kMaxSerialNodes] = {};
        for (uint32_t i = 0; i < node_count; ++i)
        {
            new_nodes[i] = ZPushStructCtor(arena, ZUIDockNode);
            if (!new_nodes[i])
            {
                return false;
            }
        }

        // Wire the tree
        for (uint32_t i = 0; i < node_count; ++i)
        {
            LoadNode&    ln   = load_nodes[i];
            ZUIDockNode* n    = new_nodes[i];

            n->SplitAxis      = (ln.axis == 0) ? ZUIAxis::X : ZUIAxis::Y;
            n->PctOfParent    = ln.pct;
            n->ContentKey     = ln.content_key;
            n->IsCentral      = (ln.is_central != 0);
            n->AutoHideTabBar = (ln.auto_hide_tab_bar != 0);

            if (ln.parent_id >= 0 && (uint32_t) ln.parent_id < node_count)
            {
                ZUIDockNode* parent = new_nodes[ln.parent_id];
                n->Parent           = parent;
                n->Prev             = parent->Last;
                if (parent->Last)
                {
                    parent->Last->Next = n;
                }
                else
                {
                    parent->First = n;
                }
                parent->Last = n;
                ++parent->ChildCount;
            }
        }

        manager->DockTree->Root    = new_nodes[0];
        manager->DockTree->Focused = nullptr;

        // Restore panel state + view assignments
        // Snapshot current view assignments before clearing so we can restore
        // panels that are NOT in the ini (stale DockKey after a rename, etc.).
        // Without this, unmatched panels end up with ViewCount=0 → invisible.
        struct ViewSnapshot
        {
            ZUIPanelView* Views[kMaxTabsPerPanel] = {};
            uint32_t      Count                   = {};
        };
        ViewSnapshot snapshots[kMaxPanels]       = {};
        bool         restored_panels[kMaxPanels] = {};
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            snapshots[i].Count = manager->Panels[i].ViewCount;
            for (uint32_t v = 0; v < manager->Panels[i].ViewCount; ++v)
                snapshots[i].Views[v] = manager->Panels[i].Views[v];
            manager->Panels[i].ViewCount = 0;
            // Reserve the default panel slots while recreating saved drag slots.
            // AddPanel otherwise reuses hidden empty slots before their records load.
            manager->Panels[i].Hidden    = false;
        }

        for (uint32_t pi = 0; pi < panel_count; ++pi)
        {
            LoadPanel& lp = load_panels[pi];
            ZUIPanel*  p  = manager->FindPanel(lp.key);
            // Drag-created panel keys are part of the saved tree. Recreate
            // them directly; a shared view (Console/Importer) need not have a
            // default panel whose key is the hash of its title.
            if (!p && lp.view_count)
                p = manager->AddPanel(lp.key);
            if (!p)
            {
                continue;
            }

            p->ActiveTab                         = lp.active_tab;
            p->Hidden                            = (lp.hidden != 0);
            restored_panels[p - manager->Panels] = true;

            // Assign views by matching titles; restore per-view Visible (v4)
            for (uint32_t vi = 0; vi < lp.view_count && p->ViewCount < kMaxTabsPerPanel; ++vi)
            {
                const char* title = panel_views[pi][vi];
                for (uint32_t gi = 0; gi < view_count; ++gi)
                {
                    if (views[gi] && views[gi]->MatchesTitle(title) && !ViewIsAssigned(manager, views[gi]))
                    {
                        views[gi]->Visible       = (panel_view_vis[pi][vi] != 0);
                        p->Views[p->ViewCount++] = views[gi];
                        break;
                    }
                }
            }

            if (p->ActiveTab >= p->ViewCount && p->ViewCount > 0)
                p->ActiveTab = p->ViewCount - 1;
        }

        // Preserve registered views omitted by older/partial layouts, including
        // hidden tools added to an existing panel. Never restore a view already
        // assigned elsewhere, or moving tabs duplicates them on the next launch.
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            for (uint32_t v = 0; v < snapshots[i].Count && p->ViewCount < kMaxTabsPerPanel; ++v)
            {
                auto* view = snapshots[i].Views[v];
                if (view && !ViewIsAssigned(manager, view))
                {
                    p->Views[p->ViewCount++] = view;
                    if (!restored_panels[i])
                        p->Hidden = false;
                }
            }
            if (!p->ViewCount)
                p->Hidden = true;
        }

        // Collapse orphaned leaves — leaves whose ContentKey has NO registered panel at all.
        // This only removes drag-created slots (session-unique DragKeySeq keys that don't
        // exist in the current session's Panels[] array). Leaves with a registered panel
        // (even one with ViewCount==0) are left alone; hidden-panel collapse handles them.
        {
            uint64_t     orphan_keys[kMaxSerialNodes] = {};
            uint32_t     orphan_count                 = 0;

            ZUIDockNode* stk[kMaxSerialNodes]         = {};
            uint32_t     stk_top                      = 0;
            if (manager->DockTree->Root)
                stk[stk_top++] = manager->DockTree->Root;
            while (stk_top > 0 && orphan_count < kMaxSerialNodes)
            {
                ZUIDockNode* n = stk[--stk_top];
                if (!n->First && n->ContentKey != 0)
                {
                    // Leaf — orphaned only when NO panel is registered with this key.
                    if (!manager->FindPanel(n->ContentKey))
                        orphan_keys[orphan_count++] = n->ContentKey;
                }
                for (ZUIDockNode* c = n->First; c; c = c->Next)
                    if (stk_top < kMaxSerialNodes)
                        stk[stk_top++] = c;
            }

            for (uint32_t i = 0; i < orphan_count; ++i)
            {
                ZUIDockNode* leaf = ZUIDockFindLeaf(manager->DockTree, orphan_keys[i]);
                if (leaf)
                    ZUIDockCollapseLeaf(manager->DockTree, leaf);
            }
        }

        // Re-insert panels that lost their leaf (all DragKeySeq leaves collapsed → no rect).
        // Claim any orphaned/unclaimed leaf, or split an existing one.
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            if (p->Hidden || p->ViewCount == 0)
            {
                continue;
            }
            if (ZUIDockFindLeaf(manager->DockTree, p->DockKey))
            {
                continue;
            } // already placed

            // DFS: find the first leaf (may be an orphaned root)
            ZUIDockNode* tgt = nullptr;
            {
                ZUIDockNode* s[kMaxSerialNodes] = {};
                uint32_t     st                 = 0;
                if (manager->DockTree->Root)
                    s[st++] = manager->DockTree->Root;
                while (st > 0 && !tgt)
                {
                    ZUIDockNode* n = s[--st];
                    if (!n->First)
                    {
                        tgt = n;
                        break;
                    } // leaf or empty root
                    for (ZUIDockNode* c = n->First; c; c = c->Next)
                        if (st < kMaxSerialNodes)
                            s[st++] = c;
                }
            }
            if (!tgt)
            {
                break;
            } // no tree

            if (!manager->FindPanel(tgt->ContentKey))
                tgt->ContentKey = p->DockKey; // claim orphaned leaf
            else
                ZUIDockSplitH(manager->DockTree, tgt, 0.5f, tgt->ContentKey, p->DockKey);
        }

        // If all views of a panel are invisible (restored from .ini), treat as hidden.
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            if (p->Hidden || p->ViewCount == 0)
                continue;
            bool any_visible = false;
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
                if (p->Views[vi] && p->Views[vi]->Visible)
                {
                    any_visible = true;
                    break;
                }
            if (!any_visible)
                p->Hidden = true;
        }

        // Collapse hidden panels
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            if (!p->Hidden)
            {
                continue;
            }
            ZUIDockNode* leaf = ZUIDockFindLeaf(manager->DockTree, p->DockKey);
            if (leaf)
            {
                ZUIDockCollapseLeaf(manager->DockTree, leaf);
            }
        }

        return true;
    }

} // namespace ZEngine::UI
