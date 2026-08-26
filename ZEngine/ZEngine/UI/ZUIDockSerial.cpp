#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <cstdio>
#include <cstring>

namespace ZEngine::UI
{

    // ---------------------------------------------------------------
    // ZUIDockSave
    // ---------------------------------------------------------------

    static void WriteLeaves(FILE* f, ZUIDockNode* node)
    {
        if (!node) { return; }
        if (node->ContentKey != 0)
            fprintf(f, "leaf %016llx %f\n",
                    (unsigned long long)node->ContentKey,
                    (double)node->PctOfParent);
        for (ZUIDockNode* c = node->First; c; c = c->Next)
            WriteLeaves(f, c);
    }

    void ZUIDockSave(ZUIPanelManager* manager, const char* path)
    {
        if (!manager || !path || !path[0]) { return; }
        FILE* f = fopen(path, "w");
        if (!f) { return; }

        fprintf(f, "# ZUI Layout v1\n");

        if (manager->DockTree)
            WriteLeaves(f, manager->DockTree->Root);

        for (uint32_t i = 0; i < manager->PanelCount; ++i)
        {
            ZUIPanel* p = &manager->Panels[i];
            fprintf(f, "panel %016llx %u %d\n",
                    (unsigned long long)p->DockKey,
                    p->ActiveTab,
                    (int)p->Hidden);
        }

        fclose(f);
    }

    // ---------------------------------------------------------------
    // ZUIDockLoad
    // ---------------------------------------------------------------

    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path)
    {
        if (!manager || !path || !path[0]) { return false; }
        FILE* f = fopen(path, "r");
        if (!f) { return false; }

        char line[512];

        // Pass 1: leaf split ratios
        while (fgets(line, sizeof(line), f))
        {
            if (line[0] != 'l') { continue; }
            // "leaf " is 5 chars
            unsigned long long key = 0; float pct = 1.f;
            if (sscanf(line + 5, "%llx %f", &key, &pct) == 2 && manager->DockTree)
            {
                ZUIDockNode* leaf = ZUIDockFindLeaf(manager->DockTree, (uint64_t)key);
                if (leaf) { leaf->PctOfParent = pct; }
            }
        }

        rewind(f);

        // Pass 2: panel state — ActiveTab + Hidden; collapse hidden panels
        while (fgets(line, sizeof(line), f))
        {
            if (line[0] != 'p' || strncmp(line, "panel ", 6) != 0) { continue; }
            // "panel " is 6 chars
            unsigned long long key = 0;
            unsigned int active_tab = 0;
            int hidden = 0;
            if (sscanf(line + 6, "%llx %u %d", &key, &active_tab, &hidden) == 3)
            {
                ZUIPanel* p = manager->FindPanel((uint64_t)key);
                if (p)
                {
                    p->ActiveTab = (active_tab < p->ViewCount) ? active_tab : 0u;
                    p->Hidden    = (hidden != 0);
                }
            }
        }

        // Collapse hidden panels AFTER applying all state (order-independent)
        if (manager->DockTree)
        {
            for (uint32_t i = 0; i < manager->PanelCount; ++i)
            {
                ZUIPanel* p = &manager->Panels[i];
                if (!p->Hidden) { continue; }
                ZUIDockNode* leaf = ZUIDockFindLeaf(manager->DockTree, p->DockKey);
                if (leaf) { ZUIDockCollapseLeaf(manager->DockTree, leaf); }
            }
        }

        fclose(f);
        return true;
    }

} // namespace ZEngine::UI
