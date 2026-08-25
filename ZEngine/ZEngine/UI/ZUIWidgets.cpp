#include <ZEngine/UI/ZUIWidgets.h>
#include <cstring>
#include <cstdio>

namespace ZEngine::UI
{
    // ---------------------------------------------------------------
    // Internal helpers
    // ---------------------------------------------------------------

    static void SetTextColor(ZUIBox* box, const float c[4])
    {
        box->TextColor[0] = c[0]; box->TextColor[1] = c[1];
        box->TextColor[2] = c[2]; box->TextColor[3] = c[3];
    }

    static void SetBgColor(ZUIBox* box, float r, float g, float b, float a)
    {
        box->BgColor[0] = r; box->BgColor[1] = g;
        box->BgColor[2] = b; box->BgColor[3] = a;
    }

    // Default palette — all colors as {r, g, b, a}
    static constexpr float k_text_default[4]      = {0.90f, 0.90f, 0.90f, 1.f};
    static constexpr float k_text_dim[4]          = {0.60f, 0.60f, 0.65f, 1.f};
    static constexpr float k_button_bg[4]         = {0.22f, 0.22f, 0.28f, 1.f};
    static constexpr float k_separator[4]         = {0.30f, 0.30f, 0.35f, 1.f};

    // ---------------------------------------------------------------
    // Layout containers
    // ---------------------------------------------------------------

    ZUIBox* ZUIBeginColumn(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len  = (uint32_t)strlen(key);
        ZUIBox*  box  = ZUIPushBox(ctx, key, len, ZUI_None);
        box->Size[0]  = w;
        box->Size[1]  = h;
        box->LayoutAxis = ZUIAxis::Y;
        return box;
    }

    void ZUIEndColumn(ZUIContext* ctx) { ZUIPopBox(ctx); }

    ZUIBox* ZUIBeginRow(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len  = (uint32_t)strlen(key);
        ZUIBox*  box  = ZUIPushBox(ctx, key, len, ZUI_None);
        box->Size[0]  = w;
        box->Size[1]  = h;
        box->LayoutAxis = ZUIAxis::X;
        return box;
    }

    void ZUIEndRow(ZUIContext* ctx) { ZUIPopBox(ctx); }

    // ---------------------------------------------------------------
    // ZUILabel
    // ---------------------------------------------------------------

    void ZUILabel(ZUIContext* ctx, const char* text, const float color[4])
    {
        const float* c   = color ? color : k_text_default;
        uint32_t     len = (uint32_t)strlen(text);

        ZUIBox* box   = ZUIPushBox(ctx, text, len, ZUI_DrawText);
        box->Size[0]  = ZText();
        box->Size[1]  = ZText();
        SetTextColor(box, c);
        ZUIPopBox(ctx);
    }

    // ---------------------------------------------------------------
    // ZUIButton
    // ---------------------------------------------------------------

    ZUISignal ZUIButton(ZUIContext* ctx, const char* label)
    {
        uint32_t len  = (uint32_t)strlen(label);
        ZUIBox*  box  = ZUIPushBox(ctx, label, len,
                            ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
        box->Size[0]  = ZText();
        box->Size[1]  = ZPx(24.f);
        SetBgColor(box, k_button_bg[0], k_button_bg[1], k_button_bg[2], k_button_bg[3]);
        SetTextColor(box, k_text_default);

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    // ---------------------------------------------------------------
    // ZUISeparator
    // ---------------------------------------------------------------

    void ZUISeparator(ZUIContext* ctx)
    {
        // Fixed key — separators have no meaningful persistent state
        ZUIBox* box   = ZUIPushBox(ctx, "##zui_sep", 9, ZUI_DrawBackground);
        box->Size[0]  = ZFill();
        box->Size[1]  = ZPx(1.f);
        SetBgColor(box, k_separator[0], k_separator[1], k_separator[2], k_separator[3]);
        ZUIPopBox(ctx);
    }

    // ---------------------------------------------------------------
    // ZUISpacer
    // ---------------------------------------------------------------

    void ZUISpacer(ZUIContext* ctx, float px)
    {
        ZUIBox* box = ZUIPushBox(ctx, "##zui_spacer", 12, ZUI_None);
        // Size on both axes so it works in both row and column parents
        box->Size[0] = ZPx(px);
        box->Size[1] = ZPx(px);
        ZUIPopBox(ctx);
    }

    // ---------------------------------------------------------------
    // ZUITreeNode
    // ---------------------------------------------------------------

    ZUISignal ZUITreeNode(ZUIContext* ctx, const char* label, bool* open)
    {
        // Row box — the entire row is the clickable hit target
        char row_key[256];
        snprintf(row_key, sizeof(row_key), "##tn_%s", label);
        uint32_t row_key_len = (uint32_t)strlen(row_key);

        ZUIBox* row      = ZUIPushBox(ctx, row_key, row_key_len, ZUI_Clickable);
        row->Size[0]     = ZFill();
        row->Size[1]     = ZPx(22.f);
        row->LayoutAxis  = ZUIAxis::X;

        // Disclosure indicator — ">" (closed) or "v" (open), ASCII-safe
        const char* indicator     = (open && *open) ? "v " : "> ";
        uint32_t    indicator_len = 2;
        ZUIBox* ind   = ZUIPushBox(ctx, indicator, indicator_len, ZUI_DrawText);
        ind->Size[0]  = ZPx(14.f);
        ind->Size[1]  = ZPx(22.f);
        SetTextColor(ind, k_text_dim);
        ZUIPopBox(ctx); // pop indicator

        // Label text
        uint32_t label_len = (uint32_t)strlen(label);
        ZUIBox*  txt  = ZUIPushBox(ctx, label, label_len, ZUI_DrawText);
        txt->Size[0]  = ZText();
        txt->Size[1]  = ZPx(22.f);
        SetTextColor(txt, k_text_default);
        ZUIPopBox(ctx); // pop label

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIPopBox(ctx); // pop row

        if ((sig.Flags & ZUI_SignalClicked) && open)
        {
            *open = !(*open);
        }

        return sig;
    }

} // namespace ZEngine::UI
