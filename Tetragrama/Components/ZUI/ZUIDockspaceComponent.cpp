#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
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
    }

    void ZUIDockspaceComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible) { return; }

        float sw   = (float)ctx->ScreenW;
        float sh   = (float)ctx->ScreenH;
        float lw   = sw * kLeftW;
        float rw   = sw * kRightW;
        float bh   = sh * kBottomH;
        float midy = kMenuH;
        float midb = sh - bh - kStatusH;   // bottom of center/side panels
        float midh = sh - kMenuH - bh - kStatusH;
        float midw = sw - lw - rw;
        float midx = lw;

        // Assign layout regions to each panel
        AssignRegion(Hierarchy,  0.f,        midy, lw,        sh - midy - kStatusH);
        AssignRegion(Inspector,  sw - rw,    midy, rw,        sh - midy - kStatusH);
        AssignRegion(Viewport,   midx,       midy, midw,      midh);
        AssignRegion(Log,        0.f,        midb, sw * 0.40f, bh);
        AssignRegion(Project,    sw * 0.40f, midb, midw + rw - sw * 0.40f + lw, bh);
        AssignRegion(StatusBar,  0.f,        sh - kStatusH, sw, kStatusH);

        // --- Full-screen background ---
        ZUIBox* bg   = ZUIBeginColumn(ctx, "##dockspace_bg", ZPx(sw), ZPx(sh));
        bg->Flags    = bg->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bg->FloatPos[0] = 0.f;
        bg->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(bg, ctx->Theme.WindowBg);
        ZUIBoxSetCornerRadius(bg, 0.f);

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
                if (Viewport)  { bool v = Viewport->Visible;  if (ZUIToggleButton(ctx, "Scene##vm",     &v, ZFill(), ZPx(22.f))) Viewport->Visible  = v; }
                if (Hierarchy) { bool v = Hierarchy->Visible; if (ZUIToggleButton(ctx, "Hierarchy##vm", &v, ZFill(), ZPx(22.f))) Hierarchy->Visible = v; }
                if (Inspector) { bool v = Inspector->Visible; if (ZUIToggleButton(ctx, "Inspector##vm", &v, ZFill(), ZPx(22.f))) Inspector->Visible = v; }
                if (Log)       { bool v = Log->Visible;       if (ZUIToggleButton(ctx, "Console##vm",   &v, ZFill(), ZPx(22.f))) Log->Visible       = v; }
                if (Project)   { bool v = Project->Visible;   if (ZUIToggleButton(ctx, "Project##vm",   &v, ZFill(), ZPx(22.f))) Project->Visible   = v; }
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
