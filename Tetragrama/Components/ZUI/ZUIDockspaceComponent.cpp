#include <Tetragrama/Components/ZUI/ZUIDockspaceComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components
{
    static constexpr float k_dim[4]   = {0.55f, 0.55f, 0.60f, 1.f};
    static constexpr float k_text[4]  = {0.90f, 0.90f, 0.90f, 1.f};
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
        bg->BgColor[0]  = 0.10f; bg->BgColor[1] = 0.10f;
        bg->BgColor[2]  = 0.11f; bg->BgColor[3]  = 1.f;

        // --- Menu bar ---
        ZUIBox* menu = ZUIBeginRow(ctx, "##menubar", ZFill(), ZPx(kMenuH));
        menu->Flags  = menu->Flags | ZUI_DrawBackground;
        menu->BgColor[0] = 0.15f; menu->BgColor[1] = 0.15f;
        menu->BgColor[2] = 0.18f; menu->BgColor[3]  = 1.f;

        ZUISpacer(ctx, 8.f);
        ZUILabel(ctx, "ZodiacEngine", k_text);
        ZUISpacer(ctx, 16.f);
        ZUILabel(ctx, "|", k_dim);
        ZUISpacer(ctx, 8.f);

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

        ZUISpacer(ctx, 16.f);
        ZUILabel(ctx, "|", k_dim);
        ZUISpacer(ctx, 8.f);

        // Panel visibility toggles
        auto PanelToggle = [&](ZUIComponent* cmp, const char* label_on, const char* label_off) {
            if (!cmp) { return; }
            ZUISignal s = ZUIButton(ctx, cmp->Visible ? label_on : label_off);
            if (s.Flags & ZUI_SignalClicked) { cmp->Visible = !cmp->Visible; }
            ZUISpacer(ctx, 4.f);
        };

        PanelToggle(Hierarchy,  "Hierarchy##on",  "Hierarchy##off");
        PanelToggle(Inspector,  "Inspector##on",  "Inspector##off");
        PanelToggle(Viewport,   "Scene##on",      "Scene##off");
        PanelToggle(Log,        "Console##on",    "Console##off");
        PanelToggle(Project,    "Project##on",    "Project##off");

        ZUIEndRow(ctx);
        ZUIEndColumn(ctx);
    }
} // namespace Tetragrama::Components
