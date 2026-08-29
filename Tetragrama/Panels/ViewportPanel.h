#pragma once
#include <Tetragrama/Panels/PanelHelpers.h>
#include <ZEngine/UI/ZUIPanel.h>

namespace Tetragrama::Panels
{
    struct ViewportPanel : ZEngine::UI::ZUIPanelView
    {
        ViewportPanel()
        {
            Title = "Viewport";
        }

        void BuildContent(ZEngine::UI::ZUIContext* ctx, float rect[4]) override
        {
            (void) rect;
            const float c[4] = {0.09f, 0.09f, 0.095f, 1.f};
            EmptyPanelBg(ctx, "##vp_bg", c, "Viewport");
        }
    };
} // namespace Tetragrama::Panels
