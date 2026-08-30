#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUILayout.h>
#include <cstring>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    void ZUIContextInit(ZUIContext* ctx, ArenaAllocator* parent, size_t FrameArenaBytes, size_t PersistentArenaBytes, uint32_t StateCapacity, uint32_t MaxBoxesPerFrame)
    {
        ZENGINE_VALIDATE_ASSERT(StateCapacity > 0 && (StateCapacity & (StateCapacity - 1)) == 0, "ZUIContextInit: StateCapacity must be a power of two — hash probing is bitmasked");

        parent->CreateSubArena(FrameArenaBytes, &ctx->FrameArena);
        parent->CreateSubArena(PersistentArenaBytes, &ctx->PersistentArena);

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
        ZUIStyleUpdate(&ctx->Style); // recompute FrameHeight = FontSize + FramePadding.y*2
        ctx->FrameArena.Clear();
        ctx->Root             = nullptr;
        ctx->Current          = nullptr;
        ctx->DeltaTime        = dt;
        ctx->Time            += dt;
        ctx->ResizeCursor     = 0;
        ctx->PopupBuildDepth  = 0; // reset render depth; rebuilt during each BuildUI pass
        // Clear stale Box* pointers — boxes are re-created each frame in FrameArena
        for (uint32_t i = 0; i < ctx->PopupStackSize; i++)
            ctx->PopupStack[i].Box = nullptr;
        ctx->ModalBox = nullptr; // re-set each frame by whoever owns the active modal
        // TextInputLen, BackspacePressed, MousePressed/Released, ScrollDelta are NOT
        // cleared here — GLFW events fire before BeginFrame (in window->PollEvent) and
        // must survive until ZUIEndFrame runs the interaction pass and widget logic.
    }

    void ZUIEndFrame(ZUIContext* ctx)
    {
        // Clear drop result from the previous frame before the interaction pass may set a new one
        ctx->DragDropFired   = false;
        ctx->DragTargetKey   = 0;
        // ViewportHovered is written fresh each BuildUI; reset it so a missing panel = false
        ctx->ViewportHovered = false;

        ZUILayoutSolve(ctx);
        ZUIInteractionPass(ctx);

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

        // Escape / Enter: clear focus
        if (ctx->EscapePressed || ctx->EnterPressed)
        {
            ctx->FocusKey = 0;
        }
        ctx->EscapePressed = false;
        ctx->EnterPressed  = false;
        ctx->SpacePressed  = false;

        // Popup keyboard navigation (applies to the innermost open popup)
        if (ctx->PopupStackSize > 0)
        {
            int count = ctx->PopupBuildCount > 0 ? ctx->PopupBuildCount : 1;
            if (ctx->ArrowDownPressed)
                ctx->PopupNavIdx = (ctx->PopupNavIdx + 1) % count;
            if (ctx->ArrowUpPressed)
                ctx->PopupNavIdx = (ctx->PopupNavIdx <= 0) ? (count - 1) : (ctx->PopupNavIdx - 1);
            if (ctx->ArrowLeftPressed && ctx->PopupStackSize > 1)
            {
                ctx->PopupStackSize--; // close innermost submenu
                ctx->PopupNavIdx = -1;
            }
            if (ctx->EscapePressed || ctx->TabPressed || ctx->ShiftTabPressed)
            {
                ctx->PopupStackSize = 0; // close all popups on escape
                ctx->PopupNavIdx    = -1;
            }
        }

        // Tab focus navigation — apply after interaction pass so click-focus wins
        if (ctx->TabPressed)
        {
            uint64_t next = ctx->TabNavNextKey ? ctx->TabNavNextKey : ctx->TabNavFirstKey;
            if (next)
            {
                ctx->FocusKey = next;
            }
        }
        if (ctx->ShiftTabPressed)
        {
            uint64_t prev = ctx->TabNavPrevKey ? ctx->TabNavPrevKey : ctx->TabNavLastKey;
            if (prev)
            {
                ctx->FocusKey = prev;
            }
        }
        ctx->TabPressed      = false;
        ctx->ShiftTabPressed = false;
        ctx->TabNavNextKey   = 0;
        ctx->TabNavPrevKey   = 0;
        ctx->TabNavFirstKey  = 0;
        ctx->TabNavLastKey   = 0;
        ctx->TabNavSeenFocus = false;

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

        // Apply pending popup open — truncate stack to target depth then push.
        if (ctx->PendingPopupKey != 0)
        {
            uint32_t depth = ctx->PendingPopupDepth;
            if (depth <= ctx->PopupStackSize) // can only open at/above current depth
            {
                ctx->PopupStackSize                    = depth; // close any deeper popups
                ctx->PopupStack[ctx->PopupStackSize++] = {ctx->PendingPopupKey, nullptr, nullptr, ctx->PendingPopupPosX, ctx->PendingPopupPosY};
            }
            ctx->PendingPopupKey = 0;
        }
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
                // Arena zero-fills memory, overriding C++ default initializers.
                // Explicitly set the sentinel so first-use detection works.
                slot->State.UserData = -1.f;
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

    ZUIStr ZUIPushStr(ArenaAllocator* arena, const char* str, uint32_t len)
    {
        char* buf = ZPushString(arena, len + 1);
        memcpy(buf, str, len);
        buf[len] = '\0';
        return {buf, len};
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
            case ZUIStyleVar_DisabledAlpha:
                return &s->DisabledAlpha;
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
