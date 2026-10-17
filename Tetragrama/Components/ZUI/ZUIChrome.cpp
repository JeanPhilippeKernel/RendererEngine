#include <Tetragrama/Components/ZUI/ZUIChrome.h>
#include <ZEngine/Helpers/MemoryOperations.h>
#include <cstdio>

using namespace ZEngine::UI;

namespace Tetragrama::Components::Chrome
{
    namespace
    {
        bool Clicked(const ZUISignal& sig)
        {
            return (sig.Flags & ZUI_SignalClicked) != 0;
        }

        void Spacer(ZUIContext* ctx, const char* key)
        {
            ZUIBox* s  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_None);
            s->Size[0] = ZFill();
            s->Size[1] = ZPx(1.f);
            ZUIPopBox(ctx);
        }

        void Divider(ZUIContext* ctx, const char* key)
        {
            ZUIBox* d  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground);
            d->Size[0] = ZPx(1.f);
            d->Size[1] = ZPx(22.f);
            ZUIBoxSetColorArr(d, ctx->Theme.PanelBorder);
            d->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }

        ZUISignal ToolbarButton(ZUIContext* ctx, const char* key, ZUIIcon icon, const char* label, bool chevron, const float* icon_color = nullptr, bool dim = false)
        {
            return ZUIEditorControl(ctx, key, icon, label, chevron, false, icon_color, dim);
        }
    } // namespace

    void BuildLevelTabs(ZUIContext* ctx, ZUIPanelManager* panels, const char* level_name, bool dirty)
    {
        ZUIBox* row = ZUIBeginRow(ctx, "##level_tabs", ZFill(), ZPx(kLevelTabsH));
        row->Flags  = row->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(row, ctx->Theme.WindowBg);
        row->EdgeSoftness    = 0.f;

        char level_text[160] = {};
        snprintf(level_text, sizeof(level_text), "%s%s", (level_name && *level_name) ? level_name : "Untitled", dirty ? "*" : "");
        if (panels)
            panels->BuildDocumentTabBar(ctx, "Viewport", level_text, kLevelTabsH);
        ZUIEndRow(ctx); // row
    }

    ToolbarResult BuildMainToolbar(ZUIContext* ctx)
    {
        ToolbarResult result = {};
        ZUIBox*       bar    = ZUIBeginRow(ctx, "##main_toolbar", ZFill(), ZPx(kToolbarH));
        bar->Flags           = bar->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(bar, ctx->Theme.PanelBg);
        bar->EdgeSoftness = 0.f;
        bar->Padding[0] = bar->Padding[2] = 10.f;
        bar->Gap                          = 8.f;
        bar->CrossAlign                   = ZUIAlign::Center;

        if (Clicked(ToolbarButton(ctx, "##tb_save", ZUIIcon::Save, nullptr, false)))
            result.SaveScene = true;
        Divider(ctx, "##tb_div0");
        ToolbarButton(ctx, "##tb_mode", ZUIIcon::MousePointer2, "Selection Mode", true);
        if (Clicked(ToolbarButton(ctx, "##tb_add", ZUIIcon::Plus, "Add", true)))
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, ZUIHashStr("##tb_add", 8));
            const float         px = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : ctx->MousePos[0];
            const float         py = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY + 2.f : ctx->MousePos[1];
            ZUIOpenPopup(ctx, "##tb_add_menu", px, py);
        }
        if (ZUIBeginPopup(ctx, "##tb_add_menu"))
        {
            if (ZUIMenuItemEx(ctx, "Empty Actor", nullptr, false, true, (int) ZUIIcon::Box))
                result.NewActor = true;
            if (ZUIMenuItemEx(ctx, "Collection", nullptr, false, true, (int) ZUIIcon::Folder))
                result.NewCollection = true;
            ZUIEndPopup(ctx);
        }
        ToolbarButton(ctx, "##tb_scripts", ZUIIcon::Code, "Scripts", true);
        ToolbarButton(ctx, "##tb_cine", ZUIIcon::Clapperboard, "Cinematics", true);
        Divider(ctx, "##tb_div1");
        {
            const float green[4] = ZUI_TOKEN(ZEngine::UI::Tokens::AccentGreen);
            ToolbarButton(ctx, "##tb_play", ZUIIcon::Play, nullptr, false, green);
        }
        ToolbarButton(ctx, "##tb_pause", ZUIIcon::Pause, nullptr, false, nullptr, true);
        ToolbarButton(ctx, "##tb_stop", ZUIIcon::Square, nullptr, false, nullptr, true);
        ToolbarButton(ctx, "##tb_more", ZUIIcon::ChevronDown, nullptr, false);
        Spacer(ctx, "##tb_fill");
        if (Clicked(ToolbarButton(ctx, "##tb_settings", ZUIIcon::Settings, "Settings", false)))
            result.OpenSettings = true;

        // 1 px bottom border of the chrome.
        ZUIBox* line      = ZUIPushBox(ctx, "##chrome_bottom", 15, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        line->Size[0]     = ZPct(1.f);
        line->Size[1]     = ZPx(1.f);
        line->FloatPos[0] = 0.f;
        line->FloatPos[1] = kToolbarH - 1.f;
        ZUIBoxSetColorArr(line, ctx->Theme.PanelBorder);
        line->EdgeSoftness = 0.f;
        ZUIPopBox(ctx);

        ZUIEndRow(ctx);
        return result;
    }
} // namespace Tetragrama::Components::Chrome
