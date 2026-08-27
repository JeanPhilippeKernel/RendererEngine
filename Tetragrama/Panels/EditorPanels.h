#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##hier_bg", ZFill(), ZFill());
            bg->Flags = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
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
            bg->Flags = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColor(bg, 0.09f, 0.09f, 0.095f, 1.f);
            bg->EdgeSoftness = 0.f;
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
            bg->Flags = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
            ZUIEndColumn(ctx);
        }
    };

    struct ConsolePanel : ZUIPanelView
    {
        ConsolePanel() { Title = "Console"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            (void)rect;
            ZUIBox* bg = ZUIBeginColumn(ctx, "##con_bg", ZFill(), ZFill());
            bg->Flags = bg->Flags | ZUI_DrawBackground;
            ZUIBoxSetColorArr(bg, ctx->Theme.PanelBg);
            bg->EdgeSoftness = 0.f;
            ZUIEndColumn(ctx);
        }
    };

} // namespace Tetragrama::Panels
