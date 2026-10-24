#include <ZEngine/Helpers/MemoryOperations.h>
#include <ZEngine/UI/ZUIWidgets.h>
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace ZEngine::UI
{
    // Internal helpers

    static void SetTextColor(ZUIBox* box, const float c[4])
    {
        box->TextColor[0] = c[0];
        box->TextColor[1] = c[1];
        box->TextColor[2] = c[2];
        box->TextColor[3] = c[3];
    }

    static void SetBgArr(ZUIBox* b, const float c[4])
    {
        ZUIBoxSetColorArr(b, c);
    }
    static void SetBdrArr(ZUIBox* b, const float c[4])
    {
        b->BorderColor[0] = c[0];
        b->BorderColor[1] = c[1];
        b->BorderColor[2] = c[2];
        b->BorderColor[3] = c[3];
    }

    static bool CaretVisible(const ZUIContext* ctx)
    {
        const float period = ctx->Style.CursorBlinkRate;
        return ctx->Style.ReducedMotion || period <= 0.f || fmodf(ctx->Time, period) < period * 0.5f;
    }

    static void GrowPopupForLabel(ZUIContext* ctx, const char* label, float extra, ZUIFontSize role = ZUIFontSize::Body)
    {
        if (!ctx->PopupBuildDepth || !label)
            return;
        auto* popup = ctx->PopupStack[ctx->PopupBuildDepth - 1].Box;
        if (!popup || !popup->FirstChild)
            return;
        float measured[2] = {};
        if (auto* font = ctx->GetFont(role))
            ZUIMeasureText(font, label, (uint32_t) Helpers::secure_strlen(label), measured);
        auto* sizer          = popup->FirstChild;
        sizer->Size[0].Value = fmaxf(sizer->Size[0].Value, measured[0] + extra);
    }

    // Lerp box background through rest → hover → active using HotT/ActiveT.
    // Call AFTER ZUISignalFromBox so the persistent state is already updated this frame.
    static void ApplyHotActive(ZUIBox* box, ZUIContext* ctx, const float rest[4], const float hov[4], const float act[4])
    {
        ZUIPersistentState* st = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        float               ht = st ? st->HotT : 0.f;
        float               at = st ? st->ActiveT : 0.f;
        // Floor to 50% on first hover frame so the transition is visible immediately.
        // The lerp in ZUISignalFromBox catches up to 1.0 within a few frames.
        if (ctx->HotKey == box->Key)
            ht = ht > 0.5f ? ht : 0.5f;
        if (ctx->ActiveKey == box->Key)
            at = 1.f;
        for (int ch = 0; ch < 4; ++ch)
        {
            const float hover = rest[ch] + (hov[ch] - rest[ch]) * ht;
            const float col   = hover + (act[ch] - hover) * at;
            for (int k = 0; k < 4; ++k)
                box->Colors[k][ch] = col;
        }
    }

    // Layout containers

    ZUIBox* ZUIBeginColumn(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len    = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  box    = ZUIPushBox(ctx, key, len, ZUI_None);
        box->Size[0]    = w;
        box->Size[1]    = h;
        box->LayoutAxis = ZUIAxis::Y;
        return box;
    }

    void ZUIEndColumn(ZUIContext* ctx)
    {
        ZUIPopBox(ctx);
    }

    ZUIBox* ZUIBeginRow(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len    = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  box    = ZUIPushBox(ctx, key, len, ZUI_None);
        box->Size[0]    = w;
        box->Size[1]    = h;
        box->LayoutAxis = ZUIAxis::X;
        return box;
    }

    void ZUIEndRow(ZUIContext* ctx)
    {
        ZUIPopBox(ctx);
    }

    static void ApplyScrollbarDrag(ZUIContext* ctx, uint64_t region_key, ZUIPersistentState& state, int axis)
    {
        if (ctx->Disabled || !ctx->ActiveKey || !ctx->MouseDown[0])
            return;
        const float extent  = axis ? state.ScreenMaxY - state.ScreenMinY : state.ScreenMaxX - state.ScreenMinX;
        const float maximum = axis ? state.MaxScrollY : state.MaxScrollX;
        if (!(extent > 0.f) || !(maximum > 1.f))
            return;
        char key[64] = {};
        snprintf(key, sizeof(key), axis ? "##sbthm_%llu" : "##hsbthm_%llu", static_cast<unsigned long long>(region_key));
        if (ctx->ActiveKey != ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key))))
            return;
        const float delta = ctx->MousePos[axis] - ctx->PrevMousePos[axis];
        if (!std::isfinite(delta) || delta == 0.f)
            return;
        const float thumb    = std::clamp(extent * extent / (extent + maximum), fminf(ctx->Style.ScrollbarMinThumbPx, extent), extent);
        const float ratio    = extent > thumb ? maximum / (extent - thumb) : 0.f;
        float&      position = axis ? state.ScrollY : state.ScrollX;
        float&      target   = axis ? state.ScrollYTarget : state.ScrollXTarget;
        position = target        = std::clamp(position + delta * ratio, 0.f, maximum);
        state.ScrollbarShowTimer = .5f;
    }

    ZUIBox* ZUIBeginScrollRegion(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t len           = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        // Clickable for wheel/scrollbar routing, not a button: its transparent
        // background suppresses the generic whole-region hover wash.
        ZUIBox*  box           = ZUIPushBox(ctx, key, len, ZUI_Scrollable | ZUI_ClipChildren | ZUI_Clickable | ZUI_NoKeyboardFocus | ZUI_DrawBackground);
        box->Size[0]           = w;
        box->Size[1]           = h;
        box->LayoutAxis        = ZUIAxis::Y;

        // Smooth scroll — RAD Debugger model:
        //   Wheel input writes only ScrollYTarget (ZUIInteractionPass).
        //   Each frame here ScrollY animates toward that target via exponential lerp.
        //   Snap < 2px prevents infinite micro-oscillation.
        //   Clamps are unconditional — MaxScrollY == 0 on first frame correctly
        //   clamps to [0,0], and the `> 0` guard that was here previously let
        //   ScrollYTarget accumulate unbounded on first-frame wheel events.
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, box->Key);
        if (ps)
        {
            float dt     = ctx->DeltaTime > 0.f ? ctx->DeltaTime : (1.f / 60.f);
            float alpha  = ctx->Style.ReducedMotion ? 1.f : 1.f - expf(-ctx->Style.ScrollSmoothSpeed * dt);

            ps->ScrollY += (ps->ScrollYTarget - ps->ScrollY) * alpha;
            if (fabsf(ps->ScrollYTarget - ps->ScrollY) < 2.f)
                ps->ScrollY = ps->ScrollYTarget;
            if (ps->ScrollY < 0.f)
                ps->ScrollY = 0.f;
            if (ps->ScrollY > ps->MaxScrollY)
                ps->ScrollY = ps->MaxScrollY;

            ps->ScrollX += (ps->ScrollXTarget - ps->ScrollX) * alpha;
            if (fabsf(ps->ScrollXTarget - ps->ScrollX) < 2.f)
                ps->ScrollX = ps->ScrollXTarget;
            if (ps->ScrollX < 0.f)
                ps->ScrollX = 0.f;
            if (ps->ScrollX > ps->MaxScrollX)
                ps->ScrollX = ps->MaxScrollX;
            // Finalize immediate thumb motion before children choose a virtual
            // window. Applying it at EndScrollRegion makes those rows disappear
            // for a frame when layout sees a different scroll offset.
            ApplyScrollbarDrag(ctx, box->Key, *ps, 1);
            ApplyScrollbarDrag(ctx, box->Key, *ps, 0);
        }

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
            ZUISignalFromBox(ctx, sb);

            // kScrollbarShowDuration: seconds the scrollbar stays visible after a scroll event.
            // Shared by vertical and horizontal scrollbar sections — change in one place.
            static constexpr float kScrollbarShowDuration = 0.5f;

            ZUIPersistentState*    ps                     = ZUIStateGetOrInsert(&ctx->StateStore, sb->Key);
            if (ps)
            {
                const float dt         = ctx->DeltaTime > 0.f ? ctx->DeltaTime : (1.f / 60.f);
                ps->ScrollbarShowTimer = fmaxf(0.f, ps->ScrollbarShowTimer - dt);
                if (fabsf(ps->ScrollY - ps->ScrollYTarget) > 0.5f || fabsf(ps->ScrollX - ps->ScrollXTarget) > 0.5f)
                    ps->ScrollbarShowTimer = fmaxf(ps->ScrollbarShowTimer, kScrollbarShowDuration * 0.6f);
            }
            if (ps && ps->MaxScrollY > 1.f && ps->ScreenMaxX > ps->ScreenMinX)
            {
                float sx0 = ps->ScreenMinX, sy0 = ps->ScreenMinY;
                float sx1 = ps->ScreenMaxX, sy1 = ps->ScreenMaxY;

                float track_h   = sy1 - sy0;
                float content_h = track_h + ps->MaxScrollY;
                float thumb_h   = (content_h > 0.f) ? (track_h * track_h / content_h) : track_h;
                if (thumb_h < ctx->Style.ScrollbarMinThumbPx)
                    thumb_h = ctx->Style.ScrollbarMinThumbPx;
                if (thumb_h > track_h)
                    thumb_h = track_h;
                float       thumb_y   = (ps->MaxScrollY > 0.f) ? sy0 + (ps->ScrollY / ps->MaxScrollY) * (track_h - thumb_h) : sy0;

                // Visibility: floor (always discoverable) + scroll-timer + hover — VS Code model.
                // Floor keeps a faint hint at rest; timer flashes full on wheel/drag; hover sustains it.
                float       vis_timer = ctx->Style.ReducedMotion ? (ctx->ScrollDelta != 0.f ? 1.f : 0.f) : ps->ScrollbarShowTimer / kScrollbarShowDuration;
                float       vis       = fmaxf(fmaxf(ps->HotT, vis_timer), ctx->Style.ScrollbarAutoHideAlpha);
                const float kBarW     = ctx->Style.ScrollbarSize;

                // FloatPos is parent-relative (scroll region ScreenMin = (sx0, sy0)).
                // sx1-kBarW and thumb_y are absolute screen coords — subtract sx0/sy0.
                float       bar_x     = (sx1 - kBarW) - sx0; // right edge, relative to scroll region left

                // Transparent 10 px hit lane; only its 6 px child is visible.
                char        thm[64]   = {};
                snprintf(thm, sizeof(thm), "##sbthm_%llu", (unsigned long long) sb->Key);
                ZUIBox* thumb      = ZUIPushBox(ctx, thm, (uint32_t) ZEngine::Helpers::secure_strlen(thm), ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | ZUI_NoKeyboardFocus);
                thumb->Size[0]     = ZPx(kBarW);
                thumb->Size[1]     = ZPx(thumb_h);
                thumb->FloatPos[0] = bar_x;
                thumb->FloatPos[1] = thumb_y - sy0; // relative to scroll region top
                (void) ZUISignalFromBox(ctx, thumb);
                const bool   active = ctx->ActiveKey == thumb->Key;
                const bool   hot    = ctx->HotKey == thumb->Key;
                const float* grab   = active ? ctx->Theme.ScrollbarGrabAct : (hot ? ctx->Theme.ScrollbarGrabHov : ctx->Theme.ScrollbarGrab);
                if (hot || active)
                    vis = 1.f;
                vis                 = fminf(vis, 1.f);
                char visual_key[80] = {};
                snprintf(visual_key, sizeof(visual_key), "%s_v", thm);
                auto*       visual   = ZUIPushBox(ctx, visual_key, (uint32_t) Helpers::secure_strlen(visual_key), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                const float visual_w = fminf(Metrics::Space6, kBarW);
                visual->Size[0]      = ZPx(visual_w);
                visual->Size[1]      = ZPx(thumb_h);
                visual->FloatPos[0]  = (kBarW - visual_w) * 0.5f;
                ZUIBoxSetColor(visual, grab[0], grab[1], grab[2], grab[3] * vis);
                ZUIBoxSetCornerRadius(visual, ctx->Style.ScrollbarRounding);
                ZUIPopBox(ctx);

                ZUIPopBox(ctx);
            }

            // Horizontal scrollbar (appears when content overflows on X)
            if (ps && ps->MaxScrollX > 1.f && ps->ScreenMaxY > ps->ScreenMinY)
            {
                float sx0 = ps->ScreenMinX, sy0 = ps->ScreenMinY;
                float sx1 = ps->ScreenMaxX, sy1 = ps->ScreenMaxY;
                float track_w   = sx1 - sx0;
                float content_w = track_w + ps->MaxScrollX;
                float thumb_w   = (content_w > 0.f) ? (track_w * track_w / content_w) : track_w;
                if (thumb_w < ctx->Style.ScrollbarMinThumbPx)
                    thumb_w = ctx->Style.ScrollbarMinThumbPx;
                if (thumb_w > track_w)
                    thumb_w = track_w;
                // FloatPos is parent-relative; convert absolute coords to scroll-region-relative
                const float kBarH       = ctx->Style.ScrollbarSize;
                float       thumb_x_rel = (ps->MaxScrollX > 0.f) ? (ps->ScrollX / ps->MaxScrollX) * (track_w - thumb_w) : 0.f;
                float       bar_y_h     = (sy1 - kBarH) - sy0; // bottom edge, relative to scroll region top
                float       vis_h       = fmaxf(fmaxf(ps->HotT, ctx->Style.ReducedMotion ? (ctx->ScrollDelta != 0.f ? 1.f : 0.f) : ps->ScrollbarShowTimer / kScrollbarShowDuration), ctx->Style.ScrollbarAutoHideAlpha);

                // Horizontal hit lane and centred visual thumb, no track fill.
                char        hthm[64]    = {};
                snprintf(hthm, sizeof(hthm), "##hsbthm_%llu", (unsigned long long) sb->Key);
                ZUIBox* hthumb      = ZUIPushBox(ctx, hthm, (uint32_t) ZEngine::Helpers::secure_strlen(hthm), ZUI_DrawBackground | ZUI_Clickable | ZUI_FloatX | ZUI_FloatY | ZUI_NoKeyboardFocus);
                hthumb->Size[0]     = ZPx(thumb_w);
                hthumb->Size[1]     = ZPx(kBarH);
                hthumb->FloatPos[0] = thumb_x_rel;
                hthumb->FloatPos[1] = bar_y_h;
                (void) ZUISignalFromBox(ctx, hthumb);
                const bool   active = ctx->ActiveKey == hthumb->Key;
                const bool   hot    = ctx->HotKey == hthumb->Key;
                const float* grab   = active ? ctx->Theme.ScrollbarGrabAct : (hot ? ctx->Theme.ScrollbarGrabHov : ctx->Theme.ScrollbarGrab);
                if (hot || active)
                    vis_h = 1.f;
                vis_h               = fminf(vis_h, 1.f);
                char visual_key[80] = {};
                snprintf(visual_key, sizeof(visual_key), "%s_v", hthm);
                auto*       visual   = ZUIPushBox(ctx, visual_key, (uint32_t) Helpers::secure_strlen(visual_key), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                const float visual_h = fminf(Metrics::Space6, kBarH);
                visual->Size[0]      = ZPx(thumb_w);
                visual->Size[1]      = ZPx(visual_h);
                visual->FloatPos[1]  = (kBarH - visual_h) * 0.5f;
                ZUIBoxSetColor(visual, grab[0], grab[1], grab[2], grab[3] * vis_h);
                ZUIBoxSetCornerRadius(visual, ctx->Style.ScrollbarRounding);
                ZUIPopBox(ctx);

                ZUIPopBox(ctx);
            }
        }
        ZUIPopBox(ctx);
    }

    void ZUIScrollToBottom(ZUIContext* ctx, const char* key)
    {
        uint64_t            hash = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ZUIPersistentState* ps   = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        if (ps)
        {
            // 1e9f is clamped to MaxScrollY by ZUIBeginScrollRegion on the next frame.
            // Both must be set: ScrollY drives this frame's layout, ScrollYTarget drives
            // subsequent frames. Setting only ScrollY lets the lerp animate back to the
            // old target over the next ~15 frames.
            ps->ScrollY       = 1e9f;
            ps->ScrollYTarget = 1e9f;
        }
    }

    float ZUIGetScrollY(ZUIContext* ctx, const char* key)
    {
        uint64_t            hash = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ZUIPersistentState* ps   = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        return ps ? ps->ScrollY : 0.f;
    }

    // ZUILabel

    void ZUILabel(ZUIContext* ctx, const char* text, const float color[4], ZUIFontSize size)
    {
        const float* c   = color ? color : ctx->Theme.TextDefault;
        uint32_t     len = (uint32_t) ZEngine::Helpers::secure_strlen(text);

        ZUIBox*      box = ZUIPushBox(ctx, text, len, ZUI_DrawText);
        box->Size[0]     = ZText();
        box->Size[1]     = ZText();
        box->FontSize    = size;
        SetTextColor(box, c);
        ZUIPopBox(ctx);
    }

    // Disabled-state helpers

    void ZUIBeginDisabled(ZUIContext* ctx)
    {
        ++ctx->DisabledDepth;
        ctx->Disabled = true;
    }
    void ZUIEndDisabled(ZUIContext* ctx)
    {
        if (ctx->DisabledDepth > 0)
            --ctx->DisabledDepth;
        ctx->Disabled = (ctx->DisabledDepth > 0);
    }

    // Shared button state recipes. Legacy Theme.Button* slots stay untouched:
    // asset cards, inspector rows and other non-buttons still use those surfaces.
    static void StyleButton(ZUIContext* ctx, ZUIBox* box, ZUIButtonKind kind, bool active, bool disabled)
    {
        static constexpr float kControl[4]   = ZUI_TOKEN(Tokens::BgControl);
        static constexpr float kHover[4]     = ZUI_TOKEN(Tokens::BgHover);
        static constexpr float kPressed[4]   = ZUI_TOKEN(Tokens::BgSurface);
        static constexpr float kPanel[4]     = ZUI_TOKEN(Tokens::BgPanel);
        static constexpr float kField[4]     = ZUI_TOKEN(Tokens::BorderField);
        static constexpr float kSubtle[4]    = ZUI_TOKEN(Tokens::BorderSubtle);
        static constexpr float kSelection[4] = ZUI_TOKEN(Tokens::AccentSelection);
        static constexpr float kSelHover[4]  = ZUI_TOKEN(Tokens::AccentSelectionHover);
        static constexpr float kSelPress[4]  = ZUI_TOKEN(Tokens::AccentSelectionPressed);
        static constexpr float kBlue[4]      = ZUI_TOKEN(Tokens::AccentBlue);
        static constexpr float kText[4]      = ZUI_TOKEN(Tokens::TextPrimary);
        static constexpr float kDisabled[4]  = ZUI_TOKEN(Tokens::TextDisabled);
        static constexpr float kClear[4]     = {};
        const bool             primary       = kind == ZUIButtonKind::Primary;
        const bool             ghost         = kind == ZUIButtonKind::Ghost;
        box->Flags                           = ZUIBoxFlags((box->Flags | ZUI_DrawBackground) & ~ZUI_DrawBorder);
        box->BorderThickness                 = 0.f;
        box->EdgeSoftness                    = 0.5f;
        ZUIBoxSetCornerRadius(box, Metrics::Radius4);
        SetTextColor(box, disabled ? kDisabled : kText);
        if (disabled)
            SetBgArr(box, ghost ? kClear : (primary ? kControl : kPanel));
        else if (active)
            SetBgArr(box, ghost ? kHover : kSelection);
        else
            ApplyHotActive(box, ctx, primary ? kSelection : (ghost ? kClear : kControl), primary ? kSelHover : kHover, primary ? kSelPress : kPressed);

        if (!ghost && (!primary || (active && !disabled)))
        {
            box->Flags           = box->Flags | ZUI_DrawBorder;
            box->BorderThickness = 1.f;
            SetBdrArr(box, disabled ? kSubtle : (active ? kBlue : kField));
        }
        if (!disabled && ctx->KeyboardFocus && ctx->FocusKey == box->Key)
        {
            box->Flags           = box->Flags | ZUI_DrawBorder;
            box->BorderThickness = 1.f;
            SetBdrArr(box, kBlue);
        }
    }

    static void StyleField(ZUIContext* ctx, ZUIBox* box, bool focused = false, bool dragging = false)
    {
        static constexpr float kDisabledBg[4]     = ZUI_TOKEN(Tokens::BgPanel);
        static constexpr float kDisabledBorder[4] = ZUI_TOKEN(Tokens::BorderSubtle);
        static constexpr float kDisabledText[4]   = ZUI_TOKEN(Tokens::TextDisabled);
        static constexpr float kHoverBorder[4]    = ZUI_TOKEN(Tokens::BorderStrong);
        const bool             disabled           = ctx->Disabled || (box->Flags & ZUI_Disabled);
        SetBgArr(box, disabled ? kDisabledBg : ctx->Theme.InputBg);
        SetTextColor(box, disabled ? kDisabledText : (dragging ? ctx->Theme.InputFocusBorder : ctx->Theme.TextDefault));
        SetBdrArr(box, disabled ? kDisabledBorder : (((focused && ctx->KeyboardFocus) || dragging) ? ctx->Theme.InputFocusBorder : (ctx->HotKey == box->Key ? kHoverBorder : ctx->Theme.InputBorder)));
        box->BorderThickness = 1.f;
        box->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(box, Metrics::Radius3);
    }

    // ZUIButton family

    ZUIBox* ZUILabelTight(ZUIContext* ctx, const char* text, const float color[4], ZUIFontSize size, const char* key, float text_scale)
    {
        const uint32_t len = (uint32_t) ZEngine::Helpers::secure_strlen(text);
        ZUIBox*        box = ZUIPushBox(ctx, key ? key : text, key ? static_cast<uint32_t>(Helpers::secure_strlen(key)) : len, ZUI_DrawText);
        if (key)
            box->Label = ZUIPushStr(&ctx->FrameArena, text, len);
        const ZUIFont* font   = ctx->GetFont(size);
        float          ext[2] = {0.f, 0.f};
        if (font && font->Glyphs && box->Label.Ptr)
            ZUIMeasureText(font, box->Label.Ptr, box->Label.Len, ext);
        box->TextScale = std::isfinite(text_scale) && text_scale > 0.f ? text_scale : 1.f;
        box->Size[0]   = ZPx(ext[0] * box->TextScale);
        box->Size[1]   = ZPx((ext[1] > 0.f ? ext[1] : ctx->Style.FontSize) * box->TextScale);
        box->TextAlign = ZUITextAlign::Center; // exact-width box: centring == no padding
        box->FontSize  = size;
        const float* c = color ? color : ctx->Theme.TextDefault;
        for (int i = 0; i < 4; ++i)
            box->TextColor[i] = c[i];
        ZUIPopBox(ctx);
        return box;
    }

    ZUIBox* ZUILabelEllipsized(ZUIContext* ctx, const char* text, float max_width, const float color[4], ZUIFontSize size, const char* key, float text_scale)
    {
        auto*       box  = ZUILabelTight(ctx, text, color, size, key, text_scale);
        const auto* font = ctx->GetFont(size);
        max_width        = fmaxf(max_width, 0.f);
        if (!font || !font->Glyphs || box->Size[0].Value <= max_width)
            return box;

        // ZUIPushStr has already folded the atlas-supported UTF-8 symbols to
        // their single-byte glyph slots. Measure and copy those exact bytes.
        const char*        suffix     = "...";
        uint32_t           suffix_len = 3;
        constexpr uint32_t ellipsis   = 0x85;
        if (ellipsis >= font->FirstCodepoint && ellipsis - font->FirstCodepoint < font->GlyphCount && font->Glyphs[ellipsis - font->FirstCodepoint].AdvanceX > 0.f)
        {
            suffix     = "\x85";
            suffix_len = 1;
        }
        float suffix_size[2] = {};
        ZUIMeasureText(font, suffix, suffix_len, suffix_size);
        suffix_size[0]        *= box->TextScale;
        uint32_t prefix_len    = 0;
        float    prefix_width  = 0.f;
        if (suffix_size[0] > 0.f && suffix_size[0] <= max_width)
        {
            const float scale = (font->FontScale > 0.f ? font->FontScale : 1.f) * box->TextScale;
            for (; prefix_len < box->Label.Len; ++prefix_len)
            {
                const uint32_t cp      = static_cast<uint8_t>(box->Label.Ptr[prefix_len]);
                const float    advance = cp >= font->FirstCodepoint && cp - font->FirstCodepoint < font->GlyphCount ? font->Glyphs[cp - font->FirstCodepoint].AdvanceX * scale : 0.f;
                if (prefix_width + advance + suffix_size[0] > max_width)
                    break;
                prefix_width += advance;
            }
        }
        else
        {
            suffix_len     = 0;
            suffix_size[0] = 0.f;
        }
        const uint32_t fitted_len = prefix_len + suffix_len;
        if (!prefix_len)
            box->Label = {suffix, suffix_len};
        else
        {
            // ZUILabelTight owns a mutable frame-arena copy. Reuse it rather
            // than allocating/copying the prefix again for every clipped name.
            // Proportional fonts can replace one wide glyph with three dots,
            // so the fallback occasionally needs more bytes than the original.
            const uint32_t capacity = std::max(box->Label.Len, fitted_len) + 1;
            char*          fitted   = const_cast<char*>(box->Label.Ptr);
            if (fitted_len > box->Label.Len)
            {
                fitted = ZPushString(&ctx->FrameArena, capacity);
                Helpers::secure_memcpy(fitted, capacity, box->Label.Ptr, prefix_len);
            }
            Helpers::secure_memcpy(fitted + prefix_len, capacity - prefix_len, suffix, suffix_len);
            fitted[fitted_len] = '\0';
            box->Label         = {fitted, fitted_len};
        }
        box->Size[0] = ZPx(prefix_width + suffix_size[0]);
        return box;
    }

    ZUIBox* ZUIIconBox(ZUIContext* ctx, ZUIIcon icon, float size, const float color[4])
    {
        char    key[24]   = {};
        int     len       = snprintf(key, sizeof(key), "##icon%u", (unsigned) icon);
        ZUIBox* box       = ZUIPushBox(ctx, key, (uint32_t) len, ZUI_DrawIcon);
        box->Size[0]      = ZPx(size);
        box->Size[1]      = ZPx(size);
        box->IconId       = (uint16_t) icon;
        const float* c    = color ? color : ctx->Theme.TextDim;
        box->TextColor[0] = c[0];
        box->TextColor[1] = c[1];
        box->TextColor[2] = c[2];
        box->TextColor[3] = c[3];
        ZUIPopBox(ctx);
        return box;
    }
    ZUISignal ZUIEditorControl(ZUIContext* ctx, const char* key, ZUIIcon icon, const char* label, bool chevron, bool active, const float* icon_color, bool dim, bool primary)
    {
        const bool disabled = ctx->Disabled || dim;
        ZUIBox*    b        = ZUIBeginRow(ctx, key, ZFit(), ZPx(Metrics::HeightControl));
        b->Flags            = b->Flags | ZUI_DrawBackground | ZUI_Clickable;
        b->MinSize[0] = b->MinSize[1] = Metrics::HeightControl;
        if (disabled)
            b->Flags = b->Flags | ZUI_Disabled;
        b->Padding[0] = b->Padding[2] = Metrics::Space8;
        b->Gap                        = 6.f;
        b->CrossAlign                 = ZUIAlign::Center;
        b->EdgeSoftness               = 0.5f;
        ZUIBoxSetCornerRadius(b, 4.f);

        ZUISignal sig = ZUISignalFromBox(ctx, b);
        StyleButton(ctx, b, primary ? ZUIButtonKind::Primary : ZUIButtonKind::Secondary, active, disabled);
        if (!disabled && ctx->FocusKey == b->Key && (ctx->SpacePressed || ctx->EnterPressed))
            sig.Flags |= ZUI_SignalClicked;
        const float  disabled_color[4] = ZUI_TOKEN(Tokens::TextDisabled);
        const float  accent[4]         = ZUI_TOKEN(Tokens::AccentBlue);
        const float* fg                = disabled ? disabled_color : ctx->Theme.TextDefault;
        const float* icon_fg           = disabled ? disabled_color : ((active && !primary) ? accent : (icon_color ? icon_color : fg));
        if (icon != ZUIIcon::Count)
            ZUIIconBox(ctx, icon, 16.f, icon_fg);
        if (label)
            ZUILabelTight(ctx, label, fg, ZUIFontSize::Body);
        if (chevron)
            ZUIIconBox(ctx, ZUIIcon::ChevronDown, 10.f, disabled ? disabled_color : ctx->Theme.TextDim);
        ZUIEndRow(ctx);
        if (label)
            ZUISetTooltip(ctx, sig, label);
        return sig;
    }

    ZUISignal ZUIButton(ZUIContext* ctx, const char* label, ZUISize w, ZUISize h, ZUIButtonKind kind)
    {
        uint32_t    len   = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBoxFlags flags = ZUI_DrawBackground | ZUI_DrawText | ZUI_DrawBorder;
        if (!ctx->Disabled)
            flags = flags | ZUI_Clickable;

        ZUIBox* box     = ZUIPushBox(ctx, label, len, flags);
        box->TextAlign  = ZUITextAlign::Center;
        box->Size[0]    = w;
        box->Size[1]    = h;
        box->Padding[0] = ZUIGetFramePadX(ctx); // ImGui FramePadding.x = 4px per side
        box->Padding[2] = ZUIGetFramePadX(ctx);
        if (w.Kind == ZUISizeKind::Pixels)
        {
            box->Size[0]    = ZText();
            box->MinSize[0] = w.Value;
        }
        ZUISignal sig = ZUISignalFromBox(ctx, box);
        StyleButton(ctx, box, kind, false, ctx->Disabled);
        if (!ctx->Disabled && ctx->FocusKey == box->Key && (ctx->SpacePressed || ctx->EnterPressed))
            sig.Flags |= ZUI_SignalClicked;
        ZUIPopBox(ctx);
        return sig;
    }

    ZUISignal ZUISmallButton(ZUIContext* ctx, const char* label)
    {
        return ZUIButton(ctx, label);
    }

    ZUISignal ZUIInvisibleButton(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        uint32_t    len   = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBoxFlags flags = ctx->Disabled ? ZUI_None : ZUI_Clickable;

        ZUIBox*     box   = ZUIPushBox(ctx, key, len, flags);
        box->Size[0]      = w;
        box->Size[1]      = h;

        ZUISignal sig     = ZUISignalFromBox(ctx, box);
        ZUIPopBox(ctx);
        return sig;
    }

    bool ZUIToggleButton(ZUIContext* ctx, const char* label, bool* active, ZUISize w, ZUISize h)
    {
        uint32_t    len   = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBoxFlags flags = ZUI_DrawBackground | ZUI_DrawText | ZUI_DrawBorder;
        if (!ctx->Disabled)
            flags = flags | ZUI_Clickable;

        ZUIBox* box          = ZUIPushBox(ctx, label, len, flags);
        box->TextAlign       = ZUITextAlign::Center;
        box->Size[0]         = w;
        box->Size[1]         = h;
        box->Padding[0]      = ZUIGetFramePadX(ctx);
        box->Padding[2]      = ZUIGetFramePadX(ctx);
        box->BorderThickness = 1.f;
        box->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(box, Metrics::Radius4);

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        if (!ctx->Disabled && ctx->FocusKey == box->Key && (ctx->SpacePressed || ctx->EnterPressed))
            sig.Flags |= ZUI_SignalClicked;
        const bool clicked = !ctx->Disabled && active && (sig.Flags & ZUI_SignalClicked);
        if (clicked)
            *active = !*active;
        StyleButton(ctx, box, ZUIButtonKind::Secondary, active && *active, ctx->Disabled);
        ZUIPopBox(ctx);
        return clicked;
    }

    // ZUISeparator

    void ZUISeparator(ZUIContext* ctx)
    {
        if (ctx->PopupBuildDepth > 0)
        {
            // Figma 'Menu/Separator': 9 px band, 8 px side padding, 1 px subtle line.
            ZUIBox* band     = ZUIBeginRow(ctx, "##zui_msep", ZFill(), ZPx(9.f));
            band->Padding[0] = band->Padding[2] = 8.f;
            band->CrossAlign                    = ZUIAlign::Center;
            ZUIBox* line                        = ZUIPushBox(ctx, "##zui_msep_l", 12, ZUI_DrawBackground);
            line->Size[0]                       = ZFill();
            line->Size[1]                       = ZPx(1.f);
            line->EdgeSoftness                  = 0.f;
            SetBgArr(line, ctx->Theme.PanelBorder);
            ZUIPopBox(ctx);
            ZUIEndRow(ctx);
            return;
        }
        ZUIBox* box  = ZUIPushBox(ctx, "##zui_sep", 9, ZUI_DrawBackground);
        box->Size[0] = ZFill();
        box->Size[1] = ZSPx(ctx, 2.f);
        SetBgArr(box, ctx->Theme.Separator);
        ZUIPopBox(ctx);
    }

    // ZUISpacer

    void ZUISpacer(ZUIContext* ctx, float px)
    {
        ZUIBox* box  = ZUIPushBox(ctx, "##zui_spacer", 12, ZUI_None);
        // Size on both axes so it works in both row and column parents
        box->Size[0] = ZPx(px);
        box->Size[1] = ZPx(px);
        ZUIPopBox(ctx);
    }

    // ZUITreeNode

    ZUISignal ZUITreeNode(ZUIContext* ctx, const char* label, bool* open)
    {
        // Row box — the entire row is the clickable hit target
        char row_key[256] = {};
        snprintf(row_key, sizeof(row_key), "##tn_%s", label);
        uint32_t row_key_len = (uint32_t) ZEngine::Helpers::secure_strlen(row_key);

        ZUIBox*  row         = ZUIPushBox(ctx, row_key, row_key_len, ZUI_Clickable);
        row->Size[0]         = ZFill();
        row->Size[1]         = ZPx(ZUIGetFrameHeight(ctx));
        row->LayoutAxis      = ZUIAxis::X;

        // Disclosure indicator drawn via ZUI_DrawTriArrow (same as tree view)
        char ind_key[160]    = {};
        snprintf(ind_key, sizeof(ind_key), "##tnarr_%s", row_key);
        ZUIBox* ind      = ZUIPushBox(ctx, ind_key, (uint32_t) ZEngine::Helpers::secure_strlen(ind_key), ZUI_DrawTriArrow);
        ind->Size[0]     = ZPx(ctx->Style.FontSize + 1.f);
        ind->Size[1]     = ZPx(ZUIGetFrameHeight(ctx));
        float ind_col[4] = {0.55f, 0.55f, 0.60f, 1.f};
        SetTextColor(ind, ind_col);
        {
            auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, ind->Key);
            if (ps)
                ps->UserData = (open && *open) ? 2.f : 3.f; // chevron: ∨ expanded, › collapsed
        }
        ZUIPopBox(ctx);

        // Label text
        uint32_t label_len = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBox*  txt       = ZUIPushBox(ctx, label, label_len, ZUI_DrawText);
        txt->Size[0]       = ZText();
        txt->Size[1]       = ZPx(ZUIGetFrameHeight(ctx));
        SetTextColor(txt, ctx->Theme.TextDefault);
        ZUIPopBox(ctx); // pop label

        bool      is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig        = ZUISignalFromBox(ctx, row);
        ZUIPopBox(ctx); // pop row

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = row->Key;
        }

        bool toggle = (sig.Flags & ZUI_SignalClicked) || (is_focused && (ctx->SpacePressed || ctx->EnterPressed));
        if (open)
        {
            if (toggle)
            {
                *open = !(*open);
            }
            // Arrow Right opens, Arrow Left closes
            if (is_focused && ctx->ArrowRightPressed && !(*open))
                *open = true;
            if (is_focused && ctx->ArrowLeftPressed && (*open))
                *open = false;
        }

        return sig;
    }

    // Popup / overlay system

    void ZUIOpenPopup(ZUIContext* ctx, const char* key, float pos_x, float pos_y)
    {
        if (ctx->Disabled || ctx->PopupBuildDepth >= ZUIContext::kMaxPopupDepth)
            return;
        ctx->PendingPopupKey           = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ctx->PendingPopupDepth         = ctx->PopupBuildDepth; // open at the caller's render depth
        ctx->PendingPopupOwnerFocusKey = ctx->KeyboardFocus || !ctx->HotKey ? ctx->FocusKey : ctx->HotKey;
        ctx->PendingPopupPosX          = (pos_x >= 0.f) ? pos_x : ctx->MousePos[0];
        ctx->PendingPopupPosY          = (pos_y >= 0.f) ? pos_y : ctx->MousePos[1];
        if (ctx->KeyboardFocus && ctx->FocusKey && (pos_x < 0.f || pos_y < 0.f))
            if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, ctx->FocusKey))
            {
                if (pos_x < 0.f)
                    ctx->PendingPopupPosX = state->ScreenMinX;
                if (pos_y < 0.f)
                    ctx->PendingPopupPosY = state->ScreenMaxY;
            }
        ctx->PopupNavIdx   = -1;
        ctx->PopupBuildIdx = 0;
    }

    bool ZUIBeginPopup(ZUIContext* ctx, const char* key)
    {
        uint64_t hash  = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        uint32_t depth = ctx->PopupBuildDepth;
        if (ctx->Disabled)
        {
            if (depth < ctx->PopupStackSize && ctx->PopupStack[depth].Key == hash)
                ctx->PopupStackSize = depth;
            if (ctx->PendingPopupKey == hash)
                ctx->PendingPopupKey = 0;
            return false;
        }
        if (depth >= ctx->PopupStackSize || ctx->PopupStack[depth].Key != hash)
            return false;

        // Reset item counter each frame the popup builds
        ctx->PopupBuildIdx = 0;
        ctx->PopupBuildDepth++; // inner content is one level deeper

        // Escape to root so the popup renders on top of everything else.
        // Save current parent per-entry so nested popups don't clobber each other.
        ctx->PopupStack[depth].SavedParent = ctx->Current;
        ctx->Current                       = ctx->Root;

        float    px                        = ctx->PopupStack[depth].PosX;
        float    py                        = ctx->PopupStack[depth].PosY;
        uint32_t len                       = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  popup                     = ZUIPushBox(ctx, key, len, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow | ZUI_ClipChildren | ZUI_FloatX | ZUI_FloatY | ZUI_ClampToScreen);
        popup->Size[0]                     = ZFit();
        popup->Size[1]                     = ZFit();
        popup->FloatPos[0]                 = px;
        popup->FloatPos[1]                 = py;
        popup->LayoutAxis                  = ZUIAxis::Y;
        popup->BorderThickness             = 1.f;
        popup->EdgeSoftness                = 0.5f;
        ZUIBoxSetCornerRadius(popup, ctx->Style.PopupRounding);
        popup->Padding[0] = popup->Padding[2] = ctx->Style.PopupInnerPaddingX;
        popup->Padding[1] = popup->Padding[3] = 4.f; // Figma 'Menu': 4 px inside the border all round
        SetBgArr(popup, ctx->Theme.PopupBg);
        SetBdrArr(popup, ctx->Theme.PopupBorder);

        ctx->PopupStack[depth].Box = popup; // register for hover + close-on-press detection

        // Minimum-width sizer: breaks the ZFit↔ZFill circular dependency.
        // ZFit popup width = max(sizer_width, children widths).
        // ZFill items can then fill this known minimum.  Menu items use 200px,
        // combos pass their button width via ctx->PopupDesiredW.
        {
            float min_w        = (ctx->PopupDesiredW > 0.f) ? ctx->PopupDesiredW : ctx->Style.PopupMinWidth;
            ctx->PopupDesiredW = 0.f; // consume
            char    sk[20]     = "##popup_min_w";
            ZUIBox* sizer      = ZUIPushBox(ctx, sk, 13, ZUI_None);
            sizer->Size[0]     = ZPx(min_w);
            sizer->Size[1]     = ZPx(0.f); // zero height — invisible
            ZUIPopBox(ctx);
        }

        return true;
    }

    void ZUIEndPopup(ZUIContext* ctx)
    {
        ZENGINE_VALIDATE_ASSERT(ctx->PopupBuildDepth > 0, "ZUIEndPopup requires a matching ZUIBeginPopup");
        ctx->PopupBuildCount = ctx->PopupBuildIdx;
        const uint32_t depth = --ctx->PopupBuildDepth;
        ZUIPopBox(ctx);
        // Activation can close the active stack while its builders are still
        // unwinding. Build depth and saved parents must survive that close.
        ctx->Current = ctx->PopupStack[depth].SavedParent;
    }

    void ZUIClosePopup(ZUIContext* ctx)
    {
        ctx->PopupStackSize  = 0; // close all open popups (menu item confirmed)
        ctx->PendingPopupKey = 0;
    }

    bool ZUIBeginPopupContextItem(ZUIContext* ctx, const char* key, const ZUISignal& item_signal)
    {
        // Right-click while hovered → request popup at mouse position
        if ((item_signal.Flags & ZUI_SignalHovered) && ctx->MousePressed[1])
        {
            ZUIOpenPopup(ctx, key);
        }
        return ZUIBeginPopup(ctx, key);
    }

    bool ZUIMenuItem(ZUIContext* ctx, const char* label, bool enabled, const char* key)
    {
        GrowPopupForLabel(ctx, label, 44.f);
        enabled         = enabled && !ctx->Disabled;
        // Figma 'Menu/Item': 26 px, 8 px lead + 16 px icon slot + 8 px gap before the label, 12 px trailing.
        uint32_t    len = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBoxFlags fl  = ZUI_DrawText | ZUI_DrawBackground;
        if (enabled)
            fl = fl | ZUI_Clickable;

        ZUIBox* box = ZUIPushBox(ctx, key ? key : label, key ? static_cast<uint32_t>(Helpers::secure_strlen(key)) : len, fl);
        if (key)
            box->Label = ZUIPushStr(&ctx->FrameArena, label, len);
        box->Size[0]      = ZFill();
        box->Size[1]      = ZPx(26.f);
        box->Padding[0]   = 32.f;
        box->Padding[2]   = 12.f;
        box->EdgeSoftness = 0.5f;
        ZUIBoxSetCornerRadius(box, 4.f);

        // Keyboard focus adds only the shared inside ring, not a selection fill.
        bool kb_focus = enabled && ctx->KeyboardFocus && ctx->FocusKey == box->Key;
        ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);
        const float disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
        SetTextColor(box, enabled ? ctx->Theme.TextDefault : disabled);

        if (enabled)
            ctx->PopupBuildIdx++; // disabled items are invisible to keyboard nav

        ZUISignal sig = ZUISignalFromBox(ctx, box);

        // Hover uses the selection fill, matching Figma's highlighted item.
        if (enabled)
        {
            bool is_hot    = (ctx->HotKey == box->Key);
            bool is_active = (ctx->ActiveKey == box->Key);
            if (is_active || is_hot)
                ZUIBoxSetColorArr(box, ctx->Theme.HeaderActiveBg);
        }
        ZUIPopBox(ctx);

        bool activated = (enabled && (sig.Flags & ZUI_SignalClicked)) || (kb_focus && (ctx->EnterPressed || ctx->SpacePressed));
        if (activated)
        {
            ZUIClosePopup(ctx);
            ctx->PopupNavIdx = -1;
            return true;
        }
        return false;
    }

    bool ZUIMenuItemEx(ZUIContext* ctx, const char* label, const char* shortcut, bool selected, bool enabled, int icon, bool submenu)
    {
        float shortcut_size[2] = {};
        if (shortcut && ctx->GetFont(ZUIFontSize::Small))
            ZUIMeasureText(ctx->GetFont(ZUIFontSize::Small), shortcut, (uint32_t) Helpers::secure_strlen(shortcut), shortcut_size);
        GrowPopupForLabel(ctx, label, 44.f + (shortcut ? shortcut_size[0] + 8.f : 0.f) + (submenu ? 24.f : 0.f));
        enabled           = enabled && !ctx->Disabled;
        char box_key[128] = {};
        snprintf(box_key, sizeof(box_key), "##miex_%s", label);

        ZUIBox* box = ZUIBeginRow(ctx, box_key, ZFill(), ZPx(26.f));
        box->Flags  = ZUIBoxFlags((box->Flags & ~ZUI_DrawText) | ZUI_DrawBackground);
        if (enabled)
            box->Flags = box->Flags | ZUI_Clickable;
        box->EdgeSoftness = 0.5f;
        box->Padding[0]   = 8.f;
        box->Padding[2]   = 12.f;
        box->Gap          = 8.f;
        box->CrossAlign   = ZUIAlign::Center;
        ZUIBoxSetCornerRadius(box, 4.f);

        const float  disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
        const float* fg          = enabled ? ctx->Theme.TextDefault : disabled;
        const float* fg2         = enabled ? ctx->Theme.TextDim : disabled;

        bool         kb_focus    = enabled && ctx->KeyboardFocus && ctx->FocusKey == box->Key;
        ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);

        if (enabled)
            ctx->PopupBuildIdx++;

        // 1. 16 px icon slot (always present so labels align): item icon, else a check mark when selected.
        {
            char ck[128] = {};
            snprintf(ck, sizeof(ck), "##miex_ck_%s", label);
            const bool has_glyph = icon >= 0 || selected;
            ZUIBox*    chk       = ZUIPushBox(ctx, ck, (uint32_t) ZEngine::Helpers::secure_strlen(ck), has_glyph ? ZUI_DrawIcon : ZUI_None);
            chk->Size[0]         = ZPx(16.f);
            chk->Size[1]         = ZPx(16.f);
            if (has_glyph)
            {
                chk->IconId = (uint16_t) (icon >= 0 ? icon : (int) ZUIIcon::Check);
                SetTextColor(chk, fg);
            }
            ZUIPopBox(ctx);
        }

        // 2. Label
        {
            char lk[128] = {};
            snprintf(lk, sizeof(lk), "##miex_lbl_%s", label);
            ZUIBox* lbl          = ZUIPushBox(ctx, lk, (uint32_t) ZEngine::Helpers::secure_strlen(lk), ZUI_DrawText);
            lbl->Size[0]         = ZFill();
            lbl->Size[1]         = ZPx(13.f);
            const char* id_split = strstr(label, "##"); // "text##id" shows only "text"
            lbl->Label           = ZUIPushStr(&ctx->FrameArena, label, (uint32_t) (id_split ? (id_split - label) : ZEngine::Helpers::secure_strlen(label)));
            SetTextColor(lbl, fg);
            ZUIPopBox(ctx);
        }

        // 3. Shortcut (10 px, secondary)
        if (shortcut && shortcut[0])
        {
            char sk[128] = {};
            snprintf(sk, sizeof(sk), "##miex_sc_%s", label);
            ZUIBox* sc   = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawText);
            sc->Size[0]  = ZText();
            sc->Size[1]  = ZPx(12.f);
            sc->FontSize = ZUIFontSize::Small;
            sc->Label    = ZUIPushStr(&ctx->FrameArena, shortcut, (uint32_t) ZEngine::Helpers::secure_strlen(shortcut));
            SetTextColor(sc, fg2);
            ZUIPopBox(ctx);
        }

        // 4. Submenu chevron
        if (submenu)
        {
            ZUIIconBox(ctx, ZUIIcon::ChevronRight, 16.f, fg2);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, box);

        if (enabled)
        {
            bool is_hot    = (ctx->HotKey == box->Key);
            bool is_active = (ctx->ActiveKey == box->Key);
            if (is_active || is_hot)
                ZUIBoxSetColorArr(box, ctx->Theme.HeaderActiveBg);
        }
        ZUIEndRow(ctx);

        // Drag-selection: mouse pressed elsewhere in popup, released over this item
        bool drag_release = ctx->MouseReleased[0] && ctx->HotKey == box->Key && ctx->ActiveKey != 0 && ctx->ActiveKey != box->Key;

        bool activated    = enabled && ((sig.Flags & ZUI_SignalClicked) || drag_release || (kb_focus && (ctx->EnterPressed || ctx->SpacePressed)));
        if (activated)
        {
            ZUIClosePopup(ctx);
            ctx->PopupNavIdx = -1;
            return true;
        }
        return false;
    }

    bool ZUIComboItem(ZUIContext* ctx, const char* label, bool selected)
    {
        GrowPopupForLabel(ctx, label, 32.f);
        uint32_t len    = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBox*  box    = ZUIPushBox(ctx, label, len, ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
        box->Size[0]    = ZFill();
        box->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
        box->Padding[0] = ZUIGetFramePadX(ctx) * 2.f;
        box->Padding[2] = ZUIGetFramePadX(ctx) * 2.f;

        bool kb_focus   = !ctx->Disabled && ctx->KeyboardFocus && ctx->FocusKey == box->Key;
        if (!ctx->Disabled && selected)
            ZUIBoxSetColorArr(box, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColor(box, 0.f, 0.f, 0.f, 0.f);
        const float disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
        SetTextColor(box, ctx->Disabled ? disabled : ctx->Theme.TextDefault);

        if (!ctx->Disabled)
            ctx->PopupBuildIdx++;

        ZUISignal sig = ZUISignalFromBox(ctx, box);
        if (!ctx->Disabled)
        {
            bool is_hot    = (ctx->HotKey == box->Key);
            bool is_active = (ctx->ActiveKey == box->Key);
            if (is_active)
                ZUIBoxSetColorArr(box, ctx->Theme.HeaderActiveBg);
            else if (is_hot)
                ZUIBoxSetColorArr(box, ctx->Theme.HeaderHoveredBg);
            else if (selected)
                ZUIBoxSetColorArr(box, ctx->Theme.RowSelectedBg);
        }
        ZUIPopBox(ctx);

        bool activated = !ctx->Disabled && ((sig.Flags & ZUI_SignalClicked) || (kb_focus && (ctx->EnterPressed || ctx->SpacePressed)));
        if (activated)
        {
            ZUIClosePopup(ctx);
            ctx->PopupNavIdx = -1;
            return true;
        }
        return false;
    }

    // Complex widgets

    void ZUIBeginTabBar(ZUIContext* ctx, const char* key)
    {
        uint64_t hash          = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ctx->TabBarKey         = hash;

        // Read selected index from persistent state (stored in ScrollY)
        ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        ctx->TabBarSelectedIdx = ps ? (int) ps->ScrollY : 0;
        ctx->TabBarCurrentIdx  = 0;

        ZUIBeginColumn(ctx, key, ZFill(), ZFit());

        // Button row
        char row_key[64] = {};
        snprintf(row_key, sizeof(row_key), "##tbr_%s", key);
        ZUIBox* row = ZUIBeginRow(ctx, row_key, ZFill(), ZSPx(ctx, 28.f));
        row->Flags  = row->Flags | ZUI_DrawBackground;
        SetBgArr(row, ctx->Theme.HeaderBg);
        ctx->TabBarRowBox = row;
    }

    bool ZUIBeginTabItem(ZUIContext* ctx, const char* label)
    {
        int  idx          = ctx->TabBarCurrentIdx++;
        bool active       = (idx == ctx->TabBarSelectedIdx);

        // Tab button
        char btn_key[128] = {};
        snprintf(btn_key, sizeof(btn_key), "%s##tbi_%d", label, idx);
        ZUIBoxFlags fl  = ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable;
        ZUIBox*     btn = ZUIPushBox(ctx, btn_key, (uint32_t) ZEngine::Helpers::secure_strlen(btn_key), fl);
        btn->Size[0]    = ZText();
        btn->Size[1]    = ZSPx(ctx, 28.f);
        if (active)
        {
            SetBgArr(btn, ctx->Theme.PanelBg);
            SetTextColor(btn, ctx->Theme.TextDefault);
        }
        else
        {
            ZUIBoxSetColor(btn, 0.f, 0.f, 0.f, 0.f);
            SetTextColor(btn, ctx->Theme.TextDim);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, btn);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->TabBarSelectedIdx = idx;
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, ctx->TabBarKey);
            if (ps)
                ps->ScrollY = (float) idx;
        }

        ctx->TabItemWasSelected = active;
        if (!active)
            return false;

        // Close the button row so content goes below it
        ZUIEndRow(ctx);
        ctx->TabBarRowBox     = nullptr;

        // Push content column
        char content_key[128] = {};
        snprintf(content_key, sizeof(content_key), "##tbc_%s_%d", label, idx);
        ZUIBeginColumn(ctx, content_key, ZFill(), ZFit());
        return true;
    }

    void ZUIEndTabItem(ZUIContext* ctx)
    {
        ZUIEndColumn(ctx); // close content column
    }

    void ZUIEndTabBar(ZUIContext* ctx)
    {
        if (ctx->TabBarRowBox)
        {
            ZUIEndRow(ctx);
        } // close button row if no tab was active
        ZUIEndColumn(ctx); // close outer column
        ctx->TabBarKey    = 0;
        ctx->TabBarRowBox = nullptr;
    }

    ZUIBox* ZUIBeginListBox(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h)
    {
        ZUIBox* frame = ZUIBeginColumn(ctx, key, w, h);
        frame->Flags  = frame->Flags | ZUI_DrawBackground | ZUI_DrawBorder;
        SetBgArr(frame, ctx->Theme.InputBg);
        SetBdrArr(frame, ctx->Theme.InputBorder);
        frame->BorderThickness = 1.f;

        // Content inside a scroll region
        char sr_key[64]        = {};
        snprintf(sr_key, sizeof(sr_key), "##lbsr_%s", key);
        ZUIBeginScrollRegion(ctx, sr_key, ZFill(), ZFill());
        return frame;
    }
    void ZUIEndListBox(ZUIContext* ctx)
    {
        ZUIEndScrollRegion(ctx);
        ZUIEndColumn(ctx);
    }

    bool ZUISliderFloat(ZUIContext* ctx, const char* key, float* value, float v_min, float v_max, ZUISize w, ZUISize h)
    {
        if (!value)
            return false;
        const float range          = v_max - v_min;
        const bool  enabled        = !ctx->Disabled && std::isfinite(range) && range > 0.f && std::isfinite(v_min) && std::isfinite(v_max) && std::isfinite(*value);
        uint32_t    len            = (uint32_t) Helpers::secure_strlen(key);
        // Transparent background suppresses the generic clickable hover overlay.
        ZUIBox*     track          = ZUIPushBox(ctx, key, len, enabled ? ZUI_DrawBackground | ZUI_Clickable : ZUI_None);
        track->Size[0]             = w;
        track->Size[1]             = h;
        track->LayoutAxis          = ZUIAxis::X;
        track->CrossAlign          = ZUIAlign::Center;

        const bool  is_focused     = enabled && ctx->FocusKey == track->Key;
        ZUISignal   sig            = ZUISignalFromBox(ctx, track); // Disabled fields also retire stale input ownership.
        const float previous_value = *value;
        if (enabled)
        {
            const auto* ps    = ZUIStateGetOrInsert(&ctx->StateStore, track->Key);
            const float box_w = ps ? ps->ScreenMaxX - ps->ScreenMinX : 0.f;
            if (box_w > 0.f)
            {
                if (sig.Flags & ZUI_SignalPressed)
                    *value = v_min + std::clamp((ctx->MousePos[0] - ps->ScreenMinX) / box_w, 0.f, 1.f) * range;
                else if (sig.Flags & ZUI_SignalHeld)
                    *value += sig.DragDelta[0] * (range / box_w);
            }
            if (is_focused)
            {
                if (ctx->ArrowRightPressed || ctx->ArrowUpPressed)
                    *value += range / 100.f;
                if (ctx->ArrowLeftPressed || ctx->ArrowDownPressed)
                    *value -= range / 100.f;
                track->Flags           = track->Flags | ZUI_DrawBorder;
                track->BorderThickness = 1.f;
                SetBdrArr(track, ctx->Theme.InputFocusBorder);
            }
            // Keep caller-owned values unchanged until input actually edits them.
            if (*value != previous_value)
                *value = std::clamp(*value, v_min, v_max);
        }
        const float        fraction     = std::isfinite(*value) && range > 0.f && std::isfinite(range) ? std::clamp((*value - v_min) / range, 0.f, 1.f) : 0.f;
        // Figma "Slider": 4 px rail (bg/surface) with an accent/blue fill and a 12 px round thumb.
        static const float kRail[4]     = ZUI_TOKEN(Tokens::BgSurface);
        static const float kBlue[4]     = ZUI_TOKEN(Tokens::AccentBlue);
        static const float kThumb[4]    = ZUI_TOKEN(Tokens::TextPrimary);
        static const float kDisabled[4] = ZUI_TOKEN(Tokens::TextDisabled);

        // Full rail underneath the fill, as in the master (adjacent rounded
        // segments leave notches at their join).
        {
            char rk[64] = {};
            snprintf(rk, sizeof(rk), "##sf_rail_%llu", (unsigned long long) track->Key);
            ZUIBox* rail         = ZUIPushBox(ctx, rk, (uint32_t) Helpers::secure_strlen(rk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            rail->Size[0]        = ZPct(1.f);
            rail->Size[1]        = ZPx(Metrics::Space4);
            rail->FloatAnchor[1] = 0.5f;
            rail->FloatPos[1]    = -Metrics::Space2;
            ZUIBoxSetColorArr(rail, kRail);
            ZUIBoxSetCornerRadius(rail, Metrics::Radius2);
            ZUIPopBox(ctx);
            char fk[64] = {};
            snprintf(fk, sizeof(fk), "##sf_fill_%llu", (unsigned long long) track->Key);
            ZUIBox* fill  = ZUIPushBox(ctx, fk, (uint32_t) ZEngine::Helpers::secure_strlen(fk), ZUI_DrawBackground);
            fill->Size[0] = ZPct(fraction);
            fill->Size[1] = ZPx(Metrics::Space4);
            ZUIBoxSetColorArr(fill, enabled ? kBlue : kDisabled);
            ZUIBoxSetCornerRadius(fill, Metrics::Radius2);
            fill->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);
        }
        // Thumb: rounded rect grip centered on the track
        {
            char tk[64] = {};
            snprintf(tk, sizeof(tk), "##sf_thumb_%llu", (unsigned long long) track->Key);
            ZUIBox* thumb         = ZUIPushBox(ctx, tk, (uint32_t) ZEngine::Helpers::secure_strlen(tk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            // Anchors resolve against this frame's final size, including first
            // appearance and live resizing; no previous-frame width fallback.
            thumb->FloatAnchor[0] = fraction;
            thumb->FloatAnchor[1] = 0.5f;
            thumb->FloatPos[0]    = -Metrics::Space6;
            thumb->FloatPos[1]    = -Metrics::Space6;
            thumb->Size[0]        = ZPx(Metrics::Space12);
            thumb->Size[1]        = ZPx(Metrics::Space12);
            ZUIBoxSetColorArr(thumb, enabled ? kThumb : kDisabled);
            ZUIBoxSetCornerRadius(thumb, Metrics::Radius6);
            if (enabled && (ctx->HotKey == track->Key || ctx->ActiveKey == track->Key))
            {
                thumb->Flags           = thumb->Flags | ZUI_DrawBorder;
                thumb->BorderThickness = 2.f;
                SetBdrArr(thumb, kBlue);
            }
            thumb->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);
        }

        ZUIPopBox(ctx);
        return enabled && *value != previous_value;
    }

    bool ZUIInputInt(ZUIContext* ctx, const char* key, int* value, int v_min, int v_max, ZUISize w)
    {
        if (!value)
            return false;

        // Format as string, use TextField-style box
        char buf[32] = {};
        snprintf(buf, sizeof(buf), "%d", *value);
        uint32_t len      = (uint32_t) ZEngine::Helpers::secure_strlen(key);

        ZUIBox*  field    = ZUIPushBox(ctx, key, len, ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]    = w;
        field->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
        field->Padding[0] = ZUIGetFramePadX(ctx);
        field->Padding[2] = ZUIGetFramePadX(ctx);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness = 1.f;
        field->EdgeSoftness    = 0.5f;
        ZUIBoxSetCornerRadius(field, ctx->Style.FrameRounding);

        bool focused = !ctx->Disabled && (ctx->FocusKey == field->Key);
        if (focused)
        {
            ctx->TextInputActive       = true;
            // Append digits from text input
            static char     s_ibuf[32] = {};
            static uint64_t s_last_key = 0;
            if (s_last_key != field->Key)
            {
                snprintf(s_ibuf, sizeof(s_ibuf), "%d", *value);
                s_last_key = field->Key;
            }
            bool changed = false;
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char c = ctx->TextInput[i];
                if ((c >= '0' && c <= '9') || (c == '-' && ZEngine::Helpers::secure_strlen(s_ibuf) == 0))
                {
                    size_t l = ZEngine::Helpers::secure_strlen(s_ibuf);
                    if (l < sizeof(s_ibuf) - 1)
                    {
                        s_ibuf[l]     = c;
                        s_ibuf[l + 1] = '\0';
                        changed       = true;
                    }
                }
            }
            if (ctx->BackspacePressed)
            {
                size_t l = ZEngine::Helpers::secure_strlen(s_ibuf);
                if (l > 0)
                {
                    s_ibuf[l - 1] = '\0';
                    changed       = true;
                }
            }
            if (changed || focused)
            {
                int parsed = s_ibuf[0] ? atoi(s_ibuf) : 0;
                if (parsed < v_min)
                    parsed = v_min;
                if (parsed > v_max)
                    parsed = v_max;
                *value = parsed;
            }

            char disp[34] = {};
            snprintf(disp, sizeof(disp), "%s|", s_ibuf);
            field->Label = ZUIPushStr(&ctx->FrameArena, disp, (uint32_t) ZEngine::Helpers::secure_strlen(disp));

            if (ctx->KeyboardFocus)
                SetBdrArr(field, ctx->Theme.InputFocusBorder);
        }
        else
        {
            field->Label = ZUIPushStr(&ctx->FrameArena, buf, (uint32_t) ZEngine::Helpers::secure_strlen(buf));
            SetBdrArr(field, ctx->Theme.InputBorder);
        }

        StyleField(ctx, field, focused);
        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = field->Key;
        }
        // Also support drag to change
        if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
        {
            *value += (int) (sig.DragDelta[0] * 0.5f);
            if (*value < v_min)
                *value = v_min;
            if (*value > v_max)
                *value = v_max;
        }

        return focused && ctx->TextInputLen > 0;
    }

    bool ZUIInputTextMultiline(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, ZUISize w, ZUISize h)
    {
        if (!buf || buf_size == 0)
            return false;
        // Outer bordered frame
        char frame_key[64] = {};
        snprintf(frame_key, sizeof(frame_key), "##itmf_%s", key);
        ZUIBox* frame = ZUIBeginColumn(ctx, frame_key, w, h);
        frame->Flags  = frame->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        SetBgArr(frame, ctx->Theme.InputBg);
        SetBdrArr(frame, ctx->Theme.InputBorder);
        frame->BorderThickness = 1.f;

        // Scroll region inside
        char sr_key[64]        = {};
        snprintf(sr_key, sizeof(sr_key), "##itmsr_%s", key);
        ZUIBeginScrollRegion(ctx, sr_key, ZFill(), ZFill());

        // The text as a label for now (full editing in a later pass)
        bool focused = !ctx->Disabled && (ctx->FocusKey == frame->Key);
        StyleField(ctx, frame, focused);
        if (focused)
            ctx->TextInputActive = true;
        char disp[1024] = {};
        if (focused)
            snprintf(disp, sizeof(disp), "%s|", buf);
        else
            snprintf(disp, sizeof(disp), "%s", buf);

        ZUILabel(ctx, disp, frame->TextColor);

        ZUIEndScrollRegion(ctx);

        ZUISignal sig = ZUISignalFromBox(ctx, frame);
        ZUIEndColumn(ctx);

        bool changed = false;
        if ((sig.Flags & ZUI_SignalClicked))
            ctx->FocusKey = frame->Key;
        if (focused)
        {
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char     c = ctx->TextInput[i];
                uint32_t l = (uint32_t) Helpers::secure_strlen(buf);
                if (l + 1 < buf_size)
                {
                    buf[l]     = c;
                    buf[l + 1] = '\0';
                    changed    = true;
                }
            }
            if (ctx->BackspacePressed)
            {
                uint32_t l = (uint32_t) Helpers::secure_strlen(buf);
                if (l > 0)
                {
                    buf[l - 1] = '\0';
                    changed    = true;
                }
            }
        }
        return changed;
    }

    // Layout helpers

    void ZUISameLine(ZUIContext* ctx, float /*spacing*/)
    {
        // Change the current parent's layout axis to X so the NEXT sibling
        // is placed horizontally next to the previous one.
        if (ctx->Current)
            ctx->Current->LayoutAxis = ZUIAxis::X;
    }

    void ZUIBeginTable(ZUIContext* ctx, const char* key, int columns, const float* widths, ZUISize h)
    {
        ctx->TableColumns    = columns;
        ctx->TableCurrentCol = -1;
        ctx->TableRowBox     = nullptr;

        // Allocate per-column widths in FrameArena
        ctx->TableColWidths  = ZPushArray(&ctx->FrameArena, float, columns);
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
        char row_key[40] = {};
        int  child_count = 0;
        if (ctx->Current)
        {
            for (auto* c = ctx->Current->FirstChild; c; c = c->NextSib)
                ++child_count;
        }
        snprintf(row_key, sizeof(row_key), "##trow_%p_%d", (void*) ctx->Current, child_count);
        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFill(), ZFit());
        row->LayoutAxis  = ZUIAxis::X;
        ctx->TableRowBox = row;
    }

    void ZUITableSetColumn(ZUIContext* ctx, int col_index)
    {
        // Close previous cell
        if (ctx->TableCurrentCol >= 0)
            ZUIEndColumn(ctx);

        ctx->TableCurrentCol = col_index;
        bool    has_width    = (col_index < ctx->TableColumns && ctx->TableColWidths && ctx->TableColWidths[col_index] > 0.f);
        float   w            = has_width ? ctx->TableColWidths[col_index] : 0.f;
        ZUISize cell_w       = has_width ? ZSPx(ctx, w) : ZFill(); // 0 = fill remaining

        char    cell_key[40] = {};
        snprintf(cell_key, sizeof(cell_key), "##tcell_%d_%d", col_index, ctx->TableCurrentCol + (int) (uintptr_t) ctx->TableRowBox);
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

    // Simple standalone widgets

    bool ZUICheckbox(ZUIContext* ctx, const char* label, bool* checked, float leading_label_width)
    {
        // Row: [16×16 box] [label]
        char row_key[64] = {};
        snprintf(row_key, sizeof(row_key), "##cb_%s", label);

        ZUIBox* row                = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(ZUIGetFrameHeight(ctx)));
        row->Flags                 = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis            = ZUIAxis::X;
        row->CrossAlign            = ZUIAlign::Center;
        const bool      is_focused = !ctx->Disabled && ctx->FocusKey == row->Key;
        const ZUISignal sig        = ZUISignalFromBox(ctx, row);
        const bool      activated  = !ctx->Disabled && checked && ((sig.Flags & ZUI_SignalClicked) || (is_focused && (ctx->SpacePressed || ctx->EnterPressed)));
        if (activated)
            *checked = !*checked;

        const float disabled_text[4] = ZUI_TOKEN(Tokens::TextDisabled);
        if (leading_label_width > 0.f)
        {
            row->Size[1] = ZPx(16.f);
            ZUIBeginColumn(ctx, "##checkbox_label", ZPx(leading_label_width), ZFit());
            ZUILabelTight(ctx, label, ctx->Disabled ? disabled_text : ctx->Theme.TextDefault);
            ZUIEndColumn(ctx);
            ZUISpacer(ctx, Metrics::Space12);
        }

        // Checkbox hover changes only the unchecked border, never its fill.
        bool is_checked   = checked && *checked;
        char tick_key[32] = {};
        snprintf(tick_key, sizeof(tick_key), "##tick_%s", label);
        ZUIBoxFlags tick_draw = ZUI_DrawBackground | ZUI_DrawBorder;
        if (is_checked && !ctx->Disabled)
            tick_draw = tick_draw | ZUI_DrawCheckmark;
        ZUIBox*            box         = ZUIPushBox(ctx, tick_key, (uint32_t) ZEngine::Helpers::secure_strlen(tick_key), tick_draw);
        // Figma "Checkbox": 16x16, radius 3; Checked = accent/blue fill with a dark (#171717) tick,
        // Unchecked = bg/window with a 1 px border/field (border/strong on hover).
        static const float kCbBlue[4]  = ZUI_TOKEN(Tokens::AccentBlue);
        static const float kCbTick[4]  = ZUI_TOKEN(Tokens::BgWindow);
        static const float kCbHover[4] = ZUI_TOKEN(Tokens::BorderStrong);
        box->Size[0]                   = ZPx(16.f);
        box->Size[1]                   = ZPx(16.f);
        SetBgArr(box, is_checked ? kCbBlue : ctx->Theme.InputBg);
        SetBdrArr(box, is_checked ? kCbBlue : ctx->Theme.InputBorder);
        box->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(box, 3.f);
        SetTextColor(box, is_checked ? kCbTick : ctx->Theme.CheckMark);
        if (ctx->Disabled)
        {
            StyleField(ctx, box);
        }
        else if (!is_checked && ctx->HotKey == row->Key)
            SetBdrArr(box, kCbHover);
        ZUIPopBox(ctx);

        if (leading_label_width <= 0.f)
        {
            ZUISpacer(ctx, Metrics::Space8);
            ZUILabelTight(ctx, label, ctx->Disabled ? disabled_text : ctx->Theme.TextDefault);
        }

        ZUIEndRow(ctx);
        return activated;
    }

    void ZUIActiveTabIndicator(ZUIContext* ctx, const char* key, float tab_width)
    {
        static const float kBlue[4] = ZUI_TOKEN(Tokens::AccentBlue);
        ZUIBox*            bar      = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        bar->FloatPos[0]            = 0.f; // parent-relative
        bar->FloatPos[1]            = 0.f;
        bar->Size[0]                = ZPx(tab_width);
        bar->Size[1]                = ZPx(2.f);
        ZUIBoxSetColorArr(bar, kBlue);
        bar->EdgeSoftness = 0.f;
        ZUIPopBox(ctx);
    }

    bool ZUIToggle(ZUIContext* ctx, const char* key, bool* on)
    {
        static const float kOn[4]            = ZUI_TOKEN(Tokens::AccentBlue);
        static const float kOff[4]           = ZUI_TOKEN(Tokens::BgControl);
        static const float kHover[4]         = ZUI_TOKEN(Tokens::BorderStrong);
        static const float kField[4]         = ZUI_TOKEN(Tokens::BorderField);
        static const float kThumb[4]         = ZUI_TOKEN(Tokens::TextPrimary);
        static const float kDisabledOn[4]    = ZUI_TOKEN(Tokens::BgHover);
        static const float kDisabledThumb[4] = ZUI_TOKEN(Tokens::TextDisabled);

        ZUIBox*            pill              = ZUIBeginRow(ctx, key, ZPx(28.f), ZPx(16.f));
        pill->Flags                          = pill->Flags | ZUI_DrawBackground | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        const ZUISignal sig                  = ZUISignalFromBox(ctx, pill);
        const bool      focused              = !ctx->Disabled && ctx->FocusKey == pill->Key;
        const bool      activated            = !ctx->Disabled && on && ((sig.Flags & ZUI_SignalClicked) || (focused && (ctx->SpacePressed || ctx->EnterPressed)));
        if (activated)
            *on = !*on;
        const bool is_on   = on && *on;
        float      thumb_t = is_on ? 1.f : 0.f;
        if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, pill->Key))
        {
            if (state->ToggleT < 0.f || ctx->Style.ReducedMotion || ctx->Disabled)
                state->ToggleT = thumb_t;
            else
                state->ToggleT += (thumb_t - state->ToggleT) * (1.f - expf(-ctx->Style.HoverAnimSpeed * fmaxf(ctx->DeltaTime, 0.f)));
            thumb_t = state->ToggleT;
        }
        pill->CrossAlign = ZUIAlign::Center;
        pill->MainAlign  = is_on ? ZUIAlign::End : ZUIAlign::Start;
        pill->Padding[0] = pill->Padding[2] = 2.f;
        pill->EdgeSoftness                  = 0.5f;
        ZUIBoxSetCornerRadius(pill, 8.f);
        const bool hot = !ctx->Disabled && ctx->HotKey == pill->Key;
        if (is_on)
            ZUIBoxSetColorArr(pill, ctx->Disabled ? kDisabledOn : kOn);
        else
        {
            ZUIBoxSetColorArr(pill, kOff);
            pill->Flags = pill->Flags | ZUI_DrawBorder;
            ZEngine::Helpers::secure_memcpy(pill->BorderColor, sizeof(pill->BorderColor), hot ? kHover : kField, sizeof(pill->BorderColor));
            pill->BorderThickness = 1.f;
        }
        ZUIBox* thumb       = ZUIPushBox(ctx, "##tg_thumb", 10, ZUI_DrawBackground | ZUI_FloatX);
        thumb->FloatPos[0]  = 2.f + 12.f * thumb_t;
        thumb->Size[0]      = ZPx(12.f);
        thumb->Size[1]      = ZPx(12.f);
        thumb->EdgeSoftness = 0.5f;
        ZUIBoxSetColorArr(thumb, ctx->Disabled ? kDisabledThumb : kThumb);
        ZUIBoxSetCornerRadius(thumb, 6.f);
        ZUIPopBox(ctx);
        ZUIEndRow(ctx);
        return activated;
    }

    bool ZUIRadioButton(ZUIContext* ctx, const char* label, int* selected, int index)
    {
        char row_key[64] = {};
        snprintf(row_key, sizeof(row_key), "##rb_%s_%d", label, index);

        ZUIBox* row      = ZUIBeginRow(ctx, row_key, ZFit(), ZPx(ZUIGetFrameHeight(ctx)));
        row->Flags       = row->Flags | (ctx->Disabled ? ZUI_None : ZUI_Clickable);
        row->LayoutAxis  = ZUIAxis::X;

        bool is_active   = selected && (*selected == index);
        char dot_key[64] = {};
        snprintf(dot_key, sizeof(dot_key), "##dot_%s_%d", label, index);
        ZUIBoxFlags dot_fl = ZUI_DrawBackground | ZUI_DrawBorder;
        if (is_active)
            dot_fl = dot_fl | ZUI_DrawCircleFill;
        ZUIBox* circle  = ZUIPushBox(ctx, dot_key, (uint32_t) ZEngine::Helpers::secure_strlen(dot_key), dot_fl);
        circle->Size[0] = ZPx(ctx->Style.FontSize);
        circle->Size[1] = ZPx(ctx->Style.FontSize); // ImGui: FontSize
        SetBgArr(circle, is_active ? ctx->Theme.InputActiveBg : ctx->Theme.InputBg);
        SetBdrArr(circle, is_active ? ctx->Theme.InputFocusBorder : ctx->Theme.InputBorder);
        circle->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(circle, ctx->Style.FontSize * 0.5f); // full circle
        SetTextColor(circle, ctx->Theme.CheckMark);
        ZUISignalFromBox(ctx, circle);
        if (ctx->Disabled)
        {
            StyleField(ctx, circle);
            ZUIBoxSetCornerRadius(circle, ctx->Style.FontSize * 0.5f);
        }
        else
            ApplyHotActive(circle, ctx, ctx->Theme.InputBg, ctx->Theme.InputHoveredBg, ctx->Theme.InputActiveBg);
        ZUIPopBox(ctx);

        ZUISpacer(ctx, ZUIGetInnerSpac(ctx)); // ImGui ItemInnerSpacing.x
        const float disabled_text[4] = ZUI_TOKEN(Tokens::TextDisabled);
        ZUILabel(ctx, label, ctx->Disabled ? disabled_text : ctx->Theme.TextDefault);

        bool      is_focused = !ctx->Disabled && (ctx->FocusKey == row->Key);
        ZUISignal sig        = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        bool activated = ((sig.Flags & ZUI_SignalClicked) || (is_focused && ctx->SpacePressed)) && selected && !ctx->Disabled;
        if (activated)
        {
            *selected = index;
            return true;
        }
        return false;
    }

    void ZUIProgressBar(ZUIContext* ctx, const char* key, float fraction, ZUISize w, ZUISize h, const char* overlay_text)
    {
        fraction                   = std::isfinite(fraction) ? std::clamp(fraction, 0.f, 1.f) : 0.f;

        // Track
        ZUIBox* track              = ZUIBeginRow(ctx, key, w, h);
        track->Flags               = track->Flags | ZUI_DrawBackground | ZUI_ClipChildren;
        const float track_color[4] = ZUI_TOKEN(Tokens::BgSurface);
        SetBgArr(track, track_color);
        ZUIBoxSetCornerRadius(track, Metrics::Radius3);
        track->LayoutAxis = ZUIAxis::X;

        // Fill bar
        char fill_key[64] = {};
        snprintf(fill_key, sizeof(fill_key), "##fill_%s", key);
        ZUIBox* fill  = ZUIPushBox(ctx, fill_key, (uint32_t) ZEngine::Helpers::secure_strlen(fill_key), ZUI_DrawBackground | (overlay_text ? ZUI_DrawText : ZUI_None));
        fill->Size[0] = ZPct(fraction);
        fill->Size[1] = ZFill();
        {
            const float _c[4] = ZUI_TOKEN(Tokens::AccentBlue);
            ZUIBoxSetColorArr(fill, _c);
        }
        ZUIBoxSetCornerRadius(fill, Metrics::Radius3);
        if (overlay_text)
        {
            fill->Label = ZUIPushStr(&ctx->FrameArena, overlay_text, (uint32_t) Helpers::secure_strlen(overlay_text));
            SetTextColor(fill, ctx->Theme.TextDefault);
        }
        ZUIPopBox(ctx);

        ZUIEndRow(ctx);
    }

    void ZUISetTooltip(ZUIContext* ctx, const ZUISignal& sig, const char* text)
    {
        const bool focused = sig.Key && ctx->KeyboardFocus && ctx->FocusKey == sig.Key;
        bool       hovered = (sig.Flags & ZUI_SignalHovered) && sig.Key == ctx->TooltipHoverKey && ctx->TooltipHoverTime >= 0.4f && !ctx->MouseDown[0];
        if (hovered)
            if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, sig.Key))
                hovered = ctx->MousePos[0] >= state->ScreenMinX && ctx->MousePos[0] <= state->ScreenMaxX && ctx->MousePos[1] >= state->ScreenMinY && ctx->MousePos[1] <= state->ScreenMaxY;
        if ((!focused && !hovered) || !text || !text[0])
        {
            return;
        }

        float tx = ctx->MousePos[0] + 12.f;
        float ty = ctx->MousePos[1] + 12.f;
        if (focused)
            if (auto* state = ZUIStateGetOrInsert(&ctx->StateStore, sig.Key))
            {
                tx = state->ScreenMinX + 12.f;
                ty = state->ScreenMaxY + 12.f;
            }

        ZUIBox* saved     = ZUIBeginForeground(ctx);

        char    ttkey[64] = {};
        snprintf(ttkey, sizeof(ttkey), "##tt_%llx", (unsigned long long) sig.Key);
        ZUIBox* tip      = ZUIPushBox(ctx, ttkey, (uint32_t) ZEngine::Helpers::secure_strlen(ttkey), ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_FloatX | ZUI_FloatY | ZUI_Tooltip);
        tip->Size[0]     = ZText();
        tip->Size[1]     = ZText();
        tip->FloatPos[0] = tx;
        tip->FloatPos[1] = ty;
        tip->Label       = ZUIPushStr(&ctx->FrameArena, text, (uint32_t) Helpers::secure_strlen(text));
        tip->Padding[0] = tip->Padding[2] = Metrics::Space8;
        tip->Padding[1] = tip->Padding[3] = Metrics::Space6;
        ZUIBoxSetCornerRadius(tip, Metrics::Radius4);
        ZUIBoxSetColorArr(tip, ctx->Theme.HeaderBg);
        static constexpr float kBorder[4] = ZUI_TOKEN(Tokens::BorderStrong);
        SetBdrArr(tip, kBorder);
        tip->BorderThickness = 1.f;
        SetTextColor(tip, ctx->Theme.TextDefault);
        ZUIPopBox(ctx);

        ZUIEndForeground(ctx, saved);
    }

    ZUISignal ZUICollapsingHeader(ZUIContext* ctx, const char* label, bool* open, const float* bg_color, bool show_focus_border)
    {
        char key[256] = {};
        snprintf(key, sizeof(key), "##ch_%s", label);

        ZUIBox* hdr                 = ZUIBeginRow(ctx, key, ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
        hdr->Flags                  = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->Padding[0]             = ZUIGetFramePadX(ctx);
        static const float kNone[4] = {};
        ZUIBoxSetColorArr(hdr, bg_color ? bg_color : kNone);
        hdr->LayoutAxis   = ZUIAxis::X;
        hdr->CrossAlign   = ZUIAlign::Center;
        hdr->EdgeSoftness = 0.f;

        bool is_open      = open && *open;
        bool is_focused   = (ctx->FocusKey == hdr->Key);

        // Use the same Lucide disclosure glyphs as Details and Outliner.
        char ak[272]      = {};
        snprintf(ak, sizeof(ak), "##ch_arr_%s", label);
        ZUIBox* arrow  = ZUIPushBox(ctx, ak, (uint32_t) ZEngine::Helpers::secure_strlen(ak), ZUI_DrawIcon);
        arrow->Size[0] = arrow->Size[1] = ZPx(11.f);
        arrow->IconId                   = (uint16_t) (is_open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight);
        SetTextColor(arrow, ctx->Theme.TextDim);
        ZUIPopBox(ctx);

        ZUISpacer(ctx, ZUIGetInnerSpac(ctx));
        ZUILabel(ctx, label, ctx->Theme.TextDefault, ZUIFontSize::Heading);

        ZUISignal sig = ZUISignalFromBox(ctx, hdr);
        ApplyHotActive(hdr, ctx, bg_color ? bg_color : kNone, ctx->Theme.TitleBgActive, ctx->Theme.TitleBgActive);

        // 1 px blue focus border — only when caller opts in (show_focus_border = true)
        if (!ctx->Disabled && is_focused && ctx->KeyboardFocus && show_focus_border)
        {
            hdr->Flags           = hdr->Flags | ZUI_DrawBorder;
            hdr->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
            hdr->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
            hdr->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
            hdr->BorderColor[3]  = 1.f;
            hdr->BorderThickness = 1.f;
        }

        ZUIEndRow(ctx);

        bool activated = !ctx->Disabled && ((sig.Flags & ZUI_SignalClicked) || (is_focused && (ctx->SpacePressed || ctx->EnterPressed)));
        if (activated)
        {
            ctx->FocusKey     = hdr->Key;
            ctx->SpacePressed = ctx->EnterPressed = false;
            if (open)
            {
                *open         = !(*open);
                arrow->IconId = (uint16_t) (*open ? ZUIIcon::ChevronDown : ZUIIcon::ChevronRight);
            }
        }
        return sig;
    }

    // Greedy cascade for ZUIPaneSash.
    // Walks up-side [boundary..0] absorbing delta, then down-side [boundary+1..n-1]
    // absorbing the remainder (with opposite sign).
    static void PaneSashResize(float* heights, const bool* opens, int n, int boundary, float delta, float min_h)
    {
        if (fabsf(delta) < 0.05f)
            return;

        // Pass 1: apply +delta upward from boundary
        float rem = delta;
        for (int i = boundary; i >= 0 && fabsf(rem) > 0.05f; --i)
        {
            if (!opens[i])
                continue;
            if (rem > 0.f)
            {
                heights[i] += rem; // grow freely (no hard max)
                rem         = 0.f;
            }
            else
            {
                float give  = fminf(-rem, heights[i] - min_h);
                heights[i] -= give;
                rem        += give;
            }
        }

        // Pass 2: compensate downward — down side absorbs -(total absorbed by up)
        float compensate = -(delta - rem);
        for (int i = boundary + 1; i < n && fabsf(compensate) > 0.05f; ++i)
        {
            if (!opens[i])
                continue;
            if (compensate < 0.f)
            {
                // down side must shrink
                float give  = fminf(-compensate, heights[i] - min_h);
                heights[i] -= give;
                compensate += give;
            }
            else
            {
                // down side grows
                heights[i] += compensate;
                compensate  = 0.f;
            }
        }
    }

    void ZUIPaneSash(ZUIContext* ctx, const char* key, float* heights, const bool* opens, int n, int boundary, float min_h)
    {
        // Hit zone: 4 px transparent clickable strip — wide enough to grab easily.
        // Visual:   1 px line centered inside the hit zone, mirroring BuildDividers architecture.
        // At rest: Separator color. On hover: blue tint + NS cursor.
        ZUIBox* sash        = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_Clickable | ZUI_NoKeyboardFocus);
        sash->MinHitSize[0] = sash->MinHitSize[1] = 8.f;
        sash->Size[0]                             = ZFill();
        sash->Size[1]                             = ZPx(4.f);
        sash->EdgeSoftness                        = 0.f;

        bool hot                                  = (ctx->HotKey == sash->Key) || (ctx->ActiveKey == sash->Key);
        if (hot)
            ctx->ResizeCursor = 2; // NS cursor

        // 1 px visual line centered vertically in the 4 px hit zone
        {
            char vk[264] = {};
            snprintf(vk, sizeof(vk), "##sv_%s", key);
            ZUIBox* vis       = ZUIPushBox(ctx, vk, (uint32_t) ZEngine::Helpers::secure_strlen(vk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
            vis->Size[0]      = ZFill();
            vis->Size[1]      = ZPx(1.f);
            vis->FloatPos[0]  = 0.f;
            vis->FloatPos[1]  = 1.5f; // 1.5 px from sash top → centered in 4 px
            vis->EdgeSoftness = 0.f;
            if (hot)
            {
                ZUIBoxSetColor(vis, ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], ctx->ActiveKey == sash->Key ? 0.50f : 0.35f);
            }
            else
            {
                ZUIBoxSetColor(vis, ctx->Theme.Separator[0], ctx->Theme.Separator[1], ctx->Theme.Separator[2], ctx->Theme.Separator[3]);
            }
            ZUIPopBox(ctx);
        }

        ZUISignal sig = ZUISignalFromBox(ctx, sash);
        ZUIPopBox(ctx);

        if ((sig.Flags & ZUI_SignalHeld) && fabsf(sig.DragDelta[1]) > 0.05f)
            PaneSashResize(heights, opens, n, boundary, sig.DragDelta[1], min_h);
    }

    void ZUIDropZoneFill(ZUIContext* ctx, const char* key, float float_x, float float_y, float w, float h)
    {
        const float* ac    = ctx->Theme.TabActiveBorder;
        ZUIBox*      prev  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
        prev->Size[0]      = ZPx(w);
        prev->Size[1]      = ZPx(h);
        prev->FloatPos[0]  = float_x;
        prev->FloatPos[1]  = float_y;
        prev->EdgeSoftness = 0.f;
        ZUIBoxSetColor(prev, ac[0], ac[1], ac[2], ctx->Style.DockingDropPreviewAlpha);
        prev->BorderColor[0]  = ac[0];
        prev->BorderColor[1]  = ac[1];
        prev->BorderColor[2]  = ac[2];
        prev->BorderColor[3]  = ac[3];
        prev->BorderThickness = 2.f;
        ZUIPopBox(ctx);
    }

    void ZUIDockDividerH(ZUIContext* ctx, const char* key)
    {
        const float* ac = ctx->Theme.TabActiveBorder;
        ZUIBox*      d  = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground);
        d->Size[0]      = ZFill();
        d->Size[1]      = ZPx(2.f);
        d->EdgeSoftness = 0.f;
        ZUIBoxSetColor(d, ac[0], ac[1], ac[2], 1.f);
        ZUIPopBox(ctx);
    }

    void ZUIDockGhostHeader(ZUIContext* ctx, const char* key, const char* label, float cursor_x, float cursor_y, int icon, bool closeable)
    {
        auto*       saved_parent = ZUIBeginForeground(ctx);
        const float gh           = ctx->Style.TabBarHeight;
        const float cnt_h        = ctx->Style.TabGhostContentH;

        float       text_w       = 0.f;
        if (ctx->GetFont(ZUIFontSize::Body) && label)
        {
            float ts[2] = {0.f, 0.f};
            ZUIMeasureText(ctx->GetFont(ZUIFontSize::Body), label, (uint32_t) ZEngine::Helpers::secure_strlen(label), ts);
            text_w = ts[0];
        }
        // Active Tab anatomy: 8 / 10 padding, icon 13, gap 8, label, gap 8, close 10.
        const float  gw     = Metrics::Space8 + Metrics::Space10 + text_w + Metrics::Space8 + 10.f + (icon >= 0 ? 13.f + Metrics::Space8 : 0.f);
        float        px     = cursor_x - gw * 0.3f;
        float        py     = cursor_y - gh * 0.5f;
        const float* ac     = ctx->Theme.TabActiveBorder;

        auto*        ghost  = ZUIBeginColumn(ctx, key, ZPx(gw), ZPx(gh + cnt_h));
        ghost->Flags        = ghost->Flags | ZUI_DrawBackground | ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY;
        ghost->FloatPos[0]  = px;
        ghost->FloatPos[1]  = py;
        ghost->ShadowKind   = ZUIShadowKind::Menu;
        ghost->EdgeSoftness = 0.f;
        SetBgArr(ghost, ctx->Theme.PanelBg);

        // Header row
        char hk[272] = {};
        snprintf(hk, sizeof(hk), "##%s_h", key);
        ZUIBox* header     = ZUIBeginRow(ctx, hk, ZFill(), ZPx(gh));
        header->Padding[0] = Metrics::Space8;
        header->Padding[2] = Metrics::Space10;
        header->Gap        = Metrics::Space8;
        header->CrossAlign = ZUIAlign::Center;
        ZUIActiveTabIndicator(ctx, "##ghost_active", gw);
        if (icon >= 0 && icon < (int) ZUIIcon::Count)
            ZUIIconBox(ctx, (ZUIIcon) icon, 13.f, ctx->Theme.TextDefault);
        ZUILabelTight(ctx, label ? label : "", ctx->Theme.TextDefault);
        if (closeable)
            ZUIIconBox(ctx, ZUIIcon::X, 10.f, ctx->Theme.TextDim);
        else
            ZUISpacer(ctx, 10.f);
        ZUIEndRow(ctx);

        // One outline around both header and stub, painted last so children cannot
        // cover its inside stroke. No seam between two independently bordered boxes.
        auto* outline            = ZUIPushBox(ctx, "##ghost_outline", 15, ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
        outline->Size[0]         = ZPct(1.f);
        outline->Size[1]         = ZPct(1.f);
        outline->BorderThickness = 1.f;
        outline->EdgeSoftness    = 0.f;
        SetBdrArr(outline, ac);
        ZUIPopBox(ctx);
        ZUIEndColumn(ctx);
        ZUIEndForeground(ctx, saved_parent);
    }

    void ZUIDragCursor(ZUIContext* ctx)
    {
        auto*                  saved_parent = ZUIBeginForeground(ctx);
        static constexpr float kSize        = 28.f;
        static constexpr float kFill[4]     = ZUI_TOKEN(Tokens::AccentSelection);
        static constexpr float kBorder[4]   = ZUI_TOKEN(Tokens::AccentBlue);
        constexpr char         key[]        = "##file_drag_ghost";
        auto*                  chip         = ZUIPushBox(ctx, key, sizeof(key) - 1, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_FloatX | ZUI_FloatY);
        chip->Size[0] = chip->Size[1] = ZPx(kSize);
        chip->FloatPos[0]             = ctx->MousePos[0] - kSize * 0.5f;
        chip->FloatPos[1]             = ctx->MousePos[1] - kSize * 0.5f;
        chip->BorderThickness         = 1.f;
        chip->EdgeSoftness            = 0.5f;
        ZUIBoxSetCornerRadius(chip, Metrics::Radius4);
        SetBgArr(chip, kFill);
        SetBdrArr(chip, kBorder);
        ZUIPopBox(ctx);
        // The chip sits below any named drag label, regardless of which panel
        // built that label before the shell emitted the shared cursor feedback.
        auto* foreground = ctx->ForegroundRoot;
        if (chip->PrevSib)
        {
            chip->PrevSib->NextSib          = nullptr;
            foreground->LastChild           = chip->PrevSib;
            chip->PrevSib                   = nullptr;
            chip->NextSib                   = foreground->FirstChild;
            foreground->FirstChild->PrevSib = chip;
            foreground->FirstChild          = chip;
        }
        ZUIEndForeground(ctx, saved_parent);
    }

    void ZUIDragLabel(ZUIContext* ctx, const char* label)
    {
        auto*                  saved_parent = ZUIBeginForeground(ctx);
        static constexpr float kFill[4]     = ZUI_TOKEN(Tokens::BgSurface);
        static constexpr float kBorder[4]   = ZUI_TOKEN(Tokens::BorderStrong);
        static constexpr float kText[4]     = ZUI_TOKEN(Tokens::TextPrimary);
        auto*                  tip          = ZUIPushBox(ctx, "##drag_tip", 10, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DrawText | ZUI_FloatX | ZUI_FloatY);
        tip->Size[0] = tip->Size[1] = ZText();
        tip->FloatPos[0]            = ctx->MousePos[0] + 12.f;
        tip->FloatPos[1]            = ctx->MousePos[1] + 12.f;
        tip->Padding[0] = tip->Padding[2] = Metrics::Space8;
        tip->Padding[1] = tip->Padding[3] = Metrics::Space6;
        tip->Label                        = ZUIPushStr(&ctx->FrameArena, label, (uint32_t) Helpers::secure_strlen(label));
        tip->FontSize                     = ZUIFontSize::Body;
        tip->BorderThickness              = 1.f;
        tip->EdgeSoftness                 = 0.5f;
        ZUIBoxSetCornerRadius(tip, Metrics::Radius4);
        SetBgArr(tip, kFill);
        SetBdrArr(tip, kBorder);
        SetTextColor(tip, kText);
        ZUIPopBox(ctx);
        ZUIEndForeground(ctx, saved_parent);
    }

    bool ZUISelectable(ZUIContext* ctx, const char* label, bool* selected, ZUISize h)
    {
        char key[256] = {};
        snprintf(key, sizeof(key), "##sel_%s", label);

        ZUIBox* row                        = ZUIBeginRow(ctx, key, ZFill(), h);
        row->Flags                         = row->Flags | ZUI_DrawBackground | ZUI_Clickable;

        // Use RowSelectedBg for selected, transparent for rest — hover lerps via HotT
        static const float kTransparent[4] = {0.f, 0.f, 0.f, 0.f};
        if (selected && *selected)
            ZUIBoxSetColorArr(row, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColorArr(row, kTransparent);

        ZUISpacer(ctx, 6.f);
        ZUILabel(ctx, label, ctx->Theme.TextDefault);

        bool      is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig        = ZUISignalFromBox(ctx, row);
        // Lerp transparent → RowHoverBg → RowSelectedBg
        if (!(selected && *selected))
            ApplyHotActive(row, ctx, kTransparent, ctx->Theme.HeaderHoveredBg, ctx->Theme.HeaderActiveBg);
        ZUIEndRow(ctx);

        bool activated = (sig.Flags & ZUI_SignalClicked) || (is_focused && (ctx->SpacePressed || ctx->EnterPressed));
        if (activated && selected)
        {
            *selected = !(*selected);
            return true;
        }
        return activated; // return true even when selected == nullptr (e.g. ZUIMenuItem)
    }

    void ZUISeparatorText(ZUIContext* ctx, const char* text)
    {
        ZUIBeginRow(ctx, "##septext", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
        ZUIBox* line1  = ZUIPushBox(ctx, "##sl1", 5, ZUI_DrawBackground);
        line1->Size[0] = ZPx(8.f);
        line1->Size[1] = ZPx(1.f);
        SetBgArr(line1, ctx->Theme.Separator);
        ZUIPopBox(ctx);

        ZUISpacer(ctx, 4.f);
        ZUILabel(ctx, text, ctx->Theme.TextDim);
        ZUISpacer(ctx, 4.f);

        ZUIBox* line2  = ZUIPushBox(ctx, "##sl2", 5, ZUI_DrawBackground);
        line2->Size[0] = ZFill();
        line2->Size[1] = ZPx(1.f);
        SetBgArr(line2, ctx->Theme.Separator);
        ZUIPopBox(ctx);
        ZUIEndRow(ctx);
    }

    // Popup-based widgets

    bool ZUIBeginContextMenu(ZUIContext* ctx, const char* key)
    {
        // Opens on right-click anywhere (not on a specific item)
        if (ctx->MousePressed[1])
            ZUIOpenPopup(ctx, key);
        return ZUIBeginPopup(ctx, key);
    }
    void ZUIEndContextMenu(ZUIContext* ctx)
    {
        ZUIEndPopup(ctx);
    }

    bool ZUIBeginCombo(ZUIContext* ctx, const char* key, const char* preview_label, ZUISize w)
    {
        char btn_key[80] = {};
        snprintf(btn_key, sizeof(btn_key), "##combo_btn_%s", key);

        // Preview row: label fills available space, arrow is a fixed-width flow child at the end.
        ZUIBox* row     = ZUIBeginRow(ctx, btn_key, w, ZPx(ZUIGetFrameHeight(ctx)));
        row->Flags      = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable;
        row->Padding[0] = row->Padding[2]   = Metrics::Space8;
        row->Gap                            = Metrics::Space7;
        row->CrossAlign                     = ZUIAlign::Center;
        static constexpr float kDisabled[4] = ZUI_TOKEN(Tokens::TextDisabled);

        // Preview label: ZFill so it expands and pushes the arrow to the right
        {
            char lk[96] = {};
            snprintf(lk, sizeof(lk), "##cbl_%s", key);
            ZUIBox* lbl  = ZUIPushBox(ctx, lk, (uint32_t) ZEngine::Helpers::secure_strlen(lk), ZUI_DrawText);
            lbl->Size[0] = ZFill();
            lbl->Size[1] = ZFill();
            lbl->Label   = ZUIPushStr(&ctx->FrameArena, preview_label ? preview_label : "", preview_label ? (uint32_t) ZEngine::Helpers::secure_strlen(preview_label) : 0u);
            SetTextColor(lbl, ctx->Disabled ? kDisabled : ctx->Theme.TextDefault);
            ZUIPopBox(ctx);
        }

        // Dropdown arrow — flow child, always at the right edge
        ZUIIconBox(ctx, ZUIIcon::ChevronDown, 10.f, ctx->Disabled ? kDisabled : ctx->Theme.TextDim);

        bool      is_focused = !ctx->Disabled && (ctx->FocusKey == row->Key);

        ZUISignal sig        = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = row->Key;
        }

        // Open popup on click OR Space/Enter/ArrowDown when focused — but only if
        // not already open. ArrowDown while open is handled by ZUIBeginFrame popup nav;
        // re-calling ZUIOpenPopup every frame resets the popup position and causes flicker.
        uint64_t popup_hash   = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        bool     already_open = (ctx->PopupBuildDepth < ctx->PopupStackSize && ctx->PopupStack[ctx->PopupBuildDepth].Key == popup_hash);
        StyleField(ctx, row, is_focused || already_open);
        if (ctx->Disabled)
        {
            if (already_open)
                ctx->PopupStackSize = ctx->PopupBuildDepth;
            if (ctx->PendingPopupKey == popup_hash)
                ctx->PendingPopupKey = 0;
            return false;
        }
        bool open_popup = (sig.Flags & ZUI_SignalClicked) || (!already_open && is_focused && (ctx->SpacePressed || ctx->EnterPressed || ctx->ArrowDownPressed));
        if (open_popup)
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, row->Key);
            float               py = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY : (ctx->MousePos[1] + 4.f);
            float               px = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : (ctx->MousePos[0] - 8.f);
            // Pass the button width so the dropdown popup matches the combo box width
            float               bw = (ps && ps->ScreenMaxX > ps->ScreenMinX) ? (ps->ScreenMaxX - ps->ScreenMinX) : 0.f;
            ctx->PopupDesiredW     = bw;
            ZUIOpenPopup(ctx, key, px, py);
        }

        return ZUIBeginPopup(ctx, key);
    }
    void ZUIEndCombo(ZUIContext* ctx)
    {
        ZUIEndPopup(ctx);
    }

    // Internal: menu bar saved state so EndMenuBar can pop the right box
    bool ZUIBeginMenuBar(ZUIContext* ctx)
    {
        ZUIBox* bar       = ZUIBeginRow(ctx, "##menubar_zui", ZFill(), ZPx(ctx->Style.MenuBarHeight));
        bar->Flags        = bar->Flags | ZUI_DrawBackground;
        bar->EdgeSoftness = 0.f;
        SetBgArr(bar, ctx->Theme.MenuBarBg);
        bar->LayoutAxis = ZUIAxis::X;
        bar->CrossAlign = ZUIAlign::Center;
        return true;
    }
    void ZUIEndMenuBar(ZUIContext* ctx)
    {
        ZUIEndRow(ctx);
    }

    bool ZUIBeginMenu(ZUIContext* ctx, const char* label, bool enabled)
    {
        enabled      = enabled && !ctx->Disabled;
        char key[80] = {};
        snprintf(key, sizeof(key), "%s##menu_%s", label, label);
        ZUIBoxFlags fl = ZUI_DrawText | ZUI_DrawBackground;
        if (enabled)
            fl = fl | ZUI_Clickable;

        ZUIBox* btn       = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), fl);
        btn->Size[0]      = ZText();
        btn->Size[1]      = ZFill(); // fill full bar height — VS Code style
        btn->Padding[0]   = 11.f;    // Figma: labels are 22 px apart
        btn->Padding[2]   = 11.f;
        btn->EdgeSoftness = 0.f;
        ZUIBoxSetCornerRadius(btn, 0.f);

        // Check if this menu's popup is currently open
        uint64_t popup_hash = ZUIHashStr(label, (uint32_t) ZEngine::Helpers::secure_strlen(label));
        bool     is_open    = (ctx->PopupBuildDepth < ctx->PopupStackSize && ctx->PopupStack[ctx->PopupBuildDepth].Key == popup_hash);

        // Open menu uses the selection accent; hover uses the neutral hover surface.
        if (is_open && enabled)
            ZUIBoxSetColorArr(btn, ctx->Theme.HeaderActiveBg);
        else
            ZUIBoxSetColor(btn, 0.f, 0.f, 0.f, 0.f);

        const float disabled_text[4] = ZUI_TOKEN(Tokens::TextDisabled);
        SetTextColor(btn, enabled ? ctx->Theme.TextDefault : disabled_text);

        ZUISignal sig = ZUISignalFromBox(ctx, btn);
        if (enabled && !is_open)
        {
            static const float kRest[4] = {0.f, 0.f, 0.f, 0.f};
            const float*       kHov     = ctx->Theme.HeaderHoveredBg;
            const float*       kAct     = ctx->Theme.HeaderActiveBg;
            ApplyHotActive(btn, ctx, kRest, kHov, kAct);
        }
        ZUIPopBox(ctx);

        // Open on click; or on hover-switch when another menu popup is already open.
        // Signal-based hover won't fire here because the interaction pass blocks non-popup
        // boxes from receiving ctx->HotKey when a popup is open.  Use a direct cursor-vs-
        // ScreenRect check (prev-frame coords) instead — same pattern as BuildDividers.
        ZUIPersistentState* ps            = ZUIStateGetOrInsert(&ctx->StateStore, btn->Key);
        bool                cursor_over   = ps && ps->ScreenMaxX > ps->ScreenMinX && ctx->MousePos[0] >= ps->ScreenMinX && ctx->MousePos[0] <= ps->ScreenMaxX && ctx->MousePos[1] >= ps->ScreenMinY && ctx->MousePos[1] <= ps->ScreenMaxY;
        bool                any_menu_open = (ctx->PopupStackSize > 0 && !is_open);
        if (enabled && ((sig.Flags & ZUI_SignalClicked) || (any_menu_open && cursor_over) || (ctx->FocusKey == btn->Key && (ctx->SpacePressed || ctx->EnterPressed || ctx->ArrowDownPressed))))
        {
            if (any_menu_open)
                ctx->PopupStackSize = 0; // close current menu so new one opens cleanly next frame
            // ImGui: menu popup opens at button's LEFT EDGE, BOTTOM of the menu bar.
            float px = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX : ctx->MousePos[0];
            float py = (ps && ps->ScreenMaxY > 0.f) ? ps->ScreenMaxY : (ctx->MousePos[1] + 26.f);
            ZUIOpenPopup(ctx, label, px, py);
        }

        if (!enabled)
        {
            if (is_open)
                ctx->PopupStackSize = ctx->PopupBuildDepth;
            if (ctx->PendingPopupKey == popup_hash)
                ctx->PendingPopupKey = 0;
            return false;
        }
        return ZUIBeginPopup(ctx, label);
    }
    void ZUIEndMenu(ZUIContext* ctx)
    {
        ZUIEndPopup(ctx);
    }

    // Returns true when point (px, py) lies inside the triangle defined by apex A and
    // base vertices B1/B2.  Used to give the mouse a grace period while moving toward
    // an open submenu popup (ImGui triangle-heuristic).
    static bool MenuTriangleContains(float ax, float ay, float b1x, float b1y, float b2x, float b2y, float px, float py)
    {
        // A stationary cursor is the triangle's apex, not evidence of travel.
        // Require actual progress toward its vertical base and a nonzero area.
        if (fabsf(px - b1x) >= fabsf(ax - b1x) || fabsf(ax - b1x) < 0.001f)
            return false;
        auto  cross   = [](float x1, float y1, float x2, float y2, float qx, float qy) -> float { return (qx - x1) * (y2 - y1) - (qy - y1) * (x2 - x1); };
        float d1      = cross(ax, ay, b1x, b1y, px, py);
        float d2      = cross(b1x, b1y, b2x, b2y, px, py);
        float d3      = cross(b2x, b2y, ax, ay, px, py);
        bool  has_neg = (d1 < 0.f) || (d2 < 0.f) || (d3 < 0.f);
        bool  has_pos = (d1 > 0.f) || (d2 > 0.f) || (d3 > 0.f);
        return !(has_neg && has_pos);
    }

    bool ZUIBeginSubMenu(ZUIContext* ctx, const char* label, bool enabled, int icon)
    {
        GrowPopupForLabel(ctx, label, 68.f);
        enabled           = enabled && !ctx->Disabled;
        char row_key[128] = {}, popup_key[128] = {};
        snprintf(row_key, sizeof(row_key), "##smrow_%s", label);
        snprintf(popup_key, sizeof(popup_key), "##smpop_%s", label);

        uint64_t popup_hash = ZUIHashStr(popup_key, (uint32_t) ZEngine::Helpers::secure_strlen(popup_key));
        bool     is_open    = (ctx->PopupBuildDepth < ctx->PopupStackSize && ctx->PopupStack[ctx->PopupBuildDepth].Key == popup_hash);

        // Row: [label][fill][› chevron][right-pad]
        // Figma 'Menu/Item' with a trailing chevron: 26 px, 8/12 padding, 8 gap, 16 px icon slot.
        ZUIBox*  row        = ZUIBeginRow(ctx, row_key, ZFill(), ZPx(26.f));
        row->Flags          = row->Flags | ZUI_DrawBackground | (enabled ? ZUI_Clickable : ZUI_None);
        row->Padding[0]     = 8.f;
        row->Padding[2]     = 12.f;
        row->Gap            = 8.f;
        row->CrossAlign     = ZUIAlign::Center;
        row->EdgeSoftness   = 0.5f;
        ZUIBoxSetCornerRadius(row, 4.f);

        bool kb_focus = enabled && ctx->KeyboardFocus && ctx->FocusKey == row->Key;
        if (enabled)
            ctx->PopupBuildIdx++;

        if (enabled && is_open)
            ZUIBoxSetColorArr(row, ctx->Theme.HeaderActiveBg);
        else
            ZUIBoxSetColor(row, 0.f, 0.f, 0.f, 0.f);

        const float  disabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
        const float* fg          = enabled ? ctx->Theme.TextDefault : disabled;
        {
            char ik[128] = {};
            snprintf(ik, sizeof(ik), "##smico_%s", label);
            ZUIBox* ic  = ZUIPushBox(ctx, ik, (uint32_t) ZEngine::Helpers::secure_strlen(ik), icon >= 0 ? ZUI_DrawIcon : ZUI_None);
            ic->Size[0] = ZPx(16.f);
            ic->Size[1] = ZPx(16.f);
            if (icon >= 0)
            {
                ic->IconId = (uint16_t) icon;
                SetTextColor(ic, fg);
            }
            ZUIPopBox(ctx);
        }
        {
            char lk[128] = {};
            snprintf(lk, sizeof(lk), "##smlbl_%s", label);
            ZUIBox* lbl  = ZUIPushBox(ctx, lk, (uint32_t) ZEngine::Helpers::secure_strlen(lk), ZUI_DrawText);
            lbl->Size[0] = ZFill();
            lbl->Size[1] = ZPx(13.f);
            lbl->Label   = ZUIPushStr(&ctx->FrameArena, label, (uint32_t) ZEngine::Helpers::secure_strlen(label));
            SetTextColor(lbl, fg);
            ZUIPopBox(ctx);
        }
        ZUIIconBox(ctx, ZUIIcon::ChevronRight, 16.f, enabled ? ctx->Theme.TextDim : disabled);

        ZUISignal sig = ZUISignalFromBox(ctx, row);

        if (enabled && !is_open)
        {
            bool hot = (ctx->HotKey == row->Key), act = (ctx->ActiveKey == row->Key);
            if (act || hot)
                ZUIBoxSetColorArr(row, ctx->Theme.HeaderActiveBg);
        }
        ZUIEndRow(ctx);

        if (!enabled)
        {
            if (is_open)
                ctx->PopupStackSize = ctx->PopupBuildDepth;
            if (ctx->PendingPopupKey == popup_hash)
                ctx->PendingPopupKey = 0;
            return false;
        }

        // Close submenu when cursor leaves both the row and the popup.
        // Triangle heuristic (ImGui style): if the cursor is moving toward the submenu
        // popup (apex = prev cursor, base = left edge of popup ±8px), suppress the close.
        // A fresh entry has no bounds until its first layout. Do not use a stale
        // rectangle from a prior opening, or close an ancestor while the cursor
        // is inside one of its portaled descendant menus.
        if (is_open)
        {
            auto&      submenu             = ctx->PopupStack[ctx->PopupBuildDepth];
            const bool keyboard_navigation = ctx->TabPressed || ctx->ShiftTabPressed || ctx->ArrowLeftPressed || ctx->ArrowRightPressed || ctx->ArrowUpPressed || ctx->ArrowDownPressed || ctx->EnterPressed || ctx->SpacePressed;
            if (keyboard_navigation)
                submenu.MouseNavigation = false;
            else if (!ctx->KeyboardFocus || (sig.Flags & ZUI_SignalHovered) || ctx->MousePos[0] != ctx->PrevMousePos[0] || ctx->MousePos[1] != ctx->PrevMousePos[1])
                submenu.MouseNavigation = true;
            if (submenu.ScreenMax[0] > submenu.ScreenMin[0] && submenu.ScreenMax[1] > submenu.ScreenMin[1])
            {
                const auto* owner      = ZUIStateGetOrInsert(&ctx->StateStore, row->Key);
                const bool  in_row     = owner && owner->ScreenMaxX > owner->ScreenMinX && ctx->MousePos[0] >= owner->ScreenMinX && ctx->MousePos[0] <= owner->ScreenMaxX && ctx->MousePos[1] >= owner->ScreenMinY && ctx->MousePos[1] <= owner->ScreenMaxY;
                bool        in_submenu = false;
                for (uint32_t i = ctx->PopupBuildDepth; i < ctx->PopupStackSize && !in_submenu; ++i)
                {
                    const auto& entry = ctx->PopupStack[i];
                    in_submenu        = entry.ScreenMax[0] > entry.ScreenMin[0] && entry.ScreenMax[1] > entry.ScreenMin[1] && ctx->MousePos[0] >= entry.ScreenMin[0] && ctx->MousePos[0] <= entry.ScreenMax[0] && ctx->MousePos[1] >= entry.ScreenMin[1] && ctx->MousePos[1] <= entry.ScreenMax[1];
                }
                const bool in_tri = MenuTriangleContains(ctx->PrevMousePos[0], ctx->PrevMousePos[1], submenu.ScreenMin[0], submenu.ScreenMin[1] - 8.f, submenu.ScreenMin[0], submenu.ScreenMax[1] + 8.f, ctx->MousePos[0], ctx->MousePos[1]);
                if (submenu.MouseNavigation && !in_row && !in_submenu && !in_tri)
                    ctx->PopupStackSize = ctx->PopupBuildDepth;
            }
        }

        // Open submenu popup on hover — to the right of the parent popup.
        // Use the row's own ScreenMaxX once laid out; fall back to the parent
        // popup box's right edge on the first frame (before layout runs).
        if (enabled && (sig.Flags & (ZUI_SignalHovered | ZUI_SignalClicked)) && !is_open)
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, row->Key);
            float               px = 0.f;
            float               py = ctx->MousePos[1];
            if (ps && ps->ScreenMaxX > 0.f)
            {
                px = ps->ScreenMaxX;
                py = ps->ScreenMinY;
            }
            else
            {
                // First frame — use parent popup's right edge from its persistent state
                uint32_t par_depth = (ctx->PopupBuildDepth > 0) ? ctx->PopupBuildDepth - 1 : 0;
                if (par_depth < ctx->PopupStackSize)
                    px = ctx->PopupStack[par_depth].ScreenMax[0];
                if (px == 0.f)
                    px = ctx->MousePos[0];
            }
            ZUIOpenPopup(ctx, popup_key, px, py);
        }

        // Arrow-Right on a focused or hovered submenu row opens the submenu immediately.
        if (enabled && !is_open && (kb_focus || ctx->HotKey == row->Key) && (ctx->ArrowRightPressed || (kb_focus && (ctx->EnterPressed || ctx->SpacePressed))))
        {
            ZUIPersistentState* ps = ZUIStateGetOrInsert(&ctx->StateStore, row->Key);
            float               px = 0.f;
            float               py = ctx->MousePos[1];
            if (ps && ps->ScreenMaxX > 0.f)
            {
                px = ps->ScreenMaxX;
                py = ps->ScreenMinY;
            }
            else
            {
                uint32_t par_depth = (ctx->PopupBuildDepth > 0) ? ctx->PopupBuildDepth - 1 : 0;
                if (par_depth < ctx->PopupStackSize)
                    px = ctx->PopupStack[par_depth].ScreenMax[0];
                if (px == 0.f)
                    px = ctx->MousePos[0];
            }
            ZUIOpenPopup(ctx, popup_key, px, py);
        }

        return ZUIBeginPopup(ctx, popup_key);
    }

    void ZUIEndSubMenu(ZUIContext* ctx)
    {
        ZUIEndPopup(ctx);
    }

    void ZUIOpenModal(ZUIContext* ctx, const char* key)
    {
        ctx->ActiveModalKey = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
    }

    bool ZUIBeginModal(ZUIContext* ctx, const char* key, const char* title)
    {
        uint64_t hash = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        if (ctx->ActiveModalKey != hash)
        {
            return false;
        }

        float   sw = (float) ctx->ScreenW;
        float   sh = (float) ctx->ScreenH;
        float   mw = 480.f, mh = 280.f;

        // Dim overlay — root level, covers full screen
        ZUIBox* saved    = ctx->Current;
        ctx->Current     = ctx->Root;

        ZUIBox* dim      = ZUIPushBox(ctx, "##modal_dim", 12, ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
        dim->Size[0]     = ZPx(sw);
        dim->Size[1]     = ZPx(sh);
        dim->FloatPos[0] = 0.f;
        dim->FloatPos[1] = 0.f;
        ZUIBoxSetColor(dim, 0.f, 0.f, 0.f, 0.55f);
        ZUIPopBox(ctx);

        // Modal panel — centred
        ZUIBox* panel          = ZUIPushBox(ctx, key, (uint32_t) ZEngine::Helpers::secure_strlen(key), ZUI_DrawBackground | ZUI_DrawBorder | ZUI_DropShadow | ZUI_FloatX | ZUI_FloatY);
        panel->Size[0]         = ZPx(mw);
        panel->Size[1]         = ZPx(mh);
        panel->FloatPos[0]     = (sw - mw) * 0.5f;
        panel->FloatPos[1]     = (sh - mh) * 0.5f;
        panel->LayoutAxis      = ZUIAxis::Y;
        panel->BorderThickness = 1.f;
        SetBgArr(panel, ctx->Theme.PanelBg);
        SetBdrArr(panel, ctx->Theme.PanelBorder);

        ctx->Current  = panel; // children go inside modal
        ctx->ModalBox = panel;

        // Title bar
        if (title)
        {
            ZUIBox* hdr = ZUIBeginRow(ctx, "##modal_hdr", ZFill(), ZPx(ZUIGetFrameHeight(ctx)));
            hdr->Flags  = hdr->Flags | ZUI_DrawBackground;
            SetBgArr(hdr, ctx->Theme.HeaderBg);
            ZUISpacer(ctx, 8.f);
            ZUILabel(ctx, title, ctx->Theme.TextDefault);
            ZUIEndRow(ctx);
            ZUISeparator(ctx);
        }

        ctx->ModalSavedParent = saved;
        return true;
    }

    void ZUIEndModal(ZUIContext* ctx)
    {
        ZUIPopBox(ctx); // pop modal panel
        ctx->Current          = ctx->ModalSavedParent;
        ctx->ModalSavedParent = nullptr;
    }

    // Drag-and-drop helpers

    void ZUIBeginDragSource(ZUIContext* ctx, const ZUIBox* box, const char* payload, uint32_t payload_len, ZUIDragPayloadKind kind)
    {
        if (!ctx || !box || !payload || payload_len > sizeof(ctx->DragPayload))
        {
            return;
        }
        // Activate drag when this box is the active (held) box and the mouse has moved.
        // Reads ctx state directly to avoid a second ZUISignalFromBox call on the same box.
        bool held   = (ctx->ActiveKey == box->Key) && ctx->MouseDown[0];
        bool moving = held && (ctx->MousePos[0] != ctx->PrevMousePos[0] || ctx->MousePos[1] != ctx->PrevMousePos[1]);
        if (moving && ctx->DragSourceKey == 0)
        {
            uint32_t copy_len = payload_len;
            if (kind != ZUIDragPayloadKind::Opaque)
            {
                if (copy_len && payload[copy_len - 1] == '\0')
                    --copy_len;
                if (copy_len == 0 || copy_len >= sizeof(ctx->DragPayload))
                    return;
                for (uint32_t i = 0; i < copy_len; ++i)
                    if (payload[i] == '\0')
                        return;
            }
            ctx->DragSourceKey = box->Key;
            Helpers::secure_memcpy(ctx->DragPayload, sizeof(ctx->DragPayload), payload, copy_len);
            if (copy_len < sizeof(ctx->DragPayload))
                ctx->DragPayload[copy_len] = '\0';
            ctx->DragPayloadLen  = copy_len;
            ctx->DragPayloadKind = kind;
        }
    }

    bool ZUIAcceptDrop(ZUIContext* ctx, const ZUIBox* box, char* out_buf, uint32_t out_size, ZUIDragPayloadKind* kind)
    {
        if (kind)
            *kind = ZUIDragPayloadKind::Opaque;
        if (!ctx || !box)
        {
            return false;
        }
        if (!ctx->DragDropFired || ctx->DragTargetKey != box->Key)
        {
            return false;
        }
        if (ctx->DragPayloadLen > sizeof(ctx->DragPayload))
            return false;
        const bool text     = ctx->DragPayloadKind != ZUIDragPayloadKind::Opaque;
        uint32_t   copy_len = ctx->DragPayloadLen;
        if (text)
        {
            // Accept both legacy string lengths (including NUL) and exact character counts.
            if (copy_len && ctx->DragPayload[copy_len - 1] == '\0')
                --copy_len;
            if (!copy_len || copy_len >= sizeof(ctx->DragPayload))
                return false;
            for (uint32_t i = 0; i < copy_len; ++i)
                if (ctx->DragPayload[i] == '\0')
                    return false;
        }
        if (out_buf)
        {
            if (out_size < copy_len || (text && out_size == copy_len))
                return false;
            Helpers::secure_memcpy(out_buf, out_size, ctx->DragPayload, copy_len);
            if (out_size > copy_len)
                out_buf[copy_len] = '\0';
        }
        if (kind)
            *kind = ctx->DragPayloadKind;
        return true;
    }

    // ZUIImage

    void ZUIImage(ZUIContext* ctx, const char* key, uint32_t texture_index, ZUISize w, ZUISize h)
    {
        uint32_t len      = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  box      = ZUIPushBox(ctx, key, len, ZUI_DrawBackground);
        box->Size[0]      = w;
        box->Size[1]      = h;
        box->TextureIndex = texture_index;
        // Colors must be non-transparent so the renderer draws this box
        ZUIBoxSetColor(box, 1.f, 1.f, 1.f, 1.f);
        ZUIPopBox(ctx);
    }

    // ZUIDragFloat

    bool ZUIDragFloat(ZUIContext* ctx, const char* key, float* value, float speed, float width_px)
    {
        if (!value)
            return false;
        uint32_t key_len  = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        uint64_t key_hash = ZUIHashStr(key, key_len);

        ZUIBox*  field    = ZUIPushBox(ctx, key, key_len, ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]    = ZPx(width_px);
        field->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
        field->Padding[0] = ZUIGetFramePadX(ctx);
        field->Padding[2] = ZUIGetFramePadX(ctx);
        ZUIBoxSetCornerRadius(field, ctx->Style.FrameRounding);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness = 1.f;
        if (ctx->FlatInputs)
        {
            field->Flags           = (ZUIBoxFlags) (field->Flags & ~ZUI_DrawBorder);
            field->BorderThickness = 0.f;
            ZUIBoxSetCornerRadius(field, 3.f);
        }

        // Persistent state: UserData >= 0 → text-edit mode (cursor pos); -1 → drag mode
        ZUIPersistentState* ps         = ZUIStateGetOrInsert(&ctx->StateStore, key_hash);
        bool                is_focused = !ctx->Disabled && (ctx->FocusKey == key_hash);
        bool                text_mode  = is_focused && ps && (ps->UserData >= 0.f);

        if (text_mode)
            ctx->TextInputActive = true;

        // Static edit buffer — shared, switched on focus change like ZUIInputInt
        static char     s_df_buf[64] = {};
        static uint64_t s_df_key     = 0;

        if (text_mode)
        {
            if (s_df_key != key_hash)
            {
                snprintf(s_df_buf, sizeof(s_df_buf), "%.6g", (double) *value);
                s_df_key = key_hash;
            }
            if (ctx->KeyboardFocus)
                SetBdrArr(field, ctx->Theme.InputFocusBorder);

            // Accumulate typed characters (digits, '.', '-')
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char     c = ctx->TextInput[i];
                uint32_t l = (uint32_t) ZEngine::Helpers::secure_strlen(s_df_buf);
                if (l < sizeof(s_df_buf) - 1 && ((c >= '0' && c <= '9') || c == '.' || c == '-' || c == 'e' || c == 'E'))
                {
                    s_df_buf[l]     = c;
                    s_df_buf[l + 1] = '\0';
                }
            }
            if (ctx->BackspacePressed)
            {
                uint32_t l = (uint32_t) ZEngine::Helpers::secure_strlen(s_df_buf);
                if (l > 0)
                    s_df_buf[l - 1] = '\0';
            }

            // Display with blinking cursor
            char display[72] = {};
            bool show_pipe   = CaretVisible(ctx);
            if (show_pipe)
                snprintf(display, sizeof(display), "%s|", s_df_buf);
            else
                snprintf(display, sizeof(display), "%s", s_df_buf);
            field->Label = ZUIPushStr(&ctx->FrameArena, display, (uint32_t) ZEngine::Helpers::secure_strlen(display));
        }
        else
        {
            // Normal drag mode: show value + dim arrow hint when focused
            char val_buf[40] = {};
            snprintf(val_buf, sizeof(val_buf), "%.*f%s", ctx->DragFloatDecimals, (double) *value, ctx->DragFloatSuffix ? ctx->DragFloatSuffix : "");
            if (is_focused && ctx->KeyboardFocus)
                SetBdrArr(field, ctx->Theme.InputFocusBorder);
            uint32_t vlen = (uint32_t) Helpers::secure_strlen(val_buf);
            field->Label  = ZUIPushStr(&ctx->FrameArena, val_buf, vlen);
        }

        StyleField(ctx, field, is_focused, !ctx->Disabled && !text_mode && ctx->ActiveKey == field->Key && ctx->MouseDown[0]);
        if (ctx->FlatInputs && !ctx->Disabled)
            field->BorderThickness = 0.f;
        else if (ctx->Disabled)
            field->Flags = field->Flags | ZUI_DrawBorder;
        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (ctx->Disabled)
        {
            if (ps)
                ps->UserData = -1.f;
            if (s_df_key == key_hash)
                s_df_key = 0;
            return false;
        }

        bool changed = false;

        // Ctrl+click → enter text edit mode
        if ((sig.Flags & ZUI_SignalClicked) && ctx->CtrlDown && !text_mode)
        {
            ctx->FocusKey = key_hash;
            if (ps)
                ps->UserData = 0.f; // text edit mode, cursor at 0
            snprintf(s_df_buf, sizeof(s_df_buf), "%.6g", (double) *value);
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
                    float parsed = (float) atof(s_df_buf);
                    if (parsed != *value)
                    {
                        *value  = parsed;
                        changed = true;
                    }
                }
                if (ps)
                {
                    ps->UserData = -1.f;
                } // exit text mode
                s_df_key    = 0;
                s_df_buf[0] = '\0';
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
                if (ctx->ArrowUpPressed)
                {
                    *value  += speed;
                    changed  = true;
                }
                if (ctx->ArrowDownPressed)
                {
                    *value  -= speed;
                    changed  = true;
                }
            }
        }
        return changed;
    }

    // ZUIDragInt

    bool ZUIDragInt(ZUIContext* ctx, const char* key, int* value, float speed, float width_px)
    {
        if (!value)
            return false;
        uint32_t key_len  = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        uint64_t key_hash = ZUIHashStr(key, key_len);

        ZUIBox*  field    = ZUIPushBox(ctx, key, key_len, ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        field->Size[0]    = ZPx(width_px);
        field->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
        field->Padding[0] = ZUIGetFramePadX(ctx);
        field->Padding[2] = ZUIGetFramePadX(ctx);
        ZUIBoxSetCornerRadius(field, ctx->Style.FrameRounding);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        SetBdrArr(field, ctx->Theme.InputBorder);
        field->BorderThickness         = 1.f;

        ZUIPersistentState* ps         = ZUIStateGetOrInsert(&ctx->StateStore, key_hash);
        bool                is_focused = !ctx->Disabled && (ctx->FocusKey == key_hash);
        bool                text_mode  = is_focused && ps && (ps->UserData >= 0.f);

        if (text_mode)
            ctx->TextInputActive = true;

        static char     s_di_buf[32] = {};
        static uint64_t s_di_key     = 0;

        if (text_mode)
        {
            if (s_di_key != key_hash)
            {
                snprintf(s_di_buf, sizeof(s_di_buf), "%d", *value);
                s_di_key = key_hash;
            }
            if (ctx->KeyboardFocus)
                SetBdrArr(field, ctx->Theme.InputFocusBorder);
            for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
            {
                char     c = ctx->TextInput[i];
                uint32_t l = (uint32_t) ZEngine::Helpers::secure_strlen(s_di_buf);
                if (l < sizeof(s_di_buf) - 1 && ((c >= '0' && c <= '9') || (c == '-' && l == 0)))
                {
                    s_di_buf[l]     = c;
                    s_di_buf[l + 1] = '\0';
                }
            }
            if (ctx->BackspacePressed)
            {
                uint32_t l = (uint32_t) ZEngine::Helpers::secure_strlen(s_di_buf);
                if (l > 0)
                    s_di_buf[l - 1] = '\0';
            }

            char display[36] = {};
            if (CaretVisible(ctx))
                snprintf(display, sizeof(display), "%s|", s_di_buf);
            else
                snprintf(display, sizeof(display), "%s", s_di_buf);
            field->Label = ZUIPushStr(&ctx->FrameArena, display, (uint32_t) ZEngine::Helpers::secure_strlen(display));
        }
        else
        {
            if (is_focused && ctx->KeyboardFocus)
            {
                SetBdrArr(field, ctx->Theme.InputFocusBorder);
            }
            char buf[16] = {};
            snprintf(buf, sizeof(buf), "%d", *value);
            field->Label = ZUIPushStr(&ctx->FrameArena, buf, (uint32_t) ZEngine::Helpers::secure_strlen(buf));
        }

        StyleField(ctx, field, is_focused, !ctx->Disabled && !text_mode && ctx->ActiveKey == field->Key && ctx->MouseDown[0]);
        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (ctx->Disabled)
        {
            if (ps)
                ps->UserData = -1.f;
            if (s_di_key == key_hash)
                s_di_key = 0;
            return false;
        }

        bool changed = false;

        if ((sig.Flags & ZUI_SignalClicked) && ctx->CtrlDown && !text_mode)
        {
            ctx->FocusKey = key_hash;
            if (ps)
                ps->UserData = 0.f;
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
                if (s_di_buf[0])
                {
                    *value  = atoi(s_di_buf);
                    changed = true;
                }
                if (ps)
                    ps->UserData = -1.f;
                s_di_key    = 0;
                s_di_buf[0] = '\0';
            }
            else if (ctx->EscapePressed)
            {
                if (ps)
                    ps->UserData = -1.f;
                s_di_key    = 0;
                s_di_buf[0] = '\0';
            }
        }
        else
        {
            if ((sig.Flags & ZUI_SignalHeld) && sig.DragDelta[0] != 0.f)
            {
                float fv = (float) *value + sig.DragDelta[0] * speed;
                *value   = (int) fv;
                changed  = true;
            }
            if (is_focused)
            {
                int step = (int) speed > 0 ? (int) speed : 1;
                if (ctx->ArrowUpPressed)
                {
                    *value  += step;
                    changed  = true;
                }
                if (ctx->ArrowDownPressed)
                {
                    *value  -= step;
                    changed  = true;
                }
            }
        }
        return changed;
    }

    // ZUIDragFloat3

    bool ZUIDragFloat3(ZUIContext* ctx, const char* key, float v[3], float speed, float comp_w, const char* const labels[3])
    {
        struct AxisStyle
        {
            const char* label   = {};
            float       chip[4] = {};
        };
        static const AxisStyle kAxes[3] = {
            {"X",   ZUI_TOKEN(Tokens::AccentRed)}, // accent/red
            {"Y", ZUI_TOKEN(Tokens::AccentGreen)}, // accent/green
            {"Z",  ZUI_TOKEN(Tokens::AccentBlue)}, // accent/blue
        };

        float fw      = (comp_w > 0.f) ? comp_w : 66.f;
        bool  changed = false;

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
                ZUISpacer(ctx, 2.f);

            // Colored axis chip (X / Y / Z)
            char lk[48] = {};
            snprintf(lk, sizeof(lk), "##f3l%d%s", i, key);
            uint32_t llen   = (uint32_t) ZEngine::Helpers::secure_strlen(lk);
            ZUIBox*  chip   = ZUIPushBox(ctx, lk, llen, ZUI_DrawBackground | ZUI_DrawText);
            chip->Size[0]   = ZPx(14.f);
            chip->Size[1]   = ZPx(ZUIGetFrameHeight(ctx));
            chip->TextAlign = ZUITextAlign::Center;
            chip->FontSize  = ZUIFontSize::Heading;
            ZUIBoxSetColorArr(chip, kAxes[i].chip);
            chip->TextColor[0]      = 1.f;
            chip->TextColor[1]      = 1.f;
            chip->TextColor[2]      = 1.f;
            chip->TextColor[3]      = 1.f;
            const char* const label = labels ? labels[i] : kAxes[i].label;
            chip->Label             = ZUIPushStr(&ctx->FrameArena, label, (uint32_t) ZEngine::Helpers::secure_strlen(label));
            ZUIBoxSetCornerRadius(chip, 2.f);
            chip->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);

            // Drag field for this component
            char dk[48] = {};
            snprintf(dk, sizeof(dk), "##f3d%d%s", i, key);
            changed |= ZUIDragFloat(ctx, dk, &v[i], speed, fw);
        }
        return changed;
    }

    // ZUIInputFloat

    bool ZUIInputFloat(ZUIContext* ctx, const char* key, float* value, float width_px)
    {
        if (!value)
            return false;
        const auto hash = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
        auto*      ps   = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        if (!ps)
            return false;
        const bool focused = !ctx->Disabled && ctx->FocusKey == hash;
        // Most numeric fields are only displayed. Allocate retained text state
        // on first focus, not for every field visited in an inspector/importer.
        if (!ps->FloatInputState && focused)
        {
            if (auto* storage = ZPushStruct(&ctx->PersistentArena, ZUIFloatInputState))
                ps->FloatInputState = new (storage) ZUIFloatInputState{};
            else
                ctx->FocusKey = 0; // A failed allocation must not dereference null.
        }
        if (!ps->FloatInputState)
        {
            char display[64] = {};
            snprintf(display, sizeof(display), "%.*g", std::numeric_limits<float>::max_digits10, static_cast<double>(*value));
            ZUITextField(ctx, key, display, sizeof(display), width_px);
            return false;
        }
        auto&      state    = *ps->FloatInputState;
        const auto identity = reinterpret_cast<uintptr_t>(value);
        // Reusing a widget key for another object, hiding it, or an external
        // model update must not apply an old edit to the current binding.
        const bool stale    = state.LastFrame && (state.BindingIdentity != identity || state.LastFrame + 1 != ctx->FrameIndex || std::bit_cast<uint32_t>(state.LastValue) != std::bit_cast<uint32_t>(*value));
        if (stale || ctx->Disabled)
        {
            state.Editing   = false;
            state.Edited    = false;
            ps->UserData    = -1.f;
            ps->SelectStart = -1;
            if (ctx->UndoFieldKey == hash)
            {
                ctx->UndoFieldKey = 0;
                ctx->UndoTop = ctx->RedoTop = 0;
            }
        }

        const bool blurred = !focused || (ctx->MousePressed[0] && ctx->HotKey != hash);
        const bool cancel  = state.Editing && focused && ctx->EscapePressed;
        if (cancel)
        {
            ctx->FocusKey      = 0;
            ctx->EscapePressed = false;
            state.Editing      = false;
            ps->UserData       = -1.f;
            ps->SelectStart    = -1;
        }
        if (!state.Editing)
        {
            // max_digits10 preserves even HDR/subnormal values on a no-op edit.
            snprintf(state.Buffer, sizeof(state.Buffer), "%.*g", std::numeric_limits<float>::max_digits10, static_cast<double>(*value));
            state.Edited = false;
            if (focused && !blurred && !cancel)
            {
                state.Editing = true;
                if (ctx->UndoFieldKey == hash)
                {
                    ctx->UndoFieldKey = 0;
                    ctx->UndoTop = ctx->RedoTop = 0;
                }
            }
        }

        auto* parent  = ctx->Current;
        state.Edited |= ZUITextField(ctx, key, state.Buffer, sizeof(state.Buffer), width_px);
        bool changed  = false;
        if (state.Editing && (blurred || ctx->EnterPressed || ctx->TabPressed || ctx->ShiftTabPressed))
        {
            if (state.Edited)
            {
                char* end          = nullptr;
                errno              = 0;
                const float parsed = std::strtof(state.Buffer, &end);
                // Reject empty/partial text, overflow, underflow, NaN and infinity.
                if (end != state.Buffer && *end == '\0' && errno != ERANGE && std::isfinite(parsed) && parsed != *value)
                {
                    *value  = parsed;
                    changed = true;
                }
            }
            state.Editing   = false;
            // End cursor/selection with the session, rather than resetting on
            // next focus (which would overwrite a new click's caret position).
            ps->UserData    = -1.f;
            ps->SelectStart = -1;
            if (ctx->FocusKey == hash && !ctx->TabPressed && !ctx->ShiftTabPressed)
                ctx->FocusKey = 0;
            snprintf(state.Buffer, sizeof(state.Buffer), "%.*g", std::numeric_limits<float>::max_digits10, static_cast<double>(*value));
            if (parent && parent->LastChild)
                parent->LastChild->Label = ZUIPushStr(&ctx->FrameArena, state.Buffer, static_cast<uint32_t>(Helpers::secure_strlen(state.Buffer)));
        }
        state.BindingIdentity = identity;
        state.LastFrame       = ctx->FrameIndex;
        state.LastValue       = *value;
        return changed;
    }

    // ZUISpinner  (3-dot pulse, arena-safe)

    void ZUISpinner(ZUIContext* ctx, const char* key, float radius_px, float speed)
    {
        float dot    = radius_px * 0.65f;
        float gap    = dot * 0.6f;

        char  rk[48] = {};
        snprintf(rk, sizeof(rk), "##spn_%s", key);
        ZUIBeginRow(ctx, rk, ZFit(), ZPx(dot));

        for (int i = 0; i < 3; ++i)
        {
            if (i > 0)
                ZUISpacer(ctx, gap);

            float phase  = ctx->Time * speed - (float) i * 0.5f;
            float t      = 0.5f + 0.5f * sinf(phase);
            float alpha  = 0.25f + 0.75f * t;

            char  dk[56] = {};
            snprintf(dk, sizeof(dk), "##spd%d_%s", i, key);
            ZUIBox* d    = ZUIPushBox(ctx, dk, (uint32_t) ZEngine::Helpers::secure_strlen(dk), ZUI_DrawBackground);
            d->Size[0]   = ZPx(dot);
            d->Size[1]   = ZPx(dot);
            float col[4] = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], alpha};
            ZUIBoxSetColorArr(d, col);
            ZUIBoxSetCornerRadius(d, dot * 0.5f);
            d->EdgeSoftness = 0.6f;
            ZUIPopBox(ctx);
        }

        ZUIEndRow(ctx);
    }

    // ZUITextField

    bool ZUITextField(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, float width_px)
    {
        if (!buf || buf_size == 0)
            return false;
        uint32_t key_len  = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  field    = ZUIPushBox(ctx, key, key_len, ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_DrawText);
        field->Size[0]    = ZPx(width_px);
        field->Size[1]    = ZPx(ZUIGetFrameHeight(ctx));
        field->Padding[0] = ZUIGetFramePadX(ctx);
        field->Padding[2] = ZUIGetFramePadX(ctx);
        ZUIBoxSetCornerRadius(field, ctx->Style.FrameRounding);
        SetBgArr(field, ctx->Theme.InputBg);
        SetTextColor(field, ctx->Theme.TextDefault);
        field->BorderThickness = 1.f;
        field->EdgeSoftness    = 0.5f;

        bool is_focused        = !ctx->Disabled && (ctx->FocusKey == field->Key);
        bool changed           = false;

        if (is_focused)
            ctx->TextInputActive = true;

        ZUIPersistentState* ps   = ZUIStateGetOrInsert(&ctx->StateStore, field->Key);
        ZUIFont*            font = ctx->GetFont(ZUIFontSize::Body);

        if (!is_focused)
        {
            SetBdrArr(field, ctx->Theme.InputBorder);
        }
        if (is_focused)
        {
            if (ctx->KeyboardFocus)
                SetBdrArr(field, ctx->Theme.InputFocusBorder);

            uint32_t len  = (uint32_t) Helpers::secure_strlen(buf);
            int      cpos = (ps && ps->UserData >= 0.f) ? (int) ps->UserData : (int) len;
            if (cpos < 0)
                cpos = 0;
            if ((uint32_t) cpos > len)
                cpos = (int) len;

            int sel_start = ps ? ps->SelectStart : -1;
            if (sel_start > (int) len)
                sel_start = (int) len;

            // Undo/Redo helpers
            static constexpr int kUD       = (int) ZUIContext::kUndoDepth;

            auto                 push_undo = [&]() {
                if (ctx->UndoFieldKey != field->Key)
                {
                    ctx->UndoFieldKey = field->Key;
                    ctx->UndoTop = ctx->RedoTop = 0;
                }
                ctx->RedoTop = 0;
                if (ctx->UndoTop < kUD)
                {
                    auto& e = ctx->UndoStack[ctx->UndoTop++];
                    snprintf(e.Buf, sizeof(e.Buf), "%s", buf);
                    e.Cursor = cpos;
                }
                else
                {
                    ZEngine::Helpers::secure_memmove(ctx->UndoStack, sizeof(ctx->UndoStack), ctx->UndoStack + 1, (kUD - 1) * sizeof(ZUIContext::ZUIUndoEntry));
                    auto& e = ctx->UndoStack[kUD - 1];
                    snprintf(e.Buf, sizeof(e.Buf), "%s", buf);
                    e.Cursor = cpos;
                }
            };

            // Ctrl+Z undo
            if (ctx->CtrlZPressed && ctx->UndoFieldKey == field->Key && ctx->UndoTop > 0)
            {
                if (ctx->RedoTop < kUD)
                {
                    auto& r = ctx->RedoStack[ctx->RedoTop++];
                    snprintf(r.Buf, sizeof(r.Buf), "%s", buf);
                    r.Cursor = cpos;
                }
                auto& e = ctx->UndoStack[--ctx->UndoTop];
                snprintf(buf, buf_size, "%s", e.Buf);
                cpos = e.Cursor;
                if ((uint32_t) cpos > ZEngine::Helpers::secure_strlen(buf))
                    cpos = (int) ZEngine::Helpers::secure_strlen(buf);
                sel_start = -1;
                changed   = true;
            }
            // Ctrl+Y redo
            if (ctx->CtrlYPressed && ctx->UndoFieldKey == field->Key && ctx->RedoTop > 0)
            {
                if (ctx->UndoTop < kUD)
                {
                    auto& u = ctx->UndoStack[ctx->UndoTop++];
                    snprintf(u.Buf, sizeof(u.Buf), "%s", buf);
                    u.Cursor = cpos;
                }
                auto& r = ctx->RedoStack[--ctx->RedoTop];
                snprintf(buf, buf_size, "%s", r.Buf);
                cpos = r.Cursor;
                if ((uint32_t) cpos > ZEngine::Helpers::secure_strlen(buf))
                    cpos = (int) ZEngine::Helpers::secure_strlen(buf);
                sel_start = -1;
                changed   = true;
            }

            // Selection helpers
            auto has_sel    = [&] { return sel_start >= 0 && sel_start != cpos; };
            auto sel_lo     = [&] { return sel_start >= 0 && sel_start < cpos ? sel_start : cpos; };
            auto sel_hi     = [&] { return sel_start >= 0 && sel_start > cpos ? sel_start : cpos; };

            auto delete_sel = [&]() {
                if (!has_sel())
                    return;
                int lo = sel_lo(), hi = sel_hi();
                ZEngine::Helpers::secure_memmove(buf + lo, buf_size - (uint32_t) lo, buf + hi, Helpers::secure_strlen(buf) - hi + 1);
                cpos      = lo;
                sel_start = -1;
                changed   = true;
            };

            // Mouse click / drag to position and select
            // prev-frame field X from state — used to map mouse pos → char index
            {
                float field_x = (ps && ps->ScreenMinX > 0.f) ? ps->ScreenMinX + ZUIGetFramePadX(ctx) : 0.f;

                auto  char_at = [&](float offset) -> int {
                    if (!font || offset <= 0.f)
                        return 0;
                    uint32_t slen = (uint32_t) Helpers::secure_strlen(buf);
                    float    cum  = 0.f;
                    for (uint32_t i = 0; i < slen; ++i)
                    {
                        float ts[2] = {};
                        ZUIMeasureText(font, buf + i, 1, ts);
                        if (cum + ts[0] * 0.5f > offset)
                            return (int) i;
                        cum += ts[0];
                    }
                    return (int) slen;
                };

                if (ctx->MousePressed[0] && ctx->HotKey == field->Key)
                {
                    // Click: set cursor to character under mouse, clear selection
                    cpos      = char_at(ctx->MousePos[0] - field_x);
                    sel_start = -1;
                }
                else if (ctx->MouseDown[0] && !ctx->MousePressed[0] && ctx->ActiveKey == field->Key)
                {
                    // Drag: extend selection from initial click position to current mouse
                    if (sel_start < 0)
                        sel_start = cpos; // anchor at click pos
                    cpos = char_at(ctx->MousePos[0] - field_x);
                }
            }

            // Text insertion
            if (ctx->TextInputLen > 0)
            {
                push_undo();
                delete_sel();
                for (uint32_t i = 0; i < ctx->TextInputLen; ++i)
                {
                    len = (uint32_t) Helpers::secure_strlen(buf);
                    if (len + 1 < buf_size)
                    {
                        ZEngine::Helpers::secure_memmove(buf + cpos + 1, buf_size - (uint32_t) cpos - 1, buf + cpos, len - cpos + 1);
                        buf[cpos] = ctx->TextInput[i];
                        cpos++;
                        changed = true;
                    }
                }
            }

            // Backspace
            if (ctx->BackspacePressed)
            {
                push_undo();
                if (has_sel())
                    delete_sel();
                else if (cpos > 0)
                {
                    len = (uint32_t) Helpers::secure_strlen(buf);
                    ZEngine::Helpers::secure_memmove(buf + cpos - 1, buf_size - (uint32_t) cpos + 1, buf + cpos, len - cpos + 1);
                    cpos--;
                    changed = true;
                }
            }

            // Forward delete
            len = (uint32_t) Helpers::secure_strlen(buf);
            if (ctx->DeletePressed)
            {
                push_undo();
                if (has_sel())
                    delete_sel();
                else if ((uint32_t) cpos < len)
                {
                    ZEngine::Helpers::secure_memmove(buf + cpos, buf_size - (uint32_t) cpos, buf + cpos + 1, len - cpos);
                    changed = true;
                }
            }

            // Arrow keys + Shift for selection
            len = (uint32_t) Helpers::secure_strlen(buf);
            if (ctx->ArrowLeftPressed)
            {
                if (ctx->ShiftDown)
                {
                    if (sel_start < 0)
                        sel_start = cpos;
                    if (cpos > 0)
                        cpos--;
                }
                else
                {
                    cpos      = has_sel() ? sel_lo() : (cpos > 0 ? cpos - 1 : 0);
                    sel_start = -1;
                }
            }
            if (ctx->ArrowRightPressed)
            {
                if (ctx->ShiftDown)
                {
                    if (sel_start < 0)
                        sel_start = cpos;
                    if ((uint32_t) cpos < len)
                        cpos++;
                }
                else
                {
                    cpos      = has_sel() ? sel_hi() : ((uint32_t) cpos < len ? cpos + 1 : cpos);
                    sel_start = -1;
                }
            }
            if (ctx->HomePressed)
            {
                if (ctx->ShiftDown)
                {
                    if (sel_start < 0)
                        sel_start = cpos;
                }
                else
                    sel_start = -1;
                cpos = 0;
            }
            if (ctx->EndPressed)
            {
                if (ctx->ShiftDown)
                {
                    if (sel_start < 0)
                        sel_start = cpos;
                }
                else
                    sel_start = -1;
                cpos = (int) len;
            }

            // Ctrl+A — select all
            if (ctx->CtrlAPressed)
            {
                sel_start = 0;
                cpos      = (int) len;
            }

            // Clipboard
            if (ctx->CtrlCPressed)
            {
                if (has_sel())
                {
                    int n = sel_hi() - sel_lo();
                    if (n >= (int) sizeof(ctx->ClipboardWrite))
                        n = (int) sizeof(ctx->ClipboardWrite) - 1;
                    ZEngine::Helpers::secure_memcpy(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), buf + sel_lo(), n);
                    ctx->ClipboardWrite[n] = '\0';
                }
                else
                    snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
            }
            if (ctx->CtrlXPressed)
            {
                push_undo();
                if (has_sel())
                {
                    int n = sel_hi() - sel_lo();
                    if (n >= (int) sizeof(ctx->ClipboardWrite))
                        n = (int) sizeof(ctx->ClipboardWrite) - 1;
                    ZEngine::Helpers::secure_memcpy(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), buf + sel_lo(), n);
                    ctx->ClipboardWrite[n] = '\0';
                    delete_sel();
                }
                else
                {
                    snprintf(ctx->ClipboardWrite, sizeof(ctx->ClipboardWrite), "%s", buf);
                    buf[0]    = '\0';
                    cpos      = 0;
                    sel_start = -1;
                    changed   = true;
                }
            }

            // Ctrl+Backspace — delete word
            if (ctx->CtrlBackspacePressed && cpos > 0)
            {
                push_undo();
                if (has_sel())
                    delete_sel();
                else
                {
                    len       = (uint32_t) Helpers::secure_strlen(buf);
                    int start = cpos - 1;
                    while (start > 0 && buf[start - 1] != ' ' && buf[start - 1] != '/' && buf[start - 1] != '\\' && buf[start - 1] != '.')
                        start--;
                    ZEngine::Helpers::secure_memmove(buf + start, buf_size - (uint32_t) start, buf + cpos, len - cpos + 1);
                    cpos    = start;
                    changed = true;
                }
            }

            // Clamp and save state
            len = (uint32_t) Helpers::secure_strlen(buf);
            if (cpos < 0)
                cpos = 0;
            if ((uint32_t) cpos > len)
                cpos = (int) len;
            if (ps)
            {
                ps->UserData    = (float) cpos;
                ps->SelectStart = sel_start;
            }

            // Floated children render after the field's text. Redraw the
            // selected substring over the opaque highlight so it stays legible.
            if (has_sel() && font)
            {
                float ts[2] = {};
                int   lo = sel_lo(), hi = sel_hi();
                ZUIMeasureText(font, buf, lo, ts);
                float before_w = ts[0];
                ZUIMeasureText(font, buf + lo, hi - lo, ts);
                float sel_w = ts[0];
                if (sel_w > 0.5f)
                {
                    float fpy    = ZUIGetFramePadY(ctx);
                    char  sk[80] = {};
                    snprintf(sk, sizeof(sk), "##fsel_%016llx", static_cast<unsigned long long>(field->Key));
                    ZUIBox* sb      = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawBackground | ZUI_DrawText | ZUI_FloatX | ZUI_FloatY);
                    sb->Size[0]     = ZPx(sel_w);
                    sb->Size[1]     = ZPx(ZUIGetFrameHeight(ctx) - fpy * 2.f);
                    sb->FloatPos[0] = ZUIGetFramePadX(ctx) + before_w;
                    sb->FloatPos[1] = fpy;
                    sb->Label       = ZUIPushStr(&ctx->FrameArena, buf + lo, static_cast<uint32_t>(hi - lo));
                    sb->TextAlign   = ZUITextAlign::Center; // Exact measured width: no field-padding indent.
                    SetTextColor(sb, ctx->Theme.TextDefault);
                    ZUIBoxSetColorArr(sb, ctx->Theme.SelectionBg);
                    sb->EdgeSoftness = 0.f;
                    ZUIPopBox(ctx);
                }
            }

            // Cursor caret — 1.5 px drawn rect at the cursor X position (no |
            // embedded in text, so character advance doesn't shift the layout)
            bool show_caret = !has_sel() && CaretVisible(ctx);
            if (show_caret && font)
            {
                float ts[2] = {};
                ZUIMeasureText(font, buf, cpos, ts);
                float fpy    = ZUIGetFramePadY(ctx);
                char  ck[80] = {};
                snprintf(ck, sizeof(ck), "##fcaret_%016llx", static_cast<unsigned long long>(field->Key));
                ZUIBox* caret      = ZUIPushBox(ctx, ck, (uint32_t) ZEngine::Helpers::secure_strlen(ck), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                caret->Size[0]     = ZPx(1.5f);
                caret->Size[1]     = ZPx(ZUIGetFrameHeight(ctx) - fpy * 2.f);
                caret->FloatPos[0] = ZUIGetFramePadX(ctx) + ts[0]; // ts[0] already in logical px (ZUIMeasureText applies FontScale)
                caret->FloatPos[1] = fpy;
                ZUIBoxSetColorArr(caret, ctx->Theme.TextDefault);
                caret->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }

            // Text label (cursor no longer embedded)
            uint32_t dlen = (uint32_t) Helpers::secure_strlen(buf);
            field->Label  = ZUIPushStr(&ctx->FrameArena, buf, dlen);
        }
        else
        {
            SetBdrArr(field, ctx->Theme.InputBorder);
            if (ps)
                ps->SelectStart = -1;
            uint32_t dlen = (uint32_t) Helpers::secure_strlen(buf);
            field->Label  = ZUIPushStr(&ctx->FrameArena, buf, dlen);
        }

        StyleField(ctx, field, is_focused);
        ZUISignal sig = ZUISignalFromBox(ctx, field);
        ZUIPopBox(ctx);

        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = field->Key;
            // Cursor position already set by the press handler in the focused block.
            // Only update here on first-click-to-focus (field was not focused yet).
            if (!is_focused && ps)
            {
                if (font)
                {
                    float field_x = (ps->ScreenMinX > 0.f) ? ps->ScreenMinX + ZUIGetFramePadX(ctx) : 0.f;
                    float offset  = ctx->MousePos[0] - field_x;
                    float cum     = 0.f;
                    int   cp      = (int) ZEngine::Helpers::secure_strlen(buf);
                    for (uint32_t i = 0; i < (uint32_t) cp; ++i)
                    {
                        float ts[2] = {};
                        ZUIMeasureText(font, buf + i, 1, ts);
                        if (cum + ts[0] * 0.5f > offset)
                        {
                            cp = (int) i;
                            break;
                        }
                        cum += ts[0];
                    }
                    ps->UserData = (float) cp;
                }
                else
                    ps->UserData = (float) ZEngine::Helpers::secure_strlen(buf);
                ps->SelectStart = -1;
            }
        }

        return changed;
    }

    // ZUIResizeHandle

    bool ZUIResizeHandle(ZUIContext* ctx, const char* key, float* value, float min_v, float max_v, bool horizontal)
    {
        uint32_t len = (uint32_t) ZEngine::Helpers::secure_strlen(key);
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
            float delta  = horizontal ? sig.DragDelta[1] : sig.DragDelta[0];
            *value      += delta;
            if (*value < min_v)
                *value = min_v;
            if (*value > max_v)
                *value = max_v;
            dragging = (delta != 0.f);
        }
        return dragging;
    }

    // Plot widgets

    static void PlotSetup(ZUIContext* ctx, const char* key, const float* values, int count, float v_min, float v_max, ZUIBoxFlags draw_flag, ZUISize w, ZUISize h)
    {
        if (!values || count <= 0)
            return;

        // Auto-scale
        if (v_min >= 3.0e+38f || v_max >= 3.0e+38f)
        {
            v_min = values[0];
            v_max = values[0];
            for (int i = 1; i < count; ++i)
            {
                if (values[i] < v_min)
                    v_min = values[i];
                if (values[i] > v_max)
                    v_max = values[i];
            }
            if (v_min == v_max)
            {
                v_min -= 1.f;
                v_max += 1.f;
            }
        }

        // Copy values to FrameArena so they survive until PreparePayload
        float* data = ZPushArray(&ctx->FrameArena, float, (uint32_t) count);
        for (int i = 0; i < count; ++i)
            data[i] = values[i];

        uint32_t klen   = (uint32_t) ZEngine::Helpers::secure_strlen(key);
        ZUIBox*  box    = ZUIPushBox(ctx, key, klen, ZUI_DrawBackground | draw_flag);
        box->Size[0]    = w;
        box->Size[1]    = h;
        // Repurpose Label for data pointer + count (no DrawText flag, so text path skipped)
        box->Label.Ptr  = (const char*) data;
        box->Label.Len  = (uint32_t) count;
        // Store range in Padding (normally {left,top,right,bottom} but unused for plots)
        box->Padding[0] = v_min;
        box->Padding[2] = v_max;
        SetBgArr(box, ctx->Theme.InputBg);
        box->EdgeSoftness = 0.f;
        ZUIPopBox(ctx);
    }

    void ZUIPlotLines(ZUIContext* ctx, const char* key, const float* values, int count, float v_scale_min, float v_scale_max, const char* /*overlay_text*/, ZUISize w, ZUISize h)
    {
        PlotSetup(ctx, key, values, count, v_scale_min, v_scale_max, ZUI_DrawPlotLines, w, h);
    }

    void ZUIPlotHistogram(ZUIContext* ctx, const char* key, const float* values, int count, float v_scale_min, float v_scale_max, const char* /*overlay_text*/, ZUISize w, ZUISize h)
    {
        PlotSetup(ctx, key, values, count, v_scale_min, v_scale_max, ZUI_DrawPlotBars, w, h);
    }

    // ZUIGridView

    ZUIBox* ZUIBeginGridView(ZUIContext* ctx, const char* key, float item_w, float item_h, ZUISize w, ZUISize h)
    {
        ctx->GV_ItemW = item_w;
        ctx->GV_ItemH = item_h;
        // items_per_row from container width approximation (ScreenW / item_w)
        // This stabilises after frame 0; panels typically fill most of ScreenW.
        int max_c     = (item_w > 0.f) ? (int) ((float) ctx->ScreenW / item_w) : 1;
        if (max_c < 1)
            max_c = 1;
        ctx->GV_MaxCols = max_c;
        ctx->GV_CurCol  = 0;
        ctx->GV_CurRow  = 0;
        ctx->GV_RowOpen = false;
        auto* tree      = ZUIBeginScrollRegion(ctx, key, w, h);
        tree->Flags     = tree->Flags | ZUI_NavList;
        return tree;
    }

    bool ZUIGridViewNextItem(ZUIContext* ctx, const char* item_key, bool selected)
    {
        // Start a new row when needed
        if (!ctx->GV_RowOpen || ctx->GV_CurCol >= ctx->GV_MaxCols)
        {
            if (ctx->GV_RowOpen)
            {
                ZUIEndRow(ctx);
            }
            char rk[72] = {};
            snprintf(rk, sizeof(rk), "##gvrow_%s_%d", item_key, ctx->GV_CurRow);
            ZUIBeginRow(ctx, rk, ZFill(), ZPx(ctx->GV_ItemH));
            ctx->GV_RowOpen = true;
            ctx->GV_CurCol  = 0;
            ctx->GV_CurRow++;
        }

        // Cell box
        char ck[128] = {};
        snprintf(ck, sizeof(ck), "##gvcell_%s_%d_%d", item_key, ctx->GV_CurRow, ctx->GV_CurCol);
        ZUIBox* cell     = ZUIPushBox(ctx, ck, (uint32_t) ZEngine::Helpers::secure_strlen(ck), ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_KeyboardActivate);
        cell->Size[0]    = ZPx(ctx->GV_ItemW);
        cell->Size[1]    = ZPx(ctx->GV_ItemH);
        cell->LayoutAxis = ZUIAxis::Y;

        float cell_bg[4] = {ctx->Theme.PanelBgAlt[0], ctx->Theme.PanelBgAlt[1], ctx->Theme.PanelBgAlt[2], 0.8f};
        if (selected)
            ZUIBoxSetColorArr(cell, ctx->Theme.RowSelectedBg);
        else
            ZUIBoxSetColorArr(cell, cell_bg);

        cell->BorderColor[0]  = ctx->Theme.TabActiveBorder[0];
        cell->BorderColor[1]  = ctx->Theme.TabActiveBorder[1];
        cell->BorderColor[2]  = ctx->Theme.TabActiveBorder[2];
        cell->BorderColor[3]  = 0.2f;
        cell->BorderThickness = 1.f;
        ZUIBoxSetCornerRadius(cell, 4.f);
        cell->EdgeSoftness = 0.f;

        ZUISignal sig      = ZUISignalFromBox(ctx, cell);
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
        if (ctx->GV_RowOpen)
        {
            ZUIEndRow(ctx);
            ctx->GV_RowOpen = false;
        }
        ZUIEndScrollRegion(ctx);
        ctx->GV_ItemW = 0.f;
        ctx->GV_ItemH = 0.f;
    }

    // ZUITreeView

    // Each node's open state is stored as ZUIPersistentState.UserData
    // (1.0=open, 0.0=closed), keyed by the node label hash.

    ZUIBox* ZUIBeginTreeView(ZUIContext* ctx, const char* key, ZUISize w, ZUISize h, const ZUITreeViewConfig* cfg)
    {
        if (cfg)
        {
            ctx->TV_RowH     = cfg->RowH;
            ctx->TV_IndentPx = cfg->IndentPx;
        }
        else
        {
            ctx->TV_RowH     = ZUIGetFrameHeight(ctx);   // ImGui GetFrameHeight
            ctx->TV_IndentPx = ctx->Style.IndentSpacing; // ImGui IndentSpacing
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
    static ZUISignal TV_BuildRow(ZUIContext* ctx, const char* label, bool selected, bool has_arrow, bool is_open, const float icon_col[4])
    {
        const float row_h   = ctx->TV_RowH;
        const float indent  = (float) ctx->TV_Depth * ctx->TV_IndentPx;
        const float arrow_w = ctx->Style.FontSize + 1.f; // FontSize + 1px (ImGui: g.FontSize)
        const float icon_sz = ctx->Style.FontSize - 1.f;
        const float kPadL   = ctx->Style.FramePadding[0]; // ImGui FramePadding.x

        char        rk[128] = {};
        snprintf(rk, sizeof(rk), "##tvrow_%d_%s", ctx->TV_Depth, label);

        ZUIBox* row                 = ZUIPushBox(ctx, rk, (uint32_t) ZEngine::Helpers::secure_strlen(rk), ZUI_DrawBackground | ZUI_Clickable);
        row->Size[0]                = ZFill();
        row->Size[1]                = ZPx(row_h);
        row->Padding[0]             = kPadL; // ImGui: FramePadding.x left margin before indent
        row->LayoutAxis             = ZUIAxis::X;

        // Background: transparent at rest, selection highlight on hover/select
        // ImGui: HeaderHovered at 80% opacity, Header (selected) at 31%
        static const float kRest[4] = {0.f, 0.f, 0.f, 0.f};
        ZUIBoxSetColorArr(row, selected ? ctx->Theme.RowSelectedBg : kRest);

        // Depth indent
        if (indent > 0.f)
        {
            char sk[48] = {};
            snprintf(sk, sizeof(sk), "##tvsp_%d_%s", ctx->TV_Depth, label);
            ZUIBox* sp  = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_None);
            sp->Size[0] = ZPx(indent);
            sp->Size[1] = ZPx(row_h);
            ZUIPopBox(ctx);
        }

        // Disclosure arrow (ImGuiCol_Text color) or blank spacer for leaves
        if (has_arrow)
        {
            char ak[128] = {};
            snprintf(ak, sizeof(ak), "##tvarr_%d_%s", ctx->TV_Depth, label);
            ZUIBox* ab  = ZUIPushBox(ctx, ak, (uint32_t) ZEngine::Helpers::secure_strlen(ak), ZUI_DrawTriArrow);
            ab->Size[0] = ZPx(arrow_w);
            ab->Size[1] = ZPx(row_h);
            // ImGui always uses ImGuiCol_Text for the arrow — never dimmed
            SetTextColor(ab, ctx->Theme.TextDefault);
            {
                auto* ps = ZUIStateGetOrInsert(&ctx->StateStore, ab->Key);
                if (ps)
                    ps->UserData = is_open ? 2.f : 3.f; // chevron: ∨ expanded, › collapsed
            }
            ZUIPopBox(ctx);
        }
        else
        {
            char lsk[48] = {};
            snprintf(lsk, sizeof(lsk), "##tvlsp_%d_%s", ctx->TV_Depth, label);
            ZUIBox* lsp  = ZUIPushBox(ctx, lsk, (uint32_t) ZEngine::Helpers::secure_strlen(lsk), ZUI_None);
            lsp->Size[0] = ZPx(arrow_w);
            lsp->Size[1] = ZPx(row_h);
            ZUIPopBox(ctx);
        }

        // Icon dot (engine-specific — ImGui has no icon; gap = ItemInnerSpacing.x = 4px)
        if (icon_col)
        {
            char ik[64] = {};
            snprintf(ik, sizeof(ik), "##tvic_%d_%s", ctx->TV_Depth, label);
            ZUIBox* ic  = ZUIPushBox(ctx, ik, (uint32_t) ZEngine::Helpers::secure_strlen(ik), ZUI_DrawBackground);
            ic->Size[0] = ZPx(icon_sz);
            ic->Size[1] = ZPx(icon_sz);
            ZUIBoxSetColorArr(ic, icon_col);
            ZUIBoxSetCornerRadius(ic, icon_sz * 0.5f);
            ic->EdgeSoftness = 0.5f;
            ZUIPopBox(ctx);

            char gk[64] = {};
            snprintf(gk, sizeof(gk), "##tvgap_%d_%s", ctx->TV_Depth, label);
            ZUIBox* gap  = ZUIPushBox(ctx, gk, (uint32_t) ZEngine::Helpers::secure_strlen(gk), ZUI_None);
            gap->Size[0] = ZPx(ctx->Style.ItemInnerSpacing[0]);
            gap->Size[1] = ZPx(row_h); // ItemInnerSpacing.x
            ZUIPopBox(ctx);
        }

        // Label — ImGui always renders with ImGuiCol_Text regardless of selection
        uint32_t llen = (uint32_t) ZEngine::Helpers::secure_strlen(label);
        ZUIBox*  lbox = ZUIPushBox(ctx, label, llen, ZUI_DrawText);
        lbox->Size[0] = ZText();
        lbox->Size[1] = ZPx(row_h);
        SetTextColor(lbox, ctx->Theme.TextDefault); // ImGui: always ImGuiCol_Text
        ZUIPopBox(ctx);

        bool      is_focused = (ctx->FocusKey == row->Key);

        ZUISignal sig        = ZUISignalFromBox(ctx, row);
        if (sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = row->Key;
        }

        // Hover/active: ImGui uses HeaderHovered (strong) not subtle RowHoverBg
        if (!selected)
            ApplyHotActive(
                row,
                ctx,
                kRest,
                ctx->Theme.HeaderHoveredBg, // ImGui ImGuiCol_HeaderHovered
                ctx->Theme.HeaderActiveBg); // ImGui ImGuiCol_HeaderActive

        if (is_focused && (ctx->SpacePressed || ctx->EnterPressed))
            sig.Flags = sig.Flags | ZUI_SignalClicked;

        ZUIPopBox(ctx);
        return sig;
    }

    bool ZUITreeViewBeginNode(ZUIContext* ctx, const char* label, bool selected, const float icon_col[4], bool initial_open)
    {
        uint64_t hash  = ZUIHashStr(label, (uint32_t) ZEngine::Helpers::secure_strlen(label)) ^ (uint64_t) ctx->TV_Depth;
        auto*    state = ZUIStateGetOrInsert(&ctx->StateStore, hash);
        // UserData < 0 means never explicitly set — apply initial_open on first use
        if (state && state->UserData < 0.f)
            state->UserData = initial_open ? 1.f : 0.f;
        bool      is_open = state && state->UserData > 0.5f;

        ZUISignal sig     = TV_BuildRow(ctx, label, selected, true, is_open, icon_col);

        if (sig.Flags & ZUI_SignalClicked)
        {
            is_open = !is_open;
            if (state)
                state->UserData = is_open ? 1.f : 0.f;
        }
        // Arrow Right opens a closed node; Arrow Left closes an open one (when row has focus)
        // The row key matches what TV_BuildRow built: "##tvrow_<depth>_<label>"
        {
            char tvk[128] = {};
            snprintf(tvk, sizeof(tvk), "##tvrow_%d_%s", ctx->TV_Depth, label);
            if (ctx->FocusKey == ZUIHashStr(tvk, (uint32_t) ZEngine::Helpers::secure_strlen(tvk)))
            {
                if (ctx->ArrowRightPressed && !is_open)
                {
                    is_open = true;
                    if (state)
                        state->UserData = 1.f;
                }
                if (ctx->ArrowLeftPressed && is_open)
                {
                    is_open = false;
                    if (state)
                        state->UserData = 0.f;
                }
            }
        }

        if (is_open)
        {
            ctx->TV_Depth++;
        }
        return is_open;
    }

    void ZUITreeViewEndNode(ZUIContext* ctx)
    {
        if (ctx->TV_Depth > 0)
            ctx->TV_Depth--;
    }

    bool ZUITreeViewLeaf(ZUIContext* ctx, const char* label, bool selected, const float icon_col[4])
    {
        ZUISignal sig = TV_BuildRow(ctx, label, selected, false, false, icon_col);
        return (sig.Flags & ZUI_SignalClicked) != 0;
    }

    // ZUIDataTable

    // Persistent state layout for a data table:
    //   slot key = DT_Key ^ (col * 2654435761ULL)  → UserData = column width
    //   slot key = DT_Key ^ 0xBAADF00DULL           → UserData = sort encoding

    static constexpr uint64_t kDT_SortSuffix = 0xBAADF00DULL;
    static constexpr float    kDT_ColDefault = 100.f; // default logical width
    static constexpr float    kDT_HeaderH    = 24.f;  // logical header row height
    static constexpr float    kDT_RowH       = 20.f;  // logical data row height
    static constexpr float    kDT_ResizeW    = 4.f;   // resize grip logical width

    static uint64_t           DT_ColKey(uint64_t table_key, int col)
    {
        return table_key ^ ((uint64_t) col * 2654435761ULL);
    }

    bool ZUIBeginDataTable(ZUIContext* ctx, const char* key, int col_count, const ZUIDataTableColumn* cols, ZUISize h, uint32_t flags)
    {
        ctx->DT_Key                             = ZUIHashStr(key, (uint32_t) ZEngine::Helpers::secure_strlen(key));
        ctx->DT_ColCount                        = col_count;
        ctx->DT_CurCol                          = -1;
        ctx->DT_RowIndex                        = 0;
        ctx->DT_InRow                           = false;
        ctx->DT_RowBox                          = nullptr;
        ctx->DT_SortChanged                     = false;

        // Load column widths — proportionally rescale when the panel is resized.
        // InitWidth values re-derive from panel width (pw) each frame, so their
        // sum changes when the panel resizes. Compare with the last stored total
        // and scale all stored widths by the same ratio to keep user resize ratios.
        static constexpr uint64_t kTotalWSuffix = 0x544F54574944ULL;
        ctx->DT_ColWidths                       = ZPushArray(&ctx->FrameArena, float, col_count);

        float total_init                        = 0.f;
        for (int i = 0; i < col_count; ++i)
            total_init += (cols && cols[i].InitWidth > 0.f) ? cols[i].InitWidth : kDT_ColDefault;

        // Proportional resize only when ZUIDataTableFlags_ProportionalResize is set.
        // Default: columns keep their stored pixel widths regardless of panel resize.
        bool  prop_resize = (flags & ZUIDataTableFlags_ProportionalResize) != 0;

        auto* total_s     = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_Key ^ kTotalWSuffix);
        float prev_total  = (total_s && total_s->UserData > 1.f) ? total_s->UserData : -1.f;
        float scale       = (prop_resize && prev_total > 1.f && total_init > 1.f && fabsf(total_init - prev_total) > 1.f) ? total_init / prev_total : 1.f;

        // HOT PATH — runs every frame, no heap allocation allowed.
        for (int i = 0; i < col_count; ++i)
        {
            auto* s    = ZUIStateGetOrInsert(&ctx->StateStore, DT_ColKey(ctx->DT_Key, i));
            float init = (cols && cols[i].InitWidth > 0.f) ? cols[i].InitWidth : kDT_ColDefault;
            if (s && s->UserData > 1.f)
            {
                float w = s->UserData * scale;
                w       = fmaxf(w, 30.f);
                if (prop_resize)
                    s->UserData = w; // only persist the scaled value if proportional
                ctx->DT_ColWidths[i] = w;
            }
            else
            {
                ctx->DT_ColWidths[i] = init;
                if (s)
                    s->UserData = init;
            }
        }
        if (prop_resize && total_s)
            total_s->UserData = total_init;

        // Load sort state
        {
            auto* ss = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_Key ^ kDT_SortSuffix);
            if (ss && ss->UserData != 0.f && ss->UserData > -0.5f) // skip -1 sentinel
            {
                float enc       = ss->UserData;
                ctx->DT_SortAsc = (enc > 0.f);
                ctx->DT_SortCol = (int) (enc > 0.f ? enc : -enc) - 1;
            }
            else
            {
                ctx->DT_SortCol = -1;
                ctx->DT_SortAsc = true;
            }
        }

        // Outer container — ClipChildren so the 9999px separators don't bleed out
        ZUIBox* outer_col       = ZUIBeginColumn(ctx, key, ZFill(), h);
        outer_col->Flags        = outer_col->Flags | ZUI_ClipChildren;
        outer_col->EdgeSoftness = 0.f;

        // Vertical separators are created in ZUIEndDataTable once column widths
        // are finalized — this ensures sash drag and caller overrides are reflected.
        ctx->DT_SepBoxes        = nullptr;

        // Store cols in FrameArena for HeadersRow
        if (cols)
        {
            auto* copy = ZPushArray(&ctx->FrameArena, ZUIDataTableColumn, col_count);
            for (int i = 0; i < col_count; ++i)
                copy[i] = cols[i];
            ctx->DT_Cols = copy;
        }
        else
        {
            ctx->DT_Cols = nullptr;
        }

        return true;
    }

    void ZUIDataTableHeadersRow(ZUIContext* ctx)
    {
        const ZUIDataTableColumn* cols     = (const ZUIDataTableColumn*) ctx->DT_Cols;
        float                     header_h = kDT_HeaderH;
        float                     resize_w = kDT_ResizeW;

        char                      hk[48]   = {};
        snprintf(hk, sizeof(hk), "##dthdr_%llu", (unsigned long long) ctx->DT_Key);
        ZUIBox* hrow = ZUIBeginRow(ctx, hk, ZFill(), ZPx(header_h));
        hrow->Flags  = hrow->Flags | ZUI_DrawBackground;
        ZUIBoxSetColorArr(hrow, ctx->Theme.TableHeaderBg);
        hrow->EdgeSoftness = 0.f;

        for (int i = 0; i < ctx->DT_ColCount; ++i)
        {
            float cw        = ctx->DT_ColWidths[i];
            bool  sortable  = cols && cols[i].Sortable;
            bool  resizable = cols && cols[i].Resizable;

            // Outer header cell box (clickable when sortable)
            char  ck[56]    = {};
            snprintf(ck, sizeof(ck), "##dthc_%llu_%d", (unsigned long long) ctx->DT_Key, i);
            ZUIBoxFlags cell_flags = ZUI_DrawBackground;
            if (sortable)
                cell_flags = cell_flags | ZUI_Clickable | ZUI_KeyboardActivate;
            ZUIBox* cell     = ZUIPushBox(ctx, ck, (uint32_t) ZEngine::Helpers::secure_strlen(ck), cell_flags);
            cell->Size[0]    = ZPx(cw - (resizable ? resize_w : 0.f));
            cell->Size[1]    = ZPx(header_h);
            cell->LayoutAxis = ZUIAxis::X;

            // Hover tint on sortable headers; no per-cell border (separators drawn at table level)
            bool cell_hot    = (ctx->HotKey == cell->Key);
            ZUIBoxSetColorArr(cell, (cell_hot && sortable) ? ctx->Theme.TableBorderLight : ctx->Theme.TableHeaderBg);
            cell->EdgeSoftness = 0.f;

            // Left padding
            char sp1k[32]      = {};
            snprintf(sp1k, sizeof(sp1k), "##thsp1_%d", i);
            ZUIBox* sp1  = ZUIPushBox(ctx, sp1k, (uint32_t) ZEngine::Helpers::secure_strlen(sp1k), ZUI_None);
            sp1->Size[0] = ZPx(ctx->Style.CellPadding[0]);
            sp1->Size[1] = ZPx(header_h);
            ZUIPopBox(ctx);

            // Column label
            const char* lbl         = (cols && cols[i].Label) ? cols[i].Label : "?";
            bool        is_sort_col = (ctx->DT_SortCol == i);
            uint32_t    lblen       = (uint32_t) ZEngine::Helpers::secure_strlen(lbl);
            ZUIBox*     lbox        = ZUIPushBox(ctx, lbl, lblen, ZUI_DrawText);
            lbox->Size[0]           = ZText();
            lbox->Size[1]           = ZPx(header_h);
            const float* lc         = is_sort_col ? ctx->Theme.TextDefault : ctx->Theme.TextDim;
            lbox->TextColor[0]      = lc[0];
            lbox->TextColor[1]      = lc[1];
            lbox->TextColor[2]      = lc[2];
            lbox->TextColor[3]      = lc[3];
            ZUIPopBox(ctx);

            // Sort direction indicator
            if (is_sort_col && sortable)
            {
                char sk[32] = {};
                snprintf(sk, sizeof(sk), "##ths_%d", i);
                const char* arrow = ctx->DT_SortAsc ? " ^" : " v";
                ZUIBox*     arr   = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawText);
                arr->Size[0]      = ZPx(14.f);
                arr->Size[1]      = ZPx(header_h);
                arr->Label        = ZUIPushStr(&ctx->FrameArena, arrow, (uint32_t) ZEngine::Helpers::secure_strlen(arrow));
                float ac[4]       = {ctx->Theme.TabActiveBorder[0], ctx->Theme.TabActiveBorder[1], ctx->Theme.TabActiveBorder[2], 1.f};
                arr->TextColor[0] = ac[0];
                arr->TextColor[1] = ac[1];
                arr->TextColor[2] = ac[2];
                arr->TextColor[3] = ac[3];
                ZUIPopBox(ctx);
            }

            ZUISignal cell_sig = ZUISignalFromBox(ctx, cell);
            ZUIPopBox(ctx); // cell

            // Sort on click
            if (sortable && (cell_sig.Flags & ZUI_SignalClicked))
            {
                if (ctx->DT_SortCol == i)
                {
                    ctx->DT_SortAsc = !ctx->DT_SortAsc;
                }
                else
                {
                    ctx->DT_SortCol = i;
                    ctx->DT_SortAsc = true;
                }
                ctx->DT_SortChanged = true;
                // Persist sort state
                float enc           = (float) (ctx->DT_SortCol + 1) * (ctx->DT_SortAsc ? 1.f : -1.f);
                auto* ss            = ZUIStateGetOrInsert(&ctx->StateStore, ctx->DT_Key ^ kDT_SortSuffix);
                if (ss)
                    ss->UserData = enc;
            }

            // Column resize grip (right edge of header cell)
            if (resizable)
            {
                char rk[56] = {};
                snprintf(rk, sizeof(rk), "##dtresize_%llu_%d", (unsigned long long) ctx->DT_Key, i);
                ZUIBox* grip   = ZUIPushBox(ctx, rk, (uint32_t) ZEngine::Helpers::secure_strlen(rk), ZUI_DrawBackground | ZUI_Clickable | ZUI_NoKeyboardFocus);
                grip->Size[0]  = ZPx(resize_w);
                grip->Size[1]  = ZPx(header_h);
                bool  grip_hot = (ctx->HotKey == grip->Key);
                float gc[4]    = {0.35f, 0.35f, 0.40f, grip_hot ? 0.9f : 0.3f};
                ZUIBoxSetColorArr(grip, gc);
                grip->EdgeSoftness = 0.f;
                ZUISignal gsig     = ZUISignalFromBox(ctx, grip);
                ZUIPopBox(ctx);

                // Drag to resize — respect MinWidth of this column and prevent
                // other columns from being compressed below their own minimums.
                if ((gsig.Flags & ZUI_SignalHeld) && gsig.DragDelta[0] != 0.f)
                {
                    const ZUIDataTableColumn* dt_cols = static_cast<const ZUIDataTableColumn*>(ctx->DT_Cols);
                    float                     col_min = dt_cols ? fmaxf(dt_cols[i].MinWidth, 30.f) : 30.f;
                    float                     new_w   = ctx->DT_ColWidths[i] + gsig.DragDelta[0];
                    new_w                             = fmaxf(new_w, col_min);
                    // Upper bound: total width - sum of other columns' minimums
                    if (dt_cols && ctx->DT_ColCount > 1)
                    {
                        float others_min = 0.f;
                        for (int j = 0; j < ctx->DT_ColCount; ++j)
                            if (j != i)
                                others_min += fmaxf(dt_cols[j].MinWidth, 30.f);
                        float total = 0.f;
                        for (int j = 0; j < ctx->DT_ColCount; ++j)
                            total += ctx->DT_ColWidths[j];
                        float max_w = total - others_min;
                        if (max_w > col_min)
                            new_w = fminf(new_w, max_w);
                    }
                    ctx->DT_ColWidths[i] = new_w;
                    auto* cs             = ZUIStateGetOrInsert(&ctx->StateStore, DT_ColKey(ctx->DT_Key, i));
                    if (cs)
                        cs->UserData = new_w;
                }
            }
        }

        ZUIEndRow(ctx);

        // Single full-width bottom border below the header row (replaces per-cell ZUI_DrawBorder)
        {
            char bk[48] = {};
            snprintf(bk, sizeof(bk), "##dthdrborder_%llu", (unsigned long long) ctx->DT_Key);
            ZUIBox* bsep  = ZUIPushBox(ctx, bk, (uint32_t) ZEngine::Helpers::secure_strlen(bk), ZUI_DrawBackground);
            bsep->Size[0] = ZFill();
            bsep->Size[1] = ZPx(1.f);
            ZUIBoxSetColorArr(bsep, ctx->Theme.TableBorderStrong);
            bsep->EdgeSoftness = 0.f;
            ZUIPopBox(ctx);
        }
    }

    float ZUIDataTableRowHeight()
    {
        return kDT_RowH;
    }

    uint64_t ZUIDataTableRowKey(const ZUIContext* ctx, uint64_t identity)
    {
        char key[80] = {};
        if (identity)
            snprintf(key, sizeof(key), "##dtrowid_%llu_%llu", static_cast<unsigned long long>(ctx->DT_Key), static_cast<unsigned long long>(identity));
        else
            snprintf(key, sizeof(key), "##dtrow_%llu_%d", static_cast<unsigned long long>(ctx->DT_Key), ctx->DT_RowIndex);
        return ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
    }

    void ZUIDataTableSkipRows(ZUIContext* ctx, uint32_t count)
    {
        if (!count || !ctx->DT_ColCount)
            return;
        ZENGINE_VALIDATE_ASSERT(ctx->DT_RowIndex >= 0 && count <= uint32_t(std::numeric_limits<int>::max() - ctx->DT_RowIndex), "Data table row count overflow")
        if (ctx->DT_InRow)
        {
            if (ctx->DT_CurCol >= 0)
                ZUIEndColumn(ctx);
            ZUIEndRow(ctx);
        }
        ctx->DT_InRow   = false;
        ctx->DT_CurCol  = -1;
        ctx->DT_RowBox  = nullptr;
        auto* spacer    = ZUIPushBox(ctx, "##dt_virtual_rows", 17, ZUI_None);
        spacer->Size[0] = ZFill();
        spacer->Size[1] = ZPx(kDT_RowH * float(count));
        ZUIPopBox(ctx);
        ctx->DT_RowIndex += int(count);
    }

    bool ZUIDataTableNextRow(ZUIContext* ctx, bool selected, uint64_t identity)
    {
        float row_h = kDT_RowH;

        // Close previous row if open
        if (ctx->DT_InRow)
        {
            if (ctx->DT_CurCol >= 0)
            {
                ZUIEndColumn(ctx);
                ctx->DT_CurCol = -1;
            }
            ZUIEndRow(ctx);
            ctx->DT_InRow = false;
        }

        // Alternating row background
        bool  alt   = (ctx->DT_RowIndex % 2) == 1;
        float bg[4] = {};
        if (selected)
        {
            bg[0] = ctx->Theme.RowSelectedBg[0];
            bg[1] = ctx->Theme.RowSelectedBg[1];
            bg[2] = ctx->Theme.RowSelectedBg[2];
            bg[3] = ctx->Theme.RowSelectedBg[3];
        }
        else if (alt)
        {
            bg[0] = ctx->Theme.PanelBgAlt[0];
            bg[1] = ctx->Theme.PanelBgAlt[1];
            bg[2] = ctx->Theme.PanelBgAlt[2];
            bg[3] = 0.f; // Figma rows are flat
        }
        else
        {
            bg[0] = 0.f;
            bg[1] = 0.f;
            bg[2] = 0.f;
            bg[3] = 0.f;
        }

        char rk[80] = {};
        if (identity)
            snprintf(rk, sizeof(rk), "##dtrowid_%llu_%llu", (unsigned long long) ctx->DT_Key, (unsigned long long) identity);
        else
            snprintf(rk, sizeof(rk), "##dtrow_%llu_%d", (unsigned long long) ctx->DT_Key, ctx->DT_RowIndex);
        ZUIBox* row     = ZUIPushBox(ctx, rk, (uint32_t) ZEngine::Helpers::secure_strlen(rk), ZUI_DrawBackground | ZUI_Clickable | ZUI_KeyboardActivate);
        row->Size[0]    = ZFill();
        row->Size[1]    = ZPx(row_h);
        row->LayoutAxis = ZUIAxis::X;
        ZUIBoxSetColorArr(row, bg);
        row->EdgeSoftness = 0.f;

        ZUISignal rsig    = ZUISignalFromBox(ctx, row);
        // Lerped hover via HotT
        if (!selected)
        {
            static const float kDTRest[4] = {0.f, 0.f, 0.f, 0.f};
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
        if (!ctx->DT_InRow)
        {
            return;
        }
        // Close previous cell
        if (ctx->DT_CurCol >= 0)
        {
            ZUIEndColumn(ctx);
        }

        ctx->DT_CurCol = col;
        float cw       = (col < ctx->DT_ColCount && ctx->DT_ColWidths) ? ctx->DT_ColWidths[col] : ctx->Style.DataTableDefaultColumnW;

        char  ck[48]   = {};
        snprintf(ck, sizeof(ck), "##dtcell_%llu_%d_%d", (unsigned long long) ctx->DT_Key, ctx->DT_RowIndex, col);
        ZUIBox* cc     = ZUIBeginColumn(ctx, ck, ZPx(cw), ZFill());
        cc->Padding[0] = ctx->Style.CellPadding[0]; // horizontal left indent (matches header)
        // Vertical centering: top spacer = half the dead space above the text
        {
            float pad_y = fmaxf(0.f, (kDT_RowH - ZUIGetFrameHeight(ctx)) * 0.5f);
            if (pad_y > 0.5f)
                ZUISpacer(ctx, pad_y);
        }
    }

    void ZUIEndDataTable(ZUIContext* ctx)
    {
        // Close open cell + row
        if (ctx->DT_InRow)
        {
            if (ctx->DT_CurCol >= 0)
            {
                ZUIEndColumn(ctx);
                ctx->DT_CurCol = -1;
            }
            ZUIEndRow(ctx);
            ctx->DT_InRow = false;
        }
        // Create vertical separators now that column widths are final.
        // Created as floating children of the outer container so they span
        // its full height (clipped by ZUI_ClipChildren on the outer box).
        // HOT PATH — runs every frame, no heap allocation allowed.
        if (ctx->Style.DataTableColumnLines && ctx->DT_ColCount > 1 && ctx->DT_ColWidths)
        {
            float x = 0.f;
            for (int i = 0; i < ctx->DT_ColCount - 1; ++i)
            {
                x           += ctx->DT_ColWidths[i];
                char sk[48]  = {};
                snprintf(sk, sizeof(sk), "##dtvsep_%llu_%d", (unsigned long long) ctx->DT_Key, i);
                ZUIBox* sep      = ZUIPushBox(ctx, sk, (uint32_t) ZEngine::Helpers::secure_strlen(sk), ZUI_DrawBackground | ZUI_FloatX | ZUI_FloatY);
                sep->Size[0]     = ZPx(1.f);
                sep->Size[1]     = ZPct(1.f); // full logical height, including virtualized rows
                sep->FloatPos[0] = x;
                sep->FloatPos[1] = 0.f;
                ZUIBoxSetColorArr(sep, ctx->Theme.TableBorderStrong);
                sep->EdgeSoftness = 0.f;
                ZUIPopBox(ctx);
            }
        }
        ctx->DT_SepBoxes = nullptr;
        ZUIEndColumn(ctx); // outer container
        ctx->DT_ColCount = 0;
    }

    ZUITableSortSpec ZUIDataTableGetSortSpecs(ZUIContext* ctx)
    {
        ZUITableSortSpec spec = {};
        spec.ColumnIndex      = ctx->DT_SortCol;
        spec.Ascending        = ctx->DT_SortAsc;
        spec.Changed          = ctx->DT_SortChanged;
        ctx->DT_SortChanged   = false; // consume
        return spec;
    }

    ZUISignal ZUIDataTableRowSignal(ZUIContext* ctx)
    {
        if (!ctx->DT_RowBox)
        {
            return {};
        }
        return ZUISignalFromBox(ctx, ctx->DT_RowBox);
    }

    // ZUISearchBox

    bool ZUISearchBox(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, const char* placeholder, ZUISize w, bool clear_button)
    {
        if (!buf || buf_size == 0)
            return false;
        const auto field_key_hash = ZUIHashStr(key, static_cast<uint32_t>(Helpers::secure_strlen(key)));
        // Full bordered row: [Q icon] [editable text field]
        char       rk[32]         = {};
        snprintf(rk, sizeof(rk), "##sb_%016llx", static_cast<unsigned long long>(field_key_hash));
        // Figma "Search field": 25 px high, 8 px side padding, 13 px icon, 7 px gap, 1 px subtle border.
        ZUIBox* row = ZUIBeginRow(ctx, rk, w, ZPx(25.f));
        row->Flags  = row->Flags | ZUI_DrawBackground | ZUI_DrawBorder | ZUI_Clickable | ZUI_NoKeyboardFocus | ZUI_ClipChildren;
        StyleField(ctx, row);
        row->EdgeSoftness = 0.5f;
        row->Padding[0]   = 8.f;
        row->Padding[2]   = 8.f;
        row->Gap          = 7.f;
        row->CrossAlign   = ZUIAlign::Center;

        ZUIIconBox(ctx, ZUIIcon::Search, 13.f, ctx->Theme.TextDim);

        // Reuse the shared editor for cursor/selection/clipboard/undo behavior.
        // The surrounding search row supplies the border and all padding.
        bool is_focused = !ctx->Disabled && (ctx->FocusKey == field_key_hash);
        ZUIStylePushFloat(ctx, ZUIStyleVar_FramePaddingX, 0.f);
        ZUIStylePushFloat(ctx, ZUIStyleVar_FramePaddingY, 0.f);
        bool changed = ZUITextField(ctx, key, buf, buf_size, 0.f);
        ZUIStylePop(ctx);
        ZUIStylePop(ctx);
        auto* field  = row->LastChild;
        field->Flags = ZUIBoxFlags((field->Flags & ~ZUI_DrawBorder) | ZUI_ClipChildren | ZUI_TextNoInset);
        // Keep an explicit transparent background: removing DrawBackground
        // enables the renderer's generic hover fill for bare clickable boxes.
        // Zero alpha emits no geometry; the outer row owns the field surface.
        ZUIBoxSetColor(field, 0.f, 0.f, 0.f, 0.f);
        field->FocusRingTarget = row;
        field->Size[0]         = ZFill();
        field->Size[1]         = ZPx(13.f);
        auto* ps               = ZUIStateGetOrInsert(&ctx->StateStore, field_key_hash);
        if (!is_focused && !buf[0])
            field->Label = ZUIPushStr(&ctx->FrameArena, placeholder, static_cast<uint32_t>(Helpers::secure_strlen(placeholder)));
        static constexpr float kDisabled[4] = ZUI_TOKEN(Tokens::TextDisabled);
        if (ctx->Disabled)
        {
            SetTextColor(field, kDisabled);
        }
        else if (!is_focused && buf[0] == '\0')
        {
            SetTextColor(field, ctx->Theme.TextDim);
        }
        else
        {
            SetTextColor(field, ctx->Theme.TextDefault);
        }
        if (is_focused && ctx->KeyboardFocus)
        {
            SetBdrArr(row, ctx->Theme.InputFocusBorder);
        }
        else if (!ctx->Disabled && ctx->HotKey == field->Key)
        {
            static constexpr float kHover[4] = ZUI_TOKEN(Tokens::BorderStrong);
            SetBdrArr(row, kHover);
        }

        ZUISignal clear_sig = {};
        if (clear_button && buf[0])
        {
            char clear_key[96] = {};
            snprintf(clear_key, sizeof(clear_key), "##sb_clear_%016llx", static_cast<unsigned long long>(field_key_hash));
            auto* clear    = ZUIPushBox(ctx, clear_key, static_cast<uint32_t>(Helpers::secure_strlen(clear_key)), ZUI_DrawIcon | ZUI_Clickable | ZUI_KeyboardActivate);
            clear->Size[0] = clear->Size[1] = ZPx(13.f);
            clear->IconId                   = static_cast<uint16_t>(ZUIIcon::X);
            SetTextColor(clear, ctx->Disabled ? kDisabled : ctx->HotKey == clear->Key ? ctx->Theme.TextDefault : ctx->Theme.TextDim);
            clear_sig = ZUISignalFromBox(ctx, clear);
            ZUIPopBox(ctx);
            ZUISetTooltip(ctx, clear_sig, "Clear search");
        }
        const ZUISignal row_sig = ZUISignalFromBox(ctx, row);
        ZUIEndRow(ctx);

        if (clear_sig.Flags & ZUI_SignalClicked)
        {
            buf[0] = '\0';
            if (ps)
            {
                ps->UserData    = 0.f;
                ps->SelectStart = -1;
            }
            ctx->FocusKey        = field_key_hash;
            ctx->TextInputActive = true;
            field->Label         = {};
            field->FirstChild = field->LastChild = nullptr;
            if (ctx->UndoFieldKey == field_key_hash)
            {
                ctx->UndoFieldKey = 0;
                ctx->UndoTop = ctx->RedoTop = 0;
            }
            return true;
        }

        if (row_sig.Flags & ZUI_SignalClicked)
        {
            ctx->FocusKey = field_key_hash;
            if (ps)
                ps->UserData = (float) ZEngine::Helpers::secure_strlen(buf); // cursor at end on click
        }

        return changed;
    }

} // namespace ZEngine::UI
