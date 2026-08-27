#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/EditorPanels.h>
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIDockSerial.h>

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

        HierarchyPanel       hierarchy;
        ViewportPanel        viewport;
        InspectorPanel       inspector;
        OutputPanel          output;
        ContentBrowserPanel  content;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name       = "PanelManager",
                        bool    visibility = true) override
        {
            ParentLayer = parent;
            Name        = name;
            Visible     = visibility;

            auto* arena = parent ? &parent->LocalArena : nullptr;
            if (!arena) { return; }

            Manager.Init(arena);

            using namespace ZEngine::UI;

            constexpr float kLeft    = 0.18f;  // Hierarchy width
            constexpr float kRight   = 0.22f;  // Inspector width
            constexpr float kBottom  = 0.25f;  // Output height

            // Root H-split: Hierarchy | rest
            ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root,
                          kLeft,
                          ZUIDockHashName("Hierarchy"),
                          0);

            ZUIDockNode* mid_right = Manager.DockTree->Root->Last;

            // mid_right H-split: center | Inspector
            ZUIDockSplitH(Manager.DockTree, mid_right,
                          1.f - kRight,
                          0,
                          ZUIDockHashName("Inspector"));

            ZUIDockNode* center = mid_right->First;

            // center V-split: Viewport | Output
            ZUIDockSplitV(Manager.DockTree, center,
                          1.f - kBottom,
                          ZUIDockHashName("Viewport"),
                          ZUIDockHashName("Output"));

            // Register panels
            auto* p_hier = Manager.AddPanel(ZUIDockHashName("Hierarchy"));
            Manager.AddView(p_hier, &hierarchy);

            auto* p_vp   = Manager.AddPanel(ZUIDockHashName("Viewport"));
            Manager.AddView(p_vp, &viewport);

            auto* p_insp = Manager.AddPanel(ZUIDockHashName("Inspector"));
            Manager.AddView(p_insp, &inspector);

            auto* p_out  = Manager.AddPanel(ZUIDockHashName("Output"));
            Manager.AddView(p_out, &output);
            Manager.AddView(p_out, &content); // Content Browser tab

            // Ini persistence — load saved layout (all registered views for title matching)
            ZUIPanelView* all_views[] = {
                &hierarchy, &viewport, &inspector, &output, &content
            };
            Manager.SetLayoutPath("zui_layout.ini");
            ZUIDockLoad(&Manager, "zui_layout.ini", all_views, 5); // no-op if file not found

            // No central node — all panels have normal chrome and are fully dockable,
            // including the Viewport. The central-node infrastructure (SetCentralPanel)
            // is available if a passthrough viewport is ever needed in future.
        }

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override
        {
            if (!Visible) { return; }
            Manager.BuildUI(ctx, 26.f, 24.f);
        }
    };

} // namespace Tetragrama::Panels
