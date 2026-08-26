#pragma once
#include <ZEngine/UI/ZUIPanel.h>

namespace ZEngine::UI
{
    // Save the full dock layout to path:
    //   - complete node tree (axes, pcts, content keys, central flags)
    //   - per-panel: ActiveTab, Hidden, ordered view titles
    // Called automatically by ZUIPanelManager::BuildUI when LayoutDirty is set.
    void ZUIDockSave(ZUIPanelManager* manager, const char* path);

    // Load a previously saved layout from path.
    //   - Rebuilds the dock tree from saved node records (old orphaned nodes stay in arena)
    //   - Restores panel state and view assignments by matching saved titles
    //     against the provided view list (pass ALL registered ZUIPanelView* pointers)
    //   - Hidden panels are collapsed in the tree
    // Returns true on success, false if file not found or version mismatch.
    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path,
                     ZUIPanelView** views, uint32_t view_count);

} // namespace ZEngine::UI
