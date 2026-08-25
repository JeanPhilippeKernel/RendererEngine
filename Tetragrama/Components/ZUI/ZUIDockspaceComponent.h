#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <ZEngine/UI/ZUIDockspace.h>

namespace Tetragrama::Components
{
    // Layout coordinator — must be registered FIRST with ZUILayer so it runs before
    // the panels it manages. Sets each panel's RegionX/Y/W/H based on ScreenW/H then
    // renders the menu bar. The panels render themselves in subsequent BuildUI calls.
    class ZUIDockspaceComponent : public ZUIComponent
    {
    public:
        ZUIDockspaceComponent()          = default;
        ~ZUIDockspaceComponent() override = default;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "Dockspace",
                        bool    visibility = true) override;

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override;

        // Panels registered here get their regions assigned each frame
        ZUIComponent* Viewport   = nullptr;
        ZUIComponent* Hierarchy  = nullptr;
        ZUIComponent* Inspector  = nullptr;
        ZUIComponent* Log        = nullptr;
        ZUIComponent* Project    = nullptr;
        ZUIComponent* StatusBar  = nullptr;

    private:
        ZEngine::UI::ZUIDockTree* m_dock_tree = nullptr;

        // Workspace resize state — separate from the normal hit-test pass.
        // RAD Debugger style: dividers check mouse bounds directly, not through
        // the z-ordered box tree. This gives dividers priority over panel content.
        struct Divider {
            const char* leaf_name;  // which dock leaf to resize on drag
            bool        horizontal; // true = drag Y (top/bottom split), false = drag X
            bool        dragging = false;
        };
        // Four dividers: left|center, center|right, top|bottom, left-bottom|right-bottom
        Divider m_dividers[4] = {
            {"Hierarchy", false, false},  // vertical line: hierarchy | viewport
            {"Inspector", false, false},  // vertical line: viewport | inspector (resize right)
            {"Viewport",  true,  false},  // horizontal line: viewport | bottom panels
            {"Log",       false, false},  // vertical line: log | project
        };
    };
    ZDEFINE_PTR(ZUIDockspaceComponent);
} // namespace Tetragrama::Components
