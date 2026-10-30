#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoViewport.h>
#include <ZEngine/UI/ZUIInteraction.h>

using namespace ZEngine;
using namespace Core::Maths;
using namespace Rendering::Cameras;

namespace Tetragrama::Gizmo
{
    namespace
    {
        bool Blocked(const UI::ZUIContext& ctx)
        {
            return ctx.TextInputActive || ctx.ActiveModalKey || ctx.ModalBox || ctx.PopupStackSize || ctx.PendingPopupKey || ctx.DragSourceKey;
        }

        bool DragActive(const EditorSession& session, Operation mode, Handle constraint)
        {
            if (IsCameraHandle(constraint))
            {
                return session.CameraActive();
            }
            if (IsLightHandle(constraint))
            {
                return session.LightActive();
            }
            if (constraint == Handle::PivotEdit)
            {
                return session.PivotActive();
            }
            switch (mode)
            {
                case Operation::Translate:
                    return session.TranslationActive();
                case Operation::Rotate:
                    return session.RotationActive();
                case Operation::Scale:
                    return session.ScaleActive();
                default:
                    return false;
            }
        }

        bool OwnsDrag(const EditorSession& session, Operation mode, uint64_t key, uint64_t serial, Handle constraint)
        {
            if (IsCameraHandle(constraint))
            {
                return session.CameraKey() == key && session.CameraPointerSerial() == serial;
            }
            if (IsLightHandle(constraint))
            {
                return session.LightKey() == key && session.LightPointerSerial() == serial;
            }
            if (constraint == Handle::PivotEdit)
            {
                return session.PivotKey() == key && session.PivotPointerSerial() == serial;
            }
            switch (mode)
            {
                case Operation::Translate:
                    return session.TranslationKey() == key && session.TranslationPointerSerial() == serial;
                case Operation::Rotate:
                    return session.RotationKey() == key && session.RotationPointerSerial() == serial;
                case Operation::Scale:
                    return session.ScaleKey() == key && session.ScalePointerSerial() == serial;
                default:
                    return false;
            }
        }

        void UpdateDrag(EditorSession& session, Operation mode, const UI::ZUIContext& ctx, bool released, Handle constraint)
        {
            const float* point       = released ? ctx.MouseReleasePos[0] : ctx.MousePos;
            const bool   fine        = released ? ctx.MouseReleaseShift[0] : ctx.ShiftDown;
            const bool   invert_snap = released ? ctx.MouseReleaseCtrl[0] : ctx.CtrlDown;
            if (IsCameraHandle(constraint))
            {
                (void) session.UpdateCamera({point[0], point[1]}, !released, fine, invert_snap);
            }
            else if (IsLightHandle(constraint))
            {
                (void) session.UpdateLight({point[0], point[1]}, !released, fine, invert_snap);
            }
            else if (constraint == Handle::PivotEdit)
            {
                (void) session.UpdatePivot({point[0], point[1]}, !released, fine, invert_snap);
            }
            else if (mode == Operation::Rotate)
            {
                (void) session.UpdateRotation({point[0], point[1]}, !released, fine, invert_snap);
            }
            else if (mode == Operation::Scale)
            {
                (void) session.UpdateScale({point[0], point[1]}, !released, fine, invert_snap);
            }
            else if (mode == Operation::Translate)
            {
                (void) session.UpdateTranslation({point[0], point[1]}, !released, fine, invert_snap);
            }
        }
    } // namespace

    bool Viewport::OwnsPointerCapture(const EditorSession& session, const UI::ZUIContext* ctx) const
    {
        return session.OnOwnerThread() && Bound(ctx) && m_pointer_capture.Key && m_pointer_capture.Serial && ctx->ActiveKey == m_pointer_capture.Key && ctx->MousePressSerial[0] == m_pointer_capture.Serial && m_binding_revision == m_pointer_capture.BindingRevision && session.SceneEpoch() == m_pointer_capture.SceneEpoch && DragActive(session, m_pointer_capture.Mode, m_pointer_capture.Constraint) && OwnsDrag(session, m_pointer_capture.Mode, m_pointer_capture.Key, m_pointer_capture.Serial, m_pointer_capture.Constraint);
    }

    void Viewport::RetirePointerCapture(EditorSession& session, UI::ZUIContext* ctx, bool preserve_presentation)
    {
        if (ctx)
        {
            // Never erase a new widget's owner or a later physical press.
            if (ctx->ActiveKey == m_pointer_capture.Key)
            {
                ctx->ActiveKey = 0;
            }
            if (ctx->HotKey == m_pointer_capture.Key)
            {
                ctx->HotKey = 0;
            }
            if (ctx->MousePressSerial[0] == m_pointer_capture.Serial)
            {
                ctx->MousePressed[0] = false;
            }
            ctx->InteractionCapture = {session.Active(), session.Active()};
            m_pointer_capture       = {};
        }
        // With no context, retain the small retired-owner token until a context
        // returns, so its stale active key/keyboard capture can be released then.
        if (preserve_presentation)
        {
            m_allow_hits          = false;
            m_interaction_enabled = false;
            m_hovered             = Handle::None;
            return;
        }
        Invalidate();
    }

    void Viewport::UpdateTransformInput(EditorSession& session, const CameraFrameData& camera, UI::ZUIContext* ctx, bool focused, bool allow_begin)
    {
        // Reject before touching routing/UI state, including on a wrong thread.
        if (!session.InMutationPhase())
        {
            return;
        }
        const uint64_t serial = ctx ? ctx->MousePressSerial[0] : 0;
        const bool     fresh  = serial && serial != m_observed_left_press;
        if (ctx)
        {
            m_observed_left_press = serial;
            if (fresh)
            {
                // A missing prior release cannot own a later physical click.
                m_passive_selection_release = 0;
            }
        }
        const bool owns = m_pointer_capture.Key && DragActive(session, m_pointer_capture.Mode, m_pointer_capture.Constraint) && OwnsDrag(session, m_pointer_capture.Mode, m_pointer_capture.Key, m_pointer_capture.Serial, m_pointer_capture.Constraint) && session.SceneEpoch() == m_pointer_capture.SceneEpoch;
        if (m_pointer_capture.Key && !owns)
        {
            RetirePointerCapture(session, ctx, ctx != nullptr);
            return; // A lifecycle cancellation cannot rearm a press in this update.
        }
        if (owns)
        {
            const bool released = ctx && ctx->MouseReleaseSerial[0] == m_pointer_capture.Serial;
            const bool valid    = focused && Bound(ctx) && m_binding_revision == m_pointer_capture.BindingRevision && SameCamera(camera, m_pointer_capture.Camera) && !Blocked(*ctx);
            const bool lost     = !ctx || (!released && (serial != m_pointer_capture.Serial || !ctx->MouseDown[0] || ctx->ActiveKey != m_pointer_capture.Key)) || (released && ctx->ActiveKey && ctx->ActiveKey != m_pointer_capture.Key && serial == m_pointer_capture.Serial);
            if (!valid || lost || (ctx && ctx->EscapePressed))
            {
                session.Cancel();
                if (ctx && ctx->EscapePressed)
                {
                    ctx->EscapePressed = false;
                }
            }
            else
            {
                UpdateDrag(session, m_pointer_capture.Mode, *ctx, released, m_pointer_capture.Constraint);
            }
            if (!DragActive(session, m_pointer_capture.Mode, m_pointer_capture.Constraint))
            {
                RetirePointerCapture(session, ctx, ctx != nullptr);
            }
            else
            {
                ctx->InteractionCapture = {true, true};
            }
            return;
        }
        if (!allow_begin || !fresh || !Eligible(session, ctx, focused) || Blocked(*ctx) || ctx->EscapePressed || ctx->MouseDown[1] || ctx->MouseDown[2] || (!ctx->MouseDown[0] && ctx->MouseReleaseSerial[0] != serial))
        {
            return;
        }
        // A newly pressed navigation modifier can veto an unowned transform
        // press, but cannot reinterpret it as a pivot edit or rearm it later.
        if (ctx->AltDown && !ctx->MousePressAlt[0])
        {
            return;
        }
        // Rebuild from synchronized current identity/camera, then arbitrate at
        // the latched press position, not the potentially much later cursor.
        session.Scene()->SynchronizeHierarchy();
        BuildHitFrame(session, camera, ctx, focused, ctx->MousePressAlt[0]);
        const auto passive_query = UI::ZUIQueryPointer(ctx, ctx->MousePressPos[0]);
        if (passive_query.HotBox && passive_query.HotBox->Key == m_hit_key)
        {
            EntityTarget passive = {};
            if (PickPassiveComponent({ctx->MousePressPos[0][0], ctx->MousePressPos[0][1]}, passive) && session.Resolve(passive) == passive.CachedEntity && session.RequestSelection(passive.CachedEntity))
            {
                m_passive_selection_release     = serial;
                ctx->InteractionCapture.Pointer = true;
                ctx->MousePressed[0]            = false;
                return;
            }
        }
        if (!m_allow_hits)
        {
            return;
        }
        const auto query = UI::ZUIQueryPointer(ctx, ctx->MousePressPos[0]);
        if (!query.HotBox || query.HotBox->Key != m_hit_key)
        {
            return;
        }
        const auto                handle = Handle(m_hits.Pick({ctx->MousePressPos[0][0], ctx->MousePressPos[0][1]}).Handle);
        const Vec2f               pointer(ctx->MousePressPos[0][0], ctx->MousePressPos[0][1]);
        const auto                mode       = session.GizmoSettings().Mode;
        const auto                kind       = camera.ProjectionType == CameraType::ORTHOGRAPHIC ? Geometry::ProjectionKind::Orthographic : Geometry::ProjectionKind::Perspective;
        Geometry::ProjectionFrame projection = {};
        if (!Geometry::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, kind, camera.UsesReverseZ, &projection))
        {
            return;
        }
        const bool pivot = ctx->MousePressAlt[0];
        if (pivot)
        {
            if (!IsCenterHandle(handle) || !session.BeginPivot({.Expected = m_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .Key = m_hit_key, .PointerSerial = serial}))
            {
                return;
            }
        }
        else if (IsCameraHandle(handle))
        {
            if (!session.BeginCamera({.Expected = m_camera_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .Key = m_hit_key, .PointerSerial = serial, .Constraint = handle}))
            {
                return;
            }
        }
        else if (IsLightHandle(handle))
        {
            if (!session.BeginLight({.Expected = m_light_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .Key = m_hit_key, .PointerSerial = serial, .Constraint = handle}))
            {
                return;
            }
        }
        else if (mode == Operation::Rotate)
        {
            Vec3f ring_point = {};
            if (!m_hits.PickRingPoint(handle, pointer, projection, ring_point) || !session.BeginRotation({.Expected = m_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .RingPoint = ring_point, .Key = m_hit_key, .PointerSerial = serial, .Constraint = handle}))
            {
                return;
            }
        }
        else if (mode == Operation::Scale)
        {
            if (!session.BeginScale({.Expected = m_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .Key = m_hit_key, .PointerSerial = serial, .Constraint = handle}))
            {
                return;
            }
        }
        else if (mode != Operation::Translate || !session.BeginTranslation({.Expected = m_target, .Projection = projection, .Bounds = m_bounds, .Pointer = pointer, .Key = m_hit_key, .PointerSerial = serial, .Constraint = handle}))
        {
            return;
        }
        const auto constraint = pivot ? Handle::PivotEdit : handle;
        m_pointer_capture     = {.Camera = camera, .Key = m_hit_key, .Serial = serial, .SceneEpoch = session.SceneEpoch(), .BindingRevision = m_binding_revision, .Mode = mode, .Constraint = constraint};
        m_allow_hits          = false;
        m_hovered             = Handle::None;
        ctx->ActiveKey        = m_hit_key;
        ctx->MousePressed[0]  = false;
        const bool released   = ctx->MouseReleaseSerial[0] == serial;
        UpdateDrag(session, mode, *ctx, released, constraint);
        if (!DragActive(session, mode, constraint))
        {
            RetirePointerCapture(session, ctx, true);
        }
        else
        {
            ctx->InteractionCapture = {true, true};
        }
    }
} // namespace Tetragrama::Gizmo
