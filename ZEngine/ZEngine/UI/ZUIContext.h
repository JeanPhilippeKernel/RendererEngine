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
    // ZUITheme — colors matched to ImGui's StyleColorsDark() palette so the
    // editor looks familiar to ImGui users and benefits from its years of polish.
    struct ZUITheme
    {
        // Backgrounds — ImGui dark: WindowBg=(0.06,0.06,0.06,0.94)
        float WindowBg[4]       = {0.06f, 0.06f, 0.06f, 0.94f};
        float PanelBg[4]        = {0.10f, 0.10f, 0.10f, 0.96f}; // popup/child bg
        float PanelBgAlt[4]     = {0.08f, 0.08f, 0.08f, 0.94f};
        float HeaderBg[4]       = {0.14f, 0.14f, 0.14f, 1.00f}; // title bars
        float RowHoverBg[4]     = {0.26f, 0.59f, 0.98f, 0.f};   // ImGui Header at 0 alpha, fades in
        float RowSelectedBg[4]  = {0.26f, 0.59f, 0.98f, 0.35f}; // ImGui Header
        float RowRootBg[4]      = {0.16f, 0.29f, 0.48f, 0.20f};
        float InputBg[4]        = {0.16f, 0.29f, 0.48f, 0.54f}; // ImGui FrameBg exact
        float ButtonBg[4]       = {0.26f, 0.59f, 0.98f, 0.40f}; // ImGui Button exact
        // Text — ImGui: Text=(1,1,1,1), TextDisabled=(0.5,0.5,0.5,1)
        float TextDefault[4]    = {1.00f, 1.00f, 1.00f, 1.00f};
        float TextDim[4]        = {0.50f, 0.50f, 0.50f, 1.00f};
        float TextAccent[4]     = {0.26f, 0.59f, 0.98f, 1.00f}; // ImGui blue
        // Borders — ImGui: Border=(0.43,0.43,0.50,0.50)
        float PanelBorder[4]    = {0.43f, 0.43f, 0.50f, 0.50f};
        float ButtonBorder[4]   = {0.43f, 0.43f, 0.50f, 0.50f};
        float InputBorder[4]    = {0.43f, 0.43f, 0.50f, 0.50f};
        float InputFocusBorder[4]= {0.26f, 0.59f, 0.98f, 1.00f}; // accent blue
        float Separator[4]      = {0.43f, 0.43f, 0.50f, 0.50f};
    };

    struct ZUIPersistentState
    {
        float HotT      = 0.f;
        float ActiveT   = 0.f;
        float ScrollX   = 0.f;
        float ScrollY   = 0.f;
        float MaxScrollY= 0.f; // set by layout solver; clamped in interaction pass
        float UserData  = 0.f; // general-purpose (tab selected index, open bool, etc.)
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

        // Single shared font atlas (ImGui approach: one texture, all fonts).
        // Set by Editor::OnInitialized after ZUIFontAtlasBake.
        ZUIFontAtlas*      Atlas      = nullptr;

        // Convenience accessors — delegate to Atlas
        ZUIFont* GetFont(ZUIFontSize size) const
        {
            if (!Atlas) { return nullptr; }
            if (size == ZUIFontSize::Small  && Atlas->Small)  return Atlas->Small;
            if (size == ZUIFontSize::Header && Atlas->Header) return Atlas->Header;
            return Atlas->Body;
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
        bool               BackspaceHeld    = false; // true while key is physically down

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
        // display content scale (glfwGetWindowContentScale); 1.0=standard, 2.0=Retina.
        // Widgets multiply logical pixel sizes by this to stay readable at any DPI.
        float              UIScale           = 1.f;

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

        // Modifier key state — written by ZUILayer::OnKeyPressed/Released
        bool               CtrlDown          = false;
        bool               ShiftDown         = false;
        bool               AltDown           = false;

        // Input repeat — ZUIEndFrame advances the timer; after RepeatDelay
        // it fires BackspacePressed / ArrowPressed at RepeatRate hz
        float              KeyRepeatTimer    = 0.f;
        static constexpr float kRepeatDelay  = 0.45f; // s before first repeat
        static constexpr float kRepeatRate   = 0.04f; // s between repeats

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
