#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>

namespace ZEngine::UI
{
    // Size constructors — wrap ZUISize for use as function arguments.

    /// @brief Fixed pixel size (logical coordinates; no UIScale multiplication).
    inline ZUISize ZPx(float v)
    {
        return {ZUISizeKind::Pixels, v, 1.f};
    }
    /// @brief Alias of ZPx — kept for call-site clarity.
    inline ZUISize ZSPx(const ZUIContext* /*ctx*/, float v)
    {
        return ZPx(v);
    }
    /// @brief Fill all available space along the parent's layout axis.
    inline ZUISize ZFill()
    {
        return {ZUISizeKind::Fill, 0.f, 1.f};
    }
    /// @brief Size to the rendered text width (includes FramePadding.x on each side).
    inline ZUISize ZText()
    {
        return {ZUISizeKind::Text, 0.f, 1.f};
    }
    /// @brief Percentage of the parent's dimension.
    /// @param v Fraction in [0, 1].
    inline ZUISize ZPct(float v)
    {
        return {ZUISizeKind::ParentPercent, v, 1.f};
    }
    /// @brief Size to the sum of children (intrinsic fit).
    inline ZUISize ZFit()
    {
        return {ZUISizeKind::ChildrenSum, 0.f, 1.f};
    }

    // Padding helpers — call immediately after ZUIBeginColumn / ZUIBeginRow.

    /// @brief Apply uniform padding on all four sides of @p box.
    inline void ZUIPadding(ZUIBox* box, float all)
    {
        box->Padding[0] = box->Padding[1] = box->Padding[2] = box->Padding[3] = all;
    }
    /// @brief Apply separate horizontal and vertical padding to @p box.
    inline void ZUIPaddingXY(ZUIBox* box, float horiz, float vert)
    {
        box->Padding[0] = box->Padding[2] = horiz;
        box->Padding[1] = box->Padding[3] = vert;
    }

    // Layout containers — always pair Begin with the matching End.

    /// @brief Push a vertical stack; children are laid out along the Y axis.
    /// @return The container box — override size/color before adding children.
    ZUIBox*     ZUIBeginColumn(ZUIContext* ctx, const char* key, ZUISize w = ZFill(), ZUISize h = ZFit());
    void        ZUIEndColumn(ZUIContext* ctx);

    /// @brief Push a horizontal stack; children are laid out along the X axis.
    /// @return The container box.
    ZUIBox*     ZUIBeginRow(ZUIContext* ctx, const char* key, ZUISize w = ZFill(), ZUISize h = ZFit());
    void        ZUIEndRow(ZUIContext* ctx);

    /// @brief Push a vertically-scrollable clipped region.
    ///
    /// Children that overflow the height are scissored; the user scrolls with
    /// the mouse wheel. Horizontal scroll is supported when LayoutAxis==X.
    /// @return The container box — set Size / BgColor before adding children.
    ZUIBox*     ZUIBeginScrollRegion(ZUIContext* ctx, const char* key, ZUISize w = ZFill(), ZUISize h = ZFill());
    void        ZUIEndScrollRegion(ZUIContext* ctx);

    /// @brief Request the named scroll region to jump to its bottom on the next frame.
    /// @note Call once whenever new content is appended (e.g. a new log entry).
    void        ZUIScrollToBottom(ZUIContext* ctx, const char* key);

    /// @brief Read the current vertical scroll offset of the named region.
    /// @return Scroll offset in logical pixels, or 0 if key not found.
    float       ZUIGetScrollY(ZUIContext* ctx, const char* key);

    // Leaf widgets

    /// @brief Non-interactive text label.
    /// @param text  Null-terminated string to display.
    /// @param color RGBA color, or nullptr to use Theme.TextDefault.
    /// @param size  Font variant (Body/Small/Header).
    void        ZUILabel(ZUIContext* ctx, const char* text, const float color[4] = nullptr, ZUIFontSize size = ZUIFontSize::Body);

    /// @brief Standard push button.
    /// @param w Width (default ZText() = label width + FramePadding.x × 2).
    /// @param h Height (default = FrameHeight ≈ 19 px).
    /// @return ZUISignal — check ZUI_SignalClicked for activation.
    ZUISignal   ZUIButton(ZUIContext* ctx, const char* label, ZUISize w = ZText(), ZUISize h = ZPx(19.f));

    /// @brief Compact borderless button — safe inside rows and toolbars.
    ZUISignal   ZUISmallButton(ZUIContext* ctx, const char* label);

    /// @brief Invisible hit-area box — no drawing; use for custom-drawn clickable regions.
    ZUISignal   ZUIInvisibleButton(ZUIContext* ctx, const char* key, ZUISize w = ZText(), ZUISize h = ZPx(28.f));

    /// @brief Stateful toggle button — background brightens when @p *active is true.
    /// @param active Toggled in-place on click.
    /// @return true the frame @p *active changes.
    bool        ZUIToggleButton(ZUIContext* ctx, const char* label, bool* active, ZUISize w = ZText(), ZUISize h = ZPx(28.f));

    /// @brief Clickable image drawn from the bindless texture array.
    /// @param texture_index Bindless slot (e.g. TextureHandle::Index).
    ZUISignal   ZUIImageButton(ZUIContext* ctx, const char* key, uint32_t texture_index, ZUISize w = ZPx(28.f), ZUISize h = ZPx(28.f));

    /// @brief Begin a disabled scope — nested widgets skip Clickable and are visually dimmed.
    void        ZUIBeginDisabled(ZUIContext* ctx);
    void        ZUIEndDisabled(ZUIContext* ctx);

    /// @brief 1 px horizontal divider line.
    void        ZUISeparator(ZUIContext* ctx);

    /// @brief Empty gap of @p px pixels along the parent's layout axis.
    void        ZUISpacer(ZUIContext* ctx, float px);

    /// @brief Collapsible tree row with a disclosure triangle.
    /// @param open Toggled on click.
    /// @return Signal from the row box (check ZUI_SignalClicked for external handling).
    ZUISignal   ZUITreeNode(ZUIContext* ctx, const char* label, bool* open);

    /// @brief Checkbox with a text label.
    /// @param checked Toggled in-place on click.
    /// @return true the frame @p *checked changes.
    bool        ZUICheckbox(ZUIContext* ctx, const char* label, bool* checked);

    /// @brief Radio button — sets @p *selected = index when clicked.
    /// @return true when the value changes.
    bool        ZUIRadioButton(ZUIContext* ctx, const char* label, int* selected, int index);

    /// @brief Filled horizontal progress bar.
    /// @param fraction  Progress in [0, 1].
    /// @param overlay_text Optional label drawn centered on the bar (may be nullptr).
    void        ZUIProgressBar(ZUIContext* ctx, const char* key, float fraction, ZUISize w = ZFill(), ZUISize h = ZPx(18.f), const char* overlay_text = nullptr);

    /// @brief Show a tooltip near the cursor while @p sig contains ZUI_SignalHovered.
    /// @note Call immediately after the relevant ZUISignalFromBox call.
    void        ZUISetTooltip(ZUIContext* ctx, const ZUISignal& sig, const char* text);

    /// @brief Full-width collapsible section header — click to toggle, no drag.
    ///
    /// VS Code-style: header is click-only (pointer cursor). Resize is handled
    /// by a separate ZUIPaneSash placed between adjacent sections.
    ///
    /// @param bg_color Optional RGBA background; nullptr = transparent.
    /// @return Current open state.
    bool        ZUICollapsingHeader(ZUIContext* ctx, const char* label, bool* open, const float* bg_color = nullptr);

    /// @brief 4 px horizontal drag sash between two adjacent collapsible sections.
    ///
    /// Positioned in the layout immediately between the bottom of section[boundary]
    /// and the header of section[boundary+1]. On drag applies VS Code's greedy
    /// cascade resize:
    ///   delta > 0 (drag DOWN) → sections above boundary grow, sections below shrink
    ///   delta < 0 (drag UP)   → sections above shrink, sections below grow
    /// Each side absorbs as much delta as it can (respecting min_h) before passing
    /// the remainder to the next section in that direction.
    ///
    /// @param heights   Content-height array for all N sections (in/out).
    /// @param opens     Open-state array (collapsed sections are skipped in resize).
    /// @param n         Total number of sections.
    /// @param boundary  Index of the section ABOVE this sash (sash sits after section[boundary]).
    /// @param min_h     Minimum content height per section (default 30 px).
    void        ZUIPaneSash(ZUIContext* ctx, const char* key, float* heights, const bool* opens, int n, int boundary, float min_h = 30.f);

    /// @brief Full-width selectable row.
    /// @param selected Toggled in-place on click.
    /// @return true the frame @p *selected changes.
    bool        ZUISelectable(ZUIContext* ctx, const char* label, bool* selected, ZUISize h = ZPx(24.f));

    /// @brief Horizontal separator with a centered label.
    void        ZUISeparatorText(ZUIContext* ctx, const char* text);

    /// @brief Drag-to-edit a float value.
    ///
    /// Horizontal mouse drag changes @p *value by delta * speed.
    /// Click to enter text-edit mode.
    /// @return true if @p *value changed this frame.
    bool        ZUIDragFloat(ZUIContext* ctx, const char* key, float* value, float speed = 0.05f, float width_px = 60.f);

    /// @brief Drag-to-edit an integer value. Same mechanics as ZUIDragFloat.
    /// @return true if @p *value changed this frame.
    bool        ZUIDragInt(ZUIContext* ctx, const char* key, int* value, float speed = 1.f, float width_px = 60.f);

    /// @brief Three-component XYZ drag in a single compact row.
    ///
    /// Renders [X][Y][Z] drag boxes with colored axis labels.
    /// @param component_w Per-component box width; 0 = equal distribution.
    /// @return true if any component changed this frame.
    bool        ZUIDragFloat3(ZUIContext* ctx, const char* key, float v[3], float speed = 0.05f, float component_w = 0.f);

    /// @brief Text field that edits a float — click to focus, type, press Enter.
    /// @return true when the value changes (on Enter or focus loss).
    bool        ZUIInputFloat(ZUIContext* ctx, const char* key, float* value, float width_px = 80.f);

    /// @brief Inline color editor: swatch + hex label; clicking opens ZUIColorPicker4.
    /// @param color RGBA in linear [0, 1].
    /// @return true when changed.
    bool        ZUIColorEdit4(ZUIContext* ctx, const char* key, float color[4]);

    /// @brief Animated loading arc driven by ctx->Time.
    /// @param radius_px Visual radius in logical pixels.
    /// @param speed      Angular velocity in radians per second.
    void        ZUISpinner(ZUIContext* ctx, const char* key, float radius_px = 10.f, float speed = 5.f);

    // Popup / overlay system

    /// @brief Request a popup to open at the given screen position.
    /// @param pos_x X position; -1 = current mouse X.
    /// @param pos_y Y position; -1 = current mouse Y.
    void        ZUIOpenPopup(ZUIContext* ctx, const char* key, float pos_x = -1.f, float pos_y = -1.f);

    /// @brief Begin building a popup.
    ///
    /// Pushes a floated root-level column. Always pair with ZUIEndPopup when
    /// this returns true.
    /// @return true while the popup is active.
    bool        ZUIBeginPopup(ZUIContext* ctx, const char* key);
    void        ZUIEndPopup(ZUIContext* ctx);

    /// @brief Close whichever popup is currently active.
    void        ZUIClosePopup(ZUIContext* ctx);

    /// @brief Open a popup on right-click over the previous signal's box.
    /// @param item_signal Signal obtained from ZUISignalFromBox immediately before.
    /// @return true if the popup is now active.
    bool        ZUIBeginPopupContextItem(ZUIContext* ctx, const char* key, const ZUISignal& item_signal);

    /// @brief Menu item inside a popup — returns true on click (also closes the popup).
    bool        ZUIMenuItem(ZUIContext* ctx, const char* label, bool enabled = true);

    /// @brief Selectable item inside a ZUIBeginCombo popup.
    /// @param selected true tints the item with RowSelectedBg.
    /// @return true when clicked; also closes the combo.
    bool        ZUIComboItem(ZUIContext* ctx, const char* label, bool selected = false);

    // Layout helpers

    /// @brief Place the next item on the same line as the previous one.
    /// @note Use ZUISpacer() for explicit gaps rather than @p spacing.
    void        ZUISameLine(ZUIContext* ctx, float spacing = 0.f);

    /// @brief Begin a simple fixed-column table.
    /// @param col_count Number of columns.
    /// @param widths    Per-column pixel widths; nullptr = equal distribution.
    void        ZUIBeginTable(ZUIContext* ctx, const char* key, int columns, const float* widths = nullptr, ZUISize h = ZFit());
    void        ZUITableNextRow(ZUIContext* ctx);
    void        ZUITableSetColumn(ZUIContext* ctx, int col_index);
    void        ZUIEndTable(ZUIContext* ctx);

    /// @brief Set the text alignment on any ZUIBox.
    inline void ZUISetTextAlign(ZUIBox* box, ZUITextAlign align)
    {
        box->TextAlign = align;
    }

    /// @brief Apply a vertical gradient to @p box (top → bottom).
    inline void ZUISetGradient(ZUIBox* box, const float top[4], const float bot[4])
    {
        ZUIBoxSetGradientV(box, top, bot);
    }
    /// @brief Convenience gradient: solid color at top, transparent at bottom.
    inline void ZUISetGradientFade(ZUIBox* box, float r, float g, float b, float a)
    {
        const float top[4] = {r, g, b, a};
        const float bot[4] = {r, g, b, 0.f};
        ZUIBoxSetGradientV(box, top, bot);
    }

    // Complex widgets

    /// @brief Begin a tab bar.
    ///
    /// @code
    ///   ZUIBeginTabBar(ctx, "##tabs");
    ///   if (ZUIBeginTabItem(ctx, "Tab A")) { /* content */ ZUIEndTabItem(ctx); }
    ///   if (ZUIBeginTabItem(ctx, "Tab B")) { /* content */ ZUIEndTabItem(ctx); }
    ///   ZUIEndTabBar(ctx);
    /// @endcode
    void    ZUIBeginTabBar(ZUIContext* ctx, const char* key);
    bool    ZUIBeginTabItem(ZUIContext* ctx, const char* label);
    void    ZUIEndTabItem(ZUIContext* ctx);
    void    ZUIEndTabBar(ZUIContext* ctx);

    /// @brief Scrollable list box wrapping a scroll region with Selectable items.
    /// @return The container box.
    ZUIBox* ZUIBeginListBox(ZUIContext* ctx, const char* key, ZUISize w = ZFill(), ZUISize h = ZPx(120.f));
    void    ZUIEndListBox(ZUIContext* ctx);

    /// @brief Horizontal slider — maps thumb position linearly to [v_min, v_max].
    /// @return true while the value changes.
    bool    ZUISliderFloat(ZUIContext* ctx, const char* key, float* value, float v_min, float v_max, ZUISize w = ZFill(), ZUISize h = ZPx(24.f));

    /// @brief Integer text field clamped to [v_min, v_max].
    /// @return true when value changes.
    bool    ZUIInputInt(ZUIContext* ctx, const char* key, int* value, int v_min = -0x7FFFFFFF, int v_max = 0x7FFFFFFF, ZUISize w = ZFill());

    /// @brief Multi-line text input inside a scroll region.
    /// @return true when @p buf changes.
    bool    ZUIInputTextMultiline(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, ZUISize w = ZFill(), ZUISize h = ZPx(120.f));

    /// @brief RGBA colour picker (hue bar + SV square + alpha bar).
    /// @param color RGBA in linear [0, 1]. Modified in-place.
    /// @return true when changed.
    bool    ZUIColorPicker4(ZUIContext* ctx, const char* key, float color[4]);

    /// @brief Context menu — opens on right-click anywhere in the caller's region.
    bool    ZUIBeginContextMenu(ZUIContext* ctx, const char* key);
    void    ZUIEndContextMenu(ZUIContext* ctx);

    /// @brief Dropdown combo box.
    ///
    /// @p preview_label is shown in the collapsed button.
    /// Add ZUIComboItem / ZUISelectable items inside, then call ZUIEndCombo.
    /// @return true while the dropdown is open.
    bool    ZUIBeginCombo(ZUIContext* ctx, const char* key, const char* preview_label, ZUISize w = ZFill());
    void    ZUIEndCombo(ZUIContext* ctx);

    /// @brief Horizontal menu bar. Pair with ZUIEndMenuBar.
    bool    ZUIBeginMenuBar(ZUIContext* ctx);
    void    ZUIEndMenuBar(ZUIContext* ctx);

    /// @brief Menu button inside a menu bar — opens a popup column on click.
    bool    ZUIBeginMenu(ZUIContext* ctx, const char* label, bool enabled = true);
    void    ZUIEndMenu(ZUIContext* ctx);

    /// @brief Open a modal dialog (dims background, cannot be dismissed by outside click).
    void    ZUIOpenModal(ZUIContext* ctx, const char* key);
    /// @return true while the modal is active.
    bool    ZUIBeginModal(ZUIContext* ctx, const char* key, const char* title);
    void    ZUIEndModal(ZUIContext* ctx);

    // Plot widgets

    /// @brief Line chart over @p count samples.
    /// @param v_scale_min Lower bound; FLT_MAX = auto-scale.
    /// @param v_scale_max Upper bound; FLT_MAX = auto-scale.
    void    ZUIPlotLines(ZUIContext* ctx, const char* key, const float* values, int count, float v_scale_min = 3.402823e+38f, float v_scale_max = 3.402823e+38f, const char* overlay_text = nullptr, ZUISize w = ZFill(), ZUISize h = ZPx(40.f));

    /// @brief Histogram over @p count samples. Same scale semantics as ZUIPlotLines.
    void    ZUIPlotHistogram(ZUIContext* ctx, const char* key, const float* values, int count, float v_scale_min = 3.402823e+38f, float v_scale_max = 3.402823e+38f, const char* overlay_text = nullptr, ZUISize w = ZFill(), ZUISize h = ZPx(40.f));

    // ZUITreeView — recursive tree widget

    /// @brief Per-instance configuration for ZUIBeginTreeView.
    struct ZUITreeViewConfig
    {
        float RowH     = 19.f; ///< Row height in logical px (FrameHeight)
        float IndentPx = 21.f; ///< Indent per depth level (IndentSpacing)
    };

    /// @brief Push a tree view scroll region.
    /// @param cfg Layout configuration; nullptr uses defaults.
    /// @return The scroll container box.
    ZUIBox* ZUIBeginTreeView(ZUIContext* ctx, const char* key, ZUISize w = ZFill(), ZUISize h = ZFill(), const ZUITreeViewConfig* cfg = nullptr);
    void    ZUIEndTreeView(ZUIContext* ctx);

    /// @brief Push an expandable tree node.
    ///
    /// If this returns true the node is expanded — add children, then
    /// always call ZUITreeViewEndNode.
    /// @param selected    Tints the row background.
    /// @param icon_col    RGBA icon color; nullptr = no icon dot.
    /// @param initial_open Whether the node starts expanded.
    /// @return true when the node is expanded (content should be added).
    bool    ZUITreeViewBeginNode(ZUIContext* ctx, const char* label, bool selected, const float icon_col[4] = nullptr, bool initial_open = false);
    void    ZUITreeViewEndNode(ZUIContext* ctx);

    /// @brief Leaf row (no expand arrow).
    /// @return true when clicked.
    bool    ZUITreeViewLeaf(ZUIContext* ctx, const char* label, bool selected, const float icon_col[4] = nullptr);

    // ZUIDataTable — sortable, resizable data table

    /// @brief Column descriptor for ZUIBeginDataTable.
    struct ZUIDataTableColumn
    {
        const char* Label;
        float       InitWidth; ///< 0 = 100 px default
        bool        Sortable;
        bool        Resizable;
    };

    /// @brief Sort state returned by ZUIDataTableGetSortSpecs.
    struct ZUITableSortSpec
    {
        int  ColumnIndex; ///< -1 = unsorted
        bool Ascending;
        bool Changed; ///< true the frame the sort spec changed
    };

    /// @brief Begin a data table.
    /// @param col_count Number of columns.
    /// @param cols      Column descriptors (array of length col_count).
    /// @return false if the table is off-screen (still call ZUIEndDataTable).
    bool             ZUIBeginDataTable(ZUIContext* ctx, const char* key, int col_count, const ZUIDataTableColumn* cols, ZUISize h = ZFill());

    /// @brief Render the sticky header row with labels and sort arrows.
    /// @note Must be called once before any ZUIDataTableNextRow calls.
    void             ZUIDataTableHeadersRow(ZUIContext* ctx);

    /// @brief Advance to the next data row.
    /// @param selected Tints the row with RowSelectedBg when true.
    /// @return true if the row was clicked.
    bool             ZUIDataTableNextRow(ZUIContext* ctx, bool selected = false);

    /// @brief Set the active column cell for the current row.
    void             ZUIDataTableSetColumn(ZUIContext* ctx, int col);
    void             ZUIEndDataTable(ZUIContext* ctx);

    /// @return Current sort specification; Changed==true the frame a header was clicked.
    ZUITableSortSpec ZUIDataTableGetSortSpecs(ZUIContext* ctx);

    // ZUIGridView — icon grid for content browsers

    /// @brief Push an auto-wrapping icon grid.
    /// @param item_w Cell width in logical px.
    /// @param item_h Cell height in logical px.
    ZUIBox*          ZUIBeginGridView(ZUIContext* ctx, const char* key, float item_w, float item_h, ZUISize w = ZFill(), ZUISize h = ZFill());

    /// @brief Advance to the next grid cell.
    /// @param selected Tints the cell background.
    /// @return true when the cell is clicked.
    bool             ZUIGridViewNextItem(ZUIContext* ctx, const char* item_key, bool selected = false);
    void             ZUIGridViewEndItem(ZUIContext* ctx);
    void             ZUIEndGridView(ZUIContext* ctx);

    // Drag-and-drop

    /// @brief Begin a drag source on @p box.
    ///
    /// When the box is held and the mouse has moved, records
    /// ctx->DragSourceKey and copies @p payload so the next ZUIAcceptDrop
    /// call can retrieve it.
    void             ZUIBeginDragSource(ZUIContext* ctx, const ZUIBox* box, const char* payload, uint32_t payload_len);

    /// @brief Accept a drop on @p box.
    ///
    /// Returns true exactly once — on the frame the drop fires.
    /// @param out_buf  Receives the payload (null-terminated); may be nullptr.
    /// @param out_size Capacity of @p out_buf.
    bool             ZUIAcceptDrop(ZUIContext* ctx, const ZUIBox* box, char* out_buf, uint32_t out_size);

    /// @brief Display a texture in a box.
    /// @param texture_index Bindless array slot (e.g. TextureHandle::Index).
    void             ZUIImage(ZUIContext* ctx, const char* key, uint32_t texture_index, ZUISize w = ZFill(), ZUISize h = ZFill());

    /// @brief Single-line editable text field with full selection and undo/redo.
    ///
    /// Keyboard shortcuts: Shift+Arrow to select, Ctrl+A to select all,
    /// Ctrl+C/X/V for clipboard, Ctrl+Z/Y for undo/redo (8 levels).
    /// Mouse: click to place cursor, drag to select.
    /// @param buf      Editable buffer.
    /// @param buf_size Capacity including the null terminator.
    /// @return true if @p buf changed this frame.
    bool             ZUITextField(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, float width_px = 160.f);

    /// @brief Search box — ZUITextField with a dim icon and placeholder text.
    /// @param placeholder Shown when @p buf is empty.
    bool             ZUISearchBox(ZUIContext* ctx, const char* key, char* buf, uint32_t buf_size, const char* placeholder = "Search...", ZUISize w = ZFill());

    /// @brief Thin invisible resize strip for manual splitter controls.
    ///
    /// @p horizontal = true → full-width 4 px tall (top/bottom split).
    /// @p horizontal = false → full-height 4 px wide (left/right split).
    /// While held, updates @p *value by DragDelta clamped to [min_v, max_v].
    /// @return true when actively dragging.
    bool             ZUIResizeHandle(ZUIContext* ctx, const char* key, float* value, float min_v, float max_v, bool horizontal);

} // namespace ZEngine::UI
