#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInput.h>

namespace ZEngine::UI
{
    void ZUIFeedBeginFrame(ZUIContext* ctx)
    {
        if (!ctx)
        {
            return;
        }
        ctx->MousePressed[0] = ctx->MousePressed[1] = ctx->MousePressed[2] = false;
        ctx->MouseReleased[0] = ctx->MouseReleased[1] = ctx->MouseReleased[2] = false;
        ctx->ScrollDelta                                                      = 0.f;
        ctx->TextInputLen                                                     = 0;
        ctx->TextInput[0]                                                     = '\0';
        ctx->BackspacePressed                                                 = false;
    }

    void ZUIFeedFocusLost(ZUIContext* ctx)
    {
        if (!ctx)
            return;
        for (uint32_t button = 0; button < 3; ++button)
        {
            ctx->MouseDown[button] = ctx->MousePressed[button] = ctx->MouseReleased[button] = false;
            ctx->MousePressAlt[button] = ctx->MousePressCtrl[button] = ctx->MousePressShift[button] = false;
            ctx->MouseReleaseCtrl[button] = ctx->MouseReleaseShift[button] = false;
        }
        // Retain serials so a pre-loss press cannot alias a later genuine press.
        // A platform's synthetic release after loss must not become a click/drop.
        ctx->PrevMousePos[0] = ctx->MousePos[0];
        ctx->PrevMousePos[1] = ctx->MousePos[1];
        ctx->ScrollDelta     = 0.f;
        ctx->HotKey = ctx->ActiveKey = 0;
        ctx->CtrlDown = ctx->ShiftDown = ctx->AltDown = false;
        ctx->BackspacePressed = ctx->BackspaceHeld = false;
        ctx->TabPressed = ctx->ShiftTabPressed = ctx->EscapePressed = ctx->EnterPressed = ctx->SpacePressed = false;
        ctx->ArrowUpPressed = ctx->ArrowDownPressed = ctx->ArrowLeftPressed = ctx->ArrowRightPressed = false;
        ctx->HomePressed = ctx->EndPressed = false;
        ctx->CtrlCPressed = ctx->CtrlXPressed = ctx->CtrlBackspacePressed = ctx->CtrlAPressed = ctx->CtrlZPressed = ctx->CtrlYPressed = false;
        ctx->DeletePressed = ctx->F2Pressed = false;
        ctx->ArrowLeftHeld = ctx->ArrowRightHeld = ctx->ArrowUpHeld = ctx->ArrowDownHeld = ctx->DeleteHeld = false;
        ctx->ArrowRepeatTimer = ctx->KeyRepeatTimer = 0.f;
        ctx->TextInputLen                           = 0;
        ctx->TextInput[0]                           = '\0';
        ctx->DragSourceKey = ctx->DragTargetKey = 0;
        ctx->DragDropFired                      = false;
        ctx->DragPayloadLen                     = 0;
        ctx->DragPayload[0]                     = '\0';
        ctx->DragPayloadKind                    = ZUIDragPayloadKind::Opaque;
    }

    void ZUIFeedMousePos(ZUIContext* ctx, float x, float y)
    {
        if (!ctx)
        {
            return;
        }
        ctx->MousePos[0] = x;
        ctx->MousePos[1] = y;
    }

    void ZUIFeedMouseButton(ZUIContext* ctx, int btn, bool pressed)
    {
        if (!ctx || btn < 0 || btn > 2 || ctx->MouseDown[btn] == pressed)
        {
            return;
        }
        if (pressed)
        {
            ctx->MousePressPos[btn][0] = ctx->MousePos[0];
            ctx->MousePressPos[btn][1] = ctx->MousePos[1];
            ctx->MousePressAlt[btn]    = ctx->AltDown;
            ctx->MousePressCtrl[btn]   = ctx->CtrlDown;
            ctx->MousePressShift[btn]  = ctx->ShiftDown;
            ++ctx->MousePressSerial[btn];
            ctx->MouseDown[btn]    = true;
            ctx->MousePressed[btn] = true;
        }
        else
        {
            ctx->MouseReleasePos[btn][0] = ctx->MousePos[0];
            ctx->MouseReleasePos[btn][1] = ctx->MousePos[1];
            ctx->MouseReleaseSerial[btn] = ctx->MousePressSerial[btn];
            ctx->MouseReleaseCtrl[btn]   = ctx->CtrlDown;
            ctx->MouseReleaseShift[btn]  = ctx->ShiftDown;
            ctx->MouseDown[btn]          = false;
            ctx->MouseReleased[btn]      = true;
        }
    }

    void ZUIFeedScroll(ZUIContext* ctx, float delta)
    {
        if (!ctx)
        {
            return;
        }
        ctx->ScrollDelta += delta;
    }

    void ZUIFeedText(ZUIContext* ctx, uint32_t codepoint)
    {
        if (!ctx)
        {
            return;
        }
        // Encode codepoint as UTF-8 into TextInput buffer
        if (codepoint < 0x80 && ctx->TextInputLen < 30)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char) codepoint;
        }
        else if (codepoint < 0x800 && ctx->TextInputLen < 29)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char) (0xC0 | (codepoint >> 6));
            ctx->TextInput[ctx->TextInputLen++] = (char) (0x80 | (codepoint & 0x3F));
        }
        else if (codepoint < 0x10000 && ctx->TextInputLen < 28)
        {
            ctx->TextInput[ctx->TextInputLen++] = (char) (0xE0 | (codepoint >> 12));
            ctx->TextInput[ctx->TextInputLen++] = (char) (0x80 | ((codepoint >> 6) & 0x3F));
            ctx->TextInput[ctx->TextInputLen++] = (char) (0x80 | (codepoint & 0x3F));
        }
        ctx->TextInput[ctx->TextInputLen] = '\0';
    }

    void ZUIFeedKey(ZUIContext* ctx, ZUIKey key, bool pressed, bool ctrl, bool shift, bool alt)
    {
        if (!ctx)
        {
            return;
        }

        ctx->CtrlDown  = ctrl;
        ctx->ShiftDown = shift;
        ctx->AltDown   = alt;

        if (key == ZUIKey::Backspace)
        {
            ctx->BackspaceHeld    = pressed;
            ctx->BackspacePressed = pressed;
            if (!pressed)
            {
                ctx->KeyRepeatTimer = 0.f;
            }
        }
        if (key == ZUIKey::F2 && pressed)
            ctx->F2Pressed = true;
    }

} // namespace ZEngine::UI
