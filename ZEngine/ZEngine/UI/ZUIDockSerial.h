#pragma once
#include <ZEngine/UI/ZUIPanel.h>

namespace ZEngine::UI
{
    // Save the current dock layout (split ratios + panel state) to path.
    // Called automatically by ZUIPanelManager::BuildUI when LayoutDirty is set.
    void ZUIDockSave(ZUIPanelManager* manager, const char* path);

    // Load a previously saved layout from path.
    // Applies saved PctOfParent to matching leaf nodes and restores
    // panel ActiveTab / Hidden state.  Hidden panels are collapsed in the tree.
    // Returns true on success, false if file not found or unreadable.
    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path);

} // namespace ZEngine::UI
