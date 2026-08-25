#pragma once
#include <ZEngine/UI/ZUIPanel.h>
#include <ZEngine/UI/ZUIWidgets.h>

// ---------------------------------------------------------------
// Stub panel views — real content will be added panel by panel.
// Each one renders a labelled placeholder so the dock system is
// visible and testable immediately.
// ---------------------------------------------------------------

namespace Tetragrama::Panels
{
    using namespace ZEngine::UI;

    struct HierarchyPanel : ZUIPanelView
    {
        HierarchyPanel() { Title = "Hierarchy"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Hierarchy ]", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "DefaultScene", ctx->Theme.TextDefault);
            ZUISpacer(ctx, 2.f);
            ZUILabel(ctx, "  DirectionalLight", ctx->Theme.TextDim);
        }
    };

    struct InspectorPanel : ZUIPanelView
    {
        InspectorPanel() { Title = "Inspector"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "No actor selected", ctx->Theme.TextDim);
        }
    };

    struct ViewportPanel : ZUIPanelView
    {
        ViewportPanel() { Title = "Viewport"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            // Placeholder — scene renderer decoupled
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Scene Viewport ]", ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, "(Scene renderer removed — add back later)", ctx->Theme.TextDim);
        }
    };

    struct OutputPanel : ZUIPanelView
    {
        OutputPanel() { Title = "Output"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Output / Console ]", ctx->Theme.TextDim);
        }
    };

    struct ProjectPanel : ZUIPanelView
    {
        ProjectPanel() { Title = "Project"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Project Browser ]", ctx->Theme.TextDim);
        }
    };

    struct WatchPanel : ZUIPanelView
    {
        WatchPanel() { Title = "Watch"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Watch ]", ctx->Theme.TextDim);
        }
    };

    struct TypesPanel : ZUIPanelView
    {
        TypesPanel() { Title = "Types"; }
        void BuildContent(ZUIContext* ctx, float rect[4]) override
        {
            ZUISpacer(ctx, 6.f);
            ZUILabel(ctx, "[ Types ]", ctx->Theme.TextDim);
        }
    };

} // namespace Tetragrama::Panels
