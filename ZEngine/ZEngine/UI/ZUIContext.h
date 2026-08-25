#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdint>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    struct ZUIPersistentState
    {
        float HotT    = 0.f;
        float ActiveT = 0.f;
        float ScrollX = 0.f;
        float ScrollY = 0.f;
    };

    struct ZUIPersistentSlot
    {
        uint64_t           Key   = 0; // 0 = empty
        ZUIPersistentState State = {};
    };

    struct ZUIPersistentStore
    {
        ZUIPersistentSlot* Slots    = nullptr;
        uint32_t           Capacity = 0; // must be a power of two
        uint32_t           Count    = 0;
    };

    struct ZUIContext
    {
        // sub-arenas carved from the engine's main arena via ZUIContextInit
        ArenaAllocator     FrameArena;      // Clear()-ed each BeginFrame; all ZUIBox* are stale after
        ArenaAllocator     PersistentArena; // never cleared; holds the persistent state table

        // box tree — all pointers into FrameArena
        ZUIBox*            Root    = nullptr;
        ZUIBox*            Current = nullptr;

        // persistent state — open-addressing hash table in PersistentArena
        ZUIPersistentStore StateStore;

        // font — set by caller after ZUIFontBake(); used by layout solver for ZUISizeKind::Text
        ZUIFont*           Font = nullptr;

        // input state — written by ZUILayer each frame before ZUIBeginFrame
        float              MousePos[2]      = {};
        bool               MouseDown[3]     = {};
        bool               MousePressed[3]  = {};
        bool               MouseReleased[3] = {};
        float              ScrollDelta      = 0.f;
        float              DeltaTime        = 0.f;

        // interaction state — updated by ZUIInteractionPass
        uint64_t           HotKey    = 0;
        uint64_t           ActiveKey = 0;
        uint64_t           FocusKey  = 0;

        // text input — written by OnTextInputRaised
        char               TextInput[32]     = {};
        uint32_t           TextInputLen      = 0;

        // capacity caps used by layout and interaction passes
        uint32_t           MaxBoxesPerFrame  = 0;
    };

    ZDEFINE_PTR(ZUIContext);

    // Lifecycle
    void ZUIContextInit(ZUIContext* ctx, ArenaAllocator* parent,
                        size_t FrameArenaBytes, size_t PersistentArenaBytes,
                        uint32_t StateCapacity, uint32_t MaxBoxesPerFrame);
    void ZUIContextDestroy(ZUIContext* ctx);

    // Per-frame
    void ZUIBeginFrame(ZUIContext* ctx, float dt);
    void ZUIEndFrame(ZUIContext* ctx);

    // Box tree helpers
    ZUIBox*  ZUIPushBox(ZUIContext* ctx, const char* key, uint32_t key_len, ZUIBoxFlags flags);
    void     ZUIPopBox(ZUIContext* ctx);

    // Utilities
    ZUIPersistentState* ZUIStateGetOrInsert(ZUIPersistentStore* store, uint64_t key);
    ZUIStr              ZUIPushStr(ArenaAllocator* arena, const char* str, uint32_t len);
    uint64_t            ZUIHashStr(const char* str, uint32_t len);

} // namespace ZEngine::UI
