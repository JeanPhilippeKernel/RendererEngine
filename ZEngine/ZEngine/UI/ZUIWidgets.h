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

    // Regular button. w/h default to ZText()/ZPx(28); pass ZPx(n) to override.
    ZUISignal ZUIButton(ZUIContext* ctx, const char* label,
                        ZUISize w = ZText(), ZUISize h = ZPx(28.f));

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
    // Phase 3 — simple standalone widgets
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

    // ---------------------------------------------------------------
    // Phase 5 — layout improvements
    // ---------------------------------------------------------------

    // Place the next item on the same line as the previous one.
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
    // Phase 7 — complex widgets
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
    // Phase 4 — popup-based widgets
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

    // ---------------------------------------------------------------
    // Panel drag header (gap 4)
    // ---------------------------------------------------------------

    // Renders a draggable title bar row (DrawBackground + Clickable).
    // While held + mouse is moving, *inout_x and *inout_y are updated by DragDelta.
    // Returns true if the panel was dragged this frame (position changed).
    // Double-click resets *detached to false (snap back to dockspace).
    bool ZUIPanelDragHeader(ZUIContext* ctx, const char* title,
                            float* inout_x, float* inout_y, bool* detached);

    // Display a texture in a box. texture_index is the bindless array slot
    // (e.g. TextureHandle::Index from SceneRenderer::GetFrameOutput()).
    void ZUIImage(ZUIContext* ctx, const char* key,
                  uint32_t texture_index, ZUISize w = ZFill(), ZUISize h = ZFill());

    // Single-line editable text field. When focused (after a click), text-input
    // events append to buf and backspace removes the last character.
    // Returns true if buf changed this frame.
    bool ZUITextField(ZUIContext* ctx, const char* key,
                      char* buf, uint32_t buf_size, float width_px = 160.f);

} // namespace ZEngine::UI
