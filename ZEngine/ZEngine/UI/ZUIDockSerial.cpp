#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdio>
#include <cstring>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // ---------------------------------------------------------------
    // Node collection helpers (DFS, parent-before-children)
    // ---------------------------------------------------------------

    static constexpr uint32_t kMaxSerialNodes = 128;

    struct NodeRecord
    {
        ZUIDockNode* Ptr;
        int          ParentId; // -1 = root
    };

    static void CollectNodes(ZUIDockNode* node, int parent_id,
                             NodeRecord records[], uint32_t* count)
    {
        if (!node || *count >= kMaxSerialNodes) { return; }
        int my_id = (int)(*count)++;
        records[my_id] = { node, parent_id };
        for (ZUIDockNode* c = node->First; c; c = c->Next)
            CollectNodes(c, my_id, records, count);
    }

    // ---------------------------------------------------------------
    // ZUIDockSave  (v3 format — adds AutoHideTabBar; incompatible with v2)
    // ---------------------------------------------------------------

    void ZUIDockSave(ZUIPanelManager* manager, const char* path)
    {
        if (!manager || !path || !path[0]) { return; }
        FILE* f = fopen(path, "w");
        if (!f) { return; }

        fprintf(f, "# ZUI Layout v3\n");

        // --- Dock tree ---
        // node <id> <parent_id> <is_leaf> <axis:0=X,1=Y> <pct> [<content_key_hex> <is_central> <auto_hide_tab_bar>]
        if (manager->DockTree && manager->DockTree->Root)
        {
            NodeRecord records[kMaxSerialNodes];
            uint32_t   count = 0;
            CollectNodes(manager->DockTree->Root, -1, records, &count);

            for (uint32_t i = 0; i < count; ++i)
            {
                ZUIDockNode* n = records[i].Ptr;
                bool is_leaf   = (n->ContentKey != 0);
                fprintf(f, "node %u %d %d %d %f",
                        i, records[i].ParentId,
                        is_leaf ? 1 : 0,
                        (n->SplitAxis == ZUIAxis::X) ? 0 : 1,
                        (double)n->PctOfParent);
                if (is_leaf)
                    fprintf(f, " %016llx %d %d",
                            (unsigned long long)n->ContentKey,
                            n->IsCentral ? 1 : 0,
                            n->AutoHideTabBar ? 1 : 0);
                fprintf(f, "\n");
            }
        }

        // --- Panels ---
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            // panel <dock_key_hex> <active_tab> <hidden> <view_count>
            // followed by one  `view <title>`  line per view
            fprintf(f, "panel %016llx %u %d %u\n",
                    (unsigned long long)p->DockKey,
                    p->ActiveTab, p->Hidden ? 1 : 0, p->ViewCount);
            for (uint32_t vi = 0; vi < p->ViewCount; ++vi)
            {
                const char* title = (p->Views[vi] && p->Views[vi]->Title)
                                  ? p->Views[vi]->Title : "";
                fprintf(f, "view %s\n", title);
            }
        }

        fclose(f);
    }

    // ---------------------------------------------------------------
    // ZUIDockLoad  (v3 format only — v2 files are intentionally incompatible)
    // ---------------------------------------------------------------

    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path,
                     ZUIPanelView** views, uint32_t view_count)
    {
        if (!manager || !path || !path[0]) { return false; }
        FILE* f = fopen(path, "r");
        if (!f) { return false; }

        // --- Version check (v3 only; v2 files are discarded, not migrated) ---
        char line[512];
        if (!fgets(line, sizeof(line), f) || strncmp(line, "# ZUI Layout v3", 15) != 0)
        {
            fclose(f);
            return false;
        }

        // --- Pass 1: parse all node records ---
        struct LoadNode {
            int      parent_id;
            int      is_leaf;
            int      axis;             // 0=X 1=Y
            float    pct;
            uint64_t content_key;      // 0 if split
            int      is_central;
            int      auto_hide_tab_bar;
        };

        LoadNode load_nodes[kMaxSerialNodes];
        uint32_t node_count = 0;

        // Also buffer panel lines for pass 2
        struct LoadPanel {
            uint64_t key;
            uint32_t active_tab;
            int      hidden;
            uint32_t view_count;
        };
        static constexpr uint32_t kMaxPanels = 32;
        LoadPanel load_panels[kMaxPanels];
        char      panel_views[kMaxPanels][kMaxTabsPerPanel][64]; // title buffers
        uint32_t  panel_count    = 0;
        int       pending_panel  = -1; // index of panel we're reading views for
        uint32_t  panel_view_idx = 0;

        while (fgets(line, sizeof(line), f))
        {
            // Strip trailing newline
            uint32_t len = (uint32_t)strlen(line);
            while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r')) { line[--len] = '\0'; }

            if (line[0] == '#' || line[0] == '\0') { continue; }

            if (strncmp(line, "node ", 5) == 0 && node_count < kMaxSerialNodes)
            {
                pending_panel = -1; // end any ongoing panel read
                LoadNode& n = load_nodes[node_count];
                n.content_key = 0; n.is_central = 0; n.auto_hide_tab_bar = 0;
                unsigned long long ck = 0;
                int parsed = sscanf(line + 5, "%*u %d %d %d %f %llx %d %d",
                                    &n.parent_id, &n.is_leaf, &n.axis,
                                    &n.pct, &ck, &n.is_central, &n.auto_hide_tab_bar);
                n.content_key = (uint64_t)ck;
                if (parsed >= 4) { ++node_count; }
            }
            else if (strncmp(line, "panel ", 6) == 0 && panel_count < kMaxPanels)
            {
                LoadPanel& p = load_panels[panel_count];
                unsigned long long k = 0;
                if (sscanf(line + 6, "%llx %u %d %u", &k, &p.active_tab, &p.hidden, &p.view_count) == 4)
                {
                    p.key = (uint64_t)k;
                    pending_panel  = (int)panel_count;
                    panel_view_idx = 0;
                    ++panel_count;
                }
            }
            else if (strncmp(line, "view ", 5) == 0 && pending_panel >= 0)
            {
                if (panel_view_idx < kMaxTabsPerPanel)
                {
                    snprintf(panel_views[pending_panel][panel_view_idx],
                             sizeof(panel_views[0][0]), "%s", line + 5);
                    ++panel_view_idx;
                }
            }
        }
        fclose(f);

        if (node_count == 0) { return false; }

        // --- Rebuild dock tree ---
        if (!manager->DockTree || !manager->DockTree->Arena) { return false; }
        ArenaAllocator* arena = manager->DockTree->Arena;

        // Allocate all nodes upfront
        ZUIDockNode* new_nodes[kMaxSerialNodes] = {};
        for (uint32_t i = 0; i < node_count; ++i)
        {
            new_nodes[i] = ZPushStructCtor(arena, ZUIDockNode);
            if (!new_nodes[i]) { return false; }
        }

        // Wire the tree
        for (uint32_t i = 0; i < node_count; ++i)
        {
            LoadNode& ln  = load_nodes[i];
            ZUIDockNode* n = new_nodes[i];

            n->SplitAxis        = (ln.axis == 0) ? ZUIAxis::X : ZUIAxis::Y;
            n->PctOfParent      = ln.pct;
            n->ContentKey       = ln.content_key;
            n->IsCentral        = (ln.is_central != 0);
            n->AutoHideTabBar   = (ln.auto_hide_tab_bar != 0);

            if (ln.parent_id >= 0 && (uint32_t)ln.parent_id < node_count)
            {
                ZUIDockNode* parent = new_nodes[ln.parent_id];
                n->Parent = parent;
                n->Prev   = parent->Last;
                if (parent->Last) { parent->Last->Next = n; }
                else              { parent->First = n; }
                parent->Last = n;
                ++parent->ChildCount;
            }
        }

        manager->DockTree->Root    = new_nodes[0];
        manager->DockTree->Focused = nullptr;

        // --- Restore panel state + view assignments ---
        // Clear all current panel view assignments
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
            manager->Panels[i].ViewCount = 0;

        for (uint32_t pi = 0; pi < panel_count; ++pi)
        {
            LoadPanel& lp = load_panels[pi];
            ZUIPanel* p   = manager->FindPanel(lp.key);
            if (!p) { continue; }

            p->ActiveTab = lp.active_tab;
            p->Hidden    = (lp.hidden != 0);

            // Assign views by matching titles
            for (uint32_t vi = 0; vi < lp.view_count && p->ViewCount < kMaxTabsPerPanel; ++vi)
            {
                const char* title = panel_views[pi][vi];
                for (uint32_t gi = 0; gi < view_count; ++gi)
                {
                    if (views[gi] && views[gi]->Title &&
                        strcmp(views[gi]->Title, title) == 0)
                    {
                        p->Views[p->ViewCount++] = views[gi];
                        break;
                    }
                }
            }

            if (p->ActiveTab >= p->ViewCount && p->ViewCount > 0)
                p->ActiveTab = p->ViewCount - 1;
        }

        // Collapse hidden panels
        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            if (!p->Hidden) { continue; }
            ZUIDockNode* leaf = ZUIDockFindLeaf(manager->DockTree, p->DockKey);
            if (leaf) { ZUIDockCollapseLeaf(manager->DockTree, leaf); }
        }

        return true;
    }

} // namespace ZEngine::UI
