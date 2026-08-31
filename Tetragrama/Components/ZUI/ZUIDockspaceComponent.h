#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Components
{
    // Layout coordinator — must be registered FIRST with ZUILayer so it runs before
    // the panels it manages. Sets each panel's RegionX/Y/W/H based on ScreenW/H then
    // renders the menu bar. The panels render themselves in subsequent BuildUI calls.

    /// @brief Full-screen layout coordinator and menu bar host.  Must be registered
    ///        first with ZUILayer so it runs before the panels it positions.  Each
    ///        frame it assigns RegionX/Y/W/H on registered ZUIComponent pointers
    ///        (Viewport, Hierarchy, Inspector, Log, Project, StatusBar) and then
    ///        renders the menu bar and any open overlay windows (Settings, etc.).
    class ZUIDockspaceComponent : public ZUIComponent
    {
    public:
        ZUIDockspaceComponent()           = default;
        ~ZUIDockspaceComponent() override = default;

        /// @brief Stores @p parent and name; no heap allocation here.
        /// @param parent     Owning ZUI layer.
        /// @param name       Component name.
        /// @param visibility Initial visibility.
        void          Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "Dockspace", bool visibility = true) override;

        /// @brief Positions panels, renders the menu bar, and handles modal windows.
        /// @param ctx ZUI context for the current frame.
        void          BuildUI(ZEngine::UI::ZUIContext* ctx) override;

        // Set by Editor.cpp after both PanelManager and Shell are initialized.
        // Provides access to panel visibility and layout reset from the Settings window.
        ZEngine::UI::ZUIPanelManager* ShellPanelManager = nullptr;

        // Panels registered here get their regions assigned each frame
        ZUIComponent* Viewport  = nullptr;
        ZUIComponent* Hierarchy = nullptr;
        ZUIComponent* Inspector = nullptr;
        ZUIComponent* Log       = nullptr;
        ZUIComponent* Project   = nullptr;
        ZUIComponent* StatusBar = nullptr;

    private:
        ZEngine::UI::ZUIDockTree* m_dock_tree = nullptr;

        // Engine Settings window
        bool  m_settings_open        = false;
        bool  m_settings_just_opened = false; // skip click-outside on the opening frame
        int   m_settings_page        = 0;     // 0=Grid, 1=Renderer, 2=Theme
        float m_modal_x              = -1.f;  // -1 = not yet positioned (centers on first open)
        float m_modal_y              = -1.f;
        float m_modal_w              = 600.f;
        float m_modal_h              = 420.f;

        // Performances window
        bool m_perf_open = false;

        // Workspace resize state — separate from the normal hit-test pass.
        // RAD Debugger style: dividers check mouse bounds directly, not through
        // the z-ordered box tree. This gives dividers priority over panel content.
        struct Divider
        {
            const char* leaf_name  = nullptr; // which dock leaf to resize on drag
            bool        horizontal = false;   // true = drag Y (top/bottom), false = drag X (left/right)
            bool        use_near   = false;   // true = divider on left/top edge; false = right/bottom edge
            bool        dragging = false;
        };
        // leaf_name, horizontal, use_near(left/top vs right/bottom edge)
        Divider m_dividers[4] = {
            {"Hierarchy", false, false}, // vertical at hierarchy's RIGHT edge
            {"Inspector", false,  true}, // vertical at inspector's LEFT edge
            { "Viewport",  true, false}, // horizontal at viewport's BOTTOM edge
            {      "Log", false, false}, // vertical at log's RIGHT edge
        };
    };
    ZDEFINE_PTR(ZUIDockspaceComponent);
} // namespace Tetragrama::Components
