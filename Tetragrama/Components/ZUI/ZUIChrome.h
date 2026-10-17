#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Components::Chrome
{
    // Row heights of the editor chrome, from the Figma frame "3D game engine editor" (3:2503).
    static constexpr float kMenuBarH   = 28.f;
    static constexpr float kLevelTabsH = 30.f;
    static constexpr float kToolbarH   = 42.f;
    static constexpr float kStatusBarH = 26.f;
    static constexpr float kTopH       = kMenuBarH + kLevelTabsH + kToolbarH; // 100

    /// @brief Actions requested by the main toolbar this frame.
    struct ToolbarResult
    {
        bool SaveScene     = false;
        bool OpenSettings  = false;
        bool NewCollection = false; ///< Add menu: create an empty collection actor
        bool NewActor      = false; ///< Add menu: create an empty actor
    };

    /// @brief Document tab bar, directly under the menu bar: one tab per view of the viewport slot.
    /// @param panels     Panel manager owning the viewport slot (its tabs are listed and driven from here).
    /// @param level_name Title shown on the viewport (level) tab.
    /// @param dirty      Appends '*' to the level tab when the level has unsaved changes.
    void          BuildLevelTabs(ZEngine::UI::ZUIContext* ctx, ZEngine::UI::ZUIPanelManager* panels, const char* level_name, bool dirty);

    /// @brief Main editor toolbar (42 px): save, mode/add/script/cinematic menus, transport, source control, settings.
    /// Controls without an engine feature behind them are drawn but inert.
    ToolbarResult BuildMainToolbar(ZEngine::UI::ZUIContext* ctx);
} // namespace Tetragrama::Components::Chrome
