#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <cstdint>

namespace ZEngine::UI
{
    using namespace ZEngine::Core::Memory;

    // ---------------------------------------------------------------
    // ZUITheme — single source of truth for every color in the editor.
    // Widgets and components read from ctx->Theme instead of local constants.
    // Swap the whole struct to apply a different theme at runtime.
    // ---------------------------------------------------------------
    struct ZUITheme
    {
        // Backgrounds
        float WindowBg[4]       = {0.18f, 0.18f, 0.20f, 1.f};
        float PanelBg[4]        = {0.22f, 0.22f, 0.25f, 0.96f};
        float PanelBgAlt[4]     = {0.20f, 0.20f, 0.23f, 0.96f}; // inspector header card
        float HeaderBg[4]       = {0.28f, 0.28f, 0.32f, 1.f};   // menu bar
        float RowHoverBg[4]     = {0.42f, 0.42f, 0.48f, 0.f};   // list row — transparent, fade in
        float RowSelectedBg[4]  = {0.26f, 0.44f, 0.70f, 0.50f};
        float RowRootBg[4]      = {0.30f, 0.30f, 0.36f, 0.18f}; // scene root row
        float InputBg[4]        = {0.18f, 0.18f, 0.22f, 1.f};
        float ButtonBg[4]       = {0.32f, 0.32f, 0.38f, 1.f};
        // Text
        float TextDefault[4]    = {0.92f, 0.92f, 0.92f, 1.f};
        float TextDim[4]        = {0.55f, 0.55f, 0.62f, 1.f};
        float TextAccent[4]     = {0.55f, 0.75f, 0.95f, 1.f};
        // Borders
        float PanelBorder[4]    = {0.40f, 0.42f, 0.50f, 1.f};
        float ButtonBorder[4]   = {0.50f, 0.50f, 0.60f, 1.f};
        float InputBorder[4]    = {0.40f, 0.40f, 0.52f, 1.f};
        float InputFocusBorder[4]= {0.40f, 0.65f, 0.92f, 1.f};
        float Separator[4]      = {0.38f, 0.38f, 0.44f, 1.f};
    };

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

        // Fonts — set after ZUIFontBake(). Font = Body (backwards-compat alias).
        // FontSmall / FontHeader added for multi-size support.
        ZUIFont*           Font       = nullptr; // ZUIFontSize::Body
        ZUIFont*           FontSmall  = nullptr; // ZUIFontSize::Small
        ZUIFont*           FontHeader = nullptr; // ZUIFontSize::Header

        // Helper: returns the right ZUIFont* for a given size
        ZUIFont* GetFont(ZUIFontSize size) const
        {
            if (size == ZUIFontSize::Small  && FontSmall)  return FontSmall;
            if (size == ZUIFontSize::Header && FontHeader) return FontHeader;
            return Font; // Body fallback
        }

        // input state — written by ZUILayer each frame before ZUIBeginFrame
        float              MousePos[2]      = {};
        float              PrevMousePos[2]  = {};  // saved at end of ZUIEndFrame for drag-delta
        bool               MouseDown[3]     = {};
        bool               MousePressed[3]  = {};
        bool               MouseReleased[3] = {};
        float              ScrollDelta      = 0.f;
        float              DeltaTime        = 0.f;
        bool               BackspacePressed = false; // set by ZUILayer::OnKeyPressed, cleared in ZUIEndFrame

        // interaction state — updated by ZUIInteractionPass
        uint64_t           HotKey    = 0;
        uint64_t           ActiveKey = 0;
        uint64_t           FocusKey  = 0;

        // text input — written by OnTextInputRaised
        char               TextInput[32]     = {};
        uint32_t           TextInputLen      = 0;

        // capacity caps used by layout and interaction passes
        uint32_t           MaxBoxesPerFrame  = 0;

        // active color theme — swap to retheme the whole UI at runtime
        ZUITheme           Theme;

        // current swapchain dimensions — set by AppRenderPipeline::BeginOverlayFrame each frame
        uint32_t           ScreenW           = 1280;
        uint32_t           ScreenH           = 720;

        // drag-and-drop — source is set by ZUIBeginDragSource while a box is held+moving;
        // drop result (DragDropFired/DragTargetKey) is set by ZUIInteractionPass on mouse-release
        // and cleared at the START of the next ZUIEndFrame so BuildUI can read it
        uint64_t           DragSourceKey     = 0;
        char               DragPayload[512]  = {};
        uint32_t           DragPayloadLen    = 0;
        bool               DragDropFired     = false;
        uint64_t           DragTargetKey     = 0;

        // set by ZUISceneViewportComponent each BuildUI frame; read by Editor::ProcessEvent
        // to gate camera-controller mouse routing
        bool               ViewportHovered   = false;

        // ZUIBeginDisabled / ZUIEndDisabled — widgets skip Clickable and dim colours
        bool               Disabled          = false;
        int                DisabledDepth     = 0; // supports nesting

        // ---------------------------------------------------------------
        // Popup system
        // ZUIOpenPopup → sets OpenPopupKey; ZUIEndFrame promotes to ActivePopupKey.
        // ZUIBeginPopup returns true when active and pushes a floated root-level box.
        // ---------------------------------------------------------------
        uint64_t           OpenPopupKey     = 0;     // requested this frame
        uint64_t           ActiveModalKey   = 0;     // modal (cannot close by clicking outside)

        // Tab bar state (single-level; reset by ZUIBeginTabBar)
        uint64_t           TabBarKey           = 0;  // hash of active tab bar
        int                TabBarSelectedIdx   = 0;  // which tab is open
        int                TabBarCurrentIdx    = 0;  // iteration counter
        bool               TabItemWasSelected  = false; // did last BeginTabItem match?
        ZUIBox*            TabBarOuterBox      = nullptr;
        ZUIBox*            TabBarRowBox        = nullptr;

        // Table state (single-level; reset by ZUIBeginTable/ZUIEndTable)
        int                TableColumns     = 0;
        int                TableCurrentCol  = -1;
        float*             TableColWidths   = nullptr; // FrameArena array
        ZUIBox*            TableRowBox      = nullptr;
        float              PopupPos[2]      = {};    // screen position to open at
        uint64_t           ActivePopupKey   = 0;     // currently shown popup (frame-to-frame)
        ZUIBox*            ActivePopupBox   = nullptr; // set by ZUIBeginPopup; valid this frame
        ZUIBox*            PopupSavedParent = nullptr; // ctx->Current saved during popup build
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
