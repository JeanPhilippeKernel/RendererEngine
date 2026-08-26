#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <cstring>
#include <cstdio>
#include <cmath>
#include <cstdlib>

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

    static void SetBgArr(ZUIBox* b, const float c[4]) { ZUIBoxSetColorArr(b, c); }
    static void SetBdrArr(ZUIBox* b, const float c[4])
    {
        b->BorderColor[0]=c[0]; b->BorderColor[1]=c[1];
        b->BorderColor[2]=c[2]; b->BorderColor[3]=c[3];
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

    void ZUIScrollToBottom(ZUIContext* ctx, const char* key)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t)strlen(key));
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        if (ps) ps->ScrollY = 1e9f; // clamped to MaxScrollY by layout pass
    }

    float ZUIGetScrollY(ZUIContext* ctx, const char* key)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t)strlen(key));
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        return ps ? ps->ScrollY : 0.f;
    }

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

    // Dim a single color array (e.g. TextColor) in-place when disabled.
    static void ApplyDisabledDim(float c[4]) { c[3] *= 0.38f; }
    // Dim all per-corner background colors when disabled.
    static void ApplyDisabledDimBox(ZUIBox* b) { for(int _c=0;_c<4;++_c) b->Colors[_c][3] *= 0.38f; }

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
        box->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(box, 3.f);
        if (ctx->Disabled) { ApplyDisabledDimBox(box); ApplyDisabledDim(box->TextColor); }

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
        box->Size[1]     = ZSPx(ctx, 22.f);
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        box->EdgeSoftness = 0.5f;
        ZUIBoxSetCornerRadius(box, 3.f);
        if (ctx->Disabled) { ApplyDisabledDimBox(box); ApplyDisabledDim(box->TextColor); }

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
        box->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(box, 3.f);

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
        if (ctx->Disabled) { ApplyDisabledDimBox(box); ApplyDisabledDim(box->TextColor); }

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
        ZUIBoxSetColor(box, 1.f, 1.f, 1.f, 1.f);
        if (ctx->Disabled) ApplyDisabledDimBox(box);

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
        box->Size[1]  = ZSPx(ctx, 2.f);
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
        row->Size[1]     = ZSPx(ctx, 22.f);
        row->LayoutAxis  = ZUIAxis::X;

        // Disclosure indicator — ">" (closed) or "v" (open), ASCII-safe
        const char* indicator     = (open && *open) ? "v " : "> ";
        uint32_t    indicator_len = 2;
        ZUIBox* ind   = ZUIPushBox(ctx, indicator, indicator_len, ZUI_DrawText);
        ind->Size[0]  = ZPx(14.f);
        ind->Size[1]  = ZSPx(ctx, 22.f);
        SetTextColor(ind, ctx->Theme.TextDim);
        ZUIPopBox(ctx); // pop indicator

        // Label text
        uint32_t label_len = (uint32_t)strlen(label);
        ZUIBox*  txt  = ZUIPushBox(ctx, label, label_len, ZUI_DrawText);
        txt->Size[0]  = ZText();
        txt->Size[1]  = ZSPx(ctx, 22.f);
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
        popup->EdgeSoftness     = 0.5f;
        ZUIBoxSetCornerRadius(popup, 4.f);
        popup->Padding[0] = popup->Padding[2] = 2.f; // slight horizontal inset
        // Use a slightly lighter background than panel to distinguish dropdown
        float popup_bg[4] = { ctx->Theme.PanelBg[0] + 0.04f,
                               ctx->Theme.PanelBg[1] + 0.04f,
                               ctx->Theme.PanelBg[2] + 0.04f, 1.f };
        SetBgArr(popup, popup_bg);
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
        box->Size[1]  = ZSPx(ctx, 22.f);
        ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);
        box->Padding[0] = 8.f; // left indent
        SetTextColor(box, enabled ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ZUIClosePopup(ctx); return true; }
        return false;
    }

    bool ZUIComboItem(ZUIContext* ctx, const char* label, bool selected)
    {
        uint32_t len   = (uint32_t)strlen(label);
        ZUIBox*  box   = ZUIPushBox(ctx, label, len,
                             ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
        box->Size[0]   = ZFill();
        box->Size[1]   = ZSPx(ctx, 24.f);
        if (selected) {
            ZUIBoxSetColorArr(box, ctx->Theme.RowSelectedBg);
        } else {
            ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f); // hover fade-in
        }
        SetTextColor(box, ctx->Theme.TextDefault);

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ZUIClosePopup(ctx); return true; }
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
        ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZSPx(ctx, 28.f));
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
        btn->Size[1]  = ZSPx(ctx, 28.f);
        if (active) {
            SetBgArr(btn, ctx->Theme.PanelBg);
            SetTextColor(btn, ctx->Theme.TextDefault);
        } else {
            ZUIBoxSetColor(btn, 0.f, 0.f, 0.f, 0.f);
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
        field->Size[1] = ZSPx(ctx, 28.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        field->BorderThickness = 1.f;
        field->EdgeSoftness = 0.5f;
        ZUIBoxSetCornerRadius(field, 3.f);

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
        swatch->Size[1]    = ZSPx(ctx, 28.f);
        ZUIBoxSetColorArr(swatch, color);
        SetBdrArr(swatch, ctx->Theme.PanelBorder);
        swatch->BorderThickness = 1.f;
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 4.f);

        bool changed = false;
        // R/G/B/A sliders
        const char* channel_names[] = { "R", "G", "B", "A" };
        for (int i = 0; i < 4; ++i)
        {
            ZUIBeginRow(ctx, channel_names[i], ZFill(), ZSPx(ctx, 22.f));
                ZUIBox* lbl = ZUIPushBox(ctx, channel_names[i], 1, ZUI_DrawText);
                lbl->Size[0] = ZPx(16.f); lbl->Size[1] = ZSPx(ctx, 22.f);
                SetTextColor(lbl, ctx->Theme.TextDim);
                ZUIPopBox(ctx);

                char ch_key[32];
                snprintf(ch_key, sizeof(ch_key), "##cpch_%s_%d", key, i);
                if (ZUISliderFloat(ctx, ch_key, &color[i], 0.f, 1.f, ZFill(), ZSPx(ctx, 22.f)))
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
        // Open new row — key based on current parent so it's deterministic per-frame
        char row_key[40];
        int  child_count = 0;
        if (ctx->Current) { for (auto* c = ctx->Current->FirstChild; c; c = c->NextSib) ++child_count; }
        snprintf(row_key, sizeof(row_key), "##trow_%p_%d", (void*)ctx->Current, child_count);
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
        bool  has_width = (col_index < ctx->TableColumns && ctx->TableColWidths &&
                           ctx->TableColWidths[col_index] > 0.f);
        float w         = has_width ? ctx->TableColWidths[col_index] : 0.f;
        ZUISize cell_w  = has_width ? ZSPx(ctx, w) : ZFill(); // 0 = fill remaining

        char cell_key[40];
        snprintf(cell_key, sizeof(cell_key), "##tcell_%d_%d",
                 col_index, ctx->TableCurrentCol + (int)(uintptr_t)ctx->TableRowBox);
        ZUIBeginColumn(ctx, cell_key, cell_w, ZFit());
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

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZSPx(ctx, 24.f));
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

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZSPx(ctx, 24.f));
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
        { float _c[4]={(ctx->Theme.InputFocusBorder)[0],(ctx->Theme.InputFocusBorder)[1],(ctx->Theme.InputFocusBorder)[2],0.80f}; ZUIBoxSetColorArr(fill, _c); }
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
        ZUIBoxSetColorArr(tip, ctx->Theme.HeaderBg);
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

        ZUIBox* hdr = ZUIBeginRow(ctx, key, ZFill(), ZSPx(ctx, 22.f));
        hdr->Flags  = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        ZUIBoxSetColorArr(hdr, ctx->Theme.HeaderBg);  // HeaderBg: slightly lighter than TitleBarBg
        hdr->LayoutAxis   = ZUIAxis::X;
        hdr->EdgeSoftness = 0.f;

        const char* ind = (open && *open) ? "v " : "> ";
        ZUIBox* arrow = ZUIPushBox(ctx, ind, 2, ZUI_DrawText);
        arrow->Size[0] = ZPx(16.f); arrow->Size[1] = ZSPx(ctx, 22.f);
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
            ZUIBoxSetColorArr(row, ctx->Theme.RowSelectedBg);
        } else {
            // Transparent initially; hover tint is blended in by PreparePayload via HotT.
            ZUIBoxSetColorArr(row, ctx->Theme.RowHoverBg);
            row->Colors[0][3]=row->Colors[1][3]=row->Colors[2][3]=row->Colors[3][3]=0.f;
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
        ZUIBeginRow(ctx, "##septext", ZFill(), ZSPx(ctx, 22.f));
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
        ZUIBox* row = ZUIBeginRow(ctx, btn_key, w, ZSPx(ctx, 28.f));
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        SetBgArr(row, ctx->Theme.InputBg);
        SetBdrArr(row, ctx->Theme.InputBorder);
        row->BorderThickness = 1.f;

        ZUISpacer(ctx, 4.f);
        ZUILabel(ctx, preview_label ? preview_label : "", ctx->Theme.TextDefault);

        // Right-align "v" indicator
        ZUIBox* arrow = ZUIPushBox(ctx, "v##carrow", 9, ZUI_DrawText);
        arrow->Size[0] = ZPx(18.f); arrow->Size[1] = ZSPx(ctx, 28.f);
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
        ZUIBox* bar = ZUIBeginRow(ctx, "##menubar_zui", ZFill(), ZSPx(ctx, 26.f));
        bar->Flags  = bar->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        bar->EdgeSoftness    = 0.f;
        bar->BorderThickness = 1.f;
        SetBgArr(bar, ctx->Theme.MenuBarBg);
        bar->BorderColor[0]=ctx->Theme.Separator[0]; bar->BorderColor[1]=ctx->Theme.Separator[1];
        bar->BorderColor[2]=ctx->Theme.Separator[2]; bar->BorderColor[3]=ctx->Theme.Separator[3];
        bar->LayoutAxis = ZUIAxis::X;
        return true;
    }
    void ZUIEndMenuBar(ZUIContext* ctx) { ZUIEndRow(ctx); }

    bool ZUIBeginMenu(ZUIContext* ctx, const char* label, bool enabled)
    {
        char key[80];
        // Format: "Label##menu_Label" — label before ## is the display text,
        // the full string is the hash key so multiple menus with same label don't collide.
        snprintf(key, sizeof(key), "%s##menu_%s", label, label);
        ZUIBoxFlags fl = ZUI_DrawText;
        if (enabled) fl = fl | ZUI_Clickable | ZUI_DrawBackground;

        ZUIBox* btn   = ZUIPushBox(ctx, key, (uint32_t)strlen(key), fl);
        btn->Size[0]  = ZText();
        btn->Size[1]  = ZSPx(ctx, 22.f);
        btn->Padding[0] = 8.f; // left
        btn->Padding[2] = 8.f; // right
        ZUIBoxSetColor(btn, 0.f, 0.f, 0.f, 0.f);
        btn->EdgeSoftness = 0.5f;
        ZUIBoxSetCornerRadius(btn, 3.f);
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
        ZUIBoxSetColor(dim, 0.f, 0.f, 0.f, 0.55f);
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
            ZUIBox* hdr = ZUIBeginRow(ctx, "##modal_hdr", ZFill(), ZSPx(ctx, 28.f));
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

        ZUIBox* hdr   = ZUIBeginRow(ctx, hdr_key, ZFill(), ZSPx(ctx, 22.f));
        hdr->Flags    = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        SetBgArr(hdr, ctx->Theme.HeaderBg);

        // Drag indicator ("= ") in dim colour
        ZUIBox* grip  = ZUIPushBox(ctx, "= ##grip", 8, ZUI_DrawText);
        grip->Size[0] = ZPx(18.f);
        grip->Size[1] = ZSPx(ctx, 22.f);
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
        // Colors must be non-transparent so the renderer draws this box
        ZUIBoxSetColor(box, 1.f, 1.f, 1.f, 1.f);
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
        field->Size[1]          = ZSPx(ctx, 24.f);
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
    // ZUIDragInt
    // ---------------------------------------------------------------

    bool ZUIDragInt(ZUIContext* ctx, const char* key, int* value, float speed, float width_px)
    {
        uint32_t key_len = (uint32_t)strlen(key);
        ZUIBox*  field   = ZUIPushBox(ctx, key, key_len,
                               ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]         = ZPx(width_px);
        field->Size[1]         = ZSPx(ctx, 24.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness = 1.f;

        char buf[16]; snprintf(buf, sizeof(buf), "%d", *value);
        uint32_t vlen  = (uint32_t)strlen(buf);
        field->Label   = ZUIPushStr(&ctx->FrameArena, buf, vlen);

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        bool changed = false;
        if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
        {
            float fv  = (float)*value + sig.DragDelta[0] * speed;
            *value    = (int)fv;
            changed   = true;
        }
        return changed;
    }

    // ---------------------------------------------------------------
    // ZUIDragFloat3
    // ---------------------------------------------------------------

    bool ZUIDragFloat3(ZUIContext* ctx, const char* key, float v[3], float speed, float comp_w)
    {
        struct AxisStyle { const char* label; float chip[4]; };
        static const AxisStyle kAxes[3] = {
            { "X", {0.70f,0.20f,0.20f,1.f} },
            { "Y", {0.20f,0.65f,0.20f,1.f} },
            { "Z", {0.20f,0.40f,0.80f,1.f} },
        };

        float fw = (comp_w > 0.f) ? comp_w : 66.f;
        bool  changed = false;

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0) ZUISpacer(ctx, 2.f);

            // Colored axis chip (X / Y / Z)
            char lk[48]; snprintf(lk, sizeof(lk), "##f3l%d%s", i, key);
            uint32_t llen = (uint32_t)strlen(lk);
            ZUIBox* chip  = ZUIPushBox(ctx, lk, llen, ZUI_DrawBackground | ZUI_DrawText);
            chip->Size[0]   = ZPx(14.f * ctx->UIScale);
            chip->Size[1]   = ZSPx(ctx, 22.f);
            chip->TextAlign = ZUITextAlign::Center;
            ZUIBoxSetColorArr(chip, kAxes[i].chip);
            chip->TextColor[0] = 1.f; chip->TextColor[1] = 1.f;
            chip->TextColor[2] = 1.f; chip->TextColor[3] = 1.f;
            chip->Label = ZUIPushStr(&ctx->FrameArena, kAxes[i].label, 1);
            ZUIBoxSetCornerRadius(chip, 2.f);
            chip->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);

            // Drag field for this component
            char dk[48]; snprintf(dk, sizeof(dk), "##f3d%d%s", i, key);
            changed |= ZUIDragFloat(ctx, dk, &v[i], speed, fw);
        }
        return changed;
    }

    // ---------------------------------------------------------------
    // ZUIInputFloat
    // ---------------------------------------------------------------

    bool ZUIInputFloat(ZUIContext* ctx, const char* key, float* value, float width_px)
    {
        // Use persistent UserData to distinguish edit mode (1) vs display mode (0)
        uint64_t   hash    = ZUIHashStr(key, (uint32_t)strlen(key));
        auto*      state   = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        bool       editing = state && state->UserData > 0.5f;

        // Backing char buffer lives in persistent state via a side-channel.
        // We use a static per-hash char buffer keyed approach: store the float
        // as text in a small arena-free static buf of 32 chars.
        // For simplicity we re-format from *value every non-editing frame.
        char display[32];
        if (!editing) snprintf(display, sizeof(display), "%.4f", (double)*value);

        uint32_t key_len = (uint32_t)strlen(key);
        ZUIBox*  field   = ZUIPushBox(ctx, key, key_len,
                               ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]         = ZPx(width_px);
        field->Size[1]         = ZSPx(ctx, 24.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        const float* bdr = editing ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder;
        SetBdrArr(field, bdr);
        field->BorderThickness = 1.f;

        if (!editing)
        {
            uint32_t dlen = (uint32_t)strlen(display);
            field->Label = ZUIPushStr(&ctx->FrameArena, display, dlen);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        bool changed = false;

        if (!editing && (sig.Flags & ZUI_SignalClicked))
        {
            if (state) state->UserData = 1.f;
            ctx->TextInputLen = 0;
            snprintf(ctx->TextInput, 32, "%.4f", (double)*value);
            ctx->TextInputLen = (uint32_t)strlen(ctx->TextInput);
        }

        if (editing)
        {
            // Show what the user is typing
            uint32_t tlen = (uint32_t)strlen(ctx->TextInput);
            field->Label  = ZUIPushStr(&ctx->FrameArena, ctx->TextInput, tlen);

            // Commit on Enter (no Enter key tracking yet — commit on focus loss)
            bool click_outside = ctx->MousePressed[0] && !(sig.Flags & ZUI_SignalHovered);
            if (click_outside)
            {
                float parsed = (float)atof(ctx->TextInput);
                if (parsed != *value) { *value = parsed; changed = true; }
                if (state) state->UserData = 0.f;
                ctx->TextInputLen = 0;
                ctx->TextInput[0] = '\0';
            }
        }
        return changed;
    }

    // ---------------------------------------------------------------
    // ZUIColorEdit4
    // ---------------------------------------------------------------

    bool ZUIColorEdit4(ZUIContext* ctx, const char* key, float color[4])
    {
        bool changed = false;

        // Small swatch button
        char swk[48]; snprintf(swk, sizeof(swk), "##swatch_%s", key);
        uint32_t swlen = (uint32_t)strlen(swk);
        float swatch_sz = 22.f * ctx->UIScale;
        ZUIBox* swatch = ZUIPushBox(ctx, swk, swlen,
                                     ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable);
        swatch->Size[0]     = ZPx(swatch_sz);
        swatch->Size[1]     = ZPx(swatch_sz);
        ZUIBoxSetColorArr(swatch, color);
        swatch->BorderColor[0] = 0.4f; swatch->BorderColor[1] = 0.4f;
        swatch->BorderColor[2] = 0.4f; swatch->BorderColor[3] = 0.9f;
        swatch->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(swatch, 3.f);
        swatch->EdgeSoftness = 0.f;
        ZUISignal sw_sig = ZUISignalFromBox(ctx, swatch);
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 6.f);

        // Hex label "#RRGGBBAA"
        char hex[12];
        int  r = (int)(color[0] * 255.f + 0.5f);
        int  g = (int)(color[1] * 255.f + 0.5f);
        int  b = (int)(color[2] * 255.f + 0.5f);
        int  a = (int)(color[3] * 255.f + 0.5f);
        snprintf(hex, sizeof(hex), "#%02X%02X%02X%02X", r, g, b, a);
        ZUILabel(ctx, hex, ctx->Theme.TextDim);

        // Open picker popup
        if (sw_sig.Flags & ZUI_SignalClicked)
            ZUIOpenPopup(ctx, key);

        if (ZUIBeginPopup(ctx, key))
        {
            changed |= ZUIColorPicker4(ctx, key, color);
            ZUIEndPopup(ctx);
        }

        return changed;
    }

    // ---------------------------------------------------------------
    // ZUISpinner  (3-dot pulse, arena-safe)
    // ---------------------------------------------------------------

    void ZUISpinner(ZUIContext* ctx, const char* key, float radius_px)
    {
        float dot = radius_px * 0.65f;
        float gap = dot * 0.6f;

        char rk[48]; snprintf(rk, sizeof(rk), "##spn_%s", key);
        ZUIBeginRow(ctx, rk, ZFit(), ZPx(dot));

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0) ZUISpacer(ctx, gap);

            float phase = ctx->Time * 5.f - (float)i * 0.5f;
            float t     = 0.5f + 0.5f * sinf(phase);
            float alpha = 0.25f + 0.75f * t;

            char dk[56]; snprintf(dk, sizeof(dk), "##spd%d_%s", i, key);
            ZUIBox* d = ZUIPushBox(ctx, dk, (uint32_t)strlen(dk), ZUI_DrawBackground);
            d->Size[0] = ZPx(dot);
            d->Size[1] = ZPx(dot);
            float col[4] = { ctx->Theme.TabActiveBorder[0],
                             ctx->Theme.TabActiveBorder[1],
                             ctx->Theme.TabActiveBorder[2], alpha };
            ZUIBoxSetColorArr(d, col);
            ZUIBoxSetCornerRadius(d, dot * 0.5f);
            d->EdgeSoftness = 0.6f;
            ZUIPopBox(ctx);
        }

        ZUIEndRow(ctx);
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
        field->Size[1]      = ZSPx(ctx, 28.f);
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

    // ---------------------------------------------------------------
    // ZUIResizeHandle
    // ---------------------------------------------------------------

    bool ZUIResizeHandle(ZUIContext* ctx, const char* key, float* value,
                         float min_v, float max_v, bool horizontal)
    {
        uint32_t len = (uint32_t)strlen(key);
        ZUIBox*  box = ZUIPushBox(ctx, key, len, ZUI_Clickable);
        if (horizontal)
        {
            box->Size[0] = ZFill();
            box->Size[1] = ZSPx(ctx, 4.f);
        }
        else
        {
            box->Size[0] = ZSPx(ctx, 4.f);
            box->Size[1] = ZFill();
        }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);

        bool dragging = false;
        if ((sig.Flags & ZUI_SignalHeld) && value)
        {
            float delta = horizontal ? sig.DragDelta[1] : sig.DragDelta[0];
            *value += delta;
            if (*value < min_v) *value = min_v;
            if (*value > max_v) *value = max_v;
            dragging = (delta != 0.f);
        }
        return dragging;
    }

} // namespace ZEngine::UI
