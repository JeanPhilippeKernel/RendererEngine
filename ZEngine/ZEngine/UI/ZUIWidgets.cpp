#include <ZEngine/Helpers/MemoryOperations.h>
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

    // Colors are read from ctx->Theme — no local palette constants needed.
    static void SetBgArr(ZUIBox* b, const float c[4])
    {
        b->BgColor[0]=c[0]; b->BgColor[1]=c[1]; b->BgColor[2]=c[2]; b->BgColor[3]=c[3];
    }
    static void SetBdrArr(ZUIBox* b, const float c[4])
    {
        b->BorderColor[0]=c[0]; b->BorderColor[1]=c[1]; b->BorderColor[2]=c[2]; b->BorderColor[3]=c[3];
    }

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

    ZUIBox* ZUIBeginScrollRegion(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len  = (uint32_t)strlen(key);
        ZUIBox*  box  = ZUIPushBox(ctx, key, len,
                            ZUI_Scrollable | ZUI_ClipChildren);
        box->Size[0]  = w;
        box->Size[1]  = h;
        box->LayoutAxis = ZUIAxis::Y;
        return box;
    }

    void ZUIEndScrollRegion(ZUIContext* ctx) { ZUIPopBox(ctx); }

    // ---------------------------------------------------------------
    // ZUILabel
    // ---------------------------------------------------------------

    void ZUILabel(ZUIContext* ctx, const char* text, const float color[4], ZUIFontSize size)
    {
        const float* c   = color ? color : ctx->Theme.TextDefault;
        uint32_t     len = (uint32_t)strlen(text);

        ZUIBox* box    = ZUIPushBox(ctx, text, len, ZUI_DrawText);
        box->Size[0]   = ZText();
        box->Size[1]   = ZText();
        box->FontSize  = size;
        SetTextColor(box, c);
        ZUIPopBox(ctx);
    }

    // ---------------------------------------------------------------
    // Disabled-state helpers
    // ---------------------------------------------------------------

    void ZUIBeginDisabled(ZUIContext* ctx)
    {
        ++ctx->DisabledDepth;
        ctx->Disabled = true;
    }
    void ZUIEndDisabled(ZUIContext* ctx)
    {
        if (ctx->DisabledDepth > 0) --ctx->DisabledDepth;
        ctx->Disabled = (ctx->DisabledDepth > 0);
    }

    // Dim a color array in-place when the widget is disabled
    static void ApplyDisabledDim(float c[4]) { c[3] *= 0.38f; }

    // ---------------------------------------------------------------
    // ZUIButton family
    // ---------------------------------------------------------------

    ZUISignal ZUIButton(ZUIContext* ctx, const char* label, ZUISize w, ZUISize h)
    {
        uint32_t   len   = (uint32_t)strlen(label);
        ZUIBoxFlags flags = ZUI_DrawBackground | ZUI_DrawText | ZUI_DrawBorder;
        if (!ctx->Disabled) flags = flags | ZUI_Clickable;

        ZUIBox* box         = ZUIPushBox(ctx, label, len, flags);
        box->Size[0]        = w;
        box->Size[1]        = h;
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        SetBdrArr(box, ctx->Theme.ButtonBorder);
        box->BorderThickness = 1.f;
        if (ctx->Disabled) { ApplyDisabledDim(box->BgColor); ApplyDisabledDim(box->TextColor); }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    ZUISignal ZUISmallButton(ZUIContext* ctx, const char* label)
    {
        uint32_t   len   = (uint32_t)strlen(label);
        ZUIBoxFlags flags = ZUI_DrawBackground | ZUI_DrawText;
        if (!ctx->Disabled) flags = flags | ZUI_Clickable;

        ZUIBox* box      = ZUIPushBox(ctx, label, len, flags);
        box->Size[0]     = ZText();
        box->Size[1]     = ZPx(22.f);
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        if (ctx->Disabled) { ApplyDisabledDim(box->BgColor); ApplyDisabledDim(box->TextColor); }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    ZUISignal ZUIInvisibleButton(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t    len   = (uint32_t)strlen(key);
        ZUIBoxFlags flags = ctx->Disabled ? ZUI_None : ZUI_Clickable;

        ZUIBox* box   = ZUIPushBox(ctx, key, len, flags);
        box->Size[0]  = w;
        box->Size[1]  = h;

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    bool ZUIToggleButton(ZUIContext* ctx, const char* label, bool* active,
                         ZUISize w, ZUISize h)
    {
        uint32_t   len   = (uint32_t)strlen(label);
        ZUIBoxFlags flags = ZUI_DrawBackground | ZUI_DrawText | ZUI_DrawBorder;
        if (!ctx->Disabled) flags = flags | ZUI_Clickable;

        ZUIBox* box         = ZUIPushBox(ctx, label, len, flags);
        box->Size[0]        = w;
        box->Size[1]        = h;
        box->BorderThickness = 1.f;

        // Active state uses a lighter background
        if (active && *active)
        {
            float bg[4] = { ctx->Theme.ButtonBg[0] + 0.14f,
                            ctx->Theme.ButtonBg[1] + 0.14f,
                            ctx->Theme.ButtonBg[2] + 0.14f,
                            ctx->Theme.ButtonBg[3] };
            SetBgArr(box, bg);
            SetBdrArr(box, ctx->Theme.InputFocusBorder);
        }
        else
        {
            SetBgArr(box, ctx->Theme.ButtonBg);
            SetBdrArr(box, ctx->Theme.ButtonBorder);
        }
        SetTextColor(box, ctx->Theme.TextDefault);
        if (ctx->Disabled) { ApplyDisabledDim(box->BgColor); ApplyDisabledDim(box->TextColor); }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && active)
        {
            *active = !(*active);
            return true;
        }
        return false;
    }

    ZUISignal ZUIImageButton(ZUIContext* ctx, const char* key,
                              uint32_t texture_index, ZUISize w, ZUISize h)
    {
        uint32_t    len   = (uint32_t)strlen(key);
        ZUIBoxFlags flags = ZUI_DrawBackground;
        if (!ctx->Disabled) flags = flags | ZUI_Clickable;

        ZUIBox* box          = ZUIPushBox(ctx, key, len, flags);
        box->Size[0]         = w;
        box->Size[1]         = h;
        box->TextureIndex    = texture_index;
        // Ensure bg alpha > 0 so PreparePayload emits the quad
        box->BgColor[0] = box->BgColor[1] = box->BgColor[2] = box->BgColor[3] = 1.f;
        if (ctx->Disabled) box->BgColor[3] = 0.38f;

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    // ---------------------------------------------------------------
    // ZUISeparator
    // ---------------------------------------------------------------

    void ZUISeparator(ZUIContext* ctx)
    {
        ZUIBox* box   = ZUIPushBox(ctx, "##zui_sep", 9, ZUI_DrawBackground);
        box->Size[0]  = ZFill();
        box->Size[1]  = ZPx(2.f);
        SetBgArr(box, ctx->Theme.Separator);
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
        SetTextColor(ind, ctx->Theme.TextDim);
        ZUIPopBox(ctx); // pop indicator

        // Label text
        uint32_t label_len = (uint32_t)strlen(label);
        ZUIBox*  txt  = ZUIPushBox(ctx, label, label_len, ZUI_DrawText);
        txt->Size[0]  = ZText();
        txt->Size[1]  = ZPx(22.f);
        SetTextColor(txt, ctx->Theme.TextDefault);
        ZUIPopBox(ctx); // pop label

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIPopBox(ctx); // pop row

        if ((sig.Flags & ZUI_SignalClicked) && open)
        {
            *open = !(*open);
        }

        return sig;
    }

    // ---------------------------------------------------------------
    // Popup / overlay system
    // ---------------------------------------------------------------

    void ZUIOpenPopup(ZUIContext* ctx, const char* key, float pos_x, float pos_y)
    {
        ctx->OpenPopupKey  = ZUIHashStr(key, (uint32_t)strlen(key));
        ctx->PopupPos[0]   = (pos_x >= 0.f) ? pos_x : ctx->MousePos[0];
        ctx->PopupPos[1]   = (pos_y >= 0.f) ? pos_y : ctx->MousePos[1];
    }

    bool ZUIBeginPopup(ZUIContext* ctx, const char* key)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t)strlen(key));
        if (ctx->ActivePopupKey != hash) { return false; }

        // Save current parent and escape to root so the popup box is a
        // root-level child — it renders last (on top of everything else).
        ctx->PopupSavedParent = ctx->Current;
        ctx->Current          = ctx->Root;

        uint32_t len    = (uint32_t)strlen(key);
        ZUIBox*  popup  = ZUIPushBox(ctx, key, len,
                              ZUI_DrawBackground | ZUI_DrawBorder |
                              ZUI_ClipChildren   | ZUI_FloatX | ZUI_FloatY);
        popup->Size[0]          = ZFit();
        popup->Size[1]          = ZFit();
        popup->FloatPos[0]      = ctx->PopupPos[0];
        popup->FloatPos[1]      = ctx->PopupPos[1];
        popup->LayoutAxis       = ZUIAxis::Y;
        popup->BorderThickness  = 1.f;
        SetBgArr(popup,  ctx->Theme.PanelBg);
        SetBdrArr(popup, ctx->Theme.PanelBorder);

        ctx->ActivePopupBox = popup;
        return true;
    }

    void ZUIEndPopup(ZUIContext* ctx)
    {
        ZUIPopBox(ctx);                         // pop the popup box
        ctx->Current = ctx->PopupSavedParent;  // restore original parent
        ctx->PopupSavedParent = nullptr;
    }

    void ZUIClosePopup(ZUIContext* ctx)
    {
        ctx->ActivePopupKey = 0;
    }

    bool ZUIBeginPopupContextItem(ZUIContext* ctx, const char* key,
                                   const ZUISignal& item_signal)
    {
        // Right-click while hovered → request popup at mouse position
        if ((item_signal.Flags & ZUI_SignalHovered) && ctx->MousePressed[1])
        {
            ZUIOpenPopup(ctx, key);
        }
        return ZUIBeginPopup(ctx, key);
    }

    bool ZUIMenuItem(ZUIContext* ctx, const char* label, bool enabled)
    {
        uint32_t len   = (uint32_t)strlen(label);
        ZUIBoxFlags fl = ZUI_DrawText;
        if (enabled) fl = fl | ZUI_DrawBackground | ZUI_Clickable;

        ZUIBox* box   = ZUIPushBox(ctx, label, len, fl);
        box->Size[0]  = ZFill();
        box->Size[1]  = ZPx(26.f);
        box->BgColor[3] = 0.f; // transparent base — hover fade-in via renderer
        SetTextColor(box, enabled ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ZUIClosePopup(ctx);
            return true;
        }
        return false;
    }

    // ---------------------------------------------------------------
    // Phase 7 — complex widgets
    // ---------------------------------------------------------------

    void ZUIBeginTabBar(ZUIContext* ctx, const char* key)
    {
        uint64_t hash  = ZUIHashStr(key, (uint32_t)strlen(key));
        ctx->TabBarKey = hash;

        // Read selected index from persistent state (stored in ScrollY)
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        ctx->TabBarSelectedIdx = ps ? (int)ps->ScrollY : 0;
        ctx->TabBarCurrentIdx  = 0;

        ZUIBox* outer = ZUIBeginColumn(ctx, key, ZFill(), ZFit());
        ctx->TabBarOuterBox = outer;

        // Button row
        char row_key[64];
        snprintf(row_key, sizeof(row_key), "##tbr_%s", key);
        ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(28.f));
        row->Flags  = row->Flags | ZUI_DrawBackground;
        SetBgArr(row, ctx->Theme.HeaderBg);
        ctx->TabBarRowBox = row;
    }

    bool ZUIBeginTabItem(ZUIContext* ctx, const char* label)
    {
        int idx = ctx->TabBarCurrentIdx++;
        bool active = (idx == ctx->TabBarSelectedIdx);

        // Tab button
        char btn_key[128];
        snprintf(btn_key, sizeof(btn_key), "%s##tbi_%d", label, idx);
        ZUIBoxFlags fl = ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable;
        ZUIBox* btn   = ZUIPushBox(ctx, btn_key, (uint32_t)strlen(btn_key), fl);
        btn->Size[0]  = ZText();
        btn->Size[1]  = ZPx(28.f);
        if (active) {
            SetBgArr(btn, ctx->Theme.PanelBg);
            SetTextColor(btn, ctx->Theme.TextDefault);
        } else {
            btn->BgColor[3] = 0.f;
            SetTextColor(btn, ctx->Theme.TextDim);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, btn);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->TabBarSelectedIdx = idx;
            ZUIPersistentState* ps =
                ZUIStateGetOrInsert(&ctx->StateStore, ctx->TabBarKey);
            if (ps) ps->ScrollY = (float)idx;
        }

        ctx->TabItemWasSelected = active;
        if (!active) return false;

        // Close the button row so content goes below it
        ZUIEndRow(ctx);
        ctx->TabBarRowBox = nullptr;

        // Push content column
        char content_key[128];
        snprintf(content_key, sizeof(content_key), "##tbc_%s_%d",
                 label, idx);
        ZUIBeginColumn(ctx, content_key, ZFill(), ZFit());
        return true;
    }

    void ZUIEndTabItem(ZUIContext* ctx)
    {
        ZUIEndColumn(ctx); // close content column
    }

    void ZUIEndTabBar(ZUIContext* ctx)
    {
        if (ctx->TabBarRowBox) { ZUIEndRow(ctx); } // close button row if no tab was active
        ZUIEndColumn(ctx); // close outer column
        ctx->TabBarKey     = 0;
        ctx->TabBarRowBox  = nullptr;
        ctx->TabBarOuterBox = nullptr;
    }

    ZUIBox* ZUIBeginListBox(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        ZUIBox* frame = ZUIBeginColumn(ctx, key, w, h);
        frame->Flags  = frame->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        SetBgArr(frame, ctx->Theme.InputBg);
        SetBdrArr(frame, ctx->Theme.InputBorder);
        frame->BorderThickness = 1.f;

        // Content inside a scroll region
        char sr_key[64];
        snprintf(sr_key, sizeof(sr_key), "##lbsr_%s", key);
        ZUIBeginScrollRegion(ctx, sr_key, ZFill(), ZFill());
        return frame;
    }
    void ZUIEndListBox(ZUIContext* ctx)
    {
        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }

    bool ZUISliderFloat(ZUIContext* ctx, const char* key, float* value,
                        float v_min, float v_max, ZUISize w, ZUISize h)
    {
        if (!value) return false;
        float range = v_max - v_min;
        if (range <= 0.f) range = 1.f;
        float fraction = (*value - v_min) / range;
        if (fraction < 0.f) fraction = 0.f;
        if (fraction > 1.f) fraction = 1.f;

        uint32_t len  = (uint32_t)strlen(key);
        ZUIBox*  track = ZUIPushBox(ctx, key, len,
                             ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable);
        track->Size[0] = w;
        track->Size[1] = h;
        track->LayoutAxis = ZUIAxis::X;
        SetBgArr(track, ctx->Theme.InputBg);
        SetBdrArr(track, ctx->Theme.InputBorder);
        track->BorderThickness = 1.f;

        ZUISignal sig = ZUISignalFromBox(ctx, track);
        ZUIPopBox(ctx);

        bool changed = false;
        if (sig.Flags & ZUI_SignalHeld)
        {
            // Map horizontal drag to value change
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, track->Key);
            // Box width isn't available until next frame; use 120 px estimate
            float box_w = 120.f;
            *value += sig.DragDelta[0] * (range / box_w);
            if (*value < v_min) *value = v_min;
            if (*value > v_max) *value = v_max;
            changed = (sig.DragDelta[0] != 0.f);
        }
        else if ((sig.Flags & ZUI_SignalClicked) || (sig.Flags & ZUI_SignalPressed))
        {
            // Click on track: jump to position
            float pos = ctx->MousePos[0] - track->ScreenMin[0];
            float box_w = track->ScreenMax[0] - track->ScreenMin[0];
            if (box_w > 0.f) {
                *value = v_min + (pos / box_w) * range;
                if (*value < v_min) *value = v_min;
                if (*value > v_max) *value = v_max;
                changed = true;
            }
        }
        return changed;
    }

    bool ZUIInputInt(ZUIContext* ctx, const char* key, int* value, int v_min, int v_max,
                     ZUISize w)
    {
        if (!value) return false;

        // Format as string, use TextField-style box
        char buf[32];
        snprintf(buf, sizeof(buf), "%d", *value);
        uint32_t len = (uint32_t)strlen(key);

        ZUIBox* field = ZUIPushBox(ctx, key, len,
                            ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0] = w;
        field->Size[1] = ZPx(28.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        field->BorderThickness = 1.f;

        bool focused = (ctx->FocusKey == field->Key);
        if (focused)
        {
            // Append digits from text input
            static char s_ibuf[32] = {};
            static uint64_t s_last_key = 0;
            if (s_last_key != field->Key) {
                snprintf(s_ibuf, sizeof(s_ibuf), "%d", *value);
                s_last_key = field->Key;
            }
            bool changed = false;
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char c = ctx->TextInput[i];
                if ((c >= '0' && c <= '9') || (c == '-' && strlen(s_ibuf) == 0))
                {
                    size_t l = strlen(s_ibuf);
                    if (l < sizeof(s_ibuf) - 1) { s_ibuf[l] = c; s_ibuf[l+1] = '\0'; changed = true; }
                }
            }
            if (ctx->BackspacePressed)
            {
                size_t l = strlen(s_ibuf);
                if (l > 0) { s_ibuf[l-1] = '\0'; changed = true; }
            }
            if (changed || focused) {
                int parsed = s_ibuf[0] ? atoi(s_ibuf) : 0;
                if (parsed < v_min) parsed = v_min;
                if (parsed > v_max) parsed = v_max;
                *value = parsed;
            }

            char disp[34]; snprintf(disp, sizeof(disp), "%s|", s_ibuf);
            field->Label = ZUIPushStr(&ctx->FrameArena, disp, (uint32_t)strlen(disp));

            SetBdrArr(field, ctx->Theme.InputFocusBorder);
        }
        else
        {
            field->Label = ZUIPushStr(&ctx->FrameArena, buf, (uint32_t)strlen(buf));
            SetBdrArr(field, ctx->Theme.InputBorder);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = field->Key; }
        // Also support drag to change
        if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
        {
            *value += (int)(sig.DragDelta[0] * 0.5f);
            if (*value < v_min) *value = v_min;
            if (*value > v_max) *value = v_max;
        }

        return focused && ctx->TextInputLen > 0;
    }

    bool ZUIInputTextMultiline(ZUIContext* ctx, const char* key,
                                char* buf, uint32_t buf_size,
                                ZUISize w, ZUISize h)
    {
        // Outer bordered frame
        char frame_key[64];
        snprintf(frame_key, sizeof(frame_key), "##itmf_%s", key);
        ZUIBox* frame = ZUIBeginColumn(ctx, frame_key, w, h);
        frame->Flags  = frame->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        SetBgArr(frame, ctx->Theme.InputBg);
        SetBdrArr(frame, ctx->Theme.InputBorder);
        frame->BorderThickness = 1.f;

        // Scroll region inside
        char sr_key[64];
        snprintf(sr_key, sizeof(sr_key), "##itmsr_%s", key);
        ZUIBeginScrollRegion(ctx, sr_key, ZFill(), ZFill());

        // The text as a label for now (full editing in a later pass)
        bool focused = (ctx->FocusKey == frame->Key);
        char disp[1024];
        if (focused) snprintf(disp, sizeof(disp), "%s|", buf);
        else         snprintf(disp, sizeof(disp), "%s", buf);

        ZUILabel(ctx, disp, ctx->Theme.TextDefault);

        ZUIEndScrollRegion(ctx);

        ZUISignal sig = ZUISignalFromBox(ctx, frame);
        ZUIEndColumn(ctx);

        bool changed = false;
        if ((sig.Flags & ZUI_SignalClicked)) ctx->FocusKey = frame->Key;
        if (focused)
        {
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char c = ctx->TextInput[i];
                uint32_t l = (uint32_t)Helpers::secure_strlen(buf);
                if (l + 1 < buf_size) { buf[l] = c; buf[l+1] = '\0'; changed = true; }
            }
            if (ctx->BackspacePressed)
            {
                uint32_t l = (uint32_t)Helpers::secure_strlen(buf);
                if (l > 0) { buf[l-1] = '\0'; changed = true; }
            }
        }
        return changed;
    }

    bool ZUIColorPicker4(ZUIContext* ctx, const char* key, float color[4])
    {
        // Simple version: color swatch + R/G/B/A sliders
        char outer_key[64];
        snprintf(outer_key, sizeof(outer_key), "##cp_%s", key);
        ZUIBeginColumn(ctx, outer_key, ZFill(), ZFit());

        // Color swatch
        char sw_key[64];
        snprintf(sw_key, sizeof(sw_key), "##cpswk_%s", key);
        ZUIBox* swatch     = ZUIPushBox(ctx, sw_key, (uint32_t)strlen(sw_key),
                                 ZUI_DrawBackground | ZUI_DrawBorder);
        swatch->Size[0]    = ZFill();
        swatch->Size[1]    = ZPx(28.f);
        swatch->BgColor[0] = color[0]; swatch->BgColor[1] = color[1];
        swatch->BgColor[2] = color[2]; swatch->BgColor[3] = color[3];
        SetBdrArr(swatch, ctx->Theme.PanelBorder);
        swatch->BorderThickness = 1.f;
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 4.f);

        bool changed = false;
        // R/G/B/A sliders
        const char* channel_names[] = { "R", "G", "B", "A" };
        for (int i = 0; i < 4; ++i)
        {
            ZUIBeginRow(ctx, channel_names[i], ZFill(), ZPx(22.f));
                ZUIBox* lbl = ZUIPushBox(ctx, channel_names[i], 1, ZUI_DrawText);
                lbl->Size[0] = ZPx(16.f); lbl->Size[1] = ZPx(22.f);
                SetTextColor(lbl, ctx->Theme.TextDim);
                ZUIPopBox(ctx);

                char ch_key[32];
                snprintf(ch_key, sizeof(ch_key), "##cpch_%s_%d", key, i);
                if (ZUISliderFloat(ctx, ch_key, &color[i], 0.f, 1.f, ZFill(), ZPx(22.f)))
                    changed = true;
            ZUIEndRow(ctx);
        }

        ZUIEndColumn(ctx);
        return changed;
    }

    // ---------------------------------------------------------------
    // Phase 5 — layout improvements
    // ---------------------------------------------------------------

    void ZUISameLine(ZUIContext* ctx, float /*spacing*/)
    {
        // Change the current parent's layout axis to X so the NEXT sibling
        // is placed horizontally next to the previous one.
        if (ctx->Current)
            ctx->Current->LayoutAxis = ZUIAxis::X;
    }

    void ZUIBeginTable(ZUIContext* ctx, const char* key, int columns,
                       const float* widths, ZUISize h)
    {
        ctx->TableColumns    = columns;
        ctx->TableCurrentCol = -1;
        ctx->TableRowBox     = nullptr;

        // Allocate per-column widths in FrameArena
        ctx->TableColWidths = ZPushArray(&ctx->FrameArena, float, columns);
        for (int i = 0; i < columns; ++i)
            ctx->TableColWidths[i] = widths ? widths[i] : 0.f;

        // Outer column container
        ZUIBeginColumn(ctx, key, ZFill(), h);
    }

    void ZUITableNextRow(ZUIContext* ctx)
    {
        // Close previous row if open
        if (ctx->TableCurrentCol >= 0)
        {
            ZUIEndColumn(ctx); // close last cell column
            ZUIEndRow(ctx);    // close row
            ctx->TableCurrentCol = -1;
        }
        // Open new row
        char row_key[40];
        static int s_row_idx = 0;
        snprintf(row_key, sizeof(row_key), "##trow_%d", s_row_idx++);
        ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZFit());
        row->LayoutAxis = ZUIAxis::X;
        ctx->TableRowBox = row;
    }

    void ZUITableSetColumn(ZUIContext* ctx, int col_index)
    {
        // Close previous cell
        if (ctx->TableCurrentCol >= 0)
            ZUIEndColumn(ctx);

        ctx->TableCurrentCol = col_index;
        float w = (col_index < ctx->TableColumns && ctx->TableColWidths &&
                   ctx->TableColWidths[col_index] > 0.f)
                  ? ctx->TableColWidths[col_index]
                  : 80.f; // default cell width

        char cell_key[40];
        snprintf(cell_key, sizeof(cell_key), "##tcell_%d_%d",
                 (int)(uintptr_t)ctx->TableRowBox, col_index);
        ZUIBeginColumn(ctx, cell_key, ZPx(w), ZFit());
    }

    void ZUIEndTable(ZUIContext* ctx)
    {
        // Close any open cell + row
        if (ctx->TableCurrentCol >= 0)
        {
            ZUIEndColumn(ctx);
            ZUIEndRow(ctx);
        }
        ZUIEndColumn(ctx); // outer table column
        ctx->TableColumns    = 0;
        ctx->TableCurrentCol = -1;
        ctx->TableColWidths  = nullptr;
        ctx->TableRowBox     = nullptr;
    }

    // ---------------------------------------------------------------
    // Phase 3 — simple standalone widgets
    // ---------------------------------------------------------------

    bool ZUICheckbox(ZUIContext* ctx, const char* label, bool* checked)
    {
        // Row: [16×16 box] [label]
        char row_key[64];
        snprintf(row_key, sizeof(row_key), "##cb_%s", label);

        ZUIBoxFlags row_flags = ZUI_Clickable;
        if (!ctx->Disabled) { /* keep Clickable */ } else row_flags = ZUI_None;

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(24.f));
        row->Flags       = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis  = ZUIAxis::X;

        // Tick box
        bool active = checked && *checked;
        ZUIBox* box  = ZUIPushBox(ctx, "##tick", 6,
                            ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText);
        box->Size[0] = ZPx(16.f); box->Size[1] = ZPx(16.f);
        SetBgArr(box, ctx->Theme.InputBg);
        SetBdrArr(box, active ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder);
        box->BorderThickness = 1.f;
        if (active) {
            box->Label = ZUIPushStr(&ctx->FrameArena, "v", 1);
            SetTextColor(box, ctx->Theme.InputFocusBorder);
        }
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, label, ctx->Disabled ? ctx->Theme.TextDim : ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && checked)
        {
            *checked = !(*checked);
            return true;
        }
        return false;
    }

    bool ZUIRadioButton(ZUIContext* ctx, const char* label, int* selected, int index)
    {
        char row_key[64];
        snprintf(row_key, sizeof(row_key), "##rb_%s_%d", label, index);

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(24.f));
        row->Flags       = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis  = ZUIAxis::X;

        bool active = selected && (*selected == index);
        ZUIBox* circle = ZUIPushBox(ctx, "##dot", 5,
                              ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText);
        circle->Size[0] = ZPx(16.f); circle->Size[1] = ZPx(16.f);
        SetBgArr(circle, ctx->Theme.InputBg);
        SetBdrArr(circle, active ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder);
        circle->BorderThickness = 1.f;
        if (active) {
            circle->Label = ZUIPushStr(&ctx->FrameArena, "*", 1);
            SetTextColor(circle, ctx->Theme.InputFocusBorder);
        }
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, label, ctx->Disabled ? ctx->Theme.TextDim : ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && selected && !ctx->Disabled)
        {
            *selected = index;
            return true;
        }
        return false;
    }

    void ZUIProgressBar(ZUIContext* ctx, const char* key, float fraction,
                        ZUISize w, ZUISize h, const char* overlay_text)
    {
        fraction = fraction < 0.f ? 0.f : (fraction > 1.f ? 1.f : fraction);
        uint32_t len = (uint32_t)strlen(key);

        // Track
        ZUIBox* track   = ZUIBeginRow(ctx, key, w, h);
        track->Flags    = track->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        SetBgArr(track,  ctx->Theme.InputBg);
        SetBdrArr(track, ctx->Theme.InputBorder);
        track->BorderThickness = 1.f;
        track->LayoutAxis = ZUIAxis::X;

        // Fill bar
        char fill_key[64];
        snprintf(fill_key, sizeof(fill_key), "##fill_%s", key);
        ZUIBox* fill    = ZUIPushBox(ctx, fill_key, (uint32_t)strlen(fill_key),
                               ZUI_DrawBackground | (overlay_text ? ZUI_DrawText : ZUI_None));
        fill->Size[0]   = ZPct(fraction);
        fill->Size[1]   = ZFill();
        fill->BgColor[0] = ctx->Theme.InputFocusBorder[0];
        fill->BgColor[1] = ctx->Theme.InputFocusBorder[1];
        fill->BgColor[2] = ctx->Theme.InputFocusBorder[2];
        fill->BgColor[3] = 0.80f;
        if (overlay_text) {
            fill->Label = ZUIPushStr(&ctx->FrameArena, overlay_text,
                                      (uint32_t)Helpers::secure_strlen(overlay_text));
            SetTextColor(fill, ctx->Theme.TextDefault);
        }
        ZUIPopBox(ctx);

        ZUIEndRow(ctx);
    }

    void ZUISetTooltip(ZUIContext* ctx, const ZUISignal& sig, const char* text)
    {
        if (!(sig.Flags & ZUI_SignalHovered) || !text) { return; }

        // Open a popup-style floating box near the cursor
        float tx = ctx->MousePos[0] + 14.f;
        float ty = ctx->MousePos[1] + 14.f;

        // Clamp to screen edges (rough)
        if (tx + 200.f > (float)ctx->ScreenW) tx = ctx->MousePos[0] - 200.f;
        if (ty + 32.f  > (float)ctx->ScreenH) ty = ctx->MousePos[1] - 32.f;

        // Build as a root-level floated box (same parent-escape as popups)
        ZUIBox* saved    = ctx->Current;
        ctx->Current     = ctx->Root;

        char ttkey[64];
        snprintf(ttkey, sizeof(ttkey), "##tt_%p", (void*)text);
        ZUIBox* tip       = ZUIPushBox(ctx, ttkey, (uint32_t)strlen(ttkey),
                                ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText |
                                ZUI_FloatX | ZUI_FloatY);
        tip->Size[0]      = ZFit();
        tip->Size[1]      = ZFit();
        tip->FloatPos[0]  = tx;
        tip->FloatPos[1]  = ty;
        tip->Label        = ZUIPushStr(&ctx->FrameArena, text,
                                        (uint32_t)Helpers::secure_strlen(text));
        tip->BgColor[0]   = ctx->Theme.HeaderBg[0]; tip->BgColor[1] = ctx->Theme.HeaderBg[1];
        tip->BgColor[2]   = ctx->Theme.HeaderBg[2]; tip->BgColor[3] = ctx->Theme.HeaderBg[3];
        SetBdrArr(tip, ctx->Theme.PanelBorder);
        tip->BorderThickness = 1.f;
        SetTextColor(tip, ctx->Theme.TextDefault);
        ZUIPopBox(ctx);

        ctx->Current = saved;
    }

    bool ZUICollapsingHeader(ZUIContext* ctx, const char* label, bool* open)
    {
        char key[256];
        snprintf(key, sizeof(key), "##ch_%s", label);

        ZUIBox* hdr = ZUIBeginRow(ctx, key, ZFill(), ZPx(26.f));
        hdr->Flags  = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->BgColor[0] = ctx->Theme.HeaderBg[0]; hdr->BgColor[1] = ctx->Theme.HeaderBg[1];
        hdr->BgColor[2] = ctx->Theme.HeaderBg[2]; hdr->BgColor[3] = ctx->Theme.HeaderBg[3];
        hdr->LayoutAxis = ZUIAxis::X;

        const char* ind = (open && *open) ? "v " : "> ";
        ZUIBox* arrow = ZUIPushBox(ctx, ind, 2, ZUI_DrawText);
        arrow->Size[0] = ZPx(16.f); arrow->Size[1] = ZPx(26.f);
        SetTextColor(arrow, ctx->Theme.TextDim);
        ZUIPopBox(ctx);

        ZUILabel(ctx, label, ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && open) { *open = !(*open); }
        return open ? *open : false;
    }

    bool ZUISelectable(ZUIContext* ctx, const char* label, bool* selected, ZUISize h)
    {
        char key[256];
        snprintf(key, sizeof(key), "##sel_%s", label);

        ZUIBox* row = ZUIBeginRow(ctx, key, ZFill(), h);
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;
        if (selected && *selected) {
            row->BgColor[0] = ctx->Theme.RowSelectedBg[0];
            row->BgColor[1] = ctx->Theme.RowSelectedBg[1];
            row->BgColor[2] = ctx->Theme.RowSelectedBg[2];
            row->BgColor[3] = ctx->Theme.RowSelectedBg[3];
        } else {
            row->BgColor[0] = ctx->Theme.RowHoverBg[0];
            row->BgColor[1] = ctx->Theme.RowHoverBg[1];
            row->BgColor[2] = ctx->Theme.RowHoverBg[2];
            row->BgColor[3] = 0.f; // transparent, fade in on hover
        }

        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, label, ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && selected)
        {
            *selected = !(*selected);
            return true;
        }
        return false;
    }

    void ZUISeparatorText(ZUIContext* ctx, const char* text)
    {
        ZUIBeginRow(ctx, "##septext", ZFill(), ZPx(22.f));
            ZUIBox* line1 = ZUIPushBox(ctx, "##sl1", 5, ZUI_DrawBackground);
            line1->Size[0] = ZPx(8.f); line1->Size[1] = ZPx(1.f);
            SetBgArr(line1, ctx->Theme.Separator);
            ZUIPopBox(ctx);

            ZUISpacer(ctx, 4.f);
            ZUILabel(ctx, text, ctx->Theme.TextDim);
            ZUISpacer(ctx, 4.f);

            ZUIBox* line2 = ZUIPushBox(ctx, "##sl2", 5, ZUI_DrawBackground);
            line2->Size[0] = ZFill(); line2->Size[1] = ZPx(1.f);
            SetBgArr(line2, ctx->Theme.Separator);
            ZUIPopBox(ctx);
        ZUIEndRow(ctx);
    }

    // ---------------------------------------------------------------
    // Phase 4 — popup-based widgets
    // ---------------------------------------------------------------

    bool ZUIBeginContextMenu(ZUIContext* ctx, const char* key)
    {
        // Opens on right-click anywhere (not on a specific item)
        if (ctx->MousePressed[1])
            ZUIOpenPopup(ctx, key);
        return ZUIBeginPopup(ctx, key);
    }
    void ZUIEndContextMenu(ZUIContext* ctx) { ZUIEndPopup(ctx); }

    bool ZUIBeginCombo(ZUIContext* ctx, const char* key,
                       const char* preview_label, ZUISize w)
    {
        uint32_t len   = (uint32_t)strlen(key);
        char btn_key[80];
        snprintf(btn_key, sizeof(btn_key), "##combo_btn_%s", key);

        // Preview row: bordered box + preview text + "v" arrow
        ZUIBox* row = ZUIBeginRow(ctx, btn_key, w, ZPx(28.f));
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        SetBgArr(row, ctx->Theme.InputBg);
        SetBdrArr(row, ctx->Theme.InputBorder);
        row->BorderThickness = 1.f;

        ZUISpacer(ctx, 4.f);
        ZUILabel(ctx, preview_label ? preview_label : "", ctx->Theme.TextDefault);

        // Right-align "v" indicator
        ZUIBox* arrow = ZUIPushBox(ctx, "v##carrow", 9, ZUI_DrawText);
        arrow->Size[0] = ZPx(18.f); arrow->Size[1] = ZPx(28.f);
        arrow->Flags   = arrow->Flags | ZUI_FloatX;
        arrow->FloatPos[0] = w.Kind == ZUISizeKind::Fill ? 0.f : -18.f;
        SetTextColor(arrow, ctx->Theme.TextDim);
        ZUIPopBox(ctx);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        // Open popup on click; position below this row (approx)
        if (sig.Flags & ZUI_SignalClicked)
            ZUIOpenPopup(ctx, key, ctx->MousePos[0] - 8.f, ctx->MousePos[1] + 4.f);

        return ZUIBeginPopup(ctx, key);
    }
    void ZUIEndCombo(ZUIContext* ctx) { ZUIEndPopup(ctx); }

    // Internal: menu bar saved state so EndMenuBar can pop the right box
    bool ZUIBeginMenuBar(ZUIContext* ctx)
    {
        ZUIBox* bar = ZUIBeginRow(ctx, "##menubar_zui", ZFill(), ZPx(26.f));
        bar->Flags  = bar->Flags | ZUI_DrawBackground;
        SetBgArr(bar, ctx->Theme.HeaderBg);
        bar->LayoutAxis = ZUIAxis::X;
        return true;
    }
    void ZUIEndMenuBar(ZUIContext* ctx) { ZUIEndRow(ctx); }

    bool ZUIBeginMenu(ZUIContext* ctx, const char* label, bool enabled)
    {
        char key[80];
        snprintf(key, sizeof(key), "##menu_%s", label);
        ZUIBoxFlags fl = ZUI_DrawText;
        if (enabled) fl = fl | ZUI_Clickable | ZUI_DrawBackground;

        ZUIBox* btn   = ZUIPushBox(ctx, key, (uint32_t)strlen(key), fl);
        btn->Size[0]  = ZText();
        btn->Size[1]  = ZPx(26.f);
        btn->BgColor[3] = 0.f;
        SetTextColor(btn, enabled ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

        ZUISignal sig = ZUISignalFromBox(ctx, btn);
        ZUIPopBox(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && enabled)
            ZUIOpenPopup(ctx, label, ctx->MousePos[0], ctx->MousePos[1] + 2.f);

        return ZUIBeginPopup(ctx, label);
    }
    void ZUIEndMenu(ZUIContext* ctx) { ZUIEndPopup(ctx); }

    void ZUIOpenModal(ZUIContext* ctx, const char* key)
    {
        ctx->ActiveModalKey = ZUIHashStr(key, (uint32_t)strlen(key));
    }

    bool ZUIBeginModal(ZUIContext* ctx, const char* key, const char* title)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t)strlen(key));
        if (ctx->ActiveModalKey != hash) { return false; }

        float sw = (float)ctx->ScreenW;
        float sh = (float)ctx->ScreenH;
        float mw = 480.f, mh = 280.f;

        // Dim overlay — root level, covers full screen
        ZUIBox* saved = ctx->Current;
        ctx->Current  = ctx->Root;

        ZUIBox* dim  = ZUIPushBox(ctx, "##modal_dim", 12, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        dim->Size[0] = ZPx(sw); dim->Size[1] = ZPx(sh);
        dim->FloatPos[0] = 0.f; dim->FloatPos[1] = 0.f;
        dim->BgColor[0] = 0.f; dim->BgColor[1] = 0.f;
        dim->BgColor[2] = 0.f; dim->BgColor[3] = 0.55f;
        ZUIPopBox(ctx);

        // Modal panel — centred
        ZUIBox* panel = ZUIPushBox(ctx, key, (uint32_t)strlen(key),
                            ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
        panel->Size[0]      = ZPx(mw); panel->Size[1] = ZPx(mh);
        panel->FloatPos[0]  = (sw - mw) * 0.5f;
        panel->FloatPos[1]  = (sh - mh) * 0.5f;
        panel->LayoutAxis   = ZUIAxis::Y;
        panel->BorderThickness = 1.f;
        SetBgArr(panel,  ctx->Theme.PanelBg);
        SetBdrArr(panel, ctx->Theme.PanelBorder);

        ctx->Current = panel; // children go inside modal

        // Title bar
        if (title)
        {
            ZUIBox* hdr = ZUIBeginRow(ctx, "##modal_hdr", ZFill(), ZPx(28.f));
            hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
            SetBgArr(hdr, ctx->Theme.HeaderBg);
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, title, ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
            ZUISeparator(ctx);
        }

        ctx->PopupSavedParent = saved; // reuse popup save for restore
        return true;
    }

    void ZUIEndModal(ZUIContext* ctx)
    {
        ZUIPopBox(ctx); // pop modal panel
        ctx->Current = ctx->PopupSavedParent;
        ctx->PopupSavedParent = nullptr;
    }

    // ---------------------------------------------------------------
    // Drag-and-drop helpers
    // ---------------------------------------------------------------

    void ZUIBeginDragSource(ZUIContext* ctx, const ZUIBox* box,
                            const char* payload, uint32_t payload_len)
    {
        if (!ctx || !box) { return; }
        // Activate drag when this box is the active (held) box and the mouse has moved.
        // Reads ctx state directly to avoid a second ZUISignalFromBox call on the same box.
        bool held   = (ctx->ActiveKey == box->Key) && ctx->MouseDown[0];
        bool moving = held && (ctx->MousePos[0] != ctx->PrevMousePos[0] ||
                               ctx->MousePos[1] != ctx->PrevMousePos[1]);
        if (moving && ctx->DragSourceKey == 0)
        {
            ctx->DragSourceKey = box->Key;
            uint32_t copy_len  = payload_len < 511u ? payload_len : 511u;
            Helpers::secure_memcpy(ctx->DragPayload, sizeof(ctx->DragPayload), payload, copy_len);
            ctx->DragPayload[copy_len] = '\0';
            ctx->DragPayloadLen        = copy_len;
        }
    }

    bool ZUIAcceptDrop(ZUIContext* ctx, const ZUIBox* box,
                       char* out_buf, uint32_t out_size)
    {
        if (!ctx || !box) { return false; }
        if (!ctx->DragDropFired || ctx->DragTargetKey != box->Key) { return false; }
        if (out_buf && out_size > 0)
        {
            uint32_t copy_len = ctx->DragPayloadLen < out_size - 1 ? ctx->DragPayloadLen : out_size - 1;
            Helpers::secure_memcpy(out_buf, out_size, ctx->DragPayload, copy_len);
            out_buf[copy_len] = '\0';
        }
        return true;
    }

    // ---------------------------------------------------------------
    // ZUIPanelDragHeader
    // ---------------------------------------------------------------

    bool ZUIPanelDragHeader(ZUIContext* ctx, const char* title,
                            float* inout_x, float* inout_y, bool* detached)
    {
        char hdr_key[64];
        snprintf(hdr_key, sizeof(hdr_key), "##pdh_%s", title);

        ZUIBox* hdr   = ZUIBeginRow(ctx, hdr_key, ZFill(), ZPx(22.f));
        hdr->Flags    = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        SetBgArr(hdr, ctx->Theme.HeaderBg);

        // Drag indicator ("= ") in dim colour
        ZUIBox* grip  = ZUIPushBox(ctx, "= ##grip", 8, ZUI_DrawText);
        grip->Size[0] = ZPx(18.f);
        grip->Size[1] = ZPx(22.f);
        SetTextColor(grip, ctx->Theme.TextDim);
        ZUIPopBox(ctx);

        ZUILabel(ctx, title, ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, hdr);
        ZUIEndRow(ctx);

        bool moved = false;
        if ((sig.Flags & ZUI_SignalHeld) &&
            (sig.DragDelta[0] != 0.f || sig.DragDelta[1] != 0.f))
        {
            if (inout_x) { *inout_x += sig.DragDelta[0]; }
            if (inout_y) { *inout_y += sig.DragDelta[1]; }
            if (detached) { *detached = true; }
            moved = true;
        }

        // Double-click snaps back to dockspace
        if ((sig.Flags & ZUI_SignalDoubleClicked) && detached)
        {
            *detached = false;
        }

        return moved;
    }

    // ---------------------------------------------------------------
    // ZUIImage
    // ---------------------------------------------------------------

    void ZUIImage(ZUIContext* ctx, const char* key, uint32_t texture_index, ZUISize w, ZUISize h)
    {
        uint32_t len   = (uint32_t)strlen(key);
        ZUIBox*  box   = ZUIPushBox(ctx, key, len, ZUI_DrawBackground);
        box->Size[0]   = w;
        box->Size[1]   = h;
        box->TextureIndex = texture_index;
        // BgColor alpha must be > 0 so the renderer doesn't skip this box
        box->BgColor[0] = box->BgColor[1] = box->BgColor[2] = box->BgColor[3] = 1.f;
        ZUIPopBox(ctx);
    }

    // ---------------------------------------------------------------
    // ZUIDragFloat
    // ---------------------------------------------------------------

    bool ZUIDragFloat(ZUIContext* ctx, const char* key, float* value, float speed, float width_px)
    {
        uint32_t key_len  = (uint32_t)strlen(key);
        ZUIBox*  field    = ZUIPushBox(ctx, key, key_len,
                                ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]          = ZPx(width_px);
        field->Size[1]          = ZPx(24.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness  = 1.f;

        // Format the current value and store in FrameArena so the renderer can draw it
        char val_buf[32];
        snprintf(val_buf, sizeof(val_buf), "%.3f", (double)*value);
        uint32_t vlen  = (uint32_t)Helpers::secure_strlen(val_buf);
        field->Label   = ZUIPushStr(&ctx->FrameArena, val_buf, vlen);

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        bool changed = false;
        if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
        {
            *value  += sig.DragDelta[0] * speed;
            changed  = true;
        }
        return changed;
    }

    // ---------------------------------------------------------------
    // ZUITextField
    // ---------------------------------------------------------------

    bool ZUITextField(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, float width_px)
    {
        uint32_t key_len    = (uint32_t)strlen(key);
        ZUIBox*  field      = ZUIPushBox(ctx, key, key_len,
                                ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]      = ZPx(width_px);
        field->Size[1]      = ZPx(28.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);

        bool is_focused = (ctx->FocusKey == field->Key);
        bool changed    = false;

        if (is_focused)
        {
            // Append text input characters
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                uint32_t cur = (uint32_t)Helpers::secure_strlen(buf);
                if (cur + 1 < buf_size)
                {
                    buf[cur]     = ctx->TextInput[i];
                    buf[cur + 1] = '\0';
                    changed      = true;
                }
            }
            // Backspace
            if (ctx->BackspacePressed)
            {
                uint32_t cur = (uint32_t)Helpers::secure_strlen(buf);
                if (cur > 0)
                {
                    buf[cur - 1] = '\0';
                    changed      = true;
                }
            }
            // Accent border when focused
            SetBdrArr(field, ctx->Theme.InputFocusBorder);
        }
        else
        {
            SetBdrArr(field, ctx->Theme.InputBorder);
        }

        // Build display string: add blinking cursor "|" when focused
        char display[512];
        if (is_focused)
            snprintf(display, sizeof(display), "%s|", buf);
        else
            snprintf(display, sizeof(display), "%s",  buf);

        uint32_t dlen  = (uint32_t)Helpers::secure_strlen(display);
        field->Label   = ZUIPushStr(&ctx->FrameArena, display, dlen);

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = field->Key; }

        return changed;
    }

} // namespace ZEngine::UI
