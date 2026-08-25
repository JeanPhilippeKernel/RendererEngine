#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/UI/ZUIDockspace.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4]   = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float kMenuH     = 26.f;
    static constexpr float kStatusH   = 28.f;
    static constexpr float kLeftW     = 0.18f;
    static constexpr float kRightW    = 0.22f;
    static constexpr float kBottomH   = 0.25f;

    static void AssignRegion(ZUIComponent* cmp, float x, float y, float w, float h)
    {
        if (!cmp || cmp->Detached) { return; }
        cmp->RegionX = x;
        cmp->RegionY = y;
        cmp->RegionW = w;
        cmp->RegionH = h;
    }

    void ZUIDockspaceComponent::Initialize(Tetragrama::Layers::ZUILayer* parent,
                                           cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;

        // Build the dockspace split tree once at startup
        auto* arena = ParentLayer ? &ParentLayer->LocalArena : nullptr;
        if (arena)
        {
            m_dock_tree = ZUIDockTreeCreate(arena);

            // Root: H split → left 18% (Hierarchy) | right 82%
            ZUIDockSplitH(m_dock_tree, m_dock_tree->Root,
                          kLeftW,
                          ZUIDockHashName("Hierarchy"),
                          0);

            // Right child of root: H split → left 78% (center) | right 22% (Inspector)
            ZUIDockNode* right_node = m_dock_tree->Root->Last;
            ZUIDockSplitH(m_dock_tree, right_node,
                          1.f - kRightW,
                          0,
                          ZUIDockHashName("Inspector"));

            // Center child: V split → top 75% (Viewport) | bottom 25%
            ZUIDockNode* center_node = right_node->First;
            ZUIDockSplitV(m_dock_tree, center_node,
                          1.f - kBottomH,
                          ZUIDockHashName("Viewport"),
                          0);

            // Bottom child: H split → left 40% (Log) | right 60% (Project)
            ZUIDockNode* bottom_node = center_node->Last;
            ZUIDockSplitH(m_dock_tree, bottom_node,
                          0.40f,
                          ZUIDockHashName("Log"),
                          ZUIDockHashName("Project"));
        }
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible) { return; }

        float sw       = (float)ctx->ScreenW;
        float sh       = (float)ctx->ScreenH;
        float menu_h   = kMenuH   * ctx->UIScale;
        float status_h = kStatusH * ctx->UIScale;

        // Recompute dock rects from the current window size
        if (m_dock_tree)
        {
            const float root_rect[4] = {0.f, menu_h, sw, sh - status_h};
            ZUIDockLayout(m_dock_tree, root_rect);

            auto AssignFromDock = [&](ZUIComponent* cmp, const char* panel_name)
            {
                if (!cmp || cmp->Detached) { return; }
                float r[4];
                if (ZUIDockRectForKey(m_dock_tree, ZUIDockHashName(panel_name), r))
                    AssignRegion(cmp, r[0], r[1], r[2] - r[0], r[3] - r[1]);
            };

            AssignFromDock(Hierarchy, "Hierarchy");
            AssignFromDock(Inspector, "Inspector");
            AssignFromDock(Viewport,  "Viewport");
            AssignFromDock(Log,       "Log");
            AssignFromDock(Project,   "Project");
        }
        AssignRegion(StatusBar, 0.f, sh - status_h, sw, status_h);

        // --- Full-screen background ---
        ZUIBox* bg   = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags    = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        ZUIBoxSetCornerRadius(bg, 0.f);
        bg->EdgeSoftness = 0.f;

        // --- Menu bar ---
        if (ZUIBeginMenuBar(ctx))
        {
            ZUISpacer(ctx, 4.f);

            // "File" menu
            if (ZUIBeginMenu(ctx, "File"))
            {
                if (ZUIMenuItem(ctx, "New Scene"))        { /* TODO */ }
                if (ZUIMenuItem(ctx, "Open Scene..."))    { /* TODO */ }
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Save Scene"))       { /* TODO */ }
                if (ZUIMenuItem(ctx, "Save Scene As...")) { /* TODO */ }
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Quit"))             { /* TODO */ }
                ZUIEndMenu(ctx);
            }

            // "Edit" menu
            if (ZUIBeginMenu(ctx, "Edit"))
            {
                if (ZUIMenuItem(ctx, "Undo", false)) {}
                if (ZUIMenuItem(ctx, "Redo", false)) {}
                ZUISeparator(ctx);
                if (ZUIMenuItem(ctx, "Select All")) { /* TODO */ }
                ZUIEndMenu(ctx);
            }

            // "View" menu — toggle panel visibility
            if (ZUIBeginMenu(ctx, "View"))
            {
                if (Viewport)  { bool v = Viewport->Visible;  if (ZUIToggleButton(ctx, "Scene##vm",     &v, ZFill(), ZSPx(ctx, 22.f))) Viewport->Visible  = v; }
                if (Hierarchy) { bool v = Hierarchy->Visible; if (ZUIToggleButton(ctx, "Hierarchy##vm", &v, ZFill(), ZSPx(ctx, 22.f))) Hierarchy->Visible = v; }
                if (Inspector) { bool v = Inspector->Visible; if (ZUIToggleButton(ctx, "Inspector##vm", &v, ZFill(), ZSPx(ctx, 22.f))) Inspector->Visible = v; }
                if (Log)       { bool v = Log->Visible;       if (ZUIToggleButton(ctx, "Console##vm",   &v, ZFill(), ZSPx(ctx, 22.f))) Log->Visible       = v; }
                if (Project)   { bool v = Project->Visible;   if (ZUIToggleButton(ctx, "Project##vm",   &v, ZFill(), ZSPx(ctx, 22.f))) Project->Visible   = v; }
                ZUIEndMenu(ctx);
            }

            ZUILabel(ctx, " | ", k_dim);

            // Scene name
            if (ParentLayer && ParentLayer->CurrentApp)
            {
                auto* app = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);
                if (app->Configuration)
                {
                    const char* sname = app->Configuration->ActiveSceneName.empty()
                                      ? "-" : app->Configuration->ActiveSceneName.c_str();
                    char scene_buf[128];
                    snprintf(scene_buf, sizeof(scene_buf), "Scene: %s", sname);
                    ZUILabel(ctx, scene_buf, k_dim);
                }
            }

            ZUIEndMenuBar(ctx);
        }

        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
