#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/EditorPanels.h>
#include <ZEngine/UI/ZUIDockSerial.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    // Default editor layout — matches a standard game engine editor:
    //
    //   ┌─────────┬──────────────────────┬──────────┐
    //   │         │                      │          │
    //   │Hierarchy│      Viewport        │Inspector │
    //   │  (18%)  │       (60%)          │  (22%)   │
    //   │         ├──────────────────────┤          │
    //   │         │        Output        │          │
    //   │         │        (25%)         │          │
    //   └─────────┴──────────────────────┴──────────┘

    struct ZUIPanelManagerComponent : public Tetragrama::Components::ZUIComponent
    {
        ZEngine::UI::ZUIPanelManager Manager;

        HierarchyPanel               hierarchy;
        ViewportPanel                viewport;
        InspectorPanel               inspector;
        ConsolePanel                 output;

        void                         Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name = "PanelManager", bool visibility = true) override
        {
            ParentLayer = parent;
            Name        = name;
            Visible     = visibility;

            auto* arena = parent ? &parent->LocalArena : nullptr;
            if (!arena)
            {
                return;
            }

            Manager.Init(arena);

            using namespace ZEngine::UI;

            constexpr float kLeft   = 0.18f; // Hierarchy width
            constexpr float kRight  = 0.22f; // Inspector width
            constexpr float kBottom = 0.25f; // Output height

            // Root H-split: Hierarchy | rest
            ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root, kLeft, ZUIDockHashName("Hierarchy"), 0);

            ZUIDockNode* mid_right = Manager.DockTree->Root->Last;

            // mid_right H-split: center | Inspector
            ZUIDockSplitH(Manager.DockTree, mid_right, 1.f - kRight, 0, ZUIDockHashName("Inspector"));

            ZUIDockNode* center = mid_right->First;

            // center V-split: Viewport | Output
            ZUIDockSplitV(Manager.DockTree, center, 1.f - kBottom, ZUIDockHashName("Viewport"), ZUIDockHashName("Console"));

            // Register panels
            auto* p_hier = Manager.AddPanel(ZUIDockHashName("Hierarchy"));
            Manager.AddView(p_hier, &hierarchy);

            auto* p_vp = Manager.AddPanel(ZUIDockHashName("Viewport"));
            Manager.AddView(p_vp, &viewport);

            auto* p_insp = Manager.AddPanel(ZUIDockHashName("Inspector"));
            Manager.AddView(p_insp, &inspector);

            auto* p_out = Manager.AddPanel(ZUIDockHashName("Console"));
            Manager.AddView(p_out, &output);

            // Ini persistence — v3 format (AutoHideTabBar support)
            // Old v2 files are intentionally incompatible — delete zui_layout.ini to start fresh.
            ZUIPanelView* all_views[] = {&hierarchy, &viewport, &inspector, &output};
            Manager.SetLayoutPath("zui_layout.ini");
            ZUIDockLoad(&Manager, "zui_layout.ini", all_views, 4); // no-op if file not found or v2
        }

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override
        {
            if (!Visible)
            {
                return;
            }
            // Menu bar and status bar heights from style (not hardcoded).
            // Both use GetFrameHeight() = FontSize + FramePadding.y*2 = 19px default.
            float menu_h   = ZEngine::UI::ZUIGetFrameHeight(ctx);
            float status_h = ZEngine::UI::ZUIGetFrameHeight(ctx);
            Manager.BuildUI(ctx, menu_h, status_h);
        }
    };

} // namespace Tetragrama::Panels
