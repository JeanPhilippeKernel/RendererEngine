#pragma once
#include <ZEngine/UI/ZUIBox.h>
#include <ZEngine/UI/ZUIContext.h>
#include <ZEngine/UI/ZUIInteraction.h>

namespace ZEngine::UI
{
    // ---------------------------------------------------------------
    // Size helpers — wrap ZUISize construction
    // ---------------------------------------------------------------

    inline ZUISize ZPx(float v)      { return {ZUISizeKind::Pixels,        v,    1.f}; }
    // Logical pixel size. Coordinate system is already in logical units (glfwGetWindowSize
    // space) so no UIScale multiplication is needed. Equivalent to ZPx(v).
    // Kept as a named alias for clarity at call sites.
    inline ZUISize ZSPx(const ZUIContext* /*ctx*/, float v)
    {
        return ZPx(v);
    }
    inline ZUISize ZFill()           { return {ZUISizeKind::Fill,           0.f,  1.f}; }
    inline ZUISize ZText()           { return {ZUISizeKind::Text,           0.f,  1.f}; }
    inline ZUISize ZPct(float v)     { return {ZUISizeKind::ParentPercent,  v,    1.f}; }
    inline ZUISize ZFit()            { return {ZUISizeKind::ChildrenSum,    0.f,  1.f}; }

    // ---------------------------------------------------------------
    // Padding helpers — call immediately after ZUIBeginColumn/Row
    // ---------------------------------------------------------------
    inline void ZUIPadding(ZUIBox* box, float all)
    {
        box->Padding[0] = box->Padding[1] = box->Padding[2] = box->Padding[3] = all;
    }
    inline void ZUIPaddingXY(ZUIBox* box, float horiz, float vert)
    {
        box->Padding[0] = box->Padding[2] = horiz;
        box->Padding[1] = box->Padding[3] = vert;
    }

    // ---------------------------------------------------------------
    // Layout containers
    // Push a container box and return it so the caller can override
    // size / color before adding children. Always pair with EndXxx.
    // ---------------------------------------------------------------

    // Vertical stack — children laid out along Y axis
    ZUIBox* ZUIBeginColumn(ZUIContext* ctx, const char* key,
                           ZUISize w = ZFill(), ZUISize h = ZFit());
    void    ZUIEndColumn(ZUIContext* ctx);

    // Horizontal stack — children laid out along X axis
    ZUIBox* ZUIBeginRow(ZUIContext* ctx, const char* key,
                        ZUISize w = ZFill(), ZUISize h = ZFit());
    void    ZUIEndRow(ZUIContext* ctx);

    // Vertically-scrollable clipped region. Children that overflow the
    // height are scissored out; the user scrolls with the mouse wheel.
    // Returns the container box (set Size / BgColor before adding children).
    ZUIBox* ZUIBeginScrollRegion(ZUIContext* ctx, const char* key,
                                 ZUISize w = ZFill(), ZUISize h = ZFill());
    void    ZUIEndScrollRegion(ZUIContext* ctx);

    // Scroll the named region to the bottom on the next frame.
    // Call once whenever new content is appended (e.g. new log entry).
    void ZUIScrollToBottom(ZUIContext* ctx, const char* key);

    // Read current scroll offset (useful for save/restore or position queries).
    float ZUIGetScrollY(ZUIContext* ctx, const char* key);

    // ---------------------------------------------------------------
    // Leaf widgets
    // ---------------------------------------------------------------

    // Static text — no interaction. Pass a font size for Small/Header variants.
    void ZUILabel(ZUIContext* ctx, const char* text,
                  const float color[4] = nullptr,
                  ZUIFontSize size = ZUIFontSize::Body);

    // ---------------------------------------------------------------
    // Button family
    // ---------------------------------------------------------------

    // Regular button. Height default = 19px (ImGui GetFrameHeight). ZText() width
    // auto-includes 4px left+right padding (FramePadding.x) via ZText() padding fix.
    ZUISignal ZUIButton(ZUIContext* ctx, const char* label,
                        ZUISize w = ZText(), ZUISize h = ZPx(19.f));

    // No border, 22 px tall, tight horizontal fit — safe inside rows/toolbars.
    ZUISignal ZUISmallButton(ZUIContext* ctx, const char* label);

    // Invisible hit-area only — no drawing. Used for custom-drawn clickable regions.
    ZUISignal ZUIInvisibleButton(ZUIContext* ctx, const char* key,
                                  ZUISize w = ZText(), ZUISize h = ZPx(28.f));

    // Stateful toggle — background brightens when *active is true.
    // Returns true the frame *active changes.
    bool ZUIToggleButton(ZUIContext* ctx, const char* label, bool* active,
                         ZUISize w = ZText(), ZUISize h = ZPx(28.f));

    // Image button — renders a texture rect, returns click signal.
    ZUISignal ZUIImageButton(ZUIContext* ctx, const char* key,
                              uint32_t texture_index,
                              ZUISize w = ZPx(28.f), ZUISize h = ZPx(28.f));

    // ---------------------------------------------------------------
    // Disabled state — nest freely; widgets inside skip Clickable + dim
    // ---------------------------------------------------------------
    void ZUIBeginDisabled(ZUIContext* ctx);
    void ZUIEndDisabled(ZUIContext* ctx);

    // 1 px horizontal divider
    void ZUISeparator(ZUIContext* ctx);

    // Empty space of 'px' pixels along the parent's layout axis
    void ZUISpacer(ZUIContext* ctx, float px);

    // Collapsible tree row with a disclosure indicator.
    // *open is toggled on click. Returns signal from the row box.
    ZUISignal ZUITreeNode(ZUIContext* ctx, const char* label, bool* open);

    // ---------------------------------------------------------------
    // Simple standalone widgets
    // ---------------------------------------------------------------

    // 16×16 checkbox + label. Returns true when *checked changes.
    bool ZUICheckbox(ZUIContext* ctx, const char* label, bool* checked);

    // Radio button — sets *selected = index on click. Returns true when changed.
    bool ZUIRadioButton(ZUIContext* ctx, const char* label, int* selected, int index);

    // Filled progress bar [0..1]. Optional label drawn on top when non-null.
    void ZUIProgressBar(ZUIContext* ctx, const char* key, float fraction,
                        ZUISize w = ZFill(), ZUISize h = ZPx(18.f),
                        const char* overlay_text = nullptr);

    // Shows a tooltip box near the cursor when sig contains ZUI_SignalHovered.
    // Call immediately after ZUISignalFromBox.
    void ZUISetTooltip(ZUIContext* ctx, const ZUISignal& sig, const char* text);

    // Full-width collapsible section header. Returns *open state.
    bool ZUICollapsingHeader(ZUIContext* ctx, const char* label, bool* open);

    // Full-width selectable row. *selected is toggled on click.
    // Returns true the frame *selected changes.
    bool ZUISelectable(ZUIContext* ctx, const char* label, bool* selected,
                       ZUISize h = ZPx(24.f));

    // Horizontal separator with centred label text.
    void ZUISeparatorText(ZUIContext* ctx, const char* text);

    // Drag to edit a single float value. Horizontal mouse drag changes *value
    // by delta * speed. Returns true if *value changed this frame.
    // width_px controls the box width; typically 60–80 px inside a row.
    bool ZUIDragFloat(ZUIContext* ctx, const char* key,
                      float* value, float speed = 0.05f, float width_px = 60.f);

    // Drag to edit a single integer value. Same mechanics as DragFloat.
    bool ZUIDragInt(ZUIContext* ctx, const char* key,
                    int* value, float speed = 1.f, float width_px = 60.f);

    // Three-component float drag with colored X/Y/Z axis labels.
    // Renders as a single compact row: [X drag][Y drag][Z drag]
    // Returns true if any component changed this frame.
    bool ZUIDragFloat3(ZUIContext* ctx, const char* key,
                       float v[3], float speed = 0.05f, float component_w = 0.f);

    // Text field that edits a float — click to focus, type a value, press Enter.
    // Returns true when value changes (on Enter or focus-loss).
    bool ZUIInputFloat(ZUIContext* ctx, const char* key, float* value,
                       float width_px = 80.f);

    // Inline color editor: small colored swatch + hex label.
    // Clicking the swatch opens a ZUIColorPicker4 popup.
    // color[4] in linear [0,1]. Returns true when changed.
    bool ZUIColorEdit4(ZUIContext* ctx, const char* key, float color[4]);

    // Animated loading arc. radius_px = visual size.
    // Driven by ctx->Time — call every frame while loading.
    void ZUISpinner(ZUIContext* ctx, const char* key, float radius_px = 10.f);

    // ---------------------------------------------------------------
    // Popup / overlay system
    // ---------------------------------------------------------------

    // Request this popup to open at the given position (defaults to current mouse pos).
    void ZUIOpenPopup(ZUIContext* ctx, const char* key,
                      float pos_x = -1.f, float pos_y = -1.f);

    // Returns true while this popup is active; pushes a floated root-level column.
    // Always pair with ZUIEndPopup when it returns true.
    bool ZUIBeginPopup(ZUIContext* ctx, const char* key);
    void ZUIEndPopup(ZUIContext* ctx);

    // Close whatever popup is currently open.
    void ZUIClosePopup(ZUIContext* ctx);

    // Convenience: opens a popup on right-click over the previous signal's box.
    // Call immediately after ZUISignalFromBox; returns true if now active.
    bool ZUIBeginPopupContextItem(ZUIContext* ctx, const char* key,
                                  const ZUISignal& item_signal);

    // Menu item inside a popup — returns true on click (closes popup too).
    bool ZUIMenuItem(ZUIContext* ctx, const char* label, bool enabled = true);

    // Combo item (selectable inside a ZUIBeginCombo popup).
    // Returns true when clicked; closes the combo. selected=true tints the item.
    bool ZUIComboItem(ZUIContext* ctx, const char* label, bool selected = false);

    // ---------------------------------------------------------------
    // Layout helpers
    // ---------------------------------------------------------------

    // Place the next item on the same line as the previous one.
    // Note: spacing parameter is accepted but not applied; use ZUISpacer() for explicit gaps.
    void ZUISameLine(ZUIContext* ctx, float spacing = 0.f);

    // Simple fixed-column table. widths[] = per-column pixel widths;
    // pass nullptr for equal distribution. Use ZUITableNextRow /
    // ZUITableSetColumn to fill cells. Pair with ZUIEndTable.
    void ZUIBeginTable(ZUIContext* ctx, const char* key, int columns,
                       const float* widths = nullptr, ZUISize h = ZFit());
    void ZUITableNextRow(ZUIContext* ctx);
    void ZUITableSetColumn(ZUIContext* ctx, int col_index);
    void ZUIEndTable(ZUIContext* ctx);

    // Set text alignment on a box returned by ZUIBeginColumn/Row or any
    // ZUIBox* — e.g. ZUISetTextAlign(box, ZUITextAlign::Center).
    inline void ZUISetTextAlign(ZUIBox* box, ZUITextAlign align) { box->TextAlign = align; }

    // ---------------------------------------------------------------
    // Visual helpers (call on any ZUIBox* after pushing)
    // ---------------------------------------------------------------

    // Set a vertical gradient — top corners get top_rgba, bottom corners get bot_rgba.
    inline void ZUISetGradient(ZUIBox* box, const float top[4], const float bot[4])
    {
        ZUIBoxSetGradientV(box, top, bot);
    }
    // Convenience: solid top color, fade to transparent at bottom.
    inline void ZUISetGradientFade(ZUIBox* box, float r, float g, float b, float a)
    {
        const float top[4] = {r, g, b, a};
        const float bot[4] = {r, g, b, 0.f};
        ZUIBoxSetGradientV(box, top, bot);
    }

    // ---------------------------------------------------------------
    // Complex widgets
    // ---------------------------------------------------------------

    // Tab bar. Usage:
    //   ZUIBeginTabBar(ctx, "##tabs")
    //   if (ZUIBeginTabItem(ctx, "Tab 1")) { ...content... ZUIEndTabItem(ctx); }
    //   if (ZUIBeginTabItem(ctx, "Tab 2")) { ...content... ZUIEndTabItem(ctx); }
    //   ZUIEndTabBar(ctx)
    void ZUIBeginTabBar(ZUIContext* ctx, const char* key);
    bool ZUIBeginTabItem(ZUIContext* ctx, const char* label);
    void ZUIEndTabItem(ZUIContext* ctx);
    void ZUIEndTabBar(ZUIContext* ctx);

    // Scrollable list box — wraps a scroll region + Selectables.
    // w/h control the visible area; items are added as ZUISelectable calls inside.
    ZUIBox* ZUIBeginListBox(ZUIContext* ctx, const char* key,
                             ZUISize w = ZFill(), ZUISize h = ZPx(120.f));
    void    ZUIEndListBox(ZUIContext* ctx);

    // Bounded horizontal slider — maps thumb position to [v_min, v_max].
    // Returns true while value changes.
    bool ZUISliderFloat(ZUIContext* ctx, const char* key, float* value,
                        float v_min, float v_max,
                        ZUISize w = ZFill(), ZUISize h = ZPx(24.f));

    // Integer text field — editable with keyboard; clamped to [v_min, v_max].
    // Returns true when value changes.
    bool ZUIInputInt(ZUIContext* ctx, const char* key, int* value,
                     int v_min = -0x7FFFFFFF, int v_max = 0x7FFFFFFF,
                     ZUISize w = ZFill());

    // Multi-line text input inside a scroll region.
    // Returns true when buf changes.
    bool ZUIInputTextMultiline(ZUIContext* ctx, const char* key,
                                char* buf, uint32_t buf_size,
                                ZUISize w = ZFill(), ZUISize h = ZPx(120.f));

    // RGBA colour picker (hue bar + SV square + alpha bar).
    // color[4] in linear [0,1]. Returns true when changed.
    bool ZUIColorPicker4(ZUIContext* ctx, const char* key, float color[4]);

    // ---------------------------------------------------------------
    // Popup-based widgets
    // ---------------------------------------------------------------

    // Context menu — opens on right-click anywhere in the caller's region.
    // Pair with ZUIEndContextMenu when it returns true.
    bool ZUIBeginContextMenu(ZUIContext* ctx, const char* key);
    void ZUIEndContextMenu(ZUIContext* ctx);

    // Combo / dropdown.  preview_label is shown in the collapsed box.
    // Returns true while the dropdown is open. Add ZUISelectable items inside.
    bool ZUIBeginCombo(ZUIContext* ctx, const char* key,
                       const char* preview_label, ZUISize w = ZFill());
    void ZUIEndCombo(ZUIContext* ctx);

    // Menu bar — call once per frame to get a horizontal toolbar row.
    // Pair with ZUIEndMenuBar.
    bool ZUIBeginMenuBar(ZUIContext* ctx);
    void ZUIEndMenuBar(ZUIContext* ctx);

    // Menu button inside a menu bar. Opens a popup column on click.
    bool ZUIBeginMenu(ZUIContext* ctx, const char* label, bool enabled = true);
    void ZUIEndMenu(ZUIContext* ctx);

    // Modal — dims the background and shows a centred popup that cannot
    // be dismissed by clicking outside.
    void ZUIOpenModal(ZUIContext* ctx, const char* key);
    bool ZUIBeginModal(ZUIContext* ctx, const char* key, const char* title);
    void ZUIEndModal(ZUIContext* ctx);

    // ---------------------------------------------------------------
    // Plot widgets (ZUIDrawList-backed, ImGui PlotLines / PlotHistogram parity)
    // ---------------------------------------------------------------

    // Line chart. values[count] are sampled in [v_scale_min, v_scale_max].
    // Pass FLT_MAX for auto-scale. overlay_text is accepted but not rendered.
    void ZUIPlotLines    (ZUIContext* ctx, const char* key, const float* values, int count,
                          float v_scale_min = 3.402823e+38f, float v_scale_max = 3.402823e+38f,
                          const char* overlay_text = nullptr,
                          ZUISize w = ZFill(), ZUISize h = ZPx(40.f));

    void ZUIPlotHistogram(ZUIContext* ctx, const char* key, const float* values, int count,
                          float v_scale_min = 3.402823e+38f, float v_scale_max = 3.402823e+38f,
                          const char* overlay_text = nullptr,
                          ZUISize w = ZFill(), ZUISize h = ZPx(40.f));

    // ---------------------------------------------------------------
    // ZUITreeView — full-featured recursive tree (ImGui TreeNode parity)
    // ---------------------------------------------------------------

    struct ZUITreeViewConfig
    {
        float RowH     = 19.f; // logical px (ImGui GetFrameHeight)
        float IndentPx = 21.f; // px per depth level (ImGui IndentSpacing)
    };

    // Wraps a scroll region. cfg=nullptr uses defaults.
    ZUIBox* ZUIBeginTreeView(ZUIContext* ctx, const char* key,
                             ZUISize w = ZFill(), ZUISize h = ZFill(),
                             const ZUITreeViewConfig* cfg = nullptr);
    void    ZUIEndTreeView(ZUIContext* ctx);

    // Expandable node. Returns true when expanded — if true, add children then
    // call ZUITreeViewEndNode. icon_col=nullptr = no icon.
    // Returns true on click for external selection handling.
    bool ZUITreeViewBeginNode(ZUIContext* ctx, const char* label,
                              bool selected,
                              const float icon_col[4] = nullptr,
                              bool initial_open = false);
    void ZUITreeViewEndNode(ZUIContext* ctx);

    // Leaf — no expand arrow. Returns true when clicked.
    bool ZUITreeViewLeaf(ZUIContext* ctx, const char* label,
                         bool selected,
                         const float icon_col[4] = nullptr);

    // ---------------------------------------------------------------
    // ZUIDataTable — sortable, resizable data table (ImGui Table parity)
    // ---------------------------------------------------------------

    struct ZUIDataTableColumn
    {
        const char* Label;
        float       InitWidth;  // 0 = 100 logical px default
        bool        Sortable;
        bool        Resizable;
    };

    struct ZUITableSortSpec
    {
        int  ColumnIndex; // -1 = unsorted
        bool Ascending;
        bool Changed;     // true the frame the sort spec changed
    };

    // Begin a data table. Returns false if clipped (still call EndDataTable).
    bool ZUIBeginDataTable(ZUIContext* ctx, const char* key,
                           int col_count, const ZUIDataTableColumn* cols,
                           ZUISize h = ZFill());

    // Render the sticky header row with labels + sort arrows.
    // Call once, before any ZUIDataTableNextRow calls.
    void ZUIDataTableHeadersRow(ZUIContext* ctx);

    // Start the next data row. selected=true tints the row.
    // Returns true if the row was clicked (for selection).
    bool ZUIDataTableNextRow(ZUIContext* ctx, bool selected = false);

    // Set the active column for the current row. Call before adding content.
    void ZUIDataTableSetColumn(ZUIContext* ctx, int col);

    // Close the table. Always pair with ZUIBeginDataTable.
    void ZUIEndDataTable(ZUIContext* ctx);

    // Returns current sort spec. Changed=true the frame the user clicked a header.
    ZUITableSortSpec ZUIDataTableGetSortSpecs(ZUIContext* ctx);

    // ---------------------------------------------------------------
    // ZUIGridView — icon grid for content browsers and asset pickers
    // ---------------------------------------------------------------

    // Begin a grid view with fixed-size cells that wrap automatically.
    // item_w / item_h: cell dimensions in logical px.
    // w / h: scroll region size.
    ZUIBox* ZUIBeginGridView(ZUIContext* ctx, const char* key,
                             float item_w, float item_h,
                             ZUISize w = ZFill(), ZUISize h = ZFill());

    // Advance to the next grid cell.
    // Returns true if the cell was clicked (for external selection handling).
    // selected=true tints the cell background.
    bool ZUIGridViewNextItem(ZUIContext* ctx, const char* item_key, bool selected = false);

    // Close a cell opened by ZUIGridViewNextItem. Always pair.
    void ZUIGridViewEndItem(ZUIContext* ctx);

    // Close the grid view. Always pair with ZUIBeginGridView.
    void ZUIEndGridView(ZUIContext* ctx);

    // ---------------------------------------------------------------
    // Drag-and-drop helpers
    // ---------------------------------------------------------------

    // Call after ZUISignalFromBox for a source box. When the box is held and the
    // mouse has moved, records ctx->DragSourceKey + copies payload so the next
    // ZUIAcceptDrop call on the landing box can retrieve it.
    void ZUIBeginDragSource(ZUIContext* ctx, const ZUIBox* box,
                            const char* payload, uint32_t payload_len);

    // Call after ZUISignalFromBox for a potential drop target.
    // Returns true exactly once — on the BuildUI frame after the drop fires.
    // Copies the payload into out_buf (null-terminated). out_buf may be nullptr.
    bool ZUIAcceptDrop(ZUIContext* ctx, const ZUIBox* box,
                       char* out_buf, uint32_t out_size);

    // Display a texture in a box. texture_index is the bindless array slot
    // (e.g. TextureHandle::Index from SceneRenderer::GetFrameOutput()).
    void ZUIImage(ZUIContext* ctx, const char* key,
                  uint32_t texture_index, ZUISize w = ZFill(), ZUISize h = ZFill());

    // Single-line editable text field. When focused (after a click), text-input
    // events append to buf and backspace removes the last character.
    // Returns true if buf changed this frame.
    bool ZUITextField(ZUIContext* ctx, const char* key,
                      char* buf, uint32_t buf_size, float width_px = 160.f);

    // Search box — ZUITextField with a dim search icon on the left and
    // placeholder text rendered when the buffer is empty.
    bool ZUISearchBox(ZUIContext* ctx, const char* key,
                      char* buf, uint32_t buf_size,
                      const char* placeholder = "Search...",
                      ZUISize w = ZFill());

    // Thin (4 px) invisible-but-clickable resize strip. horizontal=true creates
    // a full-width 4-px-tall strip (top/bottom split); false creates a 4-px-wide
    // full-height strip (left/right split). While held, updates *value by
    // DragDelta clamped to [min_v, max_v]. Returns true when actively dragging.
    bool ZUIResizeHandle(ZUIContext* ctx, const char* key, float* value,
                         float min_v, float max_v, bool horizontal);

} // namespace ZEngine::UI
