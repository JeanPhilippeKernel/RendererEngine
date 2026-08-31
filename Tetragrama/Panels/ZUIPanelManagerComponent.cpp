#include <Tetragrama/Panels/ZUIPanelManagerComponent.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

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
        hierarchy.m_layer            = parent;
        inspector.m_layer            = parent;
        project.m_layer              = parent;
        viewport.m_layer             = parent;

        constexpr float kLeft        = 0.14f; // Viewport-focused default
        constexpr float kRight       = 0.18f;
        constexpr float kBottom      = 0.22f;

        ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root, kLeft, ZUIDockHashName("Hierarchy"), 0);

        ZUIDockNode* mid_right = Manager.DockTree->Root->Last;
        ZUIDockSplitH(Manager.DockTree, mid_right, 1.f - kRight, 0, ZUIDockHashName("Inspector"));

        ZUIDockNode* center = mid_right->First;
        ZUIDockSplitV(Manager.DockTree, center, 1.f - kBottom, ZUIDockHashName("Viewport"), 0);

        ZUIDockNode* bottom_node = center->Last;
        ZUIDockSplitH(Manager.DockTree, bottom_node, 0.40f, ZUIDockHashName("Console"), ZUIDockHashName("Project"));

        auto* p_hier = Manager.AddPanel(ZUIDockHashName("Hierarchy"));
        Manager.AddView(p_hier, &hierarchy);

        viewport.Closeable = false; // Main viewport is permanent — no close button
        profiler.Visible   = false; // Hidden by default; opened from status bar
        importer.Visible   = false; // Hidden by default; opened from status bar
        auto* p_vp         = Manager.AddPanel(ZUIDockHashName("Viewport"));
        Manager.AddView(p_vp, &viewport);
        Manager.AddView(p_vp, &profiler); // Tabs on the Viewport dock slot
        Manager.AddView(p_vp, &importer);

        auto* p_insp = Manager.AddPanel(ZUIDockHashName("Inspector"));
        Manager.AddView(p_insp, &inspector);

        auto* p_out = Manager.AddPanel(ZUIDockHashName("Console"));
        Manager.AddView(p_out, &output);

        auto* p_proj = Manager.AddPanel(ZUIDockHashName("Project"));
        Manager.AddView(p_proj, &project);

        importer.Initialize(parent); // allocate importers from ImportPipeline budget

        // EngineAssetsBackend is rooted at <cwd>/ZodiacEngine (mounted at /ZodiacEngine).
        // Settings/ is created by the engine build. Verify it exists via the backend,
        // then use VFSPath::ResolveNative to produce the correct native path — no
        // manual string concatenation. Falls back to the working directory if absent.
        char  layout_path[512] = {};
        bool  settings_ok      = false;
        auto* eng              = ZEngine::Engine::GetContext();
        if (eng)
        {
            auto settings_check = ZEngine::Core::VFS::VFSPath::Parse("/Settings");
            auto layout_vpath   = ZEngine::Core::VFS::VFSPath::Parse("/Settings/zui_layout.ini");
            if (settings_check.Succeeded() && layout_vpath.Succeeded() && eng->EngineAssetsBackend.Exists(settings_check.Value()))
            {
                layout_vpath.Value().ResolveNative(eng->EngineAssetsBackend.NativeRoot(), layout_path, sizeof(layout_path));
                settings_ok = true;
            }
        }
        if (!settings_ok)
            snprintf(layout_path, sizeof(layout_path), "zui_layout.ini");

        ZUIPanelView* all_views[] = {&hierarchy, &viewport, &inspector, &output, &project, &profiler, &importer};
        Manager.SetLayoutPath(layout_path);
        ZUIDockLoad(&Manager, layout_path, all_views, 7);
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
