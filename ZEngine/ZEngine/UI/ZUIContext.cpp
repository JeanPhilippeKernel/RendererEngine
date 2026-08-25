#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUILayout.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <cstring>

namespace ZEngine::UI
{
    void ZUIContextInit(ZUIContext* ctx, ArenaAllocator* parent,
                        size_t FrameArenaBytes, size_t PersistentArenaBytes,
                        uint32_t StateCapacity, uint32_t MaxBoxesPerFrame)
    {
        parent->CreateSubArena(FrameArenaBytes,      &ctx->FrameArena);
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
        ctx->FrameArena.Clear();
        ctx->Root         = nullptr;
        ctx->Current      = nullptr;
        ctx->DeltaTime    = dt;
        // TextInputLen, BackspacePressed, MousePressed/Released, ScrollDelta are NOT
        // cleared here — GLFW events fire before BeginFrame (in window->PollEvent) and
        // must survive until ZUIEndFrame runs the interaction pass and widget logic.
        // ──────────────────────────────────────────────────────────────────────────
        // GLFW events fire before BeginFrame (in window->PollEvent) and must survive
        // until ZUIEndFrame runs the interaction pass.
    }

    void ZUIEndFrame(ZUIContext* ctx)
    {
        // Clear drop result from the previous frame before the interaction pass may set a new one
        ctx->DragDropFired  = false;
        ctx->DragTargetKey  = 0;
        // ViewportHovered is written fresh each BuildUI; reset it so a missing panel = false
        ctx->ViewportHovered = false;

        ZUILayoutSolve(ctx);
        ZUIInteractionPass(ctx);

        // Save mouse position before clearing edge states — used for drag delta next frame
        ctx->PrevMousePos[0] = ctx->MousePos[0];
        ctx->PrevMousePos[1] = ctx->MousePos[1];

        // Clear per-frame edge states now that the interaction pass has consumed them
        for (int i = 0; i < 3; ++i)
        {
            ctx->MousePressed[i]  = false;
            ctx->MouseReleased[i] = false;
        }
        ctx->ScrollDelta      = 0.f;
        ctx->TextInputLen     = 0;
        ctx->BackspacePressed = false;
    }

    ZUIBox* ZUIPushBox(ZUIContext* ctx, const char* key, uint32_t key_len, ZUIBoxFlags flags)
    {
        ZUIBox* box = ZPushStruct(&ctx->FrameArena, ZUIBox);
        box->Flags  = flags;

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
        box->Label = (label_len > 0) ? ZUIPushStr(&ctx->FrameArena, key, label_len)
                                     : ZUIStr{nullptr, 0};

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
        uint32_t idx = (uint32_t)(key & (uint64_t)(store->Capacity - 1));
        for (uint32_t i = 0; i < store->Capacity; ++i)
        {
            uint32_t           slot_idx = (idx + i) & (store->Capacity - 1);
            ZUIPersistentSlot* slot     = &store->Slots[slot_idx];
            if (slot->Key == 0)
            {
                slot->Key = key;
                ++store->Count;
                return &slot->State; // zero-filled by the arena allocator
            }
            if (slot->Key == key)
            {
                return &slot->State;
            }
        }
        return nullptr; // table full — increase StateCapacity at init
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
            hash ^= (uint8_t)str[i];
            hash *= 1099511628211ULL;
        }
        return hash ? hash : 1; // 0 is reserved for empty slots
    }

} // namespace ZEngine::UI
