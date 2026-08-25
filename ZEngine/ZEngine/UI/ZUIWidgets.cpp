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
        static constexpr float k_hdr_bg[4]   = {0.18f, 0.18f, 0.22f, 1.f};
        static constexpr float k_text[4]      = {0.90f, 0.90f, 0.90f, 1.f};
        static constexpr float k_drag_col[4]  = {0.45f, 0.45f, 0.55f, 1.f};

        // Build a unique header key from the title
        char hdr_key[64];
        snprintf(hdr_key, sizeof(hdr_key), "##pdh_%s", title);

        ZUIBox* hdr   = ZUIBeginRow(ctx, hdr_key, ZFill(), ZPx(22.f));
        hdr->Flags    = hdr->Flags | ZUI_DrawBackground | ZUI_Clickable;
        hdr->BgColor[0] = k_hdr_bg[0]; hdr->BgColor[1] = k_hdr_bg[1];
        hdr->BgColor[2] = k_hdr_bg[2]; hdr->BgColor[3] = k_hdr_bg[3];

        // Drag indicator ("= ") in dim colour
        ZUIBox* grip  = ZUIPushBox(ctx, "= ##grip", 8, ZUI_DrawText);
        grip->Size[0] = ZPx(18.f);
        grip->Size[1] = ZPx(22.f);
        grip->TextColor[0] = k_drag_col[0]; grip->TextColor[1] = k_drag_col[1];
        grip->TextColor[2] = k_drag_col[2]; grip->TextColor[3] = k_drag_col[3];
        ZUIPopBox(ctx);

        ZUILabel(ctx, title, k_text);

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
                                ZUI_DrawBackground | ZUI_DrawText | ZUI_Clickable);
        field->Size[0]    = ZPx(width_px);
        field->Size[1]    = ZPx(22.f);
        field->BgColor[0] = 0.18f; field->BgColor[1] = 0.18f;
        field->BgColor[2] = 0.22f; field->BgColor[3] = 1.f;
        SetTextColor(field, k_text_default);

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
        field->Size[1]      = ZPx(22.f);
        field->BgColor[0]   = 0.14f; field->BgColor[1] = 0.14f;
        field->BgColor[2]   = 0.18f; field->BgColor[3] = 1.f;
        field->BorderColor[0] = 0.35f; field->BorderColor[1] = 0.35f;
        field->BorderColor[2] = 0.45f; field->BorderColor[3] = 1.f;
        SetTextColor(field, k_text_default);

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
            // Brighter border when focused
            field->BorderColor[0] = 0.40f; field->BorderColor[1] = 0.65f;
            field->BorderColor[2] = 0.90f; field->BorderColor[3] = 1.f;
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
