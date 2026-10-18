// clang-format off
#include <ZEngine/Helpers/MemoryOperations.h>
#include <Tetragrama/Components/ZUI/ZUIStatusBarComponent.h>
#include <Tetragrama/Editor.h>
#include <ZEngine/ECS/ActorManager.h>
#include <ZEngine/ECS/Components/NameComponent.h>
#include <ZEngine/Engine.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstdio>
#include <cstring>
#include <Tetragrama/EditorScene.h>
// clang-format on

using namespace ZEngine::UI;
using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace Tetragrama::Components
{
    void ZUIStatusBarComponent::Initialize(Tetragrama::Layers::ZUILayer* parent, cstring name, bool visibility)
    {
        ParentLayer = parent;
        Name        = name;
        Visible     = visibility;
    }

    // HOT PATH — runs every frame, no heap allocation allowed.
    void ZUIStatusBarComponent::BuildUI(ZUIContext* ctx)
    {
        if (!Visible || !ParentLayer || !ParentLayer->CurrentApp)
            return;

        auto*   app      = reinterpret_cast<EditorPtr>(ParentLayer->CurrentApp);

        float   sw       = RegionW > 0 ? RegionW : (float) ctx->ScreenW;
        float   sy       = RegionW > 0 ? RegionY : (float) ctx->ScreenH - kBarH;

        // Figma "Editor status bar": 26 px, 12 px side padding, 18 px between items, 1 px top border.
        ZUIBox* bar      = ZUIBeginRow(ctx, "##status_bar", ZPx(sw), ZPx(kBarH));
        bar->Flags       = bar->Flags | ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY;
        bar->FloatPos[0] = RegionW > 0 ? RegionX : 0.f;
        bar->FloatPos[1] = sy;
        ZUIBoxSetColorArr(bar, ctx->Theme.StatusBarBg);
        bar->EdgeSoftness = 0.f;
        bar->Padding[0] = bar->Padding[2] = 12.f;
        bar->Gap                          = 18.f;
        bar->CrossAlign                   = ZUIAlign::Center;

        // Drawer toggle: [icon] [label]; click shows or hides the matching panel.
        auto drawer                       = [&](const char* key, ZUIIcon icon, const char* label, const char* panel_name) {
            ZUIBox* b     = ZUIBeginRow(ctx, key, ZFit(), ZPx(kBarH - 2.f));
            b->Flags      = b->Flags | ZUI_Clickable | ZUI_KeyboardActivate;
            b->Gap        = 18.f;
            b->CrossAlign = ZUIAlign::Center;
            ZUIIconBox(ctx, icon, 13.f, ctx->Theme.TextDefault);
            ZUILabelTight(ctx, label, ctx->Theme.TextDefault, ZUIFontSize::Body);
            ZUISignal s = ZUISignalFromBox(ctx, b);
            ZUIEndRow(ctx);
            if ((s.Flags & ZUI_SignalClicked) && ShellPanelManager)
            {
                bool on = ShellPanelManager->IsPanelVisible(panel_name);
                ShellPanelManager->SetPanelVisible(panel_name, !on);
            }
        };
        auto divider = [&](const char* key) {
            ZUIBox* d  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground);
            d->Size[0] = ZPx(1.f);
            d->Size[1] = ZPx(22.f);
            ZUIBoxSetColorArr(d, ctx->Theme.PanelBorder);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        };

        drawer("##sb_drawer", ZUIIcon::PanelBottom, "Content Drawer", "Content Browser");
        divider("##sb_div0");
        drawer("##sb_log", ZUIIcon::Terminal, "Output Log", "Console");

        // Fill pushes the right group to the edge.
        {
            ZUIBox* f  = ZUIPushBox(ctx, "##sb_fill", 9, ZUI_None);
            f->Size[0] = ZFill();
            f->Size[1] = ZPx(1.f);
            ZUIPopBox(ctx);
        }

        auto* edit_scene = app->CurrentScene ? reinterpret_cast<EditorScenePtr>(app->CurrentScene) : nullptr;
        ZUILabelTight(ctx, (edit_scene && edit_scene->IsDirty()) ? "1 Unsaved" : "All Saved", ctx->Theme.TextDim, ZUIFontSize::Body);

        // 1 px top border.
        ZUIBox* line      = ZUIPushBox(ctx, "##status_top", 12, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        line->Size[0]     = ZPct(1.f);
        line->Size[1]     = ZPx(1.f);
        line->FloatPos[0] = 0.f;
        line->FloatPos[1] = 0.f;
        ZUIBoxSetColorArr(line, ctx->Theme.PanelBorder);
        line->EdgeSoftness = 0.f;
        ZUIPopBox(ctx);

        ZUIEndRow(ctx);
    }

} // namespace Tetragrama::Components
