#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    // ================================================================
    // Minimal panel stubs — clean slate for editor rebuild.
    // Each panel renders only its title on a flat background.
    // Complex implementations (tree view, inspector, etc.) will be
    // re-added once the docking + style system is fully validated.
    // ================================================================

    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##hier_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
            ZUILabel(ctx, "Hierarchy", ctx->Theme.TextDim);
            ZUIEndColumn(ctx);
        }
    };

    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##vp_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(bg, 0.09f, 0.09f, 0.095f, 1.f);  // distinct dark for 3D area
            bg->EdgeSoftness = 0.f;
            ZUILabel(ctx, "Viewport", ctx->Theme.TextDim);
            ZUIEndColumn(ctx);
        }
    };

    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##insp_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
            ZUILabel(ctx, "Inspector", ctx->Theme.TextDim);
            ZUIEndColumn(ctx);
        }
    };

    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() { Title = "Console"; }

        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##out_bg", ZFill(), ZFill());
            bg->Flags  = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
            ZUILabel(ctx, "Output", ctx->Theme.TextDim);
            ZUIEndColumn(ctx);
        }
    };

} // namespace Tetragrama::Panels
