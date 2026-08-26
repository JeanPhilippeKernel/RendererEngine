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
        // VS Code Dark+ inspired palette.
        // All values are linear 0-1; hex refs are perceptual sRGB for designer reference.

        // --- Backgrounds ---
        float WindowBg[4]       = {0.118f,0.118f,0.118f,1.00f}; // #1e1e1e  root fill
        float PanelBg[4]        = {0.145f,0.145f,0.149f,1.00f}; // #252526  panel body
        float PanelBgAlt[4]     = {0.110f,0.110f,0.114f,1.00f}; // slightly darker alt rows
        float TitleBarBg[4]     = {0.176f,0.176f,0.176f,1.00f}; // #2d2d2d  tab bar strip
        float HeaderBg[4]       = {0.200f,0.200f,0.204f,1.00f}; // #333334  collapsing headers
        float MenuBarBg[4]      = {0.200f,0.200f,0.200f,1.00f}; // #333333  top menu bar
        float InputBg[4]        = {0.094f,0.094f,0.094f,1.00f}; // #181818  input / drag fields
        float ButtonBg[4]       = {0.000f,0.475f,0.800f,0.55f}; // #007acc  accent button

        // --- Tabs ---
        float TabActiveBg[4]    = {0.118f,0.118f,0.118f,1.00f}; // #1e1e1e  active = editor bg
        float TabInactiveBg[4]  = {0.000f,0.000f,0.000f,0.00f}; // transparent
        float TabActiveBorder[4]= {0.000f,0.475f,0.800f,1.00f}; // #007acc  VS Code blue
        float TabInactiveBorder[4]={0.200f,0.200f,0.200f,0.50f}; // #333333 subtle
        float TabAccent[4]      = {0.000f,0.475f,0.800f,1.00f}; // same as active border

        // --- Rows ---
        float RowHoverBg[4]     = {0.176f,0.176f,0.176f,0.60f}; // subtle row hover
        float RowSelectedBg[4]  = {0.012f,0.471f,0.706f,0.35f}; // #0278b4 tinted selection
        float RowRootBg[4]      = {0.000f,0.475f,0.800f,0.15f}; // root node tint

        // --- Status bar ---
        float StatusBarBg[4]    = {0.000f,0.475f,0.800f,1.00f}; // #007acc  VS Code blue

        // --- Text ---
        float TextDefault[4]    = {0.831f,0.831f,0.831f,1.00f}; // #d4d4d4  primary text
        float TextDim[4]        = {0.522f,0.522f,0.522f,1.00f}; // #858585  secondary / hint
        float TextAccent[4]     = {0.353f,0.722f,0.969f,1.00f}; // #5ab8f7  links / values
        float TextWarn[4]       = {0.949f,0.741f,0.141f,1.00f}; // #f2bd24  warnings
        float TextError[4]      = {0.937f,0.325f,0.314f,1.00f}; // #ef5350  errors

        // --- Borders ---
        float PanelBorder[4]       = {0.220f,0.220f,0.220f,1.00f}; // #383838 subtle
        float PanelFocusBorder[4]  = {0.000f,0.475f,0.800f,1.00f}; // #007acc focus accent
        float PanelInactiveOverlay[4]={0.f,  0.f,  0.f,  0.06f };  // 6% black dim
        float ButtonBorder[4]      = {0.280f,0.280f,0.320f,0.80f};
        float InputBorder[4]       = {0.280f,0.280f,0.320f,0.80f};
        float InputFocusBorder[4]  = {0.000f,0.475f,0.800f,1.00f}; // #007acc
        float Separator[4]         = {0.200f,0.200f,0.200f,0.70f}; // #333333 at 70%
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
