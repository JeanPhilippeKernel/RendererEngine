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
        // ZodiacEngine Dark — cool blue-dark palette with teal #4EC9B0 accent.
        // Inspired by VS Code Dark+, GitHub Dark Dimmed and One Dark.
        // Backgrounds carry a subtle cool-blue tint (+5% blue) to avoid
        // flat gray monotony.  All values sRGB [0,1]; hex refs are approximate.

        // --- Backgrounds (darkest → lightest) ---
        float WindowBg[4]       = {0.067f,0.067f,0.082f,1.00f}; // #111115  editor area
        float PanelBg[4]        = {0.102f,0.102f,0.125f,1.00f}; // #1a1a20  panel content
        float PanelBgAlt[4]     = {0.133f,0.133f,0.161f,1.00f}; // #222229  alt rows / headers
        float TitleBarBg[4]     = {0.157f,0.157f,0.192f,1.00f}; // #282831  tab bar strip
        float TitleBgActive[4]  = {0.157f,0.157f,0.192f,1.00f}; // same
        float HeaderBg[4]       = {0.306f,0.788f,0.690f,0.18f}; // teal 18% collapsing header
        float MenuBarBg[4]      = {0.180f,0.180f,0.220f,1.00f}; // #2e2e38  menu + toolbar bars
        float InputBg[4]        = {0.180f,0.180f,0.220f,1.00f}; // #2e2e38  input fields

        // Buttons — teal family
        float ButtonBg[4]         = {0.035f,0.384f,0.322f,1.00f}; // #093e35  rest (dark teal)
        float ButtonHoveredBg[4]  = {0.055f,0.529f,0.431f,1.00f}; // #0e876e  hover
        float ButtonActiveBg[4]   = {0.192f,0.627f,0.541f,1.00f}; // #31a08a  active

        // Input interactive states
        float InputHoveredBg[4]   = {0.216f,0.216f,0.259f,1.00f}; // #373742
        float InputActiveBg[4]    = {0.216f,0.216f,0.259f,1.00f}; // same
        float HeaderHoveredBg[4]  = {0.306f,0.788f,0.690f,0.28f}; // teal 28%
        float HeaderActiveBg[4]   = {0.306f,0.788f,0.690f,0.45f}; // teal 45%

        // --- Tabs ---
        float TabActiveBg[4]       = {0.102f,0.102f,0.125f,1.00f}; // PanelBg (active tab merges)
        float TabInactiveBg[4]     = {0.000f,0.000f,0.000f,0.00f}; // transparent
        float TabActiveBorder[4]   = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  teal
        float TabInactiveBorder[4] = {0.235f,0.235f,0.280f,0.40f}; // subtle
        float TabAccent[4]         = {0.306f,0.788f,0.690f,1.00f}; // teal

        // --- Rows ---
        float RowHoverBg[4]    = {0.306f,0.788f,0.690f,0.09f}; // teal 9%
        float RowSelectedBg[4] = {0.306f,0.788f,0.690f,0.22f}; // teal 22%
        float RowRootBg[4]     = {0.306f,0.788f,0.690f,0.11f}; // teal 11%

        // --- Status bar ---
        float StatusBarBg[4]   = {0.200f,0.627f,0.537f,1.00f}; // slightly dark teal

        // --- Text ---
        float TextDefault[4]   = {0.843f,0.843f,0.886f,1.00f}; // #d7d7e2  cool near-white
        float TextDim[4]       = {0.431f,0.431f,0.510f,1.00f}; // #6e6e82  blue-gray
        float TextAccent[4]    = {0.306f,0.788f,0.690f,1.00f}; // #4EC9B0  teal
        float TextWarn[4]      = {0.949f,0.741f,0.141f,1.00f}; // #f2bd24
        float TextError[4]     = {0.937f,0.325f,0.314f,1.00f}; // #ef5350

        // --- Widget accent marks ---
        float CheckMark[4]        = {0.306f,0.788f,0.690f,1.00f}; // teal
        float SliderGrab[4]       = {0.200f,0.627f,0.537f,1.00f}; // teal dark
        float SliderGrabActive[4] = {0.306f,0.788f,0.690f,1.00f}; // teal full

        // Scrollbar
        float ScrollbarBg[4]      = {0.000f,0.000f,0.000f,0.00f}; // transparent
        float ScrollbarGrab[4]    = {0.216f,0.216f,0.259f,1.00f}; // #373742
        float ScrollbarGrabHov[4] = {0.318f,0.318f,0.380f,1.00f}; // lighter
        float ScrollbarGrabAct[4] = {0.306f,0.788f,0.690f,0.80f}; // teal drag

        // Plot
        float PlotLines[4]        = {0.306f,0.788f,0.690f,0.80f};
        float PlotLinesHov[4]     = {0.306f,0.788f,0.690f,1.00f};
        float PlotHistogram[4]    = {0.200f,0.627f,0.537f,0.90f};
        float PlotHistogramHov[4] = {0.306f,0.788f,0.690f,1.00f};

        // Table
        float TableRowBgAlt[4]    = {1.000f,1.000f,1.000f,0.03f};

        // --- Borders ---
        float PanelBorder[4]          = {0.235f,0.235f,0.290f,1.00f}; // cool gray border
        float PanelFocusBorder[4]     = {0.306f,0.788f,0.690f,1.00f}; // teal focus strip
        float PanelInactiveOverlay[4] = {0.000f,0.000f,0.000f,0.05f}; // 5% dim
        float ButtonBorder[4]         = {0.000f,0.000f,0.000f,0.00f}; // none
        float InputBorder[4]          = {0.235f,0.235f,0.290f,0.80f}; // cool gray
        float InputFocusBorder[4]     = {0.306f,0.788f,0.690f,1.00f}; // teal
        float Separator[4]            = {0.235f,0.235f,0.290f,0.50f}; // cool gray 50%
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
        bool               CtrlXPressed          = false; // cut (copy + clear)
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
        float              TV_IndentPx      = 21.f;   // px per depth level — ImGui IndentSpacing
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
        float              PopupDesiredW    = 0.f;  // optional fixed width (set by ZUIBeginCombo)
        uint64_t           ActivePopupKey   = 0;     // currently shown popup (frame-to-frame)
        ZUIBox*            ActivePopupBox   = nullptr; // set by ZUIBeginPopup; valid this frame
        ZUIBox*            PopupSavedParent = nullptr; // ctx->Current saved during popup build
        // Keyboard navigation inside open popups (combos, menus)
        int                PopupNavIdx      = -1;    // keyboard-highlighted item index; -1 = none
        int                PopupBuildIdx    = 0;     // incremented per ZUIComboItem/ZUISelectable in popup
        int                PopupBuildCount  = 0;     // item count from previous popup frame (for clamping)
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
