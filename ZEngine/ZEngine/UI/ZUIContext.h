#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUIFont.h>
#include <cstdint>

namespace ZEngine::UI
{

    /// @brief Single source of truth for all UI colors.
    ///
    /// Swap the whole struct at runtime to change the entire editor theme.
    /// Values are sRGB linear [0,1] RGBA floats; pack with ZUIPackColor for rendering.
    struct ZUITheme
    {
        // ZodiacEngine Dark — cool blue-dark palette with teal #4EC9B0 accent.
        // Inspired by VS Code Dark+, GitHub Dark Dimmed and One Dark.
        // Subtle +5% blue tint on backgrounds avoids flat gray monotony.
        // All values sRGB [0,1]; hex refs are approximate.

        // --- Backgrounds (darkest → lightest) ---
        float WindowBg[4]             = {0.067f, 0.067f, 0.082f, 1.00f}; // #111115  editor area
        float PanelBg[4]              = {0.102f, 0.102f, 0.125f, 1.00f}; // #1a1a20  panel content
        float PanelBgAlt[4]           = {0.133f, 0.133f, 0.161f, 1.00f}; // #222229  alt rows / headers
        float TitleBarBg[4]           = {0.157f, 0.157f, 0.192f, 1.00f}; // #282831  tab bar (unfocused panel)
        float TitleBgActive[4]        = {0.176f, 0.176f, 0.216f, 1.00f}; // slightly lighter — focused panel
        float HeaderBg[4]             = {0.306f, 0.788f, 0.690f, 0.18f}; // teal 18% collapsing header
        float MenuBarBg[4]            = {0.180f, 0.180f, 0.220f, 1.00f}; // #2e2e38  menu + toolbar bars
        float InputBg[4]              = {0.180f, 0.180f, 0.220f, 1.00f}; // #2e2e38  input fields

        // Buttons — teal family
        float ButtonBg[4]             = {0.035f, 0.384f, 0.322f, 1.00f}; // #093e35  rest (dark teal)
        float ButtonHoveredBg[4]      = {0.055f, 0.529f, 0.431f, 1.00f}; // #0e876e  hover
        float ButtonActiveBg[4]       = {0.192f, 0.627f, 0.541f, 1.00f}; // #31a08a  active

        // Input interactive states
        float InputHoveredBg[4]       = {0.216f, 0.216f, 0.259f, 1.00f}; // #373742
        float InputActiveBg[4]        = {0.216f, 0.216f, 0.259f, 1.00f}; // same
        float HeaderHoveredBg[4]      = {0.306f, 0.788f, 0.690f, 0.50f}; // teal 50% — ImGui ~0.80
        float HeaderActiveBg[4]       = {0.306f, 0.788f, 0.690f, 0.70f}; // teal 70%

        // --- Tabs (4-state machine matching ImGui ImGuiCol_Tab* exactly) ---
        // Visual hierarchy lightest→darkest:
        // TabHoveredBg > TabInactiveBg > TitleBarBg(bar) > TabDimmedBg > TabDimmedSelectedBg > TabActiveBg
        float TabActiveBg[4]          = {0.102f, 0.102f, 0.125f, 1.00f}; // selected, focused — = PanelBg (sinks into content)
        float TabInactiveBg[4]        = {0.180f, 0.180f, 0.220f, 0.90f}; // inactive, focused — lighter than bar (floats on it)
        float TabHoveredBg[4]         = {0.196f, 0.196f, 0.240f, 1.00f}; // hovered inactive — even lighter
        float TabDimmedBg[4]          = {0.145f, 0.145f, 0.178f, 0.85f}; // inactive, unfocused panel — muted
        float TabDimmedSelectedBg[4]  = {0.118f, 0.118f, 0.147f, 1.00f}; // selected, unfocused panel
        float TabActiveBorder[4]      = {0.306f, 0.788f, 0.690f, 1.00f}; // #4EC9B0  teal overline
        float TabInactiveBorder[4]    = {0.235f, 0.235f, 0.280f, 0.40f}; // subtle border
        float TabAccent[4]            = {0.306f, 0.788f, 0.690f, 1.00f}; // teal

        // --- Rows ---
        float RowHoverBg[4]           = {0.306f, 0.788f, 0.690f, 0.09f}; // teal 9%
        float RowSelectedBg[4]        = {0.306f, 0.788f, 0.690f, 0.22f}; // teal 22%
        float RowRootBg[4]            = {0.306f, 0.788f, 0.690f, 0.11f}; // teal 11%
        float SelectionBg[4]          = {0.207f, 0.514f, 0.894f, 0.45f}; // text selection (VS Code blue)

        // --- Status bar ---
        float StatusBarBg[4]          = {0.200f, 0.627f, 0.537f, 1.00f}; // slightly dark teal

        // --- Text ---
        float TextDefault[4]          = {0.843f, 0.843f, 0.886f, 1.00f}; // #d7d7e2  cool near-white
        float TextDim[4]              = {0.431f, 0.431f, 0.510f, 1.00f}; // #6e6e82  blue-gray
        float TextAccent[4]           = {0.306f, 0.788f, 0.690f, 1.00f}; // #4EC9B0  teal
        float TextWarn[4]             = {0.949f, 0.741f, 0.141f, 1.00f}; // #f2bd24
        float TextError[4]            = {0.937f, 0.325f, 0.314f, 1.00f}; // #ef5350

        // --- Widget accent marks ---
        float CheckMark[4]            = {0.306f, 0.788f, 0.690f, 1.00f}; // teal
        float SliderGrab[4]           = {0.200f, 0.627f, 0.537f, 1.00f}; // teal dark
        float SliderGrabActive[4]     = {0.306f, 0.788f, 0.690f, 1.00f}; // teal full

        // Scrollbar
        float ScrollbarBg[4]          = {0.000f, 0.000f, 0.000f, 0.00f}; // transparent
        float ScrollbarGrab[4]        = {0.420f, 0.420f, 0.420f, 0.40f}; // VS Code scrollbarSlider.background
        float ScrollbarGrabHov[4]     = {0.620f, 0.620f, 0.620f, 0.65f}; // VS Code scrollbarSlider.hoverBackground
        float ScrollbarGrabAct[4]     = {0.740f, 0.740f, 0.740f, 0.80f}; // VS Code scrollbarSlider.activeBackground

        // Plot
        float PlotLines[4]            = {0.306f, 0.788f, 0.690f, 0.80f};
        float PlotLinesHov[4]         = {0.306f, 0.788f, 0.690f, 1.00f};
        float PlotHistogram[4]        = {0.200f, 0.627f, 0.537f, 0.90f};
        float PlotHistogramHov[4]     = {0.306f, 0.788f, 0.690f, 1.00f};

        // Table
        float TableRowBgAlt[4]        = {1.000f, 1.000f, 1.000f, 0.03f};

        // --- Borders ---
        float PanelBorder[4]          = {0.235f, 0.235f, 0.290f, 1.00f}; // cool gray border
        float PanelFocusBorder[4]     = {0.306f, 0.788f, 0.690f, 1.00f}; // teal focus strip
        float PanelInactiveOverlay[4] = {0.000f, 0.000f, 0.000f, 0.05f}; // 5% dim
        float ButtonBorder[4]         = {0.000f, 0.000f, 0.000f, 0.00f}; // none
        float InputBorder[4]          = {0.235f, 0.235f, 0.290f, 0.80f}; // cool gray
        float InputFocusBorder[4]     = {0.306f, 0.788f, 0.690f, 1.00f}; // teal
        float Separator[4]            = {0.235f, 0.235f, 0.290f, 0.50f}; // cool gray 50%

        // --- Popup ---
        float PopupBg[4]              = {0.184f, 0.184f, 0.224f, 1.00f}; // ImGui: ImGuiCol_PopupBg

        // --- DataTable ---
        float TableHeaderBg[4]        = {0.165f, 0.165f, 0.204f, 1.00f}; // header row background
        float TableBorderLight[4]     = {0.220f, 0.220f, 0.270f, 1.00f}; // inner cell borders
        float TableBorderStrong[4]    = {0.350f, 0.350f, 0.420f, 1.00f}; // outer border / resize grip
    };

    /// @brief Per-frame style metrics and behavioral properties.
    ///
    /// Analogous to ImGui's ImGuiStyle (non-color fields).
    /// Do NOT mutate ctx->Style.* directly — use ZUIStylePushFloat / ZUIStylePop.
    struct ZUIStyle
    {
        // Global
        float Alpha                      = 1.f;
        float DisabledAlpha              = 0.38f; // ImGui: DisabledAlpha

        // Font
        float FontSize                   = 13.f; // ImGui: g.FontSize — sync after ZUIFontAtlasBake

        // Padding / Spacing
        float WindowPadding[2]           = {8.f, 8.f};
        float FramePadding[2]            = {4.f, 3.f}; // ImGui: FramePadding
        float ItemSpacing[2]             = {8.f, 4.f}; // ImGui: ItemSpacing
        float ItemInnerSpacing[2]        = {4.f, 4.f}; // ImGui: ItemInnerSpacing
        float CellPadding[2]             = {4.f, 2.f}; // ImGui: CellPadding

        // Rounding
        float WindowRounding             = 0.f;
        float ChildRounding              = 0.f;
        float FrameRounding              = 3.f; // ImGui: FrameRounding
        float PopupRounding              = 4.f; // ImGui: PopupRounding
        float ScrollbarRounding          = 9.f; // ImGui: ScrollbarRounding
        float GrabRounding               = 3.f; // ImGui: GrabRounding
        float TabRounding                = 3.f; // ImGui: TabRounding (top corners only)

        // Border Sizes
        float WindowBorderSize           = 1.f;
        float ChildBorderSize            = 1.f;
        float FrameBorderSize            = 0.f;
        float PopupBorderSize            = 1.f;
        float TabBorderSize              = 0.f;
        float TabBarBorderSize           = 1.f; // ImGui: TabBarBorderSize
        float TabBarOverlineSize         = 2.f; // ImGui: TabBarOverlineSize
        float SeparatorTextBorderSize    = 3.f;

        // Tabs
        float TabMinWidthForClose        = 0.f; // ImGui: TabMinWidthForCloseButton

        // Scrollbar
        float ScrollbarSize              = 10.f;  // VS Code: 10px thin scrollbar
        float ScrollbarMinThumbPx        = 20.f;  // VS Code: reasonable minimum thumb
        float ScrollbarAutoHideAlpha     = 0.15f; // floor alpha at rest — VS Code sidebar keeps a faint hint

        // Grab
        float GrabMinSize                = 12.f; // ImGui: GrabMinSize

        // Tree
        float IndentSpacing              = 21.f; // ImGui: IndentSpacing
        float ColumnsMinSpacing          = 6.f;

        // Alignment
        float ButtonTextAlign[2]         = {0.5f, 0.5f};
        float SelectableTextAlign[2]     = {0.f, 0.f};
        float SeparatorTextAlign[2]      = {0.f, 0.5f};
        float SeparatorTextPadding[2]    = {20.f, 3.f};

        // Popup
        float PopupMinWidth              = 200.f;
        float PopupInnerPaddingX         = 2.f;

        // Docking (tab/panel specifics)
        float TabIconSize                = 8.f;   // colored icon dot size in tab/title strip
        float TabGhostContentH           = 39.f;  // drag ghost content area height
        float DataTableDefaultColumnW    = 100.f; // ZUIDataTable fallback column width

        // Animation
        float HoverAnimSpeed             = 50.f; // expf(-HoverAnimSpeed * dt)  ~65ms to 95%
        float ActiveAnimSpeed            = 60.f; // expf(-ActiveAnimSpeed * dt) ~50ms to 95%
        float CursorBlinkRate            = 1.f;  // text cursor blink period in seconds

        // Docking
        float DockingSeparatorSize       = 2.f; // ImGui: DockingSeparatorSize
        float DockingSeparatorSizeRest   = 1.f;
        float DockingGrabWidth           = 6.f;
        float DockingHoverBandWidth      = 6.f; ///< Width of the tinted band shown on divider hover (px). Set to 0 to disable.
        float DockingDropZoneEdge        = 0.25f;
        float DockingDropPreviewAlpha    = 0.12f;
        float DockingDragThreshold       = 8.f;
        float DockingTabReorderThreshold = 5.f;
        float DockingUndockVertical      = 12.f;
        float DockingMinTabWidth         = 40.f;
        float DockingFocusBorderWidth    = 2.f;
        // Show a left-edge accent strip on the focused panel.
        // Default false: the active tab's teal overline is sufficient visual focus indicator.
        bool  ShowFocusBorder            = false;

        // Renderer
        float DropShadowOffset           = 4.f;
        float DropShadowAlpha            = 0.38f;
        float HoverOverlayAlpha          = 0.15f;
        float MouseScrollSpeed           = 48.f; // px per scroll unit (VS Code uses ~50)
        float ScrollSmoothSpeed          = 50.f; // exponential-lerp speed — 50 → 57% first-frame, ~66ms settle

        // Plot
        float PlotLineThickness          = 1.5f;

        // Non-pushable config (bool; not compatible with float push/pop stack)
        bool  DefaultAutoHideTabBar      = false; // ImGui default: always show tab bar

        // Derived — set by ZUIStyleUpdate(), never manually
        float FrameHeight                = 19.f; // = FontSize + FramePadding[1] * 2
    };

    /// @brief Recompute derived style fields (FrameHeight) after changing FontSize or FramePadding.
    ///
    /// ZUIBeginFrame calls this automatically each frame; call manually after
    /// ZUIStylePushFloat when changed values affect FrameHeight.
    /// @param s Style to update.
    inline void ZUIStyleUpdate(ZUIStyle* s)
    {
        s->FrameHeight = s->FontSize + s->FramePadding[1] * 2.f;
    }

    /// @brief Token for ZUIStylePushFloat / ZUIStylePop.
    ///
    /// These are the ONLY legal way to temporarily override a float style property.
    /// Mutating ctx->Style.* directly without push/pop is a contract violation.
    enum ZUIStyleVar : uint32_t
    {
        ZUIStyleVar_Alpha                   = 0,  ///< Maps to ZUIStyle::Alpha.
        ZUIStyleVar_DisabledAlpha           = 1,  ///< Maps to ZUIStyle::DisabledAlpha.
        ZUIStyleVar_FramePaddingX           = 2,  ///< Maps to ZUIStyle::FramePadding[0].
        ZUIStyleVar_FramePaddingY           = 3,  ///< Maps to ZUIStyle::FramePadding[1].
        ZUIStyleVar_ItemSpacingX            = 4,  ///< Maps to ZUIStyle::ItemSpacing[0].
        ZUIStyleVar_ItemSpacingY            = 5,  ///< Maps to ZUIStyle::ItemSpacing[1].
        ZUIStyleVar_ItemInnerSpacingX       = 6,  ///< Maps to ZUIStyle::ItemInnerSpacing[0].
        ZUIStyleVar_ItemInnerSpacingY       = 7,  ///< Maps to ZUIStyle::ItemInnerSpacing[1].
        ZUIStyleVar_FrameRounding           = 8,  ///< Maps to ZUIStyle::FrameRounding.
        ZUIStyleVar_PopupRounding           = 9,  ///< Maps to ZUIStyle::PopupRounding.
        ZUIStyleVar_ScrollbarRounding       = 10, ///< Maps to ZUIStyle::ScrollbarRounding.
        ZUIStyleVar_GrabRounding            = 11, ///< Maps to ZUIStyle::GrabRounding.
        ZUIStyleVar_TabRounding             = 12, ///< Maps to ZUIStyle::TabRounding.
        ZUIStyleVar_WindowBorderSize        = 13, ///< Maps to ZUIStyle::WindowBorderSize.
        ZUIStyleVar_FrameBorderSize         = 14, ///< Maps to ZUIStyle::FrameBorderSize.
        ZUIStyleVar_PopupBorderSize         = 15, ///< Maps to ZUIStyle::PopupBorderSize.
        ZUIStyleVar_TabBarBorderSize        = 16, ///< Maps to ZUIStyle::TabBarBorderSize.
        ZUIStyleVar_TabBarOverlineSize      = 17, ///< Maps to ZUIStyle::TabBarOverlineSize.
        ZUIStyleVar_IndentSpacing           = 18, ///< Maps to ZUIStyle::IndentSpacing.
        ZUIStyleVar_ScrollbarSize           = 19, ///< Maps to ZUIStyle::ScrollbarSize.
        ZUIStyleVar_GrabMinSize             = 20, ///< Maps to ZUIStyle::GrabMinSize.
        ZUIStyleVar_DockingFocusBorderWidth = 21, ///< Maps to ZUIStyle::DockingFocusBorderWidth.
        ZUIStyleVar_HoverAnimSpeed          = 22, ///< Maps to ZUIStyle::HoverAnimSpeed.
        ZUIStyleVar_ActiveAnimSpeed         = 23, ///< Maps to ZUIStyle::ActiveAnimSpeed.
        ZUIStyleVar_COUNT                   = 24  ///< Sentinel — not a valid var.
    };

    /// @brief Per-box state that survives across frames (hover/active animation, scroll, undo).
    ///
    /// Stored in the open-addressing hash table ZUIPersistentStore and keyed by ZUIBox::Key.
    /// The layout solver writes ScreenMin/ScreenMax each frame for boxes that have ZUI_Clickable.
    struct ZUIPersistentState
    {
        float   HotT               = 0.f;
        float   ActiveT            = 0.f;
        float   ScrollX            = 0.f; // current (animated) scroll position
        float   ScrollY            = 0.f;
        float   ScrollXTarget      = 0.f; // destination; wheel input writes here; ScrollX lerps toward it
        float   ScrollYTarget      = 0.f;
        float   MaxScrollY         = 0.f; // set by layout solver; clamped in interaction pass
        float   ScrollbarShowTimer = 0.f; // seconds remaining; reset on scroll/drag; drives scrollbar alpha
        float   UserData           = -1.f;
        float   ScreenMinX         = 0.f;
        float   ScreenMinY         = 0.f;
        float   ScreenMaxX         = 0.f;
        float   ScreenMaxY         = 0.f;
        float   MaxScrollX         = 0.f; // set by layout solver for ZUI_Scrollable+LayoutAxis::X
        int32_t SelectStart        = -1;  // text selection anchor (-1 = no selection)
    };

    /// @brief One bucket in the open-addressing persistent state hash table.
    struct ZUIPersistentSlot
    {
        uint64_t           Key   = 0; ///< FNV-1a hash of the box key; 0 = empty bucket.
        ZUIPersistentState State = {};
    };

    /// @brief Open-addressing hash table mapping box keys to ZUIPersistentState.
    ///
    /// Capacity must be a power of two; probing uses a bitmask.
    struct ZUIPersistentStore
    {
        ZUIPersistentSlot* Slots    = nullptr;
        uint32_t           Capacity = 0; ///< Must be a power of two.
        uint32_t           Count    = 0; ///< Number of occupied slots.
    };

    /// @brief Central ZUI state object.
    ///
    /// Holds all per-frame and persistent data: the box tree (FrameArena),
    /// persistent box state (PersistentArena), input state, style, theme,
    /// the draw list, and all transient widget state (popups, DataTable, etc.).
    /// One context per window; created by ZUIContextInit, destroyed by ZUIContextDestroy.
    struct ZUIContext
    {
        // sub-arenas carved from the engine's main arena via ZUIContextInit
        ZEngine::Core::Memory::ArenaAllocator FrameArena      = {}; // Clear()-ed each BeginFrame; all ZUIBox* are stale after
        ZEngine::Core::Memory::ArenaAllocator PersistentArena = {}; // never cleared; holds the persistent state table

        // box tree — all pointers into FrameArena
        ZUIBox*                               Root            = nullptr;
        ZUIBox*                               Current         = nullptr;

        // persistent state — open-addressing hash table in PersistentArena
        ZUIPersistentStore                    StateStore      = {};

        // Single shared font atlas (ImGui approach: one texture, all fonts).
        // Set by Editor::OnInitialized after ZUIFontAtlasBake.
        ZUIFontAtlas*                         Atlas           = nullptr;

        // Convenience accessors — delegate to Atlas
        ZUIFont*                              GetFont(ZUIFontSize size) const
        {
            if (!Atlas)
            {
                return nullptr;
            }
            if (size == ZUIFontSize::Small && Atlas->Small)
                return Atlas->Small;
            if (size == ZUIFontSize::Header && Atlas->Header)
                return Atlas->Header;
            return Atlas->Body;
        }

        // input state — written by ZUILayer each frame before ZUIBeginFrame
        float    MousePos[2]         = {};
        float    PrevMousePos[2]     = {}; // saved at end of ZUIEndFrame for drag-delta
        bool     MouseDown[3]        = {};
        bool     MousePressed[3]     = {};
        bool     MouseReleased[3]    = {};
        float    ScrollDelta         = 0.f;
        float    DeltaTime           = 0.f;
        float    Time                = 0.f;   // accumulated seconds since init
        bool     BackspacePressed    = false; // set by ZUILayer::OnKeyPressed, cleared in ZUIEndFrame
        bool     BackspaceHeld       = false; // true while key is physically down

        // interaction state — updated by ZUIInteractionPass
        uint64_t HotKey              = 0;
        uint64_t ActiveKey           = 0;
        uint64_t FocusKey            = 0;

        // text input — written by OnTextInputRaised
        char     TextInput[32]       = {};
        uint32_t TextInputLen        = 0;
        // Clipboard write request — written by ZUITextField (Ctrl+C), read+cleared by ZUILayer
        char     ClipboardWrite[512] = {};

        // capacity caps used by layout and interaction passes
        uint32_t MaxBoxesPerFrame    = 0;

        // Style system — metrics, spacing, rounding (ImGui: ImGuiStyle non-color fields)
        ZUIStyle Style               = {};

        // Push/pop stack for temporary style overrides (64 slots, no dynamic allocation)
        struct ZUIStyleEntry
        {
            ZUIStyleVar Id  = ZUIStyleVar_Alpha; ///< Which property was overridden.
            float       Old = 0.f;               ///< Previous value to restore on pop.
        };
        ZUIStyleEntry             StyleStack[64]       = {};
        uint32_t                  StyleStackDepth      = 0;

        // active color theme — swap to retheme the whole UI at runtime
        ZUITheme                  Theme                = {};

        // Vector draw list — populated by PreparePayload each frame (FrameArena-backed)
        ZUIDrawList               DrawList             = {};

        // current swapchain dimensions — set by AppRenderPipeline::BeginOverlayFrame each frame
        uint32_t                  ScreenW              = 1280;
        uint32_t                  ScreenH              = 720;
        // display content scale (glfwGetWindowContentScale); 1.0=standard, 2.0=Retina.
        // Widgets multiply logical pixel sizes by this to stay readable at any DPI.
        float                     UIScale              = 1.f;
        // guard against per-frame ContentScale log spam — log only once
        bool                      UIScaleLogged        = false;

        // drag-and-drop — source is set by ZUIBeginDragSource while a box is held+moving;
        // drop result (DragDropFired/DragTargetKey) is set by ZUIInteractionPass on mouse-release
        // and cleared at the START of the next ZUIEndFrame so BuildUI can read it
        uint64_t                  DragSourceKey        = 0;
        char                      DragPayload[512]     = {};
        uint32_t                  DragPayloadLen       = 0;
        bool                      DragDropFired        = false;
        uint64_t                  DragTargetKey        = 0;

        // set by ZUISceneViewportComponent each BuildUI frame; read by Editor::ProcessEvent
        // to gate camera-controller mouse routing
        bool                      ViewportHovered      = false;
        int                       ResizeCursor         = 0; // 0=default 1=H-resize 2=V-resize; set by panel dividers, read by ZUILayer

        // Modifier key state — written by ZUILayer::OnKeyPressed/Released
        bool                      CtrlDown             = false;
        bool                      ShiftDown            = false;
        bool                      AltDown              = false;

        // Tab focus navigation — set by ZUILayer, consumed by ZUIEndFrame
        bool                      TabPressed           = false;
        bool                      ShiftTabPressed      = false;
        bool                      EscapePressed        = false; // clear FocusKey
        bool                      EnterPressed         = false; // confirm / deactivate field
        bool                      SpacePressed         = false; // activate focused button
        bool                      ArrowUpPressed       = false; // nudge drag float / combo nav
        bool                      ArrowDownPressed     = false;
        bool                      ArrowLeftPressed     = false; // text cursor left
        bool                      ArrowRightPressed    = false; // text cursor right
        bool                      HomePressed          = false; // cursor to start of field
        bool                      EndPressed           = false; // cursor to end of field
        bool                      CtrlCPressed         = false;
        bool                      CtrlXPressed         = false;
        bool                      CtrlBackspacePressed = false;
        bool                      CtrlAPressed         = false;
        bool                      CtrlZPressed         = false; // undo
        bool                      CtrlYPressed         = false; // redo
        bool                      DeletePressed        = false; // forward-delete at cursor
        // Held state for key-repeat (same mechanism as BackspaceHeld)
        bool                      ArrowLeftHeld        = false;
        bool                      ArrowRightHeld       = false;
        bool                      ArrowUpHeld          = false;
        bool                      ArrowDownHeld        = false;
        bool                      DeleteHeld           = false;
        float                     ArrowRepeatTimer     = 0.f;
        // Per-frame tracking updated in ZUISignalFromBox during the build pass
        uint64_t                  TabNavNextKey        = 0; // first clickable after FocusKey
        uint64_t                  TabNavPrevKey        = 0; // last clickable before FocusKey
        uint64_t                  TabNavFirstKey       = 0; // first clickable seen (wraparound)
        uint64_t                  TabNavLastKey        = 0; // last clickable seen (Shift+Tab wrap)
        bool                      TabNavSeenFocus      = false;

        // Input repeat — ZUIEndFrame advances the timer; after RepeatDelay
        // it fires BackspacePressed / ArrowPressed at RepeatRate hz
        float                     KeyRepeatTimer       = 0.f;
        static constexpr float    kRepeatDelay         = 0.45f; // s before first repeat
        static constexpr float    kRepeatRate          = 0.04f; // s between repeats

        // ZUIBeginDisabled / ZUIEndDisabled — widgets skip Clickable and dim colours
        bool                      Disabled             = false;
        int                       DisabledDepth        = 0; // supports nesting

        // Popup stack — supports nested popups (menus + submenus).
        // ZUIOpenPopup queues a push at the current PopupBuildDepth.
        // ZUIEndFrame applies the pending push (truncating deeper entries first).
        // ZUIBeginPopup renders if the key matches PopupStack[PopupBuildDepth]
        //   and increments PopupBuildDepth for nested content.
        // ZUIClosePopup clears the entire stack.
        // Interaction pass pops from innermost outward when pressing outside.
        static constexpr uint32_t kMaxPopupDepth       = 8;
        struct ZUIPopupEntry
        {
            uint64_t Key         = 0;
            ZUIBox*  Box         = nullptr; // set by ZUIBeginPopup; valid this frame
            ZUIBox*  SavedParent = nullptr; // ctx->Current before popup opened; restored on End
            float    PosX        = 0.f;
            float    PosY        = 0.f;
        };
        ZUIPopupEntry PopupStack[kMaxPopupDepth] = {};
        uint32_t      PopupStackSize             = 0; // active popup count
        uint32_t      PopupBuildDepth            = 0; // current render depth (reset in BeginFrame)
        uint64_t      PendingPopupKey            = 0; // queued by ZUIOpenPopup, applied in EndFrame
        uint32_t      PendingPopupDepth          = 0;
        float         PendingPopupPosX           = 0.f;
        float         PendingPopupPosY           = 0.f;
        uint64_t      ActiveModalKey             = 0; // modal (cannot close by clicking outside)

        // Tab bar state (single-level; reset by ZUIBeginTabBar)
        uint64_t      TabBarKey                  = 0;     // hash of active tab bar
        int           TabBarSelectedIdx          = 0;     // which tab is open
        int           TabBarCurrentIdx           = 0;     // iteration counter
        bool          TabItemWasSelected         = false; // did last BeginTabItem match?
        ZUIBox*       TabBarRowBox               = nullptr;

        // Basic table state (ZUIBeginTable / ZUIEndTable)
        int           TableColumns               = 0;
        int           TableCurrentCol            = -1;
        float*        TableColWidths             = nullptr; // FrameArena array
        ZUIBox*       TableRowBox                = nullptr;

        // TreeView state (ZUIBeginTreeView / ZUIEndTreeView)
        int           TV_Depth                   = 0;
        float         TV_IndentPx                = 21.f; // px per depth level — ImGui IndentSpacing
        float         TV_RowH                    = 22.f; // logical row height

        // DataTable state (ZUIBeginDataTable / ZUIEndDataTable)
        uint64_t      DT_Key                     = 0;
        int           DT_ColCount                = 0;
        int           DT_CurCol                  = -1;
        int           DT_RowIndex                = 0;
        bool          DT_InRow                   = false;
        ZUIBox*       DT_RowBox                  = nullptr;
        float*        DT_ColWidths               = nullptr; // FrameArena, size = DT_ColCount
        ZUIBox**      DT_SepBoxes                = nullptr; // FrameArena, size = DT_ColCount-1; height patched in EndDataTable
        const void*   DT_Cols                    = nullptr; // ZUIDataTableColumn* stored by BeginDataTable
        int           DT_SortCol                 = -1;      // -1 = unsorted
        bool          DT_SortAsc                 = true;
        bool          DT_SortChanged             = false;

        // GridView state (ZUIBeginGridView / ZUIEndGridView)
        float         GV_ItemW                   = 0.f;
        float         GV_ItemH                   = 0.f;
        int           GV_MaxCols                 = 1;
        int           GV_CurCol                  = 0;
        int           GV_CurRow                  = 0;
        bool          GV_RowOpen                 = false;
        float         PopupPos[2]                = {};      // unused — kept for ABI; pos now in PopupStack entry
        float         PopupDesiredW              = 0.f;     // optional fixed width (set by ZUIBeginCombo)
        ZUIBox*       ModalSavedParent           = nullptr; // ctx->Current saved by ZUIBeginModal
        ZUIBox*       ModalBox                   = nullptr; // active modal panel; set each frame while modal is open
        // Text field undo / redo
        // Per-field stacks stored in context (only the focused field uses them).
        // Undo: push BEFORE edit → Ctrl+Z pops and restores. Redo: push current
        // state when undoing → Ctrl+Y pops and restores.
        struct ZUIUndoEntry
        {
            char    Buf[512] = {}; ///< Null-terminated text content snapshot.
            int32_t Cursor   = 0;  ///< Cursor position at the time of the snapshot.
        };
        static constexpr uint32_t kUndoDepth            = 8;
        uint64_t                  UndoFieldKey          = 0;
        ZUIUndoEntry              UndoStack[kUndoDepth] = {};
        int32_t                   UndoTop               = 0; // next push index (0 = empty)
        ZUIUndoEntry              RedoStack[kUndoDepth] = {};
        int32_t                   RedoTop               = 0;

        // Keyboard navigation inside open popups (combos, menus)
        int                       PopupNavIdx           = -1; // keyboard-highlighted item index; -1 = none
        int                       PopupBuildIdx         = 0;  // incremented per ZUIComboItem/ZUISelectable in popup
        int                       PopupBuildCount       = 0;  // item count from previous popup frame (for clamping)
    };

    ZDEFINE_PTR(ZUIContext);

    // Lifecycle

    /// @brief Initialize a ZUIContext by carving sub-arenas from @p parent.
    /// @param ctx              ZUI context to initialize.
    /// @param parent           Parent arena to carve sub-arenas from.
    /// @param FrameArenaBytes      Bytes for per-frame allocations (boxes, labels, layout scratch).
    /// @param PersistentArenaBytes Bytes for the persistent state hash table.
    /// @param StateCapacity        Initial slot count for the state table; must be a power of two.
    /// @param MaxBoxesPerFrame     Upper bound on boxes per frame (sizes layout/interaction scratch).
    void                ZUIContextInit(ZUIContext* ctx, ZEngine::Core::Memory::ArenaAllocator* parent, size_t FrameArenaBytes, size_t PersistentArenaBytes, uint32_t StateCapacity, uint32_t MaxBoxesPerFrame);

    /// @brief Release sub-arenas. ctx itself is arena-allocated and is not freed.
    /// @param ctx Active ZUI context.
    void                ZUIContextDestroy(ZUIContext* ctx);

    // Per-frame

    /// @brief Begin a new UI frame — clears FrameArena, resets box tree, advances time.
    /// @param ctx Active ZUI context.
    /// @param dt Delta-time in seconds since the last frame.
    void                ZUIBeginFrame(ZUIContext* ctx, float dt);

    /// @brief End the UI frame — runs layout solve + interaction pass, flushes key states.
    /// @param ctx Active ZUI context.
    void                ZUIEndFrame(ZUIContext* ctx);

    // Box tree helpers

    /// @brief Allocate and link a new ZUIBox as a child of ctx->Current.
    /// @param ctx      Active ZUI context.
    /// @param key      Key/label string; the part after '##' is used only for hashing.
    /// @param key_len  Byte length of @p key.
    /// @param flags    Initial ZUIBoxFlags.
    /// @returns Pointer to the new box (arena-allocated; stale after next ZUIBeginFrame).
    ZUIBox*             ZUIPushBox(ZUIContext* ctx, const char* key, uint32_t key_len, ZUIBoxFlags flags);

    /// @brief Pop the current box — moves ctx->Current back to its parent.
    /// @param ctx Active ZUI context.
    void                ZUIPopBox(ZUIContext* ctx);

    // Utilities

    /// @brief Look up or insert a ZUIPersistentState slot for @p key (FNV-1a hash).
    /// @param store Persistent state hash table.
    /// @param key   FNV-1a hash key to look up.
    /// @returns Pointer to the state; nullptr only on table-full (asserts in debug).
    ZUIPersistentState* ZUIStateGetOrInsert(ZUIPersistentStore* store, uint64_t key);

    /// @brief Push a copy of @p str into @p arena.
    /// @param arena Arena to allocate the string copy from.
    /// @param str   Source string pointer.
    /// @param len   Byte length of @p str (excluding null terminator).
    /// @returns Non-owning ZUIStr view into the arena copy.
    ZUIStr              ZUIPushStr(ZEngine::Core::Memory::ArenaAllocator* arena, const char* str, uint32_t len);

    /// @brief Compute the FNV-1a 64-bit hash of a string.
    /// @param str String to hash.
    /// @param len Byte length of @p str.
    /// @returns Non-zero hash (0 is reserved for empty table slots).
    uint64_t            ZUIHashStr(const char* str, uint32_t len);

    // Style push/pop — ONLY legal mechanism for per-scope style overrides

    /// @brief Temporarily override one float style property on the push/pop stack.
    /// @param ctx Active ZUI context.
    /// @param var Property to override.
    /// @param val New value.
    void                ZUIStylePushFloat(ZUIContext* ctx, ZUIStyleVar var, float val);

    /// @brief Restore the most recently pushed style property.
    /// @param ctx Active ZUI context.
    void                ZUIStylePop(ZUIContext* ctx);

    // Style accessors — prefer these over ctx->Style.* direct reads.

    /// @brief Return the pre-computed frame height (FontSize + FramePadding.y × 2).
    /// @param ctx Active ZUI context.
    /// @returns Frame height in logical pixels.
    inline float        ZUIGetFrameHeight(const ZUIContext* ctx)
    {
        return ctx->Style.FrameHeight;
    }
    /// @brief Return horizontal frame padding.
    /// @param ctx Active ZUI context.
    /// @returns Horizontal frame padding in logical pixels.
    inline float ZUIGetFramePadX(const ZUIContext* ctx)
    {
        return ctx->Style.FramePadding[0];
    }
    /// @brief Return vertical frame padding.
    /// @param ctx Active ZUI context.
    /// @returns Vertical frame padding in logical pixels.
    inline float ZUIGetFramePadY(const ZUIContext* ctx)
    {
        return ctx->Style.FramePadding[1];
    }
    /// @brief Return horizontal item spacing.
    /// @param ctx Active ZUI context.
    /// @returns Horizontal item spacing in logical pixels.
    inline float ZUIGetItemSpacX(const ZUIContext* ctx)
    {
        return ctx->Style.ItemSpacing[0];
    }
    /// @brief Return vertical item spacing.
    /// @param ctx Active ZUI context.
    /// @returns Vertical item spacing in logical pixels.
    inline float ZUIGetItemSpacY(const ZUIContext* ctx)
    {
        return ctx->Style.ItemSpacing[1];
    }
    /// @brief Return inner item spacing (X axis).
    /// @param ctx Active ZUI context.
    /// @returns Inner item spacing in logical pixels.
    inline float ZUIGetInnerSpac(const ZUIContext* ctx)
    {
        return ctx->Style.ItemInnerSpacing[0];
    }

} // namespace ZEngine::UI
