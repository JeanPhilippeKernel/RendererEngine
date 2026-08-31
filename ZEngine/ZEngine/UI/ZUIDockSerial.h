#pragma once
#include <ZEngine/UI/ZUIPanel.h>

namespace ZEngine::UI
{
    /// @brief Save the full dock layout to @p path (v4 format).
    ///
    /// Writes the complete node tree (axes, pcts, content keys, central flags)
    /// and per-panel state (ActiveTab, Hidden, ordered view titles).
    /// Called automatically by ZUIPanelManager::BuildUI when LayoutDirty is set.
    /// @param manager Owning panel manager.
    /// @param path    File path to write; no-op if null or empty.
    void ZUIDockSave(ZUIPanelManager* manager, const char* path);

    /// @brief Load a previously saved dock layout from @p path.
    ///
    /// Rebuilds the dock tree, restores panel state and view assignments by
    /// matching saved titles against @p views. Hidden panels are collapsed in
    /// the tree. Only v4 format files are accepted.
    /// @param manager    Owning panel manager.
    /// @param path       File path to read.
    /// @param views      Array of ALL registered ZUIPanelView pointers.
    /// @param view_count Length of @p views.
    /// @returns true on success; false if file not found or version mismatch.
    bool ZUIDockLoad(ZUIPanelManager* manager, const char* path, ZUIPanelView** views, uint32_t view_count);

} // namespace ZEngine::UI
