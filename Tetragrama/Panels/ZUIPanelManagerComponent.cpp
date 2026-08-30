#include <Tetragrama/Panels/ZUIPanelManagerComponent.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>

using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    void ZUIPanelManagerComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
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
        Manager.DrawMenuBar          = false; // ZUIDockspaceComponent (shell) owns the menu bar
        Manager.DrawBuiltinStatusBar = false; // ZUIStatusBarComponent owns the status bar
        hierarchy.m_layer = parent;
        inspector.m_layer = parent;
        project.m_layer   = parent;
        viewport.m_layer  = parent;

        constexpr float kLeft   = 0.18f;
        constexpr float kRight  = 0.22f;
        constexpr float kBottom = 0.25f;

        ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root, kLeft, ZUIDockHashName("Hierarchy"), 0);

        ZUIDockNode* mid_right = Manager.DockTree->Root->Last;
        ZUIDockSplitH(Manager.DockTree, mid_right, 1.f - kRight, 0, ZUIDockHashName("Inspector"));

        ZUIDockNode* center = mid_right->First;
        ZUIDockSplitV(Manager.DockTree, center, 1.f - kBottom, ZUIDockHashName("Viewport"), 0);

        ZUIDockNode* bottom_node = center->Last;
        ZUIDockSplitH(Manager.DockTree, bottom_node, 0.40f, ZUIDockHashName("Console"), ZUIDockHashName("Project"));

        auto* p_hier = Manager.AddPanel(ZUIDockHashName("Hierarchy"));
        Manager.AddView(p_hier, &hierarchy);

        auto* p_vp = Manager.AddPanel(ZUIDockHashName("Viewport"));
        Manager.AddView(p_vp, &viewport);

        auto* p_insp = Manager.AddPanel(ZUIDockHashName("Inspector"));
        Manager.AddView(p_insp, &inspector);

        auto* p_out = Manager.AddPanel(ZUIDockHashName("Console"));
        Manager.AddView(p_out, &output);
        Manager.AddView(p_out, &profiler);  // Profiler shares the Console panel as a tab
        Manager.AddView(p_out, &importer);  // Importer shares the Console panel as a tab

        auto* p_proj = Manager.AddPanel(ZUIDockHashName("Project"));
        Manager.AddView(p_proj, &project);

        importer.Initialize(parent); // allocate importers from ImportPipeline budget

        ZUIPanelView* all_views[] = {&hierarchy, &viewport, &inspector, &output, &project, &profiler, &importer};
        Manager.SetLayoutPath("zui_layout.ini");
        ZUIDockLoad(&Manager, "zui_layout.ini", all_views, 7);
    }

    void ZUIPanelManagerComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible)
        {
            return;
        }
        float menu_h   = ZUIGetFrameHeight(ctx);
        float status_h = 28.f; // matches ZUIStatusBarComponent::kBarH
        Manager.BuildUI(ctx, menu_h, status_h);
    }

} // namespace Tetragrama::Panels
