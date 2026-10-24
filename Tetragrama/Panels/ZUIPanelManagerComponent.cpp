#include <Tetragrama/Components/ZUI/ZUIChrome.h>
#include <Tetragrama/Editor.h>
#include <Tetragrama/Panels/ZUIPanelManagerComponent.h>
#include <ZEngine/Core/VFS/VFSPath.h>
#include <ZEngine/Engine.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Panels
{
    namespace Chrome = Tetragrama::Components::Chrome;

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
        viewport.Initialize(parent);

        // Default layout follows the Figma editor frame (3:2503, 1600x1000):
        //   right dock 380 px wide: Outliner (300 px + 4 px gap) over Details;
        //   remaining area: Viewport over Content Browser (286 px of 874).
        constexpr float kRight    = 380.f / 1600.f;
        constexpr float kOutliner = 304.f / 874.f;
        constexpr float kBottom   = 286.f / 874.f;

        ZUIDockSplitH(Manager.DockTree, Manager.DockTree->Root, 1.f - kRight, 0, 0);
        ZUIDockSplitV(Manager.DockTree, Manager.DockTree->Root->First, 1.f - kBottom, ZUIDockHashName("Viewport"), ZUIDockHashName("Content Browser"));
        ZUIDockSplitV(Manager.DockTree, Manager.DockTree->Root->Last, kOutliner, ZUIDockHashName("Outliner"), ZUIDockHashName("Details"));

        auto* p_hier = Manager.AddPanel(ZUIDockHashName("Outliner"));
        Manager.AddView(p_hier, &hierarchy);

        viewport.Chromeless = true;
        viewport.Closeable  = false; // Main viewport is permanent — no close button
        profiler.Visible    = false; // Hidden by default; opened from status bar
        importer.Visible    = false; // Hidden by default; opened from status bar
        auto* p_vp          = Manager.AddPanel(ZUIDockHashName("Viewport"));
        Manager.AddView(p_vp, &viewport);
        Manager.AddView(p_vp, &profiler); // Tabs on the Viewport dock slot
        Manager.AddView(p_vp, &importer);

        auto* p_insp = Manager.AddPanel(ZUIDockHashName("Details"));
        Manager.AddView(p_insp, &inspector);

        auto* p_proj = Manager.AddPanel(ZUIDockHashName("Content Browser"));
        Manager.AddView(p_proj, &project);
        Manager.AddView(p_proj, &output); // Console shares the bottom dock as a tab

        importer.Initialize(parent, &project); // allocate importers from ImportPipeline budget

        // EngineAssetsBackend is rooted at the packaged ZodiacEngine directory
        // beside the executable (mounted at /ZodiacEngine).
        // Settings/ is created by the engine build. Verify it exists via the backend,
        // then use VFSPath::ResolveNative to produce the correct native path — no
        // manual string concatenation. A missing settings directory uses the local
        // layout filename so the editor can still start with its default layout.
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
        // Import entry points (Content Browser, File menu, viewport drop) raise ShowImporter; surface the
        // Importer tab here so the request is actually honoured.
        if (ParentLayer && ParentLayer->CurrentApp)
        {
            auto* app = reinterpret_cast<Tetragrama::EditorPtr>(ParentLayer->CurrentApp);
            if (app && app->Configuration && app->Configuration->ShowImporter)
            {
                app->Configuration->ShowImporter  = false;
                app->Configuration->FocusImporter = false;
                Manager.SetPanelVisible(importer.Title, true, true);
            }
        }
        float menu_h   = Chrome::kTopH;       // menu bar + scene tabs + main toolbar
        float status_h = Chrome::kStatusBarH; // matches ZUIStatusBarComponent::kBarH
        Manager.BuildUI(ctx, menu_h, status_h);
    }

} // namespace Tetragrama::Panels
