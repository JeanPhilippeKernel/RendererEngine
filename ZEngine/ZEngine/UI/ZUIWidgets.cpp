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

    // Lerp box background through rest → hover → active using HotT/ActiveT.
    // Call AFTER ZUISignalFromBox so the persistent state is already updated this frame.
    static void ApplyHotActive(ZUIBox* box, ZUIContext* ctx,
                                const float rest[4], const float hov[4], const float act[4])
    {
        ZUIPersistentState* st = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (!st) return;
        float ht = st->HotT, at = st->ActiveT;
        for (int ch = 0; ch < 4; ++ch)
        {
            float col = rest[ch] + (hov[ch] - rest[ch]) * ht
                                 + (act[ch] - hov[ch]) * at;
            for (int k = 0; k < 4; ++k) box->Colors[k][ch] = col;
        }
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
        // ZUI_Clickable added so HotT tracks hover — used for auto-hide scrollbar alpha
        ZUIBox*  box  = ZUIPushBox(ctx, key, len,
                            ZUI_Scrollable | ZUI_ClipChildren | ZUI_Clickable);
        box->Size[0]  = w;
        box->Size[1]  = h;
        box->LayoutAxis = ZUIAxis::Y;
        return box;
    }

    void ZUIEndScrollRegion(ZUIContext* ctx)
    {
        // Before popping, inject a visible scrollbar if content overflows.
        // We read prev-frame screen rect from ZUIPersistentState so the thumb
        // is correctly positioned even though layout hasn't run yet this frame.
        ZUIBox* sb = ctx->Current;
        if (sb && sb->Key)
        {
            // Update HotT for the scroll region — drives scrollbar auto-hide alpha
            ZUISignal scroll_sig = ZUISignalFromBox(ctx, sb);
            (void)scroll_sig;

            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, sb->Key);
            if (ps && ps->MaxScrollY > 1.f && ps->ScreenMaxX > ps->ScreenMinX)
            {
                float sx0 = ps->ScreenMinX, sy0 = ps->ScreenMinY;
                float sx1 = ps->ScreenMaxX, sy1 = ps->ScreenMaxY;
                float track_h = sy1 - sy0;
                float content_h = track_h + ps->MaxScrollY;
                float thumb_h = (content_h > 0.f)
                              ? (track_h * track_h / content_h) : track_h;
                if (thumb_h < 16.f) thumb_h = 16.f;
                if (thumb_h > track_h) thumb_h = track_h;
                float thumb_y = (ps->MaxScrollY > 0.f)
                              ? sy0 + (ps->ScrollY / ps->MaxScrollY) * (track_h - thumb_h)
                              : sy0;

                // Auto-hide: alpha driven by HotT (fades in on hover, out on leave)
                // Min 0.08 so a very faint track is always hinted at
                float vis = 0.08f + ps->HotT * 0.92f;
                const float kBarW = 4.f; // VS Code thin scrollbar

                // Track — visible only on hover
                char trk[64]; snprintf(trk, sizeof(trk), "##sbtrk_%llu", (unsigned long long)sb->Key);
                ZUIBox* track = ZUIPushBox(ctx, trk, (uint32_t)strlen(trk),
                                           ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                track->Size[0]     = ZPx(kBarW); track->Size[1] = ZPx(track_h);
                track->FloatPos[0] = sx1 - kBarW; track->FloatPos[1] = sy0;
                ZUIBoxSetColor(track,
                    ctx->Theme.ScrollbarGrab[0], ctx->Theme.ScrollbarGrab[1],
                    ctx->Theme.ScrollbarGrab[2], 0.15f * vis);
                track->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);

                // Thumb — fades in/out + hover/active animation
                char thm[64]; snprintf(thm, sizeof(thm), "##sbthm_%llu", (unsigned long long)sb->Key);
                ZUIBox* thumb = ZUIPushBox(ctx, thm, (uint32_t)strlen(thm),
                                           ZUI_DrawBackground | ZUI_Clickable |
                                           ZUI_FloatX | ZUI_FloatY);
                thumb->Size[0]     = ZPx(kBarW); thumb->Size[1] = ZPx(thumb_h);
                thumb->FloatPos[0] = sx1 - kBarW; thumb->FloatPos[1] = thumb_y;
                float grab[4]    = {ctx->Theme.ScrollbarGrab[0], ctx->Theme.ScrollbarGrab[1],
                                    ctx->Theme.ScrollbarGrab[2], vis};
                float grab_hov[4]= {ctx->Theme.ScrollbarGrabHov[0], ctx->Theme.ScrollbarGrabHov[1],
                                    ctx->Theme.ScrollbarGrabHov[2], vis};
                float grab_act[4]= {ctx->Theme.ScrollbarGrabAct[0], ctx->Theme.ScrollbarGrabAct[1],
                                    ctx->Theme.ScrollbarGrabAct[2], 1.f};
                ZUIBoxSetColorArr(thumb, grab);
                ZUIBoxSetCornerRadius(thumb, 2.f);
                thumb->EdgeSoftness = 0.5f;

                ZUISignal tsig = ZUISignalFromBox(ctx, thumb);
                ApplyHotActive(thumb, ctx, grab, grab_hov, grab_act);

                if ((tsig.Flags & ZUI_SignalHeld) && tsig.DragDelta[1] != 0.f)
                {
                    float ratio = (track_h - thumb_h) > 0.f
                                ? ps->MaxScrollY / (track_h - thumb_h) : 0.f;
                    ps->ScrollY += tsig.DragDelta[1] * ratio;
                    if (ps->ScrollY < 0.f)            ps->ScrollY = 0.f;
                    if (ps->ScrollY > ps->MaxScrollY)  ps->ScrollY = ps->MaxScrollY;
                }
                ZUIPopBox(ctx);
            }
        }
        ZUIPopBox(ctx);
    }

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
        box->Padding[0]     = 4.f; // ImGui FramePadding.x = 4px per side
        box->Padding[2]     = 4.f;
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        SetBdrArr(box, ctx->Theme.ButtonBorder);
        box->BorderThickness = 1.f;
        box->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(box, 3.f);
        if (ctx->Disabled) { ApplyDisabledDimBox(box); ApplyDisabledDim(box->TextColor); }

        // Keyboard focus: Tab can land on buttons; Space/Enter activates
        bool is_focused = !ctx->Disabled && (ctx->FocusKey == box->Key);
        if (is_focused)
        {
            SetBdrArr(box, ctx->Theme.InputFocusBorder); // teal focus ring
            box->Flags = box->Flags | ZUI_DrawBorder;
        }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        if (!ctx->Disabled)
        {
            ApplyHotActive(box, ctx, ctx->Theme.ButtonBg,
                           ctx->Theme.ButtonHoveredBg, ctx->Theme.ButtonActiveBg);
            if (is_focused && (ctx->SpacePressed || ctx->EnterPressed))
                sig.Flags |= ZUI_SignalClicked;
        }
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
        box->Size[1]     = ZPx(19.f); // ImGui GetFrameHeight = 19px
        box->Padding[0]  = 4.f;       // ImGui FramePadding.x
        box->Padding[2]  = 4.f;
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        box->EdgeSoftness = 0.5f;
        ZUIBoxSetCornerRadius(box, 3.f);
        if (ctx->Disabled) { ApplyDisabledDimBox(box); ApplyDisabledDim(box->TextColor); }

        bool is_focused = !ctx->Disabled && (ctx->FocusKey == box->Key);
        if (is_focused) { SetBdrArr(box, ctx->Theme.InputFocusBorder); box->Flags = box->Flags | ZUI_DrawBorder; }

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        if (!ctx->Disabled)
        {
            ApplyHotActive(box, ctx, ctx->Theme.ButtonBg,
                           ctx->Theme.ButtonHoveredBg, ctx->Theme.ButtonActiveBg);
            if (is_focused && (ctx->SpacePressed || ctx->EnterPressed))
                sig.Flags |= ZUI_SignalClicked;
        }
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
        box->Padding[0]     = 4.f;
        box->Padding[2]     = 4.f;
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
        row->Size[1]     = ZPx(19.f); // ImGui GetFrameHeight = 19px
        row->LayoutAxis  = ZUIAxis::X;

        // Disclosure indicator drawn via ZUI_DrawTriArrow (same as tree view)
        char ind_key[160]; snprintf(ind_key, sizeof(ind_key), "##tnarr_%s", row_key);
        ZUIBox* ind   = ZUIPushBox(ctx, ind_key, (uint32_t)strlen(ind_key), ZUI_DrawTriArrow);
        ind->Size[0]  = ZPx(14.f);
        ind->Size[1]  = ZPx(19.f);
        float ind_col[4] = {0.55f, 0.55f, 0.60f, 1.f};
        SetTextColor(ind, ind_col);
        { auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, ind->Key);
          if (ps) ps->UserData = (open && *open) ? 1.f : 0.f; }
        ZUIPopBox(ctx);

        // Label text
        uint32_t label_len = (uint32_t)strlen(label);
        ZUIBox*  txt  = ZUIPushBox(ctx, label, label_len, ZUI_DrawText);
        txt->Size[0]  = ZText();
        txt->Size[1]  = ZPx(19.f);
        SetTextColor(txt, ctx->Theme.TextDefault);
        ZUIPopBox(ctx); // pop label

        bool is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIPopBox(ctx); // pop row

        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = row->Key; }

        bool toggle = (sig.Flags & ZUI_SignalClicked) ||
                      (is_focused && (ctx->SpacePressed || ctx->EnterPressed));
        if (open)
        {
            if (toggle) { *open = !(*open); }
            // Arrow Right opens, Arrow Left closes
            if (is_focused && ctx->ArrowRightPressed && !(*open)) *open = true;
            if (is_focused && ctx->ArrowLeftPressed  &&  (*open)) *open = false;
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
        ctx->PopupNavIdx   = -1;
        ctx->PopupBuildIdx = 0;
        // PopupDesiredW is set by the CALLER before ZUIOpenPopup when a specific
        // width is required (e.g. combo matches button width). It persists until
        // ZUIBeginPopup consumes it.  Reset it here only if the caller didn't set
        // it (i.e. it's still 0 from a prior clear), so menu popups get the default.
        // Callers that want a specific width set PopupDesiredW BEFORE calling us.
    }

    bool ZUIBeginPopup(ZUIContext* ctx, const char* key)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t)strlen(key));
        if (ctx->ActivePopupKey != hash) { return false; }

        // Reset item counter each frame the popup builds
        ctx->PopupBuildIdx = 0;

        // Escape to root so the popup renders on top of everything else
        ctx->PopupSavedParent = ctx->Current;
        ctx->Current          = ctx->Root;

        uint32_t len   = (uint32_t)strlen(key);
        ZUIBox*  popup = ZUIPushBox(ctx, key, len,
                             ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow |
                             ZUI_ClipChildren   | ZUI_FloatX | ZUI_FloatY);
        popup->Size[0]         = ZFit();
        popup->Size[1]         = ZFit();
        popup->FloatPos[0]     = ctx->PopupPos[0];
        popup->FloatPos[1]     = ctx->PopupPos[1];
        popup->LayoutAxis      = ZUIAxis::Y;
        popup->BorderThickness = 1.f;
        popup->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(popup, 4.f);
        popup->Padding[0] = popup->Padding[2] = 2.f;
        float popup_bg[4] = { ctx->Theme.MenuBarBg[0] + 0.02f,
                               ctx->Theme.MenuBarBg[1] + 0.02f,
                               ctx->Theme.MenuBarBg[2] + 0.02f, 1.f };
        SetBgArr(popup, popup_bg);
        SetBdrArr(popup, ctx->Theme.PanelBorder);

        ctx->ActivePopupBox = popup;

        // Minimum-width sizer: breaks the ZFit↔ZFill circular dependency.
        // ZFit popup width = max(sizer_width, children widths).
        // ZFill items can then fill this known minimum.  Menu items use 200px,
        // combos pass their button width via ctx->PopupDesiredW.
        {
            float min_w = (ctx->PopupDesiredW > 0.f) ? ctx->PopupDesiredW : 200.f;
            ctx->PopupDesiredW = 0.f; // consume
            char sk[20] = "##popup_min_w";
            ZUIBox* sizer   = ZUIPushBox(ctx, sk, 13, ZUI_None);
            sizer->Size[0]  = ZPx(min_w);
            sizer->Size[1]  = ZPx(0.f); // zero height — invisible
            ZUIPopBox(ctx);
        }

        return true;
    }

    void ZUIEndPopup(ZUIContext* ctx)
    {
        ctx->PopupBuildCount = ctx->PopupBuildIdx; // save item count for next-frame nav clamping
        ZUIPopBox(ctx);                            // pop the popup box
        ctx->Current = ctx->PopupSavedParent;      // restore original parent
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
        box->Size[1]  = ZPx(19.f); // ImGui GetFrameHeight = 19px
        box->Padding[0] = 8.f;     // left indent (ImGui: FramePadding.x * 2)
        box->Padding[2] = 8.f;

        // Keyboard highlight via popup nav
        bool kb_focus = enabled && (ctx->PopupNavIdx >= 0 && ctx->PopupBuildIdx == ctx->PopupNavIdx);
        if (kb_focus) ZUIBoxSetColorArr(box, ctx->Theme.RowHoverBg);
        else          ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);
        SetTextColor(box, enabled ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

        if (enabled) ctx->PopupBuildIdx++; // disabled items are invisible to keyboard nav

        ZUISignal sig = ZUISignalFromBox(ctx, box);

        // ImGui uses INSTANT color switch (no lerp) for menu items.
        // Reading ctx->HotKey / ctx->ActiveKey directly matches that behaviour.
        if (enabled && !kb_focus)
        {
            bool is_hot    = (ctx->HotKey    == box->Key);
            bool is_active = (ctx->ActiveKey == box->Key);
            if      (is_active) ZUIBoxSetColorArr(box, ctx->Theme.HeaderActiveBg);
            else if (is_hot)    ZUIBoxSetColorArr(box, ctx->Theme.HeaderHoveredBg);
        }
        ZUIPopBox(ctx);

        bool activated = (enabled && (sig.Flags & ZUI_SignalClicked)) ||
                         (kb_focus && (ctx->EnterPressed || ctx->SpacePressed));
        if (activated) { ZUIClosePopup(ctx); ctx->PopupNavIdx = -1; return true; }
        return false;
    }

    bool ZUIComboItem(ZUIContext* ctx, const char* label, bool selected)
    {
        uint32_t len   = (uint32_t)strlen(label);
        ZUIBox*  box   = ZUIPushBox(ctx, label, len,
                             ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
        box->Size[0]   = ZFill();
        box->Size[1]   = ZPx(19.f); // ImGui GetFrameHeight = 19px
        box->Padding[0] = 8.f;
        box->Padding[2] = 8.f;

        // Keyboard highlight: this item is focused via Up/Down arrow
        bool kb_focus = (ctx->PopupNavIdx >= 0 && ctx->PopupBuildIdx == ctx->PopupNavIdx);
        if (selected || kb_focus)
            ZUIBoxSetColorArr(box, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);
        SetTextColor(box, ctx->Theme.TextDefault);

        ctx->PopupBuildIdx++;

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        // Instant hover (no lerp) — matching ImGui's popup item behaviour
        if (!kb_focus)
        {
            bool is_hot    = (ctx->HotKey    == box->Key);
            bool is_active = (ctx->ActiveKey == box->Key);
            if      (is_active) ZUIBoxSetColorArr(box, ctx->Theme.HeaderActiveBg);
            else if (is_hot)    ZUIBoxSetColorArr(box, ctx->Theme.HeaderHoveredBg);
            else if (selected)  ZUIBoxSetColorArr(box, ctx->Theme.RowSelectedBg);
        }
        ZUIPopBox(ctx);

        bool activated = (sig.Flags & ZUI_SignalClicked) ||
                         (kb_focus && (ctx->EnterPressed || ctx->SpacePressed));
        if (activated) { ZUIClosePopup(ctx); ctx->PopupNavIdx = -1; return true; }
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

        // Fill bar (ZPct of track width based on normalised value)
        {
            char fk[64]; snprintf(fk, sizeof(fk), "##sf_fill_%s", key);
            ZUIBox* fill = ZUIPushBox(ctx, fk, (uint32_t)strlen(fk), ZUI_DrawBackground);
            fill->Size[0] = ZPct(fraction);
            fill->Size[1] = ZFill();
            float fc[4] = {ctx->Theme.SliderGrab[0], ctx->Theme.SliderGrab[1],
                           ctx->Theme.SliderGrab[2], 0.55f};
            ZUIBoxSetColorArr(fill, fc);
            fill->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }
        // Thumb (8px square for now — Phase 2 upgrades to circle via ZUIDrawList)
        {
            char tk[64]; snprintf(tk, sizeof(tk), "##sf_thumb_%s", key);
            ZUIBox* thumb = ZUIPushBox(ctx, tk, (uint32_t)strlen(tk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            float th = 14.f, tw = 12.f; // ImGui GrabMinSize = 12px
            // Position uses prev-frame track screen coords
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, track->Key);
            float trk_x0 = ps ? ps->ScreenMinX : 0.f;
            float trk_w  = ps ? (ps->ScreenMaxX - ps->ScreenMinX) : 120.f;
            float trk_y0 = ps ? ps->ScreenMinY : 0.f;
            float trk_h  = ps ? (ps->ScreenMaxY - ps->ScreenMinY) : 22.f;
            thumb->FloatPos[0] = trk_x0 + fraction * trk_w - tw * 0.5f;
            thumb->FloatPos[1] = trk_y0 + (trk_h - th) * 0.5f;
            thumb->Size[0] = ZPx(tw); thumb->Size[1] = ZPx(th);
            ZUIBoxSetColorArr(thumb, ctx->Theme.SliderGrab);
            ZUIBoxSetCornerRadius(thumb, 3.f);
            thumb->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);
        }

        bool is_focused = (ctx->FocusKey == track->Key);
        if (is_focused) { SetBdrArr(track, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, track);
        ApplyHotActive(track, ctx, ctx->Theme.InputBg,
                       ctx->Theme.InputHoveredBg, ctx->Theme.InputActiveBg);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = track->Key; }

        bool changed = false;
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, track->Key);
            float box_w = (ps && ps->ScreenMaxX > ps->ScreenMinX)
                         ? (ps->ScreenMaxX - ps->ScreenMinX) : 120.f;

            if (sig.Flags & ZUI_SignalHeld)
            {
                *value += sig.DragDelta[0] * (range / box_w);
                if (*value < v_min) *value = v_min;
                if (*value > v_max) *value = v_max;
                changed = (sig.DragDelta[0] != 0.f);
            }
            else if (sig.Flags & ZUI_SignalPressed)
            {
                float pos = ctx->MousePos[0] - (ps ? ps->ScreenMinX : ctx->MousePos[0]);
                if (box_w > 0.f) {
                    *value = v_min + (pos / box_w) * range;
                    if (*value < v_min) *value = v_min;
                    if (*value > v_max) *value = v_max;
                    changed = true;
                }
            }
        }
        // Arrow key nudge when focused: 1% of range per press
        if (is_focused)
        {
            float step = range / 100.f;
            if (ctx->ArrowRightPressed || ctx->ArrowUpPressed)
                { *value += step; if (*value > v_max) *value = v_max; changed = true; }
            if (ctx->ArrowLeftPressed  || ctx->ArrowDownPressed)
                { *value -= step; if (*value < v_min) *value = v_min; changed = true; }
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
        field->Size[0]    = w;
        field->Size[1]    = ZPx(19.f);
        field->Padding[0] = 4.f;
        field->Padding[2] = 4.f;
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
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

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(19.f));
        row->Flags       = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis  = ZUIAxis::X;

        // Tick box — bg lerps from InputBg (rest) → InputHoveredBg (hover) → InputActiveBg (active)
        bool is_checked = checked && *checked;
        char tick_key[32]; snprintf(tick_key, sizeof(tick_key), "##tick_%s", label);
        ZUIBoxFlags tick_draw = ZUI_DrawBackground | ZUI_DrawBorder;
        if (is_checked) tick_draw = tick_draw | ZUI_DrawCheckmark;
        ZUIBox* box  = ZUIPushBox(ctx, tick_key, (uint32_t)strlen(tick_key), tick_draw);
        box->Size[0] = ZPx(13.f); box->Size[1] = ZPx(13.f); // ImGui: checkbox = FontSize = 13px
        SetBgArr(box, is_checked ? ctx->Theme.InputActiveBg : ctx->Theme.InputBg);
        SetBdrArr(box, is_checked ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder);
        box->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(box, 2.f);
        SetTextColor(box, ctx->Theme.CheckMark);
        ZUISignal tick_sig = ZUISignalFromBox(ctx, box);
        ApplyHotActive(box, ctx, ctx->Theme.InputBg,
                       ctx->Theme.InputHoveredBg, ctx->Theme.InputActiveBg);
        ZUIPopBox(ctx);
        (void)tick_sig;

        ZUISpacer(ctx, 4.f); // ImGui ItemInnerSpacing.x = 4px
        ZUILabel(ctx, label, ctx->Disabled ? ctx->Theme.TextDim : ctx->Theme.TextDefault);

        bool is_focused = !ctx->Disabled && (ctx->FocusKey == row->Key);
        if (is_focused) { SetBdrArr(box, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        bool activated = (sig.Flags & ZUI_SignalClicked) ||
                         (is_focused && ctx->SpacePressed);
        if (activated && checked)
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

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(19.f));
        row->Flags       = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis  = ZUIAxis::X;

        bool is_active = selected && (*selected == index);
        char dot_key[64]; snprintf(dot_key, sizeof(dot_key), "##dot_%s_%d", label, index);
        ZUIBoxFlags dot_fl = ZUI_DrawBackground | ZUI_DrawBorder;
        if (is_active) dot_fl = dot_fl | ZUI_DrawCircleFill;
        ZUIBox* circle = ZUIPushBox(ctx, dot_key, (uint32_t)strlen(dot_key), dot_fl);
        circle->Size[0] = ZPx(13.f); circle->Size[1] = ZPx(13.f); // ImGui: FontSize = 13px
        SetBgArr(circle, is_active ? ctx->Theme.InputActiveBg : ctx->Theme.InputBg);
        SetBdrArr(circle, is_active ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder);
        circle->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(circle, 6.5f); // full circle
        SetTextColor(circle, ctx->Theme.CheckMark);
        ZUISignal dot_sig = ZUISignalFromBox(ctx, circle);
        ApplyHotActive(circle, ctx, ctx->Theme.InputBg,
                       ctx->Theme.InputHoveredBg, ctx->Theme.InputActiveBg);
        ZUIPopBox(ctx);
        (void)dot_sig;

        ZUISpacer(ctx, 4.f); // ImGui ItemInnerSpacing.x = 4px
        ZUILabel(ctx, label, ctx->Disabled ? ctx->Theme.TextDim : ctx->Theme.TextDefault);

        bool is_focused = !ctx->Disabled && (ctx->FocusKey == row->Key);
        if (is_focused) { SetBdrArr(circle, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        bool activated = ((sig.Flags & ZUI_SignalClicked) ||
                          (is_focused && ctx->SpacePressed)) && selected && !ctx->Disabled;
        if (activated) { *selected = index; return true; }
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

        ZUIBox* hdr = ZUIBeginRow(ctx, key, ZFill(), ZPx(19.f)); // ImGui GetFrameHeight = 19px
        hdr->Flags      = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->Padding[0] = 4.f;        // ImGui FramePadding.x — arrow starts 4px from left
        ZUIBoxSetColorArr(hdr, ctx->Theme.HeaderBg);
        hdr->LayoutAxis   = ZUIAxis::X;
        hdr->EdgeSoftness = 0.f;

        // Triangle arrow — ImGui: FontSize (13px) wide, color = ImGuiCol_Text
        bool is_open = open && *open;
        char arrow_key[272]; snprintf(arrow_key, sizeof(arrow_key), "##ch_arr_%s", label);
        ZUIBox* arrow = ZUIPushBox(ctx, arrow_key, (uint32_t)strlen(arrow_key),
                                    ZUI_DrawTriArrow);
        arrow->Size[0] = ZPx(13.f); arrow->Size[1] = ZPx(19.f);
        SetTextColor(arrow, ctx->Theme.TextDefault); // ImGui: always ImGuiCol_Text
        // Write open state to UserData so PreparePayload draws the right direction
        {
            auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, arrow->Key);
            if (ps) ps->UserData = is_open ? 1.f : 0.f;
        }
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 4.f); // ImGui ItemInnerSpacing.x = 4px
        ZUILabel(ctx, label, ctx->Theme.TextDefault);

        bool is_focused = (ctx->FocusKey == hdr->Key);
        if (is_focused)
        { hdr->Flags = hdr->Flags | ZUI_DrawBorder; SetBdrArr(hdr, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, hdr);
        ApplyHotActive(hdr, ctx, ctx->Theme.HeaderBg,
                       ctx->Theme.HeaderHoveredBg, ctx->Theme.HeaderActiveBg);
        ZUIEndRow(ctx);

        bool activated = (sig.Flags & ZUI_SignalClicked) ||
                         (is_focused && (ctx->SpacePressed || ctx->EnterPressed));
        if (activated && open) { *open = !(*open); }
        return open ? *open : false;
    }

    bool ZUISelectable(ZUIContext* ctx, const char* label, bool* selected, ZUISize h)
    {
        char key[256];
        snprintf(key, sizeof(key), "##sel_%s", label);

        ZUIBox* row = ZUIBeginRow(ctx, key, ZFill(), h);
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_Clickable;

        // Use RowSelectedBg for selected, transparent for rest — hover lerps via HotT
        static const float kTransparent[4] = {0.f, 0.f, 0.f, 0.f};
        if (selected && *selected)
            ZUIBoxSetColorArr(row, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColorArr(row, kTransparent);

        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, label, ctx->Theme.TextDefault);

        bool is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        // Lerp transparent → RowHoverBg → RowSelectedBg
        if (!(selected && *selected))
            ApplyHotActive(row, ctx, kTransparent,
                           ctx->Theme.HeaderHoveredBg, ctx->Theme.HeaderActiveBg);
        ZUIEndRow(ctx);

        bool activated = (sig.Flags & ZUI_SignalClicked) ||
                         (is_focused && (ctx->SpacePressed || ctx->EnterPressed));
        if (activated && selected)
        {
            *selected = !(*selected);
            return true;
        }
        return activated; // return true even when selected == nullptr (e.g. ZUIMenuItem)
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
        ZUIBox* row = ZUIBeginRow(ctx, btn_key, w, ZPx(19.f)); // ImGui GetFrameHeight = 19px
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        row->Padding[0] = 4.f; // ImGui FramePadding.x
        row->Padding[2] = 18.f; // arrow (14px) + 4px right margin
        ZUIBoxSetCornerRadius(row, 3.f);
        SetBgArr(row, ctx->Theme.InputBg);
        SetBdrArr(row, ctx->Theme.InputBorder);
        row->BorderThickness = 1.f;

        ZUILabel(ctx, preview_label ? preview_label : "", ctx->Theme.TextDefault);

        // Dropdown arrow — always points down
        ZUIBox* arrow = ZUIPushBox(ctx, "##carrow", 8, ZUI_DrawTriArrow);
        arrow->Size[0] = ZPx(14.f); arrow->Size[1] = ZPx(19.f);
        arrow->Flags   = arrow->Flags | ZUI_FloatX;
        arrow->FloatPos[0] = w.Kind == ZUISizeKind::Fill ? 0.f : -18.f;
        SetTextColor(arrow, ctx->Theme.TextDim);
        { auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, arrow->Key); if (ps) ps->UserData = 1.f; }
        ZUIPopBox(ctx);

        bool is_focused = (ctx->FocusKey == row->Key);
        if (is_focused) { SetBdrArr(row, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = row->Key; }

        // Open popup on click OR Space/Enter/ArrowDown when focused
        bool open_popup = (sig.Flags & ZUI_SignalClicked) ||
                          (is_focused && (ctx->SpacePressed || ctx->EnterPressed ||
                                         ctx->ArrowDownPressed));
        if (open_popup)
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, row->Key);
            float py = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY : (ctx->MousePos[1] + 4.f);
            float px = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : (ctx->MousePos[0] - 8.f);
            // Pass the button width so the dropdown popup matches the combo box width
            float bw = (ps && ps->ScreenMaxX > ps->ScreenMinX) ? (ps->ScreenMaxX - ps->ScreenMinX) : 0.f;
            ctx->PopupDesiredW = bw;
            ZUIOpenPopup(ctx, key, px, py);
        }

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
        snprintf(key, sizeof(key), "%s##menu_%s", label, label);
        ZUIBoxFlags fl = ZUI_DrawText | ZUI_DrawBackground;
        if (enabled) fl = fl | ZUI_Clickable;

        ZUIBox* btn    = ZUIPushBox(ctx, key, (uint32_t)strlen(key), fl);
        btn->Size[0]   = ZText();
        btn->Size[1]   = ZFill(); // fill full bar height — VS Code style
        btn->Padding[0] = 10.f;  // left padding
        btn->Padding[2] = 10.f;  // right padding
        btn->EdgeSoftness = 0.f;
        ZUIBoxSetCornerRadius(btn, 0.f);

        // Check if this menu's popup is currently open
        uint64_t popup_hash = ZUIHashStr(label, (uint32_t)strlen(label));
        bool     is_open    = (ctx->ActivePopupKey == popup_hash);

        // When open: teal highlight matching the VS Code Dark+ accent
        if (is_open && enabled)
            ZUIBoxSetColor(btn, ctx->Theme.TabAccent[0], ctx->Theme.TabAccent[1],
                               ctx->Theme.TabAccent[2], 0.18f);
        else
            ZUIBoxSetColor(btn, 0.f, 0.f, 0.f, 0.f);

        SetTextColor(btn, enabled ? ctx->Theme.TextDefault : ctx->Theme.TextDim);

        ZUISignal sig = ZUISignalFromBox(ctx, btn);
        if (enabled && !is_open)
        {
            // Smooth hover: transparent → teal tint (VS Code-style)
            static const float kRest[4] = {0.f, 0.f, 0.f, 0.f};
            const float kHov[4] = {ctx->Theme.TabAccent[0], ctx->Theme.TabAccent[1],
                                    ctx->Theme.TabAccent[2], 0.12f};
            const float kAct[4] = {ctx->Theme.TabAccent[0], ctx->Theme.TabAccent[1],
                                    ctx->Theme.TabAccent[2], 0.22f};
            ApplyHotActive(btn, ctx, kRest, kHov, kAct);
        }
        ZUIPopBox(ctx);

        if ((sig.Flags & ZUI_SignalClicked) && enabled)
        {
            // ImGui: menu popup opens at button's LEFT EDGE, BOTTOM of the menu bar.
            // Use prev-frame screen coords from persistent state (updated by layout solve).
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, btn->Key);
            float px = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : ctx->MousePos[0];
            float py = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY : (ctx->MousePos[1] + 26.f);
            ZUIOpenPopup(ctx, label, px, py);
        }

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
                            ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow |
                            ZUI_FloatX | ZUI_FloatY);
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
        uint64_t key_hash = ZUIHashStr(key, key_len);

        ZUIBox*  field    = ZUIPushBox(ctx, key, key_len,
                                ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]          = ZPx(width_px);
        field->Size[1]          = ZPx(19.f);
        field->Padding[0]       = 4.f;
        field->Padding[2]       = 4.f;
        ZUIBoxSetCornerRadius(field, 3.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness  = 1.f;

        // Persistent state: UserData >= 0 → text-edit mode (cursor pos); -1 → drag mode
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, key_hash);
        bool is_focused  = (ctx->FocusKey == key_hash);
        bool text_mode   = is_focused && ps && (ps->UserData >= 0.f);

        // Static edit buffer — shared, switched on focus change like ZUIInputInt
        static char     s_df_buf[64] = {};
        static uint64_t s_df_key     = 0;

        if (text_mode)
        {
            if (s_df_key != key_hash)
            {
                snprintf(s_df_buf, sizeof(s_df_buf), "%.6g", (double)*value);
                s_df_key = key_hash;
            }
            SetBdrArr(field, ctx->Theme.InputFocusBorder);

            // Accumulate typed characters (digits, '.', '-')
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char c = ctx->TextInput[i];
                uint32_t l = (uint32_t)strlen(s_df_buf);
                if (l < sizeof(s_df_buf) - 1 &&
                    ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == 'E'))
                {
                    s_df_buf[l] = c; s_df_buf[l+1] = '\0';
                }
            }
            if (ctx->BackspacePressed)
            {
                uint32_t l = (uint32_t)strlen(s_df_buf);
                if (l > 0) s_df_buf[l-1] = '\0';
            }

            // Display with blinking cursor
            char display[72];
            bool show_pipe = (fmodf(ctx->Time, 1.0f) < 0.5f);
            if (show_pipe) snprintf(display, sizeof(display), "%s|", s_df_buf);
            else           snprintf(display, sizeof(display), "%s",  s_df_buf);
            field->Label = ZUIPushStr(&ctx->FrameArena, display, (uint32_t)strlen(display));
        }
        else
        {
            // Normal drag mode: show value + dim arrow hint when focused
            char val_buf[40];
            if (is_focused)
                snprintf(val_buf, sizeof(val_buf), "%.3f", (double)*value);
            else
                snprintf(val_buf, sizeof(val_buf), "%.3f", (double)*value);
            if (is_focused) SetBdrArr(field, ctx->Theme.InputFocusBorder);
            uint32_t vlen = (uint32_t)Helpers::secure_strlen(val_buf);
            field->Label  = ZUIPushStr(&ctx->FrameArena, val_buf, vlen);
        }

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        bool changed = false;

        // Ctrl+click → enter text edit mode
        if ((sig.Flags & ZUI_SignalClicked) && ctx->CtrlDown && !text_mode)
        {
            ctx->FocusKey = key_hash;
            if (ps) ps->UserData = 0.f; // text edit mode, cursor at 0
            snprintf(s_df_buf, sizeof(s_df_buf), "%.6g", (double)*value);
            s_df_key = key_hash;
        }
        else if ((sig.Flags & ZUI_SignalClicked) && !ctx->CtrlDown)
        {
            ctx->FocusKey = key_hash;
            // Normal click — stay in drag mode
        }

        // Confirm text edit on Enter/Tab or when focus leaves
        if (text_mode)
        {
            bool confirm = ctx->EnterPressed || (!is_focused);
            bool cancel  = ctx->EscapePressed;
            if (confirm || cancel)
            {
                if (confirm && s_df_buf[0])
                {
                    float parsed = (float)atof(s_df_buf);
                    if (parsed != *value) { *value = parsed; changed = true; }
                }
                if (ps) { ps->UserData = -1.f; } // exit text mode
                s_df_key = 0; s_df_buf[0] = '\0';
            }
        }
        else
        {
            // Normal drag mode
            if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
            {
                *value  += sig.DragDelta[0] * speed;
                changed  = true;
            }
            if (is_focused)
            {
                if (ctx->ArrowUpPressed)   { *value += speed; changed = true; }
                if (ctx->ArrowDownPressed) { *value -= speed; changed = true; }
            }
        }
        return changed;
    }

    // ---------------------------------------------------------------
    // ZUIDragInt
    // ---------------------------------------------------------------

    bool ZUIDragInt(ZUIContext* ctx, const char* key, int* value, float speed, float width_px)
    {
        uint32_t key_len  = (uint32_t)strlen(key);
        uint64_t key_hash = ZUIHashStr(key, key_len);

        ZUIBox*  field   = ZUIPushBox(ctx, key, key_len,
                               ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]         = ZPx(width_px);
        field->Size[1]         = ZPx(19.f);
        field->Padding[0]      = 4.f;
        field->Padding[2]      = 4.f;
        ZUIBoxSetCornerRadius(field, 3.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness = 1.f;

        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, key_hash);
        bool is_focused  = (ctx->FocusKey == key_hash);
        bool text_mode   = is_focused && ps && (ps->UserData >= 0.f);

        static char     s_di_buf[32] = {};
        static uint64_t s_di_key     = 0;

        if (text_mode)
        {
            if (s_di_key != key_hash) { snprintf(s_di_buf, sizeof(s_di_buf), "%d", *value); s_di_key = key_hash; }
            SetBdrArr(field, ctx->Theme.InputFocusBorder);
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char c = ctx->TextInput[i];
                uint32_t l = (uint32_t)strlen(s_di_buf);
                if (l < sizeof(s_di_buf) - 1 && ((c >= '0' && c <= '9') || (c == '-' && l == 0)))
                    { s_di_buf[l] = c; s_di_buf[l+1] = '\0'; }
            }
            if (ctx->BackspacePressed) { uint32_t l=(uint32_t)strlen(s_di_buf); if(l>0) s_di_buf[l-1]='\0'; }

            char display[36];
            if (fmodf(ctx->Time,1.0f)<0.5f) snprintf(display,sizeof(display),"%s|",s_di_buf);
            else                             snprintf(display,sizeof(display),"%s", s_di_buf);
            field->Label = ZUIPushStr(&ctx->FrameArena, display, (uint32_t)strlen(display));
        }
        else
        {
            if (is_focused) { SetBdrArr(field, ctx->Theme.InputFocusBorder); }
            char buf[16]; snprintf(buf, sizeof(buf), "%d", *value);
            field->Label = ZUIPushStr(&ctx->FrameArena, buf, (uint32_t)strlen(buf));
        }

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        bool changed = false;

        if ((sig.Flags & ZUI_SignalClicked) && ctx->CtrlDown && !text_mode)
        {
            ctx->FocusKey = key_hash;
            if (ps) ps->UserData = 0.f;
            snprintf(s_di_buf, sizeof(s_di_buf), "%d", *value);
            s_di_key = key_hash;
        }
        else if ((sig.Flags & ZUI_SignalClicked) && !ctx->CtrlDown)
        {
            ctx->FocusKey = key_hash;
        }

        if (text_mode)
        {
            if (ctx->EnterPressed || !is_focused)
            {
                if (s_di_buf[0]) { *value = atoi(s_di_buf); changed = true; }
                if (ps) ps->UserData = -1.f;
                s_di_key = 0; s_di_buf[0] = '\0';
            }
            else if (ctx->EscapePressed)
            {
                if (ps) ps->UserData = -1.f;
                s_di_key = 0; s_di_buf[0] = '\0';
            }
        }
        else
        {
            if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
            {
                float fv = (float)*value + sig.DragDelta[0] * speed;
                *value = (int)fv; changed = true;
            }
            if (is_focused)
            {
                int step = (int)speed > 0 ? (int)speed : 1;
                if (ctx->ArrowUpPressed)   { *value += step; changed = true; }
                if (ctx->ArrowDownPressed) { *value -= step; changed = true; }
            }
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
            chip->Size[0]   = ZPx(14.f);
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
        float swatch_sz = 22.f;
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
        field->Size[1]      = ZPx(19.f);
        field->Padding[0]   = 4.f; // ImGui FramePadding.x
        field->Padding[2]   = 4.f;
        ZUIBoxSetCornerRadius(field, 3.f);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);

        bool is_focused = (ctx->FocusKey == field->Key);
        bool changed    = false;

        // Cursor position — stored in UserData (float cast to int).
        // -1 = sentinel (cursor at end); valid range [0, strlen(buf)]
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, field->Key);

        if (is_focused)
        {
            SetBdrArr(field, ctx->Theme.InputFocusBorder);

            uint32_t len = (uint32_t)Helpers::secure_strlen(buf);

            // Clamp cursor to valid range on each focused frame
            int cpos = (ps && ps->UserData >= 0.f) ? (int)ps->UserData : (int)len;
            if (cpos < 0)        cpos = 0;
            if ((uint32_t)cpos > len) cpos = (int)len;

            // Insert characters at cursor position
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                len = (uint32_t)Helpers::secure_strlen(buf);
                if (len + 1 < buf_size)
                {
                    memmove(buf + cpos + 1, buf + cpos, len - cpos + 1);
                    buf[cpos] = ctx->TextInput[i];
                    cpos++; changed = true;
                }
            }

            // Backspace: delete character before cursor
            if (ctx->BackspacePressed && cpos > 0)
            {
                len = (uint32_t)Helpers::secure_strlen(buf);
                memmove(buf + cpos - 1, buf + cpos, len - cpos + 1);
                cpos--; changed = true;
            }

            // Forward delete at cursor
            len = (uint32_t)Helpers::secure_strlen(buf);
            if (ctx->DeletePressed && (uint32_t)cpos < len)
            {
                memmove(buf + cpos, buf + cpos + 1, len - cpos);
                changed = true;
            }

            // Left / Right / Home / End — move cursor
            len = (uint32_t)Helpers::secure_strlen(buf);
            if (ctx->ArrowLeftPressed  && cpos > 0)             cpos--;
            if (ctx->ArrowRightPressed && (uint32_t)cpos < len) cpos++;
            if (ctx->HomePressed)                               cpos = 0;
            if (ctx->EndPressed)                                cpos = (int)len;

            // Ctrl+C — copy buf to clipboard
            if (ctx->CtrlCPressed)
                snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);

            // Ctrl+X — cut: copy to clipboard + clear
            if (ctx->CtrlXPressed)
            {
                snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
                buf[0] = '\0'; cpos = 0; changed = true;
            }

            // Ctrl+A — select all: copy + clear
            if (ctx->CtrlAPressed)
            {
                snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
                buf[0] = '\0'; cpos = 0; changed = true;
            }

            // Ctrl+Backspace — delete word before cursor (back to prev space/boundary)
            if (ctx->CtrlBackspacePressed && cpos > 0)
            {
                len = (uint32_t)Helpers::secure_strlen(buf);
                int start = cpos - 1;
                while (start > 0 && buf[start-1] != ' ' && buf[start-1] != '/' &&
                       buf[start-1] != '\\' && buf[start-1] != '.') start--;
                int deleted = cpos - start;
                memmove(buf + start, buf + cpos, len - cpos + 1);
                cpos = start;
                changed = true;
                (void)deleted;
            }

            if (ps) ps->UserData = (float)cpos;

            // Build display: text_before_cursor + blinking_pipe + text_after_cursor
            char display[512];
            bool show_pipe = (fmodf(ctx->Time, 1.0f) < 0.5f);
            if (show_pipe)
                snprintf(display, sizeof(display), "%.*s|%s", cpos, buf, buf + cpos);
            else
                snprintf(display, sizeof(display), "%s", buf);

            uint32_t dlen = (uint32_t)Helpers::secure_strlen(display);
            field->Label  = ZUIPushStr(&ctx->FrameArena, display, dlen);
        }
        else
        {
            SetBdrArr(field, ctx->Theme.InputBorder);
            uint32_t dlen = (uint32_t)Helpers::secure_strlen(buf);
            field->Label  = ZUIPushStr(&ctx->FrameArena, buf, dlen);
        }

        ZUISignal sig  = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = field->Key;
            // On click, move cursor to end of text
            uint32_t len = (uint32_t)Helpers::secure_strlen(buf);
            if (ps) ps->UserData = (float)len;
        }

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

    // ================================================================
    // Plot widgets
    // ================================================================

    static void PlotSetup(ZUIContext* ctx, const char* key, const float* values, int count,
                           float v_min, float v_max,
                           ZUIBoxFlags draw_flag, ZUISize w, ZUISize h)
    {
        if (!values || count <= 0) return;

        // Auto-scale
        if (v_min >= 3.0e+38f || v_max >= 3.0e+38f)
        {
            v_min = values[0]; v_max = values[0];
            for (int i = 1; i < count; ++i)
            {
                if (values[i] < v_min) v_min = values[i];
                if (values[i] > v_max) v_max = values[i];
            }
            if (v_min == v_max) { v_min -= 1.f; v_max += 1.f; }
        }

        // Copy values to FrameArena so they survive until PreparePayload
        float* data = ZPushArray(&ctx->FrameArena, float, (uint32_t)count);
        for (int i = 0; i < count; ++i) data[i] = values[i];

        uint32_t klen = (uint32_t)strlen(key);
        ZUIBox* box   = ZUIPushBox(ctx, key, klen, ZUI_DrawBackground | draw_flag);
        box->Size[0]  = w;
        box->Size[1]  = h;
        // Repurpose Label for data pointer + count (no DrawText flag, so text path skipped)
        box->Label.Ptr = (const char*)data;
        box->Label.Len = (uint32_t)count;
        // Store range in Padding (normally {left,top,right,bottom} but unused for plots)
        box->Padding[0] = v_min;
        box->Padding[2] = v_max;
        SetBgArr(box, ctx->Theme.InputBg);
        box->EdgeSoftness = 0.f;
        ZUIPopBox(ctx);
    }

    void ZUIPlotLines(ZUIContext* ctx, const char* key, const float* values, int count,
                      float v_scale_min, float v_scale_max, const char* /*overlay_text*/,
                      ZUISize w, ZUISize h)
    {
        PlotSetup(ctx, key, values, count, v_scale_min, v_scale_max, ZUI_DrawPlotLines, w, h);
    }

    void ZUIPlotHistogram(ZUIContext* ctx, const char* key, const float* values, int count,
                          float v_scale_min, float v_scale_max, const char* /*overlay_text*/,
                          ZUISize w, ZUISize h)
    {
        PlotSetup(ctx, key, values, count, v_scale_min, v_scale_max, ZUI_DrawPlotBars, w, h);
    }

    // ================================================================
    // ZUIGridView
    // ================================================================

    ZUIBox* ZUIBeginGridView(ZUIContext* ctx, const char* key,
                             float item_w, float item_h,
                             ZUISize w, ZUISize h)
    {
        ctx->GV_ItemW   = item_w;
        ctx->GV_ItemH   = item_h;
        // items_per_row from container width approximation (ScreenW / item_w)
        // This stabilises after frame 0; panels typically fill most of ScreenW.
        int max_c = (item_w > 0.f) ? (int)((float)ctx->ScreenW / item_w) : 1;
        if (max_c < 1) max_c = 1;
        ctx->GV_MaxCols = max_c;
        ctx->GV_CurCol  = 0;
        ctx->GV_CurRow  = 0;
        ctx->GV_RowOpen = false;
        return ZUIBeginScrollRegion(ctx, key, w, h);
    }

    bool ZUIGridViewNextItem(ZUIContext* ctx, const char* item_key, bool selected)
    {
        // Start a new row when needed
        if (!ctx->GV_RowOpen || ctx->GV_CurCol >= ctx->GV_MaxCols)
        {
            if (ctx->GV_RowOpen) { ZUIEndRow(ctx); }
            char rk[72]; snprintf(rk, sizeof(rk), "##gvrow_%s_%d", item_key, ctx->GV_CurRow);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(ctx->GV_ItemH));
            ctx->GV_RowOpen = true;
            ctx->GV_CurCol  = 0;
            ctx->GV_CurRow++;
        }

        // Cell box
        char ck[128]; snprintf(ck, sizeof(ck), "##gvcell_%s_%d_%d",
                               item_key, ctx->GV_CurRow, ctx->GV_CurCol);
        ZUIBox* cell = ZUIPushBox(ctx, ck, (uint32_t)strlen(ck),
                                  ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable);
        cell->Size[0]  = ZPx(ctx->GV_ItemW);
        cell->Size[1]  = ZPx(ctx->GV_ItemH);
        cell->LayoutAxis = ZUIAxis::Y;

        float cell_bg[4] = { ctx->Theme.PanelBgAlt[0], ctx->Theme.PanelBgAlt[1],
                             ctx->Theme.PanelBgAlt[2], 0.8f };
        if (selected)
            ZUIBoxSetColorArr(cell, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColorArr(cell, cell_bg);

        cell->BorderColor[0] = ctx->Theme.TabActiveBorder[0];
        cell->BorderColor[1] = ctx->Theme.TabActiveBorder[1];
        cell->BorderColor[2] = ctx->Theme.TabActiveBorder[2];
        cell->BorderColor[3] = 0.2f;
        cell->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(cell, 4.f);
        cell->EdgeSoftness = 0.f;

        ZUISignal sig = ZUISignalFromBox(ctx, cell);
        if (!selected)
            ApplyHotActive(cell, ctx, cell_bg, ctx->Theme.RowHoverBg, ctx->Theme.RowSelectedBg);
        // Note: cell stays open (NOT popped) — caller adds content, then calls EndItem
        ctx->GV_CurCol++;
        return (sig.Flags & ZUI_SignalClicked) != 0;
    }

    void ZUIGridViewEndItem(ZUIContext* ctx)
    {
        ZUIPopBox(ctx); // close the cell box opened by NextItem
    }

    void ZUIEndGridView(ZUIContext* ctx)
    {
        if (ctx->GV_RowOpen) { ZUIEndRow(ctx); ctx->GV_RowOpen = false; }
        ZUIEndScrollRegion(ctx);
        ctx->GV_ItemW = 0.f;
        ctx->GV_ItemH = 0.f;
    }

    // ================================================================
    // ZUITreeView
    // ================================================================

    // Each node's open state is stored as ZUIPersistentState.UserData
    // (1.0=open, 0.0=closed), keyed by the node label hash.

    ZUIBox* ZUIBeginTreeView(ZUIContext* ctx, const char* key,
                             ZUISize w, ZUISize h,
                             const ZUITreeViewConfig* cfg)
    {
        if (cfg)
        {
            ctx->TV_RowH     = cfg->RowH;
            ctx->TV_IndentPx = cfg->IndentPx;
        }
        else
        {
            ctx->TV_RowH     = 19.f; // ImGui GetFrameHeight = 19px
            ctx->TV_IndentPx = 21.f; // ImGui IndentSpacing = 21px
        }
        ctx->TV_Depth = 0;
        return ZUIBeginScrollRegion(ctx, key, w, h);
    }

    void ZUIEndTreeView(ZUIContext* ctx)
    {
        ctx->TV_Depth = 0;
        ZUIEndScrollRegion(ctx);
    }

    // Shared row builder — matches ImGui TreeNodeBehavior exactly:
    //   • FramePadding.x (4px) left offset before indent
    //   • Arrow color = ImGuiCol_Text (TextDefault, not dim)
    //   • Label color = TextDefault always (selection shown via background only)
    //   • Hover/Active = HeaderHoveredBg / HeaderActiveBg (not subtle RowHoverBg)
    //   • Icon gap = ItemInnerSpacing.x = 4px
    static ZUISignal TV_BuildRow(ZUIContext* ctx, const char* label,
                                  bool selected, bool has_arrow, bool is_open,
                                  const float icon_col[4])
    {
        const float row_h    = ctx->TV_RowH;
        const float indent   = (float)ctx->TV_Depth * ctx->TV_IndentPx;
        const float arrow_w  = 14.f;  // FontSize + 1px (ImGui: g.FontSize = 13)
        const float icon_sz  = 12.f;
        const float kPadL    = 4.f;   // ImGui FramePadding.x = 4px

        char rk[128]; snprintf(rk, sizeof(rk), "##tvrow_%d_%s", ctx->TV_Depth, label);

        ZUIBox* row  = ZUIPushBox(ctx, rk, (uint32_t)strlen(rk),
                                  ZUI_DrawBackground | ZUI_Clickable);
        row->Size[0]    = ZFill();
        row->Size[1]    = ZPx(row_h);
        row->Padding[0] = kPadL;      // ImGui: FramePadding.x left margin before indent
        row->LayoutAxis = ZUIAxis::X;

        // Background: transparent at rest, teal highlight on hover/select
        // ImGui: HeaderHovered at 80% opacity, Header (selected) at 31%
        static const float kRest[4] = {0.f, 0.f, 0.f, 0.f};
        ZUIBoxSetColorArr(row, selected ? ctx->Theme.RowSelectedBg : kRest);

        // Depth indent
        if (indent > 0.f)
        {
            char sk[48]; snprintf(sk, sizeof(sk), "##tvsp_%d_%s", ctx->TV_Depth, label);
            ZUIBox* sp  = ZUIPushBox(ctx, sk, (uint32_t)strlen(sk), ZUI_None);
            sp->Size[0] = ZPx(indent); sp->Size[1] = ZPx(row_h);
            ZUIPopBox(ctx);
        }

        // Disclosure arrow (ImGuiCol_Text color) or blank spacer for leaves
        if (has_arrow)
        {
            char ak[128]; snprintf(ak, sizeof(ak), "##tvarr_%d_%s", ctx->TV_Depth, label);
            ZUIBox* ab  = ZUIPushBox(ctx, ak, (uint32_t)strlen(ak), ZUI_DrawTriArrow);
            ab->Size[0] = ZPx(arrow_w); ab->Size[1] = ZPx(row_h);
            // ImGui always uses ImGuiCol_Text for the arrow — never dimmed
            SetTextColor(ab, ctx->Theme.TextDefault);
            { auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, ab->Key);
              if (ps) ps->UserData = is_open ? 1.f : 0.f; }
            ZUIPopBox(ctx);
        }
        else
        {
            char lsk[48]; snprintf(lsk, sizeof(lsk), "##tvlsp_%d_%s", ctx->TV_Depth, label);
            ZUIBox* lsp  = ZUIPushBox(ctx, lsk, (uint32_t)strlen(lsk), ZUI_None);
            lsp->Size[0] = ZPx(arrow_w); lsp->Size[1] = ZPx(row_h);
            ZUIPopBox(ctx);
        }

        // Icon dot (engine-specific — ImGui has no icon; gap = ItemInnerSpacing.x = 4px)
        if (icon_col)
        {
            char ik[64]; snprintf(ik, sizeof(ik), "##tvic_%d_%s", ctx->TV_Depth, label);
            ZUIBox* ic  = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawBackground);
            ic->Size[0] = ZPx(icon_sz); ic->Size[1] = ZPx(icon_sz);
            ZUIBoxSetColorArr(ic, icon_col);
            ZUIBoxSetCornerRadius(ic, icon_sz * 0.5f);
            ic->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);

            char gk[64]; snprintf(gk, sizeof(gk), "##tvgap_%d_%s", ctx->TV_Depth, label);
            ZUIBox* gap  = ZUIPushBox(ctx, gk, (uint32_t)strlen(gk), ZUI_None);
            gap->Size[0] = ZPx(4.f); gap->Size[1] = ZPx(row_h); // ItemInnerSpacing.x
            ZUIPopBox(ctx);
        }

        // Label — ImGui always renders with ImGuiCol_Text regardless of selection
        uint32_t llen = (uint32_t)strlen(label);
        ZUIBox*  lbox = ZUIPushBox(ctx, label, llen, ZUI_DrawText);
        lbox->Size[0] = ZText(); lbox->Size[1] = ZPx(row_h);
        SetTextColor(lbox, ctx->Theme.TextDefault); // ImGui: always ImGuiCol_Text
        ZUIPopBox(ctx);

        bool is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig = ZUISignalFromBox(ctx, row);
        if (sig.Flags & ZUI_SignalClicked) { ctx->FocusKey = row->Key; }

        // Hover/active: ImGui uses HeaderHovered (strong) not subtle RowHoverBg
        if (!selected)
            ApplyHotActive(row, ctx, kRest,
                           ctx->Theme.HeaderHoveredBg,  // ImGui ImGuiCol_HeaderHovered
                           ctx->Theme.HeaderActiveBg);  // ImGui ImGuiCol_HeaderActive

        if (is_focused && (ctx->SpacePressed || ctx->EnterPressed))
            sig.Flags = sig.Flags | ZUI_SignalClicked;

        ZUIPopBox(ctx);
        return sig;
    }

    bool ZUITreeViewBeginNode(ZUIContext* ctx, const char* label,
                               bool selected, const float icon_col[4],
                               bool initial_open)
    {
        uint64_t hash  = ZUIHashStr(label, (uint32_t)strlen(label)) ^ (uint64_t)ctx->TV_Depth;
        auto*    state = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        // UserData < 0 means never explicitly set — apply initial_open on first use
        if (state && state->UserData < 0.f)
            state->UserData = initial_open ? 1.f : 0.f;
        bool     is_open = state && state->UserData > 0.5f;

        ZUISignal sig = TV_BuildRow(ctx, label, selected, true, is_open, icon_col);

        if (sig.Flags & ZUI_SignalClicked)
        {
            is_open = !is_open;
            if (state) state->UserData = is_open ? 1.f : 0.f;
        }
        // Arrow Right opens a closed node; Arrow Left closes an open one (when row has focus)
        // The row key matches what TV_BuildRow built: "##tvrow_<depth>_<label>"
        {
            char tvk[128]; snprintf(tvk, sizeof(tvk), "##tvrow_%d_%s", ctx->TV_Depth, label);
            if (ctx->FocusKey == ZUIHashStr(tvk, (uint32_t)strlen(tvk)))
            {
                if (ctx->ArrowRightPressed && !is_open) { is_open = true;  if (state) state->UserData = 1.f; }
                if (ctx->ArrowLeftPressed  &&  is_open) { is_open = false; if (state) state->UserData = 0.f; }
            }
        }

        if (is_open) { ctx->TV_Depth++; }
        return is_open;
    }

    void ZUITreeViewEndNode(ZUIContext* ctx)
    {
        if (ctx->TV_Depth > 0) ctx->TV_Depth--;
    }

    bool ZUITreeViewLeaf(ZUIContext* ctx, const char* label,
                          bool selected, const float icon_col[4])
    {
        ZUISignal sig = TV_BuildRow(ctx, label, selected, false, false, icon_col);
        return (sig.Flags & ZUI_SignalClicked) != 0;
    }

    // ================================================================
    // ZUIDataTable
    // ================================================================

    // Persistent state layout for a data table:
    //   slot key = DT_Key ^ (col * 2654435761ULL)  → UserData = column width
    //   slot key = DT_Key ^ 0xBAADF00DULL           → UserData = sort encoding

    static constexpr uint64_t kDT_SortSuffix = 0xBAADF00DULL;
    static constexpr float    kDT_ColDefault  = 100.f; // default logical width
    static constexpr float    kDT_HeaderH     = 24.f;  // logical header row height
    static constexpr float    kDT_RowH        = 22.f;  // logical data row height
    static constexpr float    kDT_ResizeW     =  4.f;  // resize grip logical width

    static uint64_t DT_ColKey(uint64_t table_key, int col)
    {
        return table_key ^ ((uint64_t)col * 2654435761ULL);
    }

    bool ZUIBeginDataTable(ZUIContext* ctx, const char* key,
                            int col_count, const ZUIDataTableColumn* cols,
                            ZUISize h)
    {
        ctx->DT_Key      = ZUIHashStr(key, (uint32_t)strlen(key));
        ctx->DT_ColCount = col_count;
        ctx->DT_CurCol   = -1;
        ctx->DT_RowIndex = 0;
        ctx->DT_InRow    = false;
        ctx->DT_InHeader = false;
        ctx->DT_RowBox   = nullptr;
        ctx->DT_SortChanged = false;

        // Load column widths from persistent state; apply InitWidth for new tables
        ctx->DT_ColWidths = ZPushArray(&ctx->FrameArena, float, col_count);
        for (int i = 0; i < col_count; ++i)
        {
            auto* s = ZUIStateGetOrInsert(&ctx->StateStore, DT_ColKey(ctx->DT_Key, i));
            float init = (cols && cols[i].InitWidth > 0.f) ? cols[i].InitWidth : kDT_ColDefault;
            if (s && s->UserData > 1.f) // already set
                ctx->DT_ColWidths[i] = s->UserData;
            else
            {
                ctx->DT_ColWidths[i] = init;
                if (s) s->UserData = init;
            }
        }

        // Load sort state
        {
            auto* ss = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_Key ^ kDT_SortSuffix);
            if (ss && ss->UserData != 0.f && ss->UserData > -0.5f) // skip -1 sentinel
            {
                float enc = ss->UserData;
                ctx->DT_SortAsc = (enc > 0.f);
                ctx->DT_SortCol = (int)(enc > 0.f ? enc : -enc) - 1;
            }
            else { ctx->DT_SortCol = -1; ctx->DT_SortAsc = true; }
        }

        // Outer container (full-width column that clips)
        ZUIBeginColumn(ctx, key, ZFill(), h);

        // Store cols in FrameArena for HeadersRow
        if (cols)
        {
            auto* copy = ZPushArray(&ctx->FrameArena, ZUIDataTableColumn, col_count);
            for (int i = 0; i < col_count; ++i) copy[i] = cols[i];
            ctx->DT_Cols = copy;
        }
        else { ctx->DT_Cols = nullptr; }

        return true;
    }

    void ZUIDataTableHeadersRow(ZUIContext* ctx)
    {
        const ZUIDataTableColumn* cols = (const ZUIDataTableColumn*)ctx->DT_Cols;
        float header_h = kDT_HeaderH;
        float resize_w = kDT_ResizeW;

        char hk[48]; snprintf(hk, sizeof(hk), "##dthdr_%llu", (unsigned long long)ctx->DT_Key);
        ZUIBox* hrow = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
        hrow->Flags  = hrow->Flags | ZUI_DrawBackground;
        ZUIBoxSetColor(hrow, 0.165f, 0.165f, 0.170f, 1.f);
        hrow->EdgeSoftness = 0.f;

        for (int i = 0; i < ctx->DT_ColCount; ++i)
        {
            float cw = ctx->DT_ColWidths[i];
            bool sortable  = cols && cols[i].Sortable;
            bool resizable = cols && cols[i].Resizable;

            // Outer header cell box (clickable when sortable)
            char ck[56]; snprintf(ck, sizeof(ck), "##dthc_%llu_%d", (unsigned long long)ctx->DT_Key, i);
            ZUIBoxFlags cell_flags = ZUI_DrawBackground | ZUI_DrawBorder;
            if (sortable) cell_flags = cell_flags | ZUI_Clickable;
            ZUIBox* cell = ZUIPushBox(ctx, ck, (uint32_t)strlen(ck), cell_flags);
            cell->Size[0] = ZPx(cw - (resizable ? resize_w : 0.f));
            cell->Size[1] = ZPx(header_h);
            cell->LayoutAxis = ZUIAxis::X;

            // Hover tint on sortable headers
            bool cell_hot = (ctx->HotKey == cell->Key);
            if (cell_hot && sortable)
                ZUIBoxSetColor(cell, 0.22f, 0.22f, 0.26f, 1.f);
            else
                ZUIBoxSetColor(cell, 0.165f, 0.165f, 0.170f, 1.f);
            cell->BorderColor[0]=0.22f; cell->BorderColor[1]=0.22f;
            cell->BorderColor[2]=0.22f; cell->BorderColor[3]=1.f;
            cell->BorderThickness = 1.f;
            cell->EdgeSoftness    = 0.f;

            // Left padding
            char sp1k[32]; snprintf(sp1k, sizeof(sp1k), "##thsp1_%d", i);
            ZUIBox* sp1 = ZUIPushBox(ctx, sp1k, (uint32_t)strlen(sp1k), ZUI_None);
            sp1->Size[0] = ZPx(6.f); sp1->Size[1] = ZPx(header_h);
            ZUIPopBox(ctx);

            // Column label
            const char* lbl = (cols && cols[i].Label) ? cols[i].Label : "?";
            bool is_sort_col = (ctx->DT_SortCol == i);
            uint32_t lblen = (uint32_t)strlen(lbl);
            ZUIBox* lbox = ZUIPushBox(ctx, lbl, lblen, ZUI_DrawText);
            lbox->Size[0] = ZText(); lbox->Size[1] = ZPx(header_h);
            const float* lc = is_sort_col ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
            lbox->TextColor[0]=lc[0]; lbox->TextColor[1]=lc[1];
            lbox->TextColor[2]=lc[2]; lbox->TextColor[3]=lc[3];
            ZUIPopBox(ctx);

            // Sort direction indicator
            if (is_sort_col && sortable)
            {
                char sk[32]; snprintf(sk, sizeof(sk), "##ths_%d", i);
                const char* arrow = ctx->DT_SortAsc ? " ^" : " v";
                ZUIBox* arr = ZUIPushBox(ctx, sk, (uint32_t)strlen(sk), ZUI_DrawText);
                arr->Size[0] = ZPx(14.f); arr->Size[1] = ZPx(header_h);
                arr->Label = ZUIPushStr(&ctx->FrameArena, arrow, (uint32_t)strlen(arrow));
                float ac[4] = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1],
                               ctx->Theme.TabActiveBorder[2], 1.f};
                arr->TextColor[0]=ac[0]; arr->TextColor[1]=ac[1];
                arr->TextColor[2]=ac[2]; arr->TextColor[3]=ac[3];
                ZUIPopBox(ctx);
            }

            ZUISignal cell_sig = ZUISignalFromBox(ctx, cell);
            ZUIPopBox(ctx); // cell

            // Sort on click
            if (sortable && (cell_sig.Flags & ZUI_SignalClicked))
            {
                if (ctx->DT_SortCol == i) { ctx->DT_SortAsc = !ctx->DT_SortAsc; }
                else { ctx->DT_SortCol = i; ctx->DT_SortAsc = true; }
                ctx->DT_SortChanged = true;
                // Persist sort state
                float enc = (float)(ctx->DT_SortCol + 1) * (ctx->DT_SortAsc ? 1.f : -1.f);
                auto* ss = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_Key ^ kDT_SortSuffix);
                if (ss) ss->UserData = enc;
            }

            // Column resize grip (right edge of header cell)
            if (resizable)
            {
                char rk[56]; snprintf(rk, sizeof(rk), "##dtresize_%llu_%d", (unsigned long long)ctx->DT_Key, i);
                ZUIBox* grip = ZUIPushBox(ctx, rk, (uint32_t)strlen(rk),
                                          ZUI_DrawBackground | ZUI_Clickable);
                grip->Size[0] = ZPx(resize_w);
                grip->Size[1] = ZPx(header_h);
                bool grip_hot = (ctx->HotKey == grip->Key);
                float gc[4] = { 0.35f, 0.35f, 0.40f, grip_hot ? 0.9f : 0.3f };
                ZUIBoxSetColorArr(grip, gc);
                grip->EdgeSoftness = 0.f;
                ZUISignal gsig = ZUISignalFromBox(ctx, grip);
                ZUIPopBox(ctx);

                // Drag to resize
                if ((gsig.Flags & ZUI_SignalHeld) && gsig.DragDelta[0] != 0.f)
                {
                    float new_w = ctx->DT_ColWidths[i] + gsig.DragDelta[0];
                    if (new_w < 30.f) new_w = 30.f;
                    ctx->DT_ColWidths[i] = new_w;
                    auto* cs = ZUIStateGetOrInsert(&ctx->StateStore, DT_ColKey(ctx->DT_Key, i));
                    if (cs) cs->UserData = new_w;
                }
            }
        }

        ZUIEndRow(ctx);
    }

    bool ZUIDataTableNextRow(ZUIContext* ctx, bool selected)
    {
        float row_h  = kDT_RowH;

        // Close previous row if open
        if (ctx->DT_InRow)
        {
            if (ctx->DT_CurCol >= 0) { ZUIEndColumn(ctx); ctx->DT_CurCol = -1; }
            ZUIEndRow(ctx);
            ctx->DT_InRow = false;
        }

        // Alternating row background
        bool  alt = (ctx->DT_RowIndex % 2) == 1;
        float bg[4];
        if (selected)
            { bg[0]=ctx->Theme.RowSelectedBg[0]; bg[1]=ctx->Theme.RowSelectedBg[1];
              bg[2]=ctx->Theme.RowSelectedBg[2]; bg[3]=ctx->Theme.RowSelectedBg[3]; }
        else if (alt)
            { bg[0]=ctx->Theme.PanelBgAlt[0]; bg[1]=ctx->Theme.PanelBgAlt[1];
              bg[2]=ctx->Theme.PanelBgAlt[2]; bg[3]=0.5f; }
        else
            { bg[0]=0.f; bg[1]=0.f; bg[2]=0.f; bg[3]=0.f; }

        char rk[48]; snprintf(rk, sizeof(rk), "##dtrow_%llu_%d",
                              (unsigned long long)ctx->DT_Key, ctx->DT_RowIndex);
        ZUIBox* row  = ZUIPushBox(ctx, rk, (uint32_t)strlen(rk),
                                  ZUI_DrawBackground | ZUI_Clickable);
        row->Size[0] = ZFill();
        row->Size[1] = ZPx(row_h);
        row->LayoutAxis = ZUIAxis::X;
        ZUIBoxSetColorArr(row, bg);
        row->EdgeSoftness = 0.f;

        ZUISignal rsig = ZUISignalFromBox(ctx, row);
        // Lerped hover via HotT
        if (!selected)
        {
            static const float kDTRest[4] = {0.f,0.f,0.f,0.f};
            ApplyHotActive(row, ctx, kDTRest, ctx->Theme.RowHoverBg, ctx->Theme.RowSelectedBg);
        }

        ctx->DT_RowBox = row;
        ctx->DT_InRow  = true;
        ctx->DT_CurCol = -1;
        ctx->DT_RowIndex++;

        return (rsig.Flags & ZUI_SignalClicked) != 0;
    }

    void ZUIDataTableSetColumn(ZUIContext* ctx, int col)
    {
        if (!ctx->DT_InRow) { return; }
        // Close previous cell
        if (ctx->DT_CurCol >= 0) { ZUIEndColumn(ctx); }

        ctx->DT_CurCol = col;
        float cw = (col < ctx->DT_ColCount && ctx->DT_ColWidths)
                   ? ctx->DT_ColWidths[col] : 80.f;

        char ck[48]; snprintf(ck, sizeof(ck), "##dtcell_%llu_%d_%d",
                              (unsigned long long)ctx->DT_Key, ctx->DT_RowIndex, col);
        ZUIBeginColumn(ctx, ck, ZPx(cw), ZFill());
        ZUISpacer(ctx, 4.f); // left padding
    }

    void ZUIEndDataTable(ZUIContext* ctx)
    {
        // Close open cell + row
        if (ctx->DT_InRow)
        {
            if (ctx->DT_CurCol >= 0) { ZUIEndColumn(ctx); ctx->DT_CurCol = -1; }
            ZUIEndRow(ctx);
            ctx->DT_InRow = false;
        }
        ZUIEndColumn(ctx); // outer container
        ctx->DT_ColCount = 0;
    }

    ZUITableSortSpec ZUIDataTableGetSortSpecs(ZUIContext* ctx)
    {
        ZUITableSortSpec spec;
        spec.ColumnIndex = ctx->DT_SortCol;
        spec.Ascending   = ctx->DT_SortAsc;
        spec.Changed     = ctx->DT_SortChanged;
        ctx->DT_SortChanged = false; // consume
        return spec;
    }

    // ---------------------------------------------------------------
    // ZUISearchBox
    // ---------------------------------------------------------------

    bool ZUISearchBox(ZUIContext* ctx, const char* key,
                      char* buf, uint32_t buf_size,
                      const char* placeholder, ZUISize w)
    {
        // Full bordered row: [Q icon] [editable text field]
        char rk[80]; snprintf(rk, sizeof(rk), "##sb_%s", key);
        ZUIBox* row = ZUIBeginRow(ctx, rk, w, ZPx(19.f));
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        SetBgArr(row, ctx->Theme.InputBg);
        SetBdrArr(row, ctx->Theme.InputBorder);
        row->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(row, 3.f);
        row->EdgeSoftness = 0.f;

        // Q — search icon, dim
        {
            char ik[48]; snprintf(ik, sizeof(ik), "Q##si_%s", key);
            ZUIBox* ic = ZUIPushBox(ctx, ik, (uint32_t)strlen(ik), ZUI_DrawText);
            ic->Size[0] = ZPx(18.f); ic->Size[1] = ZFill();
            ic->TextAlign = ZUITextAlign::Center;
            float col[4] = {0.38f, 0.38f, 0.40f, 1.f};
            SetTextColor(ic, col);
            ZUIPopBox(ctx);
        }

        // Text field (no border — border is on the outer row)
        uint32_t klen           = (uint32_t)strlen(key);
        uint32_t field_key_hash = ZUIHashStr(key, klen);
        bool     is_focused     = (ctx->FocusKey == field_key_hash);

        ZUIPersistentState* ps  = ZUIStateGetOrInsert(&ctx->StateStore, field_key_hash);

        // Build display string — cursor position from persistent state
        char display[512];
        if (!is_focused && buf[0] == '\0')
        {
            snprintf(display, sizeof(display), "%s", placeholder);
        }
        else if (is_focused)
        {
            uint32_t len = (uint32_t)strlen(buf);
            int cpos = (ps && ps->UserData >= 0.f) ? (int)ps->UserData : (int)len;
            if (cpos < 0) cpos = 0;
            if ((uint32_t)cpos > len) cpos = (int)len;
            bool show_pipe = (fmodf(ctx->Time, 1.0f) < 0.5f);
            if (show_pipe) snprintf(display, sizeof(display), "%.*s|%s", cpos, buf, buf + cpos);
            else           snprintf(display, sizeof(display), "%s", buf);
        }
        else
        {
            snprintf(display, sizeof(display), "%s", buf);
        }

        ZUIBox* field = ZUIPushBox(ctx, key, klen, ZUI_DrawText | ZUI_Clickable);
        field->Size[0] = ZFill(); field->Size[1] = ZFill();
        field->Padding[0] = 2.f;
        uint32_t dlen = (uint32_t)Helpers::secure_strlen(display);
        field->Label  = ZUIPushStr(&ctx->FrameArena, display, dlen);
        if (!is_focused && buf[0] == '\0')
        { float ph_col[4]={0.38f,0.38f,0.40f,1.f}; SetTextColor(field, ph_col); }
        else
        { SetTextColor(field, ctx->Theme.TextDefault); }
        if (is_focused) { SetBdrArr(row, ctx->Theme.InputFocusBorder); }

        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);
        ZUIEndRow(ctx);

        bool changed = false;
        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = field_key_hash;
            if (ps) ps->UserData = (float)strlen(buf); // cursor at end on click
        }

        // Accept text/cursor operations when focused
        if (is_focused)
        {
            uint32_t len = (uint32_t)strlen(buf);
            int cpos = (ps && ps->UserData >= 0.f) ? (int)ps->UserData : (int)len;
            if (cpos < 0) cpos = 0;
            if ((uint32_t)cpos > len) cpos = (int)len;

            for (uint32_t i = 0; i < ctx->TextInputLen && len + 1 < buf_size; ++i)
            {
                memmove(buf + cpos + 1, buf + cpos, len - cpos + 1);
                buf[cpos] = ctx->TextInput[i];
                cpos++; len++; changed = true;
            }
            if (ctx->BackspacePressed && cpos > 0)
            {
                len = (uint32_t)strlen(buf);
                memmove(buf + cpos - 1, buf + cpos, len - cpos + 1);
                cpos--; changed = true;
            }
            len = (uint32_t)strlen(buf);
            if (ctx->DeletePressed && (uint32_t)cpos < len)
            { memmove(buf + cpos, buf + cpos + 1, len - cpos); changed = true; }
            if (ctx->ArrowLeftPressed  && cpos > 0)              cpos--;
            if (ctx->ArrowRightPressed && (uint32_t)cpos < len)  cpos++;
            if (ctx->HomePressed)  cpos = 0;
            if (ctx->EndPressed)   cpos = (int)(uint32_t)strlen(buf);
            if (ctx->CtrlXPressed || ctx->CtrlAPressed)
            {
                snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
                buf[0] = '\0'; cpos = 0; changed = true;
            }
            if (ctx->CtrlCPressed)
                snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
            if (ps) ps->UserData = (float)cpos;
        }

        return changed;
    }

} // namespace ZEngine::UI
