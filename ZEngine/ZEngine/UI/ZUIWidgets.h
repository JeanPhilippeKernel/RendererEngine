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

    // ---------------------------------------------------------------
    // Leaf widgets
    // ---------------------------------------------------------------

    // Static text — no interaction
    void ZUILabel(ZUIContext* ctx, const char* text, const float color[4] = nullptr);

    // Clickable text-on-background button — returns signal with ZUI_SignalClicked set on click
    ZUISignal ZUIButton(ZUIContext* ctx, const char* label);

    // 1 px horizontal divider
    void ZUISeparator(ZUIContext* ctx);

    // Empty space of 'px' pixels along the parent's layout axis
    void ZUISpacer(ZUIContext* ctx, float px);

    // Collapsible tree row with a disclosure indicator.
    // *open is toggled on click. Returns signal from the row box.
    ZUISignal ZUITreeNode(ZUIContext* ctx, const char* label, bool* open);

    // Drag to edit a single float value. Horizontal mouse drag changes *value
    // by delta * speed. Returns true if *value changed this frame.
    // width_px controls the box width; typically 60–80 px inside a row.
    bool ZUIDragFloat(ZUIContext* ctx, const char* key,
                      float* value, float speed = 0.05f, float width_px = 60.f);

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
