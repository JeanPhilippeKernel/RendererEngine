#include <ZEngine/UI/ZUIInput.h>
#include <ZEngine/UI/ZUIContext.h>
#include <cstring>

namespace ZEngine::UI
{
    void ZUIFeedBeginFrame(ZUIContext* ctx)
    {
        if (!ctx) { return; }
        ctx->MousePressed[0]  = ctx->MousePressed[1]  = ctx->MousePressed[2]  = false;
        ctx->MouseReleased[0] = ctx->MouseReleased[1] = ctx->MouseReleased[2] = false;
        ctx->ScrollDelta      = 0.f;
        ctx->TextInputLen     = 0;
        ctx->TextInput[0]     = '\0';
        ctx->BackspacePressed = false;
    }

    void ZUIFeedMousePos(ZUIContext* ctx, float x, float y)
    {
        if (!ctx) { return; }
        ctx->MousePos[0] = x;
        ctx->MousePos[1] = y;
    }

    void ZUIFeedMouseButton(ZUIContext* ctx, int btn, bool pressed)
    {
        if (!ctx || btn < 0 || btn > 2) { return; }
        if (pressed)
        {
            ctx->MouseDown[btn]    = true;
            ctx->MousePressed[btn] = true;
        }
        else
        {
            ctx->MouseDown[btn]     = false;
            ctx->MouseReleased[btn] = true;
        }
    }

    void ZUIFeedScroll(ZUIContext* ctx, float delta)
    {
        if (!ctx) { return; }
        ctx->ScrollDelta += delta;
    }

    void ZUIFeedText(ZUIContext* ctx, uint32_t codepoint)
    {
        if (!ctx) { return; }
        // Encode codepoint as UTF-8 into TextInput buffer
        if (codepoint < 0x80 && ctx->TextInputLen < 30)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char)codepoint;
        }
        else if (codepoint < 0x800 && ctx->TextInputLen < 29)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char)(0xC0 | (codepoint >> 6));
            ctx->TextInput[ctx->TextInputLen++] = (char)(0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000 && ctx->TextInputLen < 28)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char)(0xE0 | (codepoint >> 12));
            ctx->TextInput[ctx->TextInputLen++] = (char)(0x80 | ((codepoint >> 6) & 0x3F));
            ctx->TextInput[ctx->TextInputLen++] = (char)(0x80 | (codepoint & 0x3F));
        }
        ctx->TextInput[ctx->TextInputLen] = '\0';
    }

    void ZUIFeedKey(ZUIContext* ctx, ZUIKey key, bool pressed,
                    bool ctrl, bool shift, bool alt)
    {
        if (!ctx) { return; }

        ctx->CtrlDown  = ctrl;
        ctx->ShiftDown = shift;
        ctx->AltDown   = alt;

        if (key == ZUIKey::Backspace)
        {
            ctx->BackspaceHeld    = pressed;
            ctx->BackspacePressed = pressed;
            if (!pressed) { ctx->KeyRepeatTimer = 0.f; }
        }
    }

} // namespace ZEngine::UI
