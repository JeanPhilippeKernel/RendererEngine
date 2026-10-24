#pragma once
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIColorState.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUIFont.h>
#include <ZEngine/UI/ZUITokens.h>
#include <cstdint>

namespace ZEngine::UI
{
    /// Path namespaces are explicit: a logical VFS path must never be opened as a native filename.
    enum class ZUIDragPayloadKind : uint8_t
    {
        Opaque,
        NativePath,
        VFSPath,
    };

    /// @brief UI input ownership consumed by application-level input clients.
    struct ZUIInputCapture
    {
        bool Pointer  = false;
        bool Keyboard = false;
    };

    /// @brief Single source of truth for all UI colors.
    ///
    /// Defaults are the Figma "Editor tokens" (Dark) set: every value below is a Tokens::
    /// reference (see ZUITokens.h) so the editor can be diffed against the design by name.
    /// Entries marked "derived" have no Figma counterpart yet; they are built from tokens.
    /// Swap the whole struct at runtime to change the entire editor theme.
    /// Values are sRGB [0,1] RGBA floats; pack with ZUIPackColor for rendering.
    struct ZUITheme
    {
        // Backgrounds
        float WindowBg[4]             = ZUI_TOKEN(Tokens::BgWindow);   // bg/window   editor area, menu bar, tab strip
        float WindowBackdrop[4]       = ZUI_TOKEN(Tokens::BgBackdrop); // frame fill visible in the gaps between docks
        float PanelBg[4]              = ZUI_TOKEN(Tokens::BgPanel);    // bg/panel    panel content, toolbar
        float PanelBgAlt[4]           = ZUI_TOKEN(Tokens::BgSurface);  // bg/surface  alt rows / headers
        float TitleBarBg[4]           = ZUI_TOKEN(Tokens::BgWindow);   // bg/window   tab strip, status bar (unfocused panel)
        float TitleBgActive[4]        = ZUI_TOKEN(Tokens::BgPanel);    // bg/panel    panel header (focused panel)
        float HeaderBg[4]             = ZUI_TOKEN(Tokens::BgSurface);  // bg/surface  collapsing / section header
        float MenuBarBg[4]            = ZUI_TOKEN(Tokens::BgWindow);   // bg/window   menu bar
        float ThumbnailBg[4]          = ZUI_TOKEN(Tokens::BgControl);  // bg/control  asset thumbnail well
        float InputBg[4]              = ZUI_TOKEN(Tokens::BgWindow);   // bg/window   input fields

        // Buttons
        float ButtonBg[4]             = ZUI_TOKEN(Tokens::BgSurface);       // bg/surface          rest (toolbar buttons, sampled #292929)
        float ButtonHoveredBg[4]      = ZUI_TOKEN(Tokens::BgHover);         // bg/hover            hover
        float ButtonActiveBg[4]       = ZUI_TOKEN(Tokens::AccentSelection); // accent/selection    pressed / toggled on

        // Input interactive states
        float InputHoveredBg[4]       = ZUI_TOKEN(Tokens::BgSurface);       // derived: one step above the field
        float InputActiveBg[4]        = ZUI_TOKEN(Tokens::BgSurface);       // derived
        float HeaderHoveredBg[4]      = ZUI_TOKEN(Tokens::BgHover);         // bg/hover
        float HeaderActiveBg[4]       = ZUI_TOKEN(Tokens::AccentSelection); // accent/selection

        // Tabs. In the design the active tab sinks into the panel with a 2px accent overline;
        // inactive tabs have no fill and sit directly on the tab strip.
        float TabActiveBg[4]          = ZUI_TOKEN(Tokens::BgPanel);                          // bg/panel
        float TabInactiveBg[4]        = ZUI_TOKEN(Tokens::WithAlpha(Tokens::BgWindow, 0.f)); // no fill
        float TabHoveredBg[4]         = ZUI_TOKEN(Tokens::BgSurface);                        // derived
        float TabDimmedBg[4]          = ZUI_TOKEN(Tokens::WithAlpha(Tokens::BgWindow, 0.f)); // no fill, unfocused panel
        float TabDimmedSelectedBg[4]  = ZUI_TOKEN(Tokens::BgPanel);                          // bg/panel, unfocused panel
        float TabActiveBorder[4]      = ZUI_TOKEN(Tokens::AccentBlue);                       // accent/blue  overline
        float TabInactiveBorder[4]    = ZUI_TOKEN(Tokens::WithAlpha(Tokens::BorderSubtle, 0.f));
        float TabAccent[4]            = ZUI_TOKEN(Tokens::AccentBlue); // accent/blue

        // Rows
        float RowHoverBg[4]           = ZUI_TOKEN(Tokens::BgSurface);       // derived
        float RowSelectedBg[4]        = ZUI_TOKEN(Tokens::AccentSelection); // accent/selection
        float RowRootBg[4]            = ZUI_TOKEN(Tokens::BgSurface);       // derived
        float SelectionBg[4]          = ZUI_TOKEN(Tokens::AccentSelection); // accent/selection  text selection

        // Status bar
        float StatusBarBg[4]          = ZUI_TOKEN(Tokens::BgWindow); // bg/window

        // Text
        float TextDefault[4]          = ZUI_TOKEN(Tokens::TextPrimary);   // text/primary
        float TextDim[4]              = ZUI_TOKEN(Tokens::TextSecondary); // text/secondary
        float TextAccent[4]           = ZUI_TOKEN(Tokens::AccentBlue);    // accent/blue
        float TextWarn[4]             = ZUI_TOKEN(Tokens::AccentOrange);  // accent/orange
        float TextError[4]            = ZUI_TOKEN(Tokens::AccentRed);     // accent/red

        // Widget accent marks
        float CheckMark[4]            = ZUI_TOKEN(Tokens::TextSecondary); // text/secondary (the design's check icon colour)
        float SliderGrab[4]           = ZUI_TOKEN(Tokens::TextPrimary);   // text/primary       thumb
        float SliderGrabActive[4]     = ZUI_TOKEN(Tokens::TextPrimary);   // text/primary

        // Figma Scrollbar: transparent track, bg/hover thumb, border/strong on interaction.
        float ScrollbarBg[4]          = {0.000f, 0.000f, 0.000f, 0.00f}; // transparent
        float ScrollbarGrab[4]        = ZUI_TOKEN(Tokens::BgHover);
        float ScrollbarGrabHov[4]     = ZUI_TOKEN(Tokens::BorderStrong);
        float ScrollbarGrabAct[4]     = ZUI_TOKEN(Tokens::BorderStrong);

        // Plot (derived from the accent)
        float PlotLines[4]            = ZUI_TOKEN(Tokens::WithAlpha(Tokens::AccentBlue, 0.80f));
        float PlotLinesHov[4]         = ZUI_TOKEN(Tokens::AccentBlue);
        float PlotHistogram[4]        = ZUI_TOKEN(Tokens::WithAlpha(Tokens::AccentBlue, 0.90f));
        float PlotHistogramHov[4]     = ZUI_TOKEN(Tokens::AccentBlue);

        // Table
        float TableRowBgAlt[4]        = {1.000f, 1.000f, 1.000f, 0.03f};

        // Borders
        float PanelBorder[4]          = ZUI_TOKEN(Tokens::BorderSubtle); // border/subtle
        float PanelFocusBorder[4]     = ZUI_TOKEN(Tokens::AccentBlue);   // accent/blue
        float PanelInactiveOverlay[4] = {0.000f, 0.000f, 0.000f, 0.05f}; // 5% dim
        float ButtonBorder[4]         = {0.000f, 0.000f, 0.000f, 0.00f}; // none
        float InputBorder[4]          = ZUI_TOKEN(Tokens::BorderField);  // border/field
        float InputFocusBorder[4]     = ZUI_TOKEN(Tokens::AccentBlue);   // accent/blue
        float Separator[4]            = ZUI_TOKEN(Tokens::BorderSubtle); // border/subtle

        // Popup / menu
        float PopupBg[4]              = ZUI_TOKEN(Tokens::BgPanel);      // bg/panel
        float PopupBorder[4]          = ZUI_TOKEN(Tokens::BorderStrong); // border/strong  (menus and flyouts)

        // DataTable
        float TableHeaderBg[4]        = ZUI_TOKEN(Tokens::BgSurface);    // bg/surface  header row background
        float TableBorderLight[4]     = ZUI_TOKEN(Tokens::BorderSubtle); // border/subtle  inner cell borders
        float TableBorderStrong[4]    = ZUI_TOKEN(Tokens::BorderStrong); // border/strong  outer border / resize grip
    };

    /// @brief Per-frame style metrics and behavioral properties.
    ///
    /// Analogous to ImGui's ImGuiStyle (non-color fields).
    /// Do NOT mutate ctx->Style.* directly — use ZUIStylePushFloat / ZUIStylePop.
    struct ZUIStyle
    {
        // Global
        float Alpha                      = 1.f;

        // Font
        float FontSize                   = 11.f; // body size (Figma Inter 11) — sync after ZUIFontAtlasBake

        // Padding / Spacing
        float WindowPadding[2]           = {8.f, 8.f};
        float FramePadding[2]            = {8.f, 5.5f}; // Figma field: 8 px horizontal, 22 px tall at 11 px text
        float ItemSpacing[2]             = {8.f, 4.f};  // ImGui: ItemSpacing
        float ItemInnerSpacing[2]        = {4.f, 4.f};  // ImGui: ItemInnerSpacing
        float MenuBarHeight              = 28.f;        // Figma: application menu row
        float CellPadding[2]             = {4.f, 2.f};  // ImGui: CellPadding
        bool  DataTableColumnLines       = false;       // vertical separators between data-table columns (Figma tables have none)

        // Rounding
        float WindowRounding             = 0.f;
        float ChildRounding              = 0.f;
        float FrameRounding              = 3.f; // ImGui: FrameRounding
        float PopupRounding              = 6.f; // ImGui: PopupRounding
        float ScrollbarRounding          = Metrics::Radius3;
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
        float ScrollbarSize              = Metrics::Space10; // Hit lane; the visible thumb is 6 px.
        float ScrollbarMinThumbPx        = 20.f;             // VS Code: reasonable minimum thumb
        float ScrollbarAutoHideAlpha     = 0.15f;            // floor alpha at rest — VS Code sidebar keeps a faint hint

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
        float PopupInnerPaddingX         = 4.f;

        // Docking (tab/panel specifics)
        float TabBarHeight               = 30.f;            // Figma 'Panel tab strip'
        float TabWidth                   = 180.f;           // Figma 'Active panel tab'
        float DockGap                    = Metrics::Space4; // backdrop visible between docked panels
        float TabIconSize                = 8.f;             // colored icon dot size in tab/title strip
        float TabGhostContentH           = 39.f;            // drag ghost content area height
        float DataTableDefaultColumnW    = 100.f;           // ZUIDataTable fallback column width

        // Animation
        float HoverAnimSpeed             = 50.f;  // expf(-HoverAnimSpeed * dt)  ~65ms to 95%
        float ActiveAnimSpeed            = 60.f;  // expf(-ActiveAnimSpeed * dt) ~50ms to 95%
        float CursorBlinkRate            = 1.f;   // text cursor blink period in seconds
        bool  ReducedMotion              = false; // Snap hover, press, thumb and scroll animations.

        // Docking
        float DockingSeparatorSize       = Metrics::Space2;
        float DockingSeparatorSizeRest   = 0.f;             ///< Only the backdrop gap is visible at rest.
        float DockingGrabWidth           = Metrics::Space8; ///< Invisible resize hit lane, centered on the gap.
        float DockingHoverBandWidth      = Metrics::Space6; ///< Proximity band that activates the thin hover indicator; 0 disables hover.
        float DockingDropZoneEdge        = 0.25f;
        float DockingDropPreviewAlpha    = 0.12f;
        float DockingDragThreshold       = 8.f;
        float DockingTabReorderThreshold = 5.f;
        float DockingUndockVertical      = 12.f;
        float DockingMinTabWidth         = 40.f;
        float DockingFocusBorderWidth    = 2.f;
        // Show a left-edge accent strip on the focused panel.
        // Default false: the active tab's blue overline is sufficient visual focus indicator.
        bool  ShowFocusBorder            = false;

        // Renderer — Figma Shadow/Menu and Shadow/Modal (logical px).
        float MenuShadowOffsetY          = 10.f;
        float MenuShadowBlur             = 28.f;
        float MenuShadowAlpha            = 0.55f;
        float ModalShadowOffsetY         = 8.f;
        float ModalShadowBlur            = 24.f;
        float ModalShadowAlpha           = 0.50f;
        float HoverOverlayAlpha          = 0.15f;
        float MouseScrollSpeed           = 48.f; // px per scroll unit (VS Code uses ~50)
        float ScrollSmoothSpeed          = 50.f; // exponential-lerp speed — 50 → 57% first-frame, ~66ms settle

        // Plot
        float PlotLineThickness          = 1.5f;

        // Non-pushable config (bool; not compatible with float push/pop stack)
        bool  DefaultAutoHideTabBar      = false; // ImGui default: always show tab bar

        // Derived — set by ZUIStyleUpdate(), never manually
        float FrameHeight                = 22.f; // = FontSize + FramePadding[1] * 2
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

    /// @brief Lazily allocated float text edit; never retains a dereferenceable model pointer.
    struct ZUIFloatInputState
    {
        char      Buffer[64]      = {};
        uintptr_t BindingIdentity = 0;
        uint64_t  LastFrame       = 0;
        float     LastValue       = 0.f;
        bool      Editing         = false;
        bool      Edited          = false;
    };

    /// @brief Per-box state that survives across frames (hover/active animation, scroll, undo).
    ///
    /// Stored in the open-addressing hash table ZUIPersistentStore and keyed by ZUIBox::Key.
    /// The layout solver writes ScreenMin/ScreenMax each frame for boxes that have ZUI_Clickable.
    struct ZUIPersistentState
    {
        float                HotT               = 0.f;
        float                ActiveT            = 0.f;
        float                ToggleT            = -1.f; ///< -1 initializes to the model state without an entrance animation.
        float                ScrollX            = 0.f;  // current (animated) scroll position
        float                ScrollY            = 0.f;
        float                ScrollXTarget      = 0.f; // destination; wheel input writes here; ScrollX lerps toward it
        float                ScrollYTarget      = 0.f;
        float                MaxScrollY         = 0.f; // set by layout solver; clamped in interaction pass
        float                ScrollbarShowTimer = 0.f; // seconds remaining; reset on scroll/drag; drives scrollbar alpha
        float                UserData           = -1.f;
        float                ScreenMinX         = 0.f;
        float                ScreenMinY         = 0.f;
        float                ScreenMaxX         = 0.f;
        float                ScreenMaxY         = 0.f;
        float                MaxScrollX         = 0.f;     // set by layout solver for ZUI_Scrollable+LayoutAxis::X
        int32_t              SelectStart        = -1;      // text selection anchor (-1 = no selection)
        ZUIColorWidgetState* ColorState         = nullptr; ///< Lazily allocated, keyed color editing state.
        ZUIFloatInputState*  FloatInputState    = nullptr; ///< Allocated only on the float input's first focus; reused thereafter.
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
        uint64_t                              FrameIndex                   = 0;
        ZUIColorModel                         ColorModel                   = ZUIColorModel::RGB; ///< Shared by color pickers in this window/session.
        ColorMath::RGBA                       RecentColors[24]             = {};
        uint32_t                              RecentColorCount             = 0;
        static constexpr uint32_t             ColorUndoCapacity            = 64;
        ZUIColorUndoEntry                     ColorUndo[ColorUndoCapacity] = {};
        uint32_t                              ColorUndoCount               = 0;
        uint32_t                              ColorUndoCursor              = 0;
        // sub-arenas carved from the engine's main arena via ZUIContextInit
        ZEngine::Core::Memory::ArenaAllocator FrameArena                   = {}; // Clear()-ed each BeginFrame; all ZUIBox* are stale after
        ZEngine::Core::Memory::ArenaAllocator PersistentArena              = {}; // never cleared; holds the persistent state table

        // box tree — all pointers into FrameArena
        ZUIBox*                               Root                         = nullptr;
        ZUIBox*                               Current                      = nullptr;
        ZUIBox*                               ForegroundRoot               = nullptr; ///< Frame-local, non-interactive decorations; promoted above the shell at EndFrame.

        // persistent state — open-addressing hash table in PersistentArena
        ZUIPersistentStore                    StateStore                   = {};

        // Single shared font atlas (ImGui approach: one texture, all fonts).
        // Set by Editor::OnInitialized after ZUIFontAtlasBake.
        ZUIFontAtlas*                         Atlas                        = nullptr;

        // Convenience accessors — delegate to Atlas
        ZUIFont*                              GetFont(ZUIFontSize size) const
        {
            if (!Atlas)
            {
                return nullptr;
            }
            if (size == ZUIFontSize::Small && Atlas->Small)
            {
                return Atlas->Small;
            }
            if (size == ZUIFontSize::Header && Atlas->Header)
            {
                return Atlas->Header;
            }
            if (size == ZUIFontSize::Heading && Atlas->Heading)
            {
                return Atlas->Heading;
            }
            if (size == ZUIFontSize::Tiny && Atlas->Tiny)
            {
                return Atlas->Tiny;
            }
            if (size == ZUIFontSize::Large && Atlas->Large)
            {
                return Atlas->Large;
            }
            return Atlas->Body;
        }

        // input state — written by ZUILayer each frame before ZUIBeginFrame
        float    MousePos[2]           = {};
        float    MousePressPos[3][2]   = {}; // logical position latched by each physical button press
        float    MouseReleasePos[3][2] = {}; // final physical release, not later uncaptured motion
        uint64_t MousePressSerial[3]   = {};
        uint64_t MouseReleaseSerial[3] = {};
        bool     MousePressAlt[3]      = {}; // gesture intent survives modifier release before routing
        bool     MousePressCtrl[3]     = {};
        bool     MousePressShift[3]    = {};
        bool     MouseReleaseCtrl[3]   = {}; // final drag modifiers, not later key events
        bool     MouseReleaseShift[3]  = {};
        float    PrevMousePos[2]       = {}; // saved at end of ZUIEndFrame for drag-delta
        bool     MouseDown[3]          = {};
        bool     MousePressed[3]       = {};
        bool     MouseReleased[3]      = {};
        float    ScrollDelta           = 0.f;
        float    DeltaTime             = 0.f;
        float    Time                  = 0.f;   // accumulated seconds since init
        bool     BackspacePressed      = false; // set by ZUILayer::OnKeyPressed, cleared in ZUIEndFrame
        bool     BackspaceHeld         = false; // true while key is physically down

        // interaction state — updated by ZUIInteractionPass
        uint64_t HotKey                = 0;
        uint64_t ActiveKey             = 0;
        uint64_t FocusKey              = 0;
        bool     KeyboardFocus         = false; ///< Mouse presses hide focus rings without disabling text editing.
        uint64_t PreferredFocusScope   = 0;     ///< Active dock panel; start here when no control owns focus.
        uint64_t FocusTrapKey          = 0;     ///< Innermost popup/modal from the last completed frame.
        uint64_t SavedFocusKey         = 0;     ///< Focus to restore when the overlay closes.
        uint64_t TooltipHoverKey       = 0;
        float    TooltipHoverTime      = 0.f;

        // text input — written by OnTextInputRaised
        char     TextInput[32]         = {};
        uint32_t TextInputLen          = 0;
        // Clipboard write request — written by ZUITextField (Ctrl+C), read+cleared by ZUILayer
        char     ClipboardWrite[512]   = {};

        // capacity caps used by layout and interaction passes
        uint32_t MaxBoxesPerFrame      = 0;

        // Style system — metrics, spacing, rounding (ImGui: ImGuiStyle non-color fields)
        ZUIStyle Style                 = {};

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

        // Logical UI dimensions — set by AppRenderPipeline::BeginOverlayFrame each frame.
        uint32_t                  ScreenW              = 1280;
        uint32_t                  ScreenH              = 720;
        // Physical framebuffer pixels per ZUI coordinate.  This incorporates the GLFW
        // content scale on platforms where the framebuffer/window ratio remains 1:1.
        float                     UIScale              = 1.f;
        // Converts GLFW cursor coordinates into ZUI coordinates.  GLFW reports
        // positions in window coordinates, which may be larger than ZUI's
        // DPI-independent coordinate space.
        float                     InputScale[2]        = {1.f, 1.f};
        // Log dimensions initially and on changes, never on every frame.
        bool                      UIScaleLogged        = false;

        // drag-and-drop — source is set by ZUIBeginDragSource while a box is held+moving;
        // drop result (DragDropFired/DragTargetKey) is set by ZUIInteractionPass on mouse-release
        // and cleared at the START of the next ZUIEndFrame so BuildUI can read it
        uint64_t                  DragSourceKey        = 0;
        char                      DragPayload[512]     = {};
        uint32_t                  DragPayloadLen       = 0;
        ZUIDragPayloadKind        DragPayloadKind      = ZUIDragPayloadKind::Opaque;
        bool                      DragDropFired        = false;
        uint64_t                  DragTargetKey        = 0;

        // Written by ViewportPanel each UI frame; consumed by the next application update
        // to identify the scene viewport's pointer target.
        bool                      ViewportHovered      = false;
        // Field presentation knobs set by a panel around its widgets (restored by the panel).
        bool                      FlatInputs           = false;   ///< Drag fields draw without a border and with a 3 px radius.
        int                       DragFloatDecimals    = 3;       ///< Digits after the point shown by ZUIDragFloat.
        const char*               DragFloatSuffix      = nullptr; ///< Appended to ZUIDragFloat's display text (e.g. a degree sign).
        uint64_t                  ViewportInputKey     = 0;
        int                       ResizeCursor         = 0; // 0=default 1=H-resize 2=V-resize; set by panel dividers, read by ZUILayer

        // Modifier key state — written by ZUILayer::OnKeyPressed/Released
        bool                      CtrlDown             = false;
        bool                      ShiftDown            = false;
        bool                      AltDown              = false;
        // Set by text-editing widgets while their field owns FocusKey. This
        // remains separate from generic keyboard focus used for buttons and
        // tree rows so editor navigation can stay available after selection.
        bool                      TextInputActive      = false;
        // Set by the editor's per-update interaction owner, not a slot-gated UI
        // build. Retained overlays must neither lose nor latch drag capture.
        ZUIInputCapture           InteractionCapture   = {};

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
        bool                      F2Pressed            = false; // rename selected item in the focused editor panel
        // Held state for key-repeat (same mechanism as BackspaceHeld)
        bool                      ArrowLeftHeld        = false;
        bool                      ArrowRightHeld       = false;
        bool                      ArrowUpHeld          = false;
        bool                      ArrowDownHeld        = false;
        bool                      DeleteHeld           = false;
        float                     ArrowRepeatTimer     = 0.f;

        // Input repeat — ZUIEndFrame advances the timer; after RepeatDelay
        // it fires BackspacePressed / ArrowPressed at RepeatRate hz
        float                     KeyRepeatTimer       = 0.f;
        static constexpr float    kRepeatDelay         = 0.45f; // s before first repeat
        static constexpr float    kRepeatRate          = 0.04f; // s between repeats

        // ZUIBeginDisabled / ZUIEndDisabled — no interaction; dedicated disabled tokens.
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
            uint64_t Key             = 0;
            ZUIBox*  Box             = nullptr; // set by ZUIBeginPopup; valid this frame
            ZUIBox*  SavedParent     = nullptr; // ctx->Current before popup opened; restored on End
            float    PosX            = 0.f;
            float    PosY            = 0.f;
            uint64_t OwnerFocusKey   = 0; // restore the opener when returning to a parent overlay
            // Last completed layout; popup boxes are not clickable and therefore
            // do not publish their bounds to the widget-state store.
            float    ScreenMin[2]    = {};
            float    ScreenMax[2]    = {};
            bool     MouseNavigation = false; // A submenu follows pointer dismissal after mouse takeover.
        };
        ZUIPopupEntry PopupStack[kMaxPopupDepth] = {};
        uint32_t      PopupStackSize             = 0; // active popup count
        uint32_t      PopupBuildDepth            = 0; // current render depth (reset in BeginFrame)
        uint64_t      PendingPopupKey            = 0; // queued by ZUIOpenPopup, applied in EndFrame
        uint32_t      PendingPopupDepth          = 0;
        uint64_t      PendingPopupOwnerFocusKey  = 0;
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

    /// @brief Return whether the previous UI frame owns pointer or keyboard input.
    /// @details The viewport's scene image is deliberately excluded from pointer capture so
    ///          editor-camera pan and orbit remain available over it. When no viewport image
    ///          was built, the editor owns pointer input; this prevents a stale camera viewport
    ///          rect from receiving input while another tab is active.
    inline ZUIInputCapture ZUIGetInputCapture(const ZUIContext* ctx)
    {
        if (!ctx)
        {
            return {};
        }

        const bool viewport_active = ctx->ViewportInputKey != 0;
        // A queued popup owns input before its first overlay can be published.
        const bool modal_or_popup  = ctx->ActiveModalKey != 0 || ctx->ModalBox != nullptr || ctx->PopupStackSize != 0 || ctx->PendingPopupKey != 0;
        const bool other_hot       = ctx->HotKey != 0 && ctx->HotKey != ctx->ViewportInputKey;
        const bool other_active    = ctx->ActiveKey != 0 && ctx->ActiveKey != ctx->ViewportInputKey;
        const bool other_drag      = ctx->DragSourceKey != 0 && ctx->DragSourceKey != ctx->ViewportInputKey;

        return {
            .Pointer  = ctx->InteractionCapture.Pointer || !viewport_active || modal_or_popup || other_hot || other_active || other_drag,
            .Keyboard = ctx->InteractionCapture.Keyboard || modal_or_popup || ctx->TextInputActive,
        };
    }

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

    /// @brief Portal screen-positioned decorations out of panel clipping into the foreground.
    /// @details Requires an existing root. Do not put interactive widgets in this layer.
    /// @return Saved build parent; pass it to ZUIEndForeground after building decorations.
    ZUIBox*             ZUIBeginForeground(ZUIContext* ctx);

    /// @brief Restore the build parent saved by ZUIBeginForeground.
    void                ZUIEndForeground(ZUIContext* ctx, ZUIBox* saved_parent);

    // Utilities

    /// @brief Look up or insert a ZUIPersistentState slot for @p key (FNV-1a hash).
    /// @param store Persistent state hash table.
    /// @param key   FNV-1a hash key to look up.
    /// @returns Pointer to the state; nullptr only on table-full (asserts in debug).
    ZUIPersistentState* ZUIStateGetOrInsert(ZUIPersistentStore* store, uint64_t key);
    /// Erase caller-owned transient box state without allocating/tombstones.
    /// Rejects states owning color/float storage. Backward-shift deletion can
    /// relocate other slots: do not retain state pointers across this call.
    [[nodiscard]] bool  ZUIStateEraseTransient(ZUIPersistentStore* store, uint64_t key);

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
