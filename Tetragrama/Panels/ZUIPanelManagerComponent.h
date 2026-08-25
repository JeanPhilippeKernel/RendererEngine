#pragma once
#include <Tetragrama/Components/ZUI/ZUIComponent.h>
#include <Tetragrama/Layers/ZUILayer.h>
#include <Tetragrama/Panels/EditorPanels.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    // Single ZUIComponent that hosts the entire ZUIPanelManager.
    // Replaces all old separate components (Hierarchy, Inspector, etc.).
    struct ZUIPanelManagerComponent : public Tetragrama::Components::ZUIComponent
    {
        ZEngine::UI::ZUIPanelManager Manager;

        // Panel views (arena-free, owned here)
        HierarchyPanel hierarchy;
        InspectorPanel inspector;
        ViewportPanel  viewport;
        OutputPanel    output;
        ProjectPanel   project;
        WatchPanel     watch;
        TypesPanel     types;

        void Initialize(Tetragrama::Layers::ZUILayer* parent,
                        cstring name = "PanelManager",
                        bool visibility = true) override
        {
            ParentLayer = parent;
            Name        = name;
            Visible     = visibility;

            auto* arena = parent ? &parent->LocalArena : nullptr;
            if (!arena) { return; }

            Manager.Init(arena);

            // Build the dock tree — same layout as before
            using namespace ZEngine::UI;

            constexpr float kLeftW   = 0.18f;
            constexpr float kRightW  = 0.22f;
            constexpr float kBottomH = 0.25f;

            // Root: H split → Hierarchy | rest
            ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root,
                          kLeftW,
                          ZUIDockHashName("Hierarchy"), 0);

            ZUIDockNode* right = Manager.DockTree->Root->Last;
            // rest: H split → center | Inspector
            ZUIDockSplitH(Manager.DockTree, right,
                          1.f - kRightW,
                          0, ZUIDockHashName("Inspector"));

            ZUIDockNode* center = right->First;
            // center: V split → Viewport | bottom
            ZUIDockSplitV(Manager.DockTree, center,
                          1.f - kBottomH,
                          ZUIDockHashName("Viewport"), 0);

            ZUIDockNode* bottom = center->Last;
            // bottom: H split → Output | Project
            ZUIDockSplitH(Manager.DockTree, bottom,
                          0.40f,
                          ZUIDockHashName("Output"), ZUIDockHashName("Project"));

            // Register panels (each panel = one dock slot)
            auto* p_hier = Manager.AddPanel(ZUIDockHashName("Hierarchy"));
            Manager.AddView(p_hier, &hierarchy);
            Manager.AddView(p_hier, &watch);   // Hierarchy + Watch in same panel

            auto* p_insp = Manager.AddPanel(ZUIDockHashName("Inspector"));
            Manager.AddView(p_insp, &inspector);
            Manager.AddView(p_insp, &types);

            auto* p_vp   = Manager.AddPanel(ZUIDockHashName("Viewport"));
            Manager.AddView(p_vp, &viewport);

            auto* p_out  = Manager.AddPanel(ZUIDockHashName("Output"));
            Manager.AddView(p_out, &output);

            auto* p_proj = Manager.AddPanel(ZUIDockHashName("Project"));
            Manager.AddView(p_proj, &project);
        }

        void BuildUI(ZEngine::UI::ZUIContext* ctx) override
        {
            if (!Visible) { return; }
            // 26px menu bar, 24px status bar (base logical px, Manager scales internally)
            Manager.BuildUI(ctx, 26.f, 24.f);
        }
    };

} // namespace Tetragrama::Panels
