#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>

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
    };
    ZDEFINE_PTR(ZUIDockspaceComponent);
} // namespace Tetragrama::Components
