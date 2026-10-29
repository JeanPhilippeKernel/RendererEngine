#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUILayout.h>
#include <cstring>

using namespace ZEngine::Core::Memory;

namespace ZEngine::UI
{
    void ZUIContextInit(ZUIContext* ctx, ArenaAllocator* parent, size_t FrameArenaBytes, size_t PersistentArenaBytes, uint32_t StateCapacity, uint32_t MaxBoxesPerFrame)
    {
        ZENGINE_VALIDATE_ASSERT(StateCapacity > 0 && (StateCapacity & (StateCapacity - 1)) == 0, "ZUIContextInit: StateCapacity must be a power of two — hash probing is bitmasked");

        parent->CreateSubArena(FrameArenaBytes, &ctx->FrameArena, "UIContext/Frame");
        parent->CreateSubArena(PersistentArenaBytes, &ctx->PersistentArena, "UIContext/Persistent");

        ctx->StateStore.Slots    = ZPushArray(&ctx->PersistentArena, ZUIPersistentSlot, StateCapacity);
        ctx->StateStore.Capacity = StateCapacity;
        ctx->StateStore.Count    = 0;
        ctx->MaxBoxesPerFrame    = MaxBoxesPerFrame;
    }

    void ZUIContextDestroy(ZUIContext* ctx)
    {
        ctx->FrameArena.Shutdown();
        ctx->PersistentArena.Shutdown();
    }

    void ZUIBeginFrame(ZUIContext* ctx, float dt)
    {
        ++ctx->FrameIndex;
        ZUIStyleUpdate(&ctx->Style); // recompute FrameHeight = FontSize + FramePadding.y*2
        ctx->FrameArena.Clear();
        ctx->Root                = nullptr;
        ctx->Current             = nullptr;
        ctx->ForegroundRoot      = nullptr;
        ctx->DeltaTime           = dt;
        ctx->Time               += dt;
        ctx->Cursor              = ZUICursor::Default;
        ctx->CursorPriority      = 0;
        // Rebuilt by text-editing widgets below. The application consumes the
        // prior frame's value before ZUIBeginFrame runs.
        ctx->TextInputActive     = false;
        // The application consumes the prior frame's viewport key before this point.
        ctx->ViewportInputKey    = 0;
        ctx->ViewportOverlayKey  = 0;
        ctx->ViewportSelectable  = false;
        ctx->ViewportEditable    = false;
        ctx->PopupBuildDepth     = 0; // reset render depth; rebuilt during each BuildUI pass
        // Clear stale Box* pointers — boxes are re-created each frame in FrameArena
        for (uint32_t i = 0; i < ctx->PopupStackSize; i++)
            ctx->PopupStack[i].Box = nullptr;
        ctx->ModalBox = nullptr; // re-set each frame by whoever owns the active modal
        if (ctx->MousePressed[0] || ctx->MousePressed[1] || ctx->MousePressed[2])
            ctx->KeyboardFocus = false;
        else if (ctx->TabPressed || ctx->ShiftTabPressed || ctx->ArrowUpPressed || ctx->ArrowDownPressed || ctx->ArrowLeftPressed || ctx->ArrowRightPressed || ctx->EnterPressed || ctx->SpacePressed)
            ctx->KeyboardFocus = true;
        // TextInputLen, BackspacePressed, MousePressed/Released, ScrollDelta are NOT
        // cleared here — GLFW events fire before BeginFrame (in window->PollEvent) and
        // must survive until ZUIEndFrame runs the interaction pass and widget logic.
    }

    void ZUIEndFrame(ZUIContext* ctx)
    {
        // HOT PATH — runs every frame, no heap allocation allowed.
        // Clear drop result from the previous frame before the interaction pass may set a new one
        ctx->DragDropFired       = false;
        ctx->DragTargetKey       = 0;
        // ViewportHovered is written fresh each BuildUI; reset it so a missing panel = false
        ctx->ViewportHovered     = false;

        // Builders may emit panel popups before later shell chrome. Portal
        // roots escape ancestor clipping but must also escape builder ordering.
        // Relink in O(1) per overlay, without a tree scan or frame allocation.
        const auto raise_overlay = [&](ZUIBox* box) {
            if (!box || !ctx->Root || box->Parent != ctx->Root || ctx->Root->LastChild == box)
                return;
            if (box->PrevSib)
                box->PrevSib->NextSib = box->NextSib;
            else
                ctx->Root->FirstChild = box->NextSib;
            if (box->NextSib)
                box->NextSib->PrevSib = box->PrevSib;
            box->PrevSib                  = ctx->Root->LastChild;
            box->NextSib                  = nullptr;
            ctx->Root->LastChild->NextSib = box;
            ctx->Root->LastChild          = box;
        };

        // A hidden/removed owner no longer builds its popup. Drop its stale
        // entry before input gating, or an invisible popup captures all input.
        for (uint32_t i = 0; i < ctx->PopupStackSize; ++i)
            if (!ctx->PopupStack[i].Box)
            {
                ctx->PopupStackSize = i;
                break;
            }

        // Shallow menus precede their submenus; non-interactive drag ghosts and
        // tooltips remain above every popup, regardless of when they were built.
        for (uint32_t i = 0; i < ctx->PopupStackSize; ++i)
            raise_overlay(ctx->PopupStack[i].Box);
        raise_overlay(ctx->ForegroundRoot);

        ZUILayoutSolve(ctx);
        for (uint32_t i = 0; i < ctx->PopupStackSize; ++i)
        {
            auto& entry = ctx->PopupStack[i];
            for (int axis = 0; axis < 2; ++axis)
            {
                entry.ScreenMin[axis] = entry.Box->ScreenMin[axis];
                entry.ScreenMax[axis] = entry.Box->ScreenMax[axis];
            }
        }
        ZUIInteractionPass(ctx);

        // Selection and placement are precise viewport interactions. Request
        // the crosshair only after the final paint-order hit test, so toolbar,
        // popup, and dock controls keep their own normal cursor behavior.
        const bool viewport_hot      = ctx->HotKey == ctx->ViewportInputKey || ctx->HotKey == ctx->ViewportOverlayKey;
        const bool viewport_eligible = ctx->ViewportEditable && ctx->ViewportInputKey != 0 && !ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && !ctx->PendingPopupKey && !ctx->DragSourceKey;
        if (viewport_eligible && viewport_hot)
        {
            ZUIRequestCursor(ctx, ZUICursor::Crosshair, ZUI_CursorPriorityViewport);
        }

        // Save mouse position before clearing edge states — used for drag delta next frame
        ctx->PrevMousePos[0] = ctx->MousePos[0];
        ctx->PrevMousePos[1] = ctx->MousePos[1];

        // Key repeat: fire BackspacePressed again when held past the delay
        if (ctx->BackspaceHeld && ctx->FocusKey != 0)
        {
            ctx->KeyRepeatTimer += ctx->DeltaTime;
            if (ctx->KeyRepeatTimer >= ZUIContext::kRepeatDelay)
            {
                float excess = ctx->KeyRepeatTimer - ZUIContext::kRepeatDelay;
                if ((int) (excess / ZUIContext::kRepeatRate) != (int) ((excess - ctx->DeltaTime) / ZUIContext::kRepeatRate))
                {
                    ctx->BackspacePressed = true; // fire repeat event
                }
            }
        }

        // Key repeat: Arrow left/right and Delete when held
        bool any_arrow = ctx->ArrowLeftHeld || ctx->ArrowRightHeld || ctx->ArrowUpHeld || ctx->ArrowDownHeld || ctx->DeleteHeld;
        if (any_arrow && ctx->FocusKey != 0)
        {
            ctx->ArrowRepeatTimer += ctx->DeltaTime;
            if (ctx->ArrowRepeatTimer >= ZUIContext::kRepeatDelay)
            {
                float excess = ctx->ArrowRepeatTimer - ZUIContext::kRepeatDelay;
                bool  fire   = (int) (excess / ZUIContext::kRepeatRate) != (int) ((excess - ctx->DeltaTime) / ZUIContext::kRepeatRate);
                if (fire)
                {
                    if (ctx->ArrowLeftHeld)
                        ctx->ArrowLeftPressed = true;
                    if (ctx->ArrowRightHeld)
                        ctx->ArrowRightPressed = true;
                    if (ctx->ArrowUpHeld)
                        ctx->ArrowUpPressed = true;
                    if (ctx->ArrowDownHeld)
                        ctx->ArrowDownPressed = true;
                    if (ctx->DeleteHeld)
                        ctx->DeletePressed = true;
                }
            }
        }
        if (!any_arrow)
            ctx->ArrowRepeatTimer = 0.f;

        // Dismiss overlays before resolving focus. Tab stays inside the open
        // menu/modal; Enter activates controls without dropping their focus.
        if (ctx->PopupStackSize > 0)
        {
            if (ctx->ArrowLeftPressed && ctx->PopupStackSize > 1)
            {
                ctx->PopupStackSize--; // close innermost submenu
                ctx->PopupNavIdx = -1;
            }
            if (ctx->EscapePressed)
            {
                ctx->PopupStackSize = 0; // close all popups on escape
                ctx->PopupNavIdx    = -1;
            }
        }

        // Text editing still commits/deactivates on Enter (rename callers rely
        // on focus leaving), while ordinary buttons keep their keyboard focus.
        if (ctx->EnterPressed && ctx->TextInputActive && !ctx->PopupStackSize)
            ctx->FocusKey = 0;
        ZUIFocusPass(ctx);
        ctx->EscapePressed   = false;
        ctx->EnterPressed    = false;
        ctx->SpacePressed    = false;
        ctx->TabPressed      = false;
        ctx->ShiftTabPressed = false;

        // Clear per-frame edge states now that the interaction pass has consumed them
        for (int i = 0; i < 3; ++i)
        {
            ctx->MousePressed[i]  = false;
            ctx->MouseReleased[i] = false;
        }
        ctx->ScrollDelta          = 0.f;
        ctx->TextInputLen         = 0;
        ctx->TextInput[0]         = '\0';
        ctx->BackspacePressed     = false;
        ctx->ArrowUpPressed       = false;
        ctx->ArrowDownPressed     = false;
        ctx->ArrowLeftPressed     = false;
        ctx->ArrowRightPressed    = false;
        ctx->HomePressed          = false;
        ctx->EndPressed           = false;
        ctx->CtrlCPressed         = false;
        ctx->CtrlXPressed         = false;
        ctx->CtrlBackspacePressed = false;
        ctx->CtrlAPressed         = false;
        ctx->CtrlZPressed         = false;
        ctx->CtrlYPressed         = false;
        ctx->DeletePressed        = false;
        ctx->F2Pressed            = false;

        // Apply pending popup open — truncate stack to target depth then push.
        if (ctx->PendingPopupKey != 0)
        {
            uint32_t depth = ctx->PendingPopupDepth;
            if (depth <= ctx->PopupStackSize && depth < ZUIContext::kMaxPopupDepth) // can only open at/above current depth
            {
                ctx->PopupStackSize                    = depth; // close any deeper popups
                ctx->PopupStack[ctx->PopupStackSize++] = {ctx->PendingPopupKey, nullptr, nullptr, ctx->PendingPopupPosX, ctx->PendingPopupPosY, ctx->PendingPopupOwnerFocusKey};
            }
            ctx->PendingPopupKey = 0;
        }
    }

    ZUIBox* ZUIBeginForeground(ZUIContext* ctx)
    {
        ZENGINE_VALIDATE_ASSERT(ctx && ctx->Root, "ZUIBeginForeground requires a frame root");
        ZUIBox* saved_parent = ctx->Current;
        if (!ctx->ForegroundRoot)
        {
            ctx->Current              = ctx->Root;
            constexpr char key[]      = "##zui_foreground";
            auto*          foreground = ZUIPushBox(ctx, key, sizeof(key) - 1, ZUI_FloatX | ZUI_FloatY);
            foreground->Size[0]       = {ZUISizeKind::Pixels, (float) ctx->ScreenW, 1.f};
            foreground->Size[1]       = {ZUISizeKind::Pixels, (float) ctx->ScreenH, 1.f};
            ctx->ForegroundRoot       = foreground;
        }
        ctx->Current = ctx->ForegroundRoot;
        return saved_parent;
    }

    void ZUIEndForeground(ZUIContext* ctx, ZUIBox* saved_parent)
    {
        ctx->Current = saved_parent;
    }

    ZUIBox* ZUIPushBox(ZUIContext* ctx, const char* key, uint32_t key_len, ZUIBoxFlags flags)
    {
        ZUIBox* box = ZPushStructCtor(&ctx->FrameArena, ZUIBox);
        ZENGINE_VALIDATE_ASSERT(box != nullptr, "ZUI FrameArena exhausted — increase FrameArenaBytes");
        box->Flags             = flags;

        // split key on '##': part before is the visible label, full string hashes the key
        const char* hash_start = key;
        uint32_t    label_len  = key_len;
        for (uint32_t i = 0; i + 1 < key_len; ++i)
        {
            if (key[i] == '#' && key[i + 1] == '#')
            {
                label_len  = i;
                hash_start = key;
                break;
            }
        }

        box->Key   = ZUIHashStr(hash_start, key_len);
        box->Label = (label_len > 0) ? ZUIPushStr(&ctx->FrameArena, key, label_len) : ZUIStr{nullptr, 0};

        if (ctx->Disabled)
        {
            box->Flags = ZUIBoxFlags((box->Flags | ZUI_Disabled) & ~ZUI_Clickable);
            if (ctx->FocusKey == box->Key)
                ctx->FocusKey = 0;
            if (ctx->ActiveKey == box->Key)
                ctx->ActiveKey = 0;
            if (ctx->HotKey == box->Key)
                ctx->HotKey = 0;
        }

        // link into tree
        if (ctx->Current)
        {
            box->Parent = ctx->Current;
            if (ctx->Current->LastChild)
            {
                ctx->Current->LastChild->NextSib = box;
                box->PrevSib                     = ctx->Current->LastChild;
            }
            else
            {
                ctx->Current->FirstChild = box;
            }
            ctx->Current->LastChild = box;
        }
        else
        {
            ctx->Root = box;
        }

        ctx->Current = box;
        return box;
    }

    void ZUIPopBox(ZUIContext* ctx)
    {
        if (ctx->Current)
        {
            ctx->Current = ctx->Current->Parent;
        }
    }

    ZUIPersistentState* ZUIStateGetOrInsert(ZUIPersistentStore* store, uint64_t key)
    {
        uint32_t idx = (uint32_t) (key & (uint64_t) (store->Capacity - 1));
        for (uint32_t i = 0; i < store->Capacity; ++i)
        {
            uint32_t           slot_idx = (idx + i) & (store->Capacity - 1);
            ZUIPersistentSlot* slot     = &store->Slots[slot_idx];
            if (slot->Key == 0)
            {
                slot->Key = key;
                ++store->Count;
                // Arena storage is zero-filled, not constructed. Apply every
                // declared default, including the cursor and selection sentinels.
                slot->State = {};
                return &slot->State;
            }
            if (slot->Key == key)
            {
                return &slot->State;
            }
        }
        // Table full — widgets that dereference the returned nullptr will crash.
        // Increase StateCapacity in ZUIContextInit.
        ZENGINE_VALIDATE_ASSERT(false, "ZUI state table full — increase StateCapacity");
        return nullptr;
    }

    bool ZUIStateEraseTransient(ZUIPersistentStore* store, uint64_t key)
    {
        if (!store || !store->Slots || !store->Capacity || !key)
            return false;
        const uint32_t mask = store->Capacity - 1;
        uint32_t       hole = static_cast<uint32_t>(key) & mask;
        for (uint32_t probes = 0; probes < store->Capacity; ++probes, hole = (hole + 1) & mask)
        {
            auto& slot = store->Slots[hole];
            if (!slot.Key)
                return false;
            if (slot.Key != key)
                continue;
            if (slot.State.ColorState || slot.State.FloatInputState)
                return false;
            // Keep every displaced key reachable from its home bucket, including
            // a collision cluster that wraps across the end of the table.
            for (uint32_t scan = (hole + 1) & mask, moved = 0; moved < store->Capacity - 1; scan = (scan + 1) & mask, ++moved)
            {
                const auto& candidate = store->Slots[scan];
                if (!candidate.Key)
                    break;
                const uint32_t home = static_cast<uint32_t>(candidate.Key) & mask;
                if (((hole - home) & mask) < ((scan - home) & mask))
                {
                    store->Slots[hole] = candidate;
                    hole               = scan;
                }
            }
            store->Slots[hole] = {};
            --store->Count;
            return true;
        }
        return false;
    }

    // Text is rendered byte-per-glyph. The few multi-byte characters the UI uses are folded to the
    // single-byte slots baked by the font atlas (see ZUI_GLYPH_* in ZUIFont.h); other bytes pass through.
    static uint8_t ZUIFoldUtf8(const char* s, uint32_t remaining, uint32_t* consumed)
    {
        const uint8_t b0 = (uint8_t) s[0];
        if (b0 == 0xC2 && remaining >= 2)
        {
            const uint8_t b1 = (uint8_t) s[1];
            if (b1 == 0xB0 || b1 == 0xB7) // degree, middle dot
            {
                *consumed = 2;
                return b1;
            }
        }
        else if (b0 == 0xE2 && remaining >= 3)
        {
            const uint16_t tail = (uint16_t) (((uint8_t) s[1] << 8) | (uint8_t) s[2]);
            switch (tail)
            {
                case 0x8892:
                    *consumed = 3;
                    return 0x90; // typographic minus
                case 0x80A6:
                    *consumed = 3;
                    return 0x85; // ellipsis
                case 0x80A2:
                    *consumed = 3;
                    return 0x95; // bullet
                case 0x8093:
                    *consumed = 3;
                    return 0x96; // en dash
                case 0x8094:
                    *consumed = 3;
                    return 0x97; // em dash
                case 0x8691:
                    *consumed = 3;
                    return 0x8F; // up arrow
                case 0x8693:
                    *consumed = 3;
                    return 0x8E; // down arrow
                default:
                    break;
            }
        }
        *consumed = 1;
        return b0;
    }

    ZUIStr ZUIPushStr(ArenaAllocator* arena, const char* str, uint32_t len)
    {
        char*    buf = ZPushString(arena, len + 1);
        uint32_t out = 0;
        for (uint32_t i = 0; i < len;)
        {
            uint32_t used  = 1;
            buf[out++]     = (char) ((str[i] & 0x80) ? ZUIFoldUtf8(str + i, len - i, &used) : (uint8_t) str[i]);
            i             += used;
        }
        buf[out] = '\0';
        return {buf, out};
    }

    // FNV-1a 64-bit
    uint64_t ZUIHashStr(const char* str, uint32_t len)
    {
        uint64_t hash = 14695981039346656037ULL;
        for (uint32_t i = 0; i < len; ++i)
        {
            hash ^= (uint8_t) str[i];
            hash *= 1099511628211ULL;
        }
        return hash ? hash : 1; // 0 is reserved for empty slots
    }

    // ZUIStylePushFloat / ZUIStylePop
    // Maps a ZUIStyleVar enum to the corresponding float in ctx->Style,
    // saves the old value on the stack, writes the new value.

    static float* StyleVarToPtr(ZUIStyle* s, ZUIStyleVar var)
    {
        switch (var)
        {
            case ZUIStyleVar_Alpha:
                return &s->Alpha;
            case ZUIStyleVar_FramePaddingX:
                return &s->FramePadding[0];
            case ZUIStyleVar_FramePaddingY:
                return &s->FramePadding[1];
            case ZUIStyleVar_ItemSpacingX:
                return &s->ItemSpacing[0];
            case ZUIStyleVar_ItemSpacingY:
                return &s->ItemSpacing[1];
            case ZUIStyleVar_ItemInnerSpacingX:
                return &s->ItemInnerSpacing[0];
            case ZUIStyleVar_ItemInnerSpacingY:
                return &s->ItemInnerSpacing[1];
            case ZUIStyleVar_FrameRounding:
                return &s->FrameRounding;
            case ZUIStyleVar_PopupRounding:
                return &s->PopupRounding;
            case ZUIStyleVar_ScrollbarRounding:
                return &s->ScrollbarRounding;
            case ZUIStyleVar_GrabRounding:
                return &s->GrabRounding;
            case ZUIStyleVar_TabRounding:
                return &s->TabRounding;
            case ZUIStyleVar_WindowBorderSize:
                return &s->WindowBorderSize;
            case ZUIStyleVar_FrameBorderSize:
                return &s->FrameBorderSize;
            case ZUIStyleVar_PopupBorderSize:
                return &s->PopupBorderSize;
            case ZUIStyleVar_TabBarBorderSize:
                return &s->TabBarBorderSize;
            case ZUIStyleVar_TabBarOverlineSize:
                return &s->TabBarOverlineSize;
            case ZUIStyleVar_IndentSpacing:
                return &s->IndentSpacing;
            case ZUIStyleVar_ScrollbarSize:
                return &s->ScrollbarSize;
            case ZUIStyleVar_GrabMinSize:
                return &s->GrabMinSize;
            case ZUIStyleVar_DockingFocusBorderWidth:
                return &s->DockingFocusBorderWidth;
            case ZUIStyleVar_HoverAnimSpeed:
                return &s->HoverAnimSpeed;
            case ZUIStyleVar_ActiveAnimSpeed:
                return &s->ActiveAnimSpeed;
            default:
                ZENGINE_VALIDATE_ASSERT(false, "ZUIStylePushFloat: unknown ZUIStyleVar");
                return nullptr;
        }
    }

    void ZUIStylePushFloat(ZUIContext* ctx, ZUIStyleVar var, float val)
    {
        ZENGINE_VALIDATE_ASSERT(ctx->StyleStackDepth < 64, "ZUIStyle push/pop stack overflow");
        float* ptr = StyleVarToPtr(&ctx->Style, var);
        if (!ptr)
            return;
        ctx->StyleStack[ctx->StyleStackDepth++] = {var, *ptr};
        *ptr                                    = val;
        ZUIStyleUpdate(&ctx->Style); // recompute derived fields if FramePadding changed
    }

    void ZUIStylePop(ZUIContext* ctx)
    {
        ZENGINE_VALIDATE_ASSERT(ctx->StyleStackDepth > 0, "ZUIStyle pop with empty stack");
        if (ctx->StyleStackDepth == 0)
            return;
        const auto& entry = ctx->StyleStack[--ctx->StyleStackDepth];
        float*      ptr   = StyleVarToPtr(&ctx->Style, entry.Id);
        if (ptr)
            *ptr = entry.Old;
        ZUIStyleUpdate(&ctx->Style);
    }

} // namespace ZEngine::UI
