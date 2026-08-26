#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIDrawList.h>
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
        // VS Code Dark+ palette with teal accent #4EC9B0.
        // All values are linear 0-1; hex refs are perceptual sRGB.
        // Accent: #4EC9B0 = (0.306, 0.788, 0.690) — VS Code type/class teal.

        // --- Backgrounds (darkest → lightest) ---
        float WindowBg[4]       = {0.118f,0.118f,0.118f,1.00f}; // #1e1e1e  editor area
        float PanelBg[4]        = {0.145f,0.145f,0.149f,1.00f}; // #252526  panel body
        float PanelBgAlt[4]     = {0.165f,0.165f,0.169f,1.00f}; // #2a2a2b  alt rows
        float TitleBarBg[4]     = {0.176f,0.176f,0.176f,1.00f}; // #2d2d2d  tab bar strip
        float TitleBgActive[4]  = {0.176f,0.176f,0.176f,1.00f}; // same — VS Code tab bar same focused/unfocused
        float HeaderBg[4]       = {0.306f,0.788f,0.690f,0.22f}; // teal 22%  collapsing header
        float MenuBarBg[4]      = {0.235f,0.235f,0.235f,1.00f}; // #3c3c3c  menu + title bars
        float InputBg[4]        = {0.235f,0.235f,0.235f,1.00f}; // #3c3c3c  input fields

        // Buttons — teal family
        float ButtonBg[4]         = {0.051f,0.478f,0.396f,1.00f}; // #0d7a65  rest
        float ButtonHoveredBg[4]  = {0.059f,0.659f,0.502f,1.00f}; // #0fa880  hover
        float ButtonActiveBg[4]   = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  active = full teal

        // Input interactive states
        float InputHoveredBg[4]   = {0.278f,0.278f,0.278f,1.00f}; // #474747 slightly lighter
        float InputActiveBg[4]    = {0.278f,0.278f,0.278f,1.00f}; // same on active
        float HeaderHoveredBg[4]  = {0.306f,0.788f,0.690f,0.35f}; // teal 35%
        float HeaderActiveBg[4]   = {0.306f,0.788f,0.690f,0.55f}; // teal 55%

        // --- Tabs ---
        float TabActiveBg[4]       = {0.118f,0.118f,0.118f,1.00f}; // #1e1e1e = editor bg (active tab merges)
        float TabInactiveBg[4]     = {0.000f,0.000f,0.000f,0.00f}; // transparent
        float TabActiveBorder[4]   = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  teal top accent
        float TabInactiveBorder[4] = {0.278f,0.278f,0.278f,0.40f}; // #474747 subtle
        float TabAccent[4]         = {0.306f,0.788f,0.690f,1.00f}; // teal

        // --- Rows ---
        float RowHoverBg[4]    = {0.306f,0.788f,0.690f,0.10f}; // teal 10%  row hover
        float RowSelectedBg[4] = {0.306f,0.788f,0.690f,0.25f}; // teal 25%  selection
        float RowRootBg[4]     = {0.306f,0.788f,0.690f,0.12f}; // teal 12%  root tint

        // --- Status bar ---
        float StatusBarBg[4]   = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  teal

        // --- Text ---
        float TextDefault[4]   = {0.831f,0.831f,0.831f,1.00f}; // #d4d4d4
        float TextDim[4]       = {0.522f,0.522f,0.522f,1.00f}; // #858585
        float TextAccent[4]    = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  teal
        float TextWarn[4]      = {0.949f,0.741f,0.141f,1.00f}; // #f2bd24
        float TextError[4]     = {0.937f,0.325f,0.314f,1.00f}; // #ef5350

        // --- Widget accent marks ---
        float CheckMark[4]        = {0.306f,0.788f,0.690f,1.00f}; // teal
        float SliderGrab[4]       = {0.200f,0.627f,0.537f,1.00f}; // teal -20%
        float SliderGrabActive[4] = {0.306f,0.788f,0.690f,1.00f}; // teal

        // Scrollbar (#424242 at rest, lighter on hover — VS Code exact)
        float ScrollbarBg[4]      = {0.000f,0.000f,0.000f,0.00f}; // transparent track
        float ScrollbarGrab[4]    = {0.259f,0.259f,0.259f,1.00f}; // #424242
        float ScrollbarGrabHov[4] = {0.408f,0.408f,0.408f,1.00f}; // #686868
        float ScrollbarGrabAct[4] = {0.306f,0.788f,0.690f,0.80f}; // teal on drag

        // Plot
        float PlotLines[4]        = {0.306f,0.788f,0.690f,0.80f}; // teal
        float PlotLinesHov[4]     = {0.306f,0.788f,0.690f,1.00f}; // teal full
        float PlotHistogram[4]    = {0.200f,0.627f,0.537f,0.90f}; // teal -20%
        float PlotHistogramHov[4] = {0.306f,0.788f,0.690f,1.00f}; // teal

        // Table
        float TableRowBgAlt[4]    = {1.000f,1.000f,1.000f,0.04f}; // very subtle zebra

        // --- Borders ---
        float PanelBorder[4]          = {0.278f,0.278f,0.278f,1.00f}; // #474747  1px VS Code border
        float PanelFocusBorder[4]     = {0.306f,0.788f,0.690f,1.00f}; // teal  3px left = focused panel
        float PanelInactiveOverlay[4] = {0.000f,0.000f,0.000f,0.04f}; // 4% black dim
        float ButtonBorder[4]         = {0.000f,0.000f,0.000f,0.00f}; // no border on buttons
        float InputBorder[4]          = {0.278f,0.278f,0.278f,0.80f}; // #474747
        float InputFocusBorder[4]     = {0.306f,0.788f,0.690f,1.00f}; // teal focus ring
        float Separator[4]            = {0.278f,0.278f,0.278f,0.60f}; // #474747 60%
    };

    struct ZUIPersistentState
    {
        float HotT       = 0.f;
        float ActiveT    = 0.f;
        float ScrollX    = 0.f;
        float ScrollY    = 0.f;
        float MaxScrollY = 0.f; // set by layout solver; clamped in interaction pass
        float UserData   =-1.f; // general-purpose; -1 = never set (first-use sentinel for open states)
        float ScreenMinX = 0.f; // prev-frame screen position — written by layout, read next frame
        float ScreenMinY = 0.f;
        float ScreenMaxX = 0.f;
        float ScreenMaxY = 0.f;
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
        float              Time             = 0.f; // accumulated seconds since init
        bool               BackspacePressed = false; // set by ZUILayer::OnKeyPressed, cleared in ZUIEndFrame
        bool               BackspaceHeld    = false; // true while key is physically down

        // interaction state — updated by ZUIInteractionPass
        uint64_t           HotKey    = 0;
        uint64_t           ActiveKey = 0;
        uint64_t           FocusKey  = 0;

        // text input — written by OnTextInputRaised
        char               TextInput[32]     = {};
        uint32_t           TextInputLen      = 0;
        // Clipboard write request — written by ZUITextField (Ctrl+C), read+cleared by ZUILayer
        char               ClipboardWrite[512] = {};

        // capacity caps used by layout and interaction passes
        uint32_t           MaxBoxesPerFrame  = 0;

        // active color theme — swap to retheme the whole UI at runtime
        ZUITheme           Theme;

        // Vector draw list — populated by PreparePayload each frame (FrameArena-backed)
        ZUIDrawList        DrawList;

        // current swapchain dimensions — set by AppRenderPipeline::BeginOverlayFrame each frame
        uint32_t           ScreenW           = 1280;
        uint32_t           ScreenH           = 720;
        // display content scale (glfwGetWindowContentScale); 1.0=standard, 2.0=Retina.
        // Widgets multiply logical pixel sizes by this to stay readable at any DPI.
        float              UIScale           = 1.f;
        // guard against per-frame ContentScale log spam — log only once
        bool               UIScaleLogged     = false;

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
        int                ResizeCursor      = 0; // 0=default 1=H-resize 2=V-resize; set by panel dividers, read by ZUILayer

        // Modifier key state — written by ZUILayer::OnKeyPressed/Released
        bool               CtrlDown          = false;
        bool               ShiftDown         = false;
        bool               AltDown           = false;

        // Tab focus navigation — set by ZUILayer, consumed by ZUIEndFrame
        bool               TabPressed        = false;
        bool               ShiftTabPressed   = false;
        bool               EscapePressed     = false; // clear FocusKey
        bool               EnterPressed      = false; // confirm / deactivate field
        bool               SpacePressed      = false; // activate focused button
        bool               ArrowUpPressed    = false; // nudge drag float / combo nav
        bool               ArrowDownPressed  = false;
        bool               ArrowLeftPressed  = false;  // text cursor left
        bool               ArrowRightPressed = false;  // text cursor right
        bool               HomePressed           = false; // cursor to start of field
        bool               EndPressed            = false; // cursor to end of field
        bool               CtrlCPressed          = false; // copy focused field to clipboard
        bool               CtrlBackspacePressed  = false; // delete word before cursor
        bool               CtrlAPressed          = false; // select all (clears field + copies to clipboard)
        bool               DeletePressed         = false; // forward-delete at cursor
        // Held state for key-repeat (same mechanism as BackspaceHeld)
        bool               ArrowLeftHeld         = false;
        bool               ArrowRightHeld        = false;
        bool               ArrowUpHeld           = false;
        bool               ArrowDownHeld         = false;
        bool               DeleteHeld            = false;
        float              ArrowRepeatTimer      = 0.f;
        // Per-frame tracking updated in ZUISignalFromBox during the build pass
        uint64_t           TabNavNextKey     = 0; // first clickable after FocusKey
        uint64_t           TabNavPrevKey     = 0; // last clickable before FocusKey
        uint64_t           TabNavFirstKey    = 0; // first clickable seen (wraparound)
        uint64_t           TabNavLastKey     = 0; // last clickable seen (Shift+Tab wrap)
        bool               TabNavSeenFocus   = false;

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

        // Basic table state (ZUIBeginTable / ZUIEndTable)
        int                TableColumns     = 0;
        int                TableCurrentCol  = -1;
        float*             TableColWidths   = nullptr; // FrameArena array
        ZUIBox*            TableRowBox      = nullptr;

        // TreeView state (ZUIBeginTreeView / ZUIEndTreeView)
        int                TV_Depth         = 0;
        float              TV_IndentPx      = 16.f;   // px per depth level
        float              TV_RowH          = 22.f;   // logical row height

        // DataTable state (ZUIBeginDataTable / ZUIEndDataTable)
        uint64_t           DT_Key           = 0;
        int                DT_ColCount      = 0;
        int                DT_CurCol        = -1;
        int                DT_RowIndex      = 0;
        bool               DT_InRow         = false;
        bool               DT_InHeader      = false;
        ZUIBox*            DT_RowBox        = nullptr;
        float*             DT_ColWidths     = nullptr; // FrameArena, size = DT_ColCount
        const void*        DT_Cols          = nullptr; // ZUIDataTableColumn* stored by BeginDataTable
        int                DT_SortCol       = -1;      // -1 = unsorted
        bool               DT_SortAsc       = true;
        bool               DT_SortChanged   = false;

        // GridView state (ZUIBeginGridView / ZUIEndGridView)
        float              GV_ItemW         = 0.f;
        float              GV_ItemH         = 0.f;
        int                GV_MaxCols       = 1;
        int                GV_CurCol        = 0;
        int                GV_CurRow        = 0;
        bool               GV_RowOpen       = false;
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
