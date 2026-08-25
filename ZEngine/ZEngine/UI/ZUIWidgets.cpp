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

    void ZUILabel(ZUIContext* ctx, const char* text, const float color[4])
    {
        const float* c   = color ? color : ctx->Theme.TextDefault;
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
                            ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable | ZUI_DrawBorder);
        box->Size[0]          = ZText();
        box->Size[1]          = ZPx(28.f);
        SetBgArr(box, ctx->Theme.ButtonBg);
        SetTextColor(box, ctx->Theme.TextDefault);
        SetBdrArr(box, ctx->Theme.ButtonBorder);
        box->BorderThickness  = 1.f;

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
