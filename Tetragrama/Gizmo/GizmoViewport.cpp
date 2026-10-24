#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoValuePill.h>
#include <Tetragrama/Gizmo/GizmoViewport.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <cmath>
#include <limits>

using namespace ZEngine;
using namespace Core::Maths;
using namespace Rendering::Cameras;

namespace Tetragrama::Gizmo
{
    bool Viewport::SameCamera(const CameraFrameData& a, const CameraFrameData& b)
    {
        if (a.ProjectionType != b.ProjectionType || a.UsesReverseZ != b.UsesReverseZ || a.Position.x != b.Position.x || a.Position.y != b.Position.y || a.Position.z != b.Position.z)
        {
            return false;
        }
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                if (a.View(row, col) != b.View(row, col) || a.Projection(row, col) != b.Projection(row, col))
                {
                    return false;
                }
            }
        }
        return true;
    }

    void Viewport::Invalidate()
    {
        m_cache_valid = m_allow_hits = false;
        m_hovered                    = Handle::None;
        m_target                     = {};
        m_light_target               = {};
        m_camera_target              = {};
        m_hits.Reset();
        m_scratch.Reset();
    }

    void Viewport::Bind(const UI::ZUIContext* ctx, const float rect[4], uint64_t viewport_key, uint64_t hit_key)
    {
        const bool was_bound = m_bound;
        m_bound              = false;
        if (!ctx || !rect || !viewport_key || !hit_key)
        {
            Invalidate();
            return;
        }
        const Geometry::Viewport bounds = {
            {          rect[0],           rect[1]},
            {rect[2] - rect[0], rect[3] - rect[1]}
        };
        for (uint32_t axis = 0; axis < 2; ++axis)
        {
            if (!std::isfinite(bounds.Origin[axis]) || !std::isfinite(bounds.Size[axis]) || bounds.Size[axis] < 1.f || double(bounds.Size[axis]) > std::numeric_limits<uint32_t>::max() || !std::isfinite(ctx->InputScale[axis]) || ctx->InputScale[axis] <= 0.f)
            {
                Invalidate();
                return;
            }
        }
        if (!was_bound || m_bounds.Origin.x != bounds.Origin.x || m_bounds.Origin.y != bounds.Origin.y || m_bounds.Size.x != bounds.Size.x || m_bounds.Size.y != bounds.Size.y || m_viewport_key != viewport_key || m_hit_key != hit_key || m_screen_w != ctx->ScreenW || m_screen_h != ctx->ScreenH || m_input_scale[0] != ctx->InputScale[0] || m_input_scale[1] != ctx->InputScale[1])
        {
            Invalidate();
            if (++m_binding_revision == 0)
            {
                ++m_binding_revision;
            }
            // A press predating this layout must not be replayed into it.
            m_observed_left_press = ctx->MousePressSerial[0];
        }
        m_bounds         = bounds;
        m_viewport_key   = viewport_key;
        m_hit_key        = hit_key;
        m_ui_frame       = ctx->FrameIndex;
        m_screen_w       = ctx->ScreenW;
        m_screen_h       = ctx->ScreenH;
        m_input_scale[0] = ctx->InputScale[0];
        m_input_scale[1] = ctx->InputScale[1];
        m_bound          = true;
    }

    bool Viewport::Bound(const UI::ZUIContext* ctx) const
    {
        return m_bound && ctx && ctx->ViewportInputKey == m_viewport_key && ctx->FrameIndex == m_ui_frame && ctx->ScreenW == m_screen_w && ctx->ScreenH == m_screen_h && ctx->InputScale[0] == m_input_scale[0] && ctx->InputScale[1] == m_input_scale[1];
    }

    bool Viewport::Eligible(const EditorSession& session, const UI::ZUIContext* ctx, bool focused) const
    {
        return Bound(ctx) && focused && session.Editable() && !session.Active() && !ctx->InteractionCapture.Keyboard && !ctx->TextInputActive && !ctx->ActiveModalKey && !ctx->ModalBox && !ctx->PopupStackSize && !ctx->PendingPopupKey && !ctx->DragSourceKey && (!ctx->ActiveKey || ctx->ActiveKey == m_viewport_key || ctx->ActiveKey == m_hit_key);
    }

    void Viewport::BuildHitFrame(EditorSession& session, const CameraFrameData& camera, const UI::ZUIContext* ctx, bool focused)
    {
        BuildHitFrame(session, camera, ctx, focused, ctx && ctx->AltDown);
    }

    void Viewport::BuildHitFrame(EditorSession& session, const CameraFrameData& camera, const UI::ZUIContext* ctx, bool focused, bool pivot_editable)
    {
        if (!Bound(ctx) || !focused)
        {
            Invalidate();
            return;
        }
        TargetFrame target          = {};
        LightFrame  light           = {};
        CameraFrame frustum         = {};
        const auto  mode            = session.GizmoSettings().Mode;
        const bool  grid            = session.Selection().ActiveSceneTarget() == SceneTarget::Grid;
        pivot_editable              = pivot_editable && !grid;
        const bool transform        = mode != Operation::Select && (!grid || mode != Operation::Scale) && ResolveTargetFrame(session, target);
        const bool component        = ResolveLightFrame(session, light);
        const bool camera_component = ResolveCameraFrame(session, frustum);
        if (!transform && !component && !camera_component)
        {
            Invalidate();
            return;
        }
        const bool same_transform = target.Valid == m_target.Valid && (!target.Valid || SameTargetFrame(target, m_target));
        if (!m_cache_valid || !same_transform || !SameLightFrame(light, m_light_target) || !SameCameraFrame(frustum, m_camera_target) || m_input.Mode != mode || !SameCamera(camera, m_input.Camera) || m_input.PivotEditable != pivot_editable)
        {
            m_input             = {.Camera = camera, .Viewport = m_bounds, .Pivot = target.Pivot, .Axes = target.Axes, .TargetWidth = uint32_t(m_bounds.Size.x), .TargetHeight = uint32_t(m_bounds.Size.y), .Mode = session.GizmoSettings().Mode, .Editable = true, .AuthoredScale = target.AuthoredScale};
            m_input.PivotMarker = {
                .Origin = {target.World(0, 3), target.World(1, 3), target.World(2, 3)},
                  .Visible = target.CustomPivot
            };
            m_input.PivotEditable     = pivot_editable;
            m_input.Light             = light;
            m_input.Frustum           = frustum;
            m_input.TransformsVisible = transform;
            if (!transform)
            {
                m_input.Pivot = light.Valid ? light.Origin : frustum.Origin;
            }
            m_target        = target;
            m_light_target  = light;
            m_camera_target = frustum;
            m_hovered       = Handle::None;
            m_cache_valid   = BuildFrame(m_input, m_scratch, m_hits) == FrameBuildResult::Built;
        }
        m_allow_hits = m_cache_valid && Eligible(session, ctx, focused);
        for (bool navigating : m_navigation)
        {
            m_allow_hits &= !navigating;
        }
    }

    bool Viewport::Filter(const UI::ZUIBox* box, const float point[2], const void* data)
    {
        if (!box || !point || !data)
        {
            return false;
        }
        const auto& viewport = *static_cast<const Viewport*>(data);
        if (!viewport.m_allow_hits || !viewport.m_cache_valid || box->Key != viewport.m_hit_key)
        {
            return false;
        }
        const auto handle = Handle(viewport.m_hits.Pick({point[0], point[1]}).Handle);
        return handle != Handle::None && (!viewport.m_input.PivotEditable || IsCenterHandle(handle));
    }

    Handle Viewport::QueryHover(const UI::ZUIContext* ctx) const
    {
        if (!m_allow_hits || !ctx)
        {
            return Handle::None;
        }
        const auto query = UI::ZUIQueryPointer(ctx, ctx->MousePos);
        return query.HotBox && query.HotBox->Key == m_hit_key ? Handle(m_hits.Pick({ctx->MousePos[0], ctx->MousePos[1]}).Handle) : Handle::None;
    }

    void Viewport::UpdateGestures(const UI::ZUIContext* ctx)
    {
        if (!ctx)
        {
            return;
        }
        // A camera gesture begun away from a handle must not be stolen when
        // its locked/moving cursor later crosses the gizmo. Press serials work
        // even when overlay edge flags have not yet been consumed.
        for (uint32_t button = 0; button < 3; ++button)
        {
            if (!ctx->MouseDown[button])
            {
                m_navigation[button]   = false;
                m_handle_press[button] = false;
            }
            else if (!m_was_down[button] || m_press_serial[button] != ctx->MousePressSerial[button])
            {
                // The mutation phase may have captured this press before the
                // hover pass. Active sessions reject new hit eligibility, but
                // the matching owned press is still a gizmo gesture, not camera
                // navigation. Never extend this exemption to a foreign owner
                // or a later physical press.
                const bool captured  = button == 0 && m_pointer_capture.Key && m_pointer_capture.Key == m_hit_key && ctx->ActiveKey == m_pointer_capture.Key && m_pointer_capture.Serial == ctx->MousePressSerial[0];
                m_navigation[button] = !captured;
                if (!captured && m_allow_hits)
                {
                    const auto query     = UI::ZUIQueryPointer(ctx, ctx->MousePressPos[button]);
                    m_navigation[button] = !query.HotBox || query.HotBox->Key != m_hit_key;
                }
                m_handle_press[button] = !m_navigation[button];
            }
            m_was_down[button]     = ctx->MouseDown[button];
            m_press_serial[button] = ctx->MousePressSerial[button];
        }
    }

    void Viewport::UpdateHover(EditorSession& session, const CameraFrameData& camera, UI::ZUIContext* ctx, bool focused)
    {
        // Release latches before eligibility, including when the previous frame
        // rejected a camera gesture. Do not replay an old press on focus return.
        for (uint32_t button = 0; button < 3; ++button)
        {
            if (!ctx || !focused || !ctx->MouseDown[button] || m_press_serial[button] != ctx->MousePressSerial[button])
            {
                m_navigation[button]   = false;
                m_was_down[button]     = false;
                m_handle_press[button] = false;
            }
        }
        BuildHitFrame(session, camera, ctx, focused);
        UpdateGestures(ctx);
        for (bool navigating : m_navigation)
        {
            m_allow_hits &= !navigating;
        }
        if (!ctx)
        {
            m_hovered = Handle::None;
            return;
        }
        const bool owned_hot = m_hit_key && ctx->HotKey == m_hit_key;
        uint64_t   hot       = 0;
        // Idle/hidden gizmos add no retained-tree scan. Only live handles or
        // retiring our own stale hot state need paint-order arbitration.
        if (m_allow_hits || owned_hot)
        {
            const auto query = UI::ZUIQueryPointer(ctx, ctx->MousePos);
            hot              = query.HotBox ? query.HotBox->Key : 0;
        }
        m_hovered = m_allow_hits && hot == m_hit_key ? Handle(m_hits.Pick({ctx->MousePos[0], ctx->MousePos[1]}).Handle) : Handle::None;
        // Retire our own stale hot/active state, never another widget's owner.
        if (m_hit_key && (ctx->HotKey == m_hit_key || hot == m_hit_key))
        {
            ctx->HotKey = hot;
        }
        if (m_hit_key && ctx->ActiveKey == m_hit_key && (!m_cache_valid || !focused || !ctx->MouseDown[0] || m_navigation[0]))
        {
            ctx->ActiveKey = 0;
        }
        // Hover-only presentation must not start camera motion halfway through
        // an Alt/left, middle or right press that began on a handle.
        const bool handle_press          = m_handle_press[0] || m_handle_press[1] || m_handle_press[2];
        ctx->InteractionCapture.Pointer |= m_hovered != Handle::None || (handle_press && m_cache_valid && Eligible(session, ctx, focused));
    }

    void Viewport::BuildRender(EditorSession& session, const CameraFrameData& camera, const UI::ZUIContext* ctx, bool focused, uint32_t width, uint32_t height, Rendering::Renderers::GizmoFrameData& output)
    {
        output.Reset();
        BuildHitFrame(session, camera, ctx, focused);
        if (!m_cache_valid)
        {
            return;
        }
        m_hovered          = QueryHover(ctx);
        FrameInput input   = m_input;
        input.TargetWidth  = width;
        input.TargetHeight = height;
        input.Hovered      = m_hovered;
        if (session.CameraActive())
        {
            CameraSnapshot snapshot = {};
            if (!session.ReadCameraSnapshot(snapshot))
            {
                Invalidate();
                return;
            }
            input.Frustum = snapshot.Frame;
            input.Active  = snapshot.Constraint;
        }
        else if (session.LightActive())
        {
            LightSnapshot snapshot = {};
            if (!session.ReadLightSnapshot(snapshot))
            {
                Invalidate();
                return;
            }
            input.Light  = snapshot.Frame;
            input.Active = snapshot.Constraint;
        }
        else if (session.PivotActive())
        {
            BuildPivotPresentation(session, input);
            if (input.Active != Handle::PivotEdit)
            {
                Invalidate();
                return;
            }
        }
        else if (session.TranslationActive())
        {
            BuildTranslationPresentation(session, input);
            if (!input.Drag.Visible)
            {
                Invalidate();
                return;
            }
        }
        else if (session.RotationActive())
        {
            BuildRotationPresentation(session, input);
            if (!input.Rotation.Visible || !input.Drag.Visible)
            {
                Invalidate();
                return;
            }
        }
        else if (session.ScaleActive())
        {
            BuildScalePresentation(session, input);
            if (!input.Scale.Visible || !input.Drag.Visible)
            {
                Invalidate();
                return;
            }
        }
        else
        {
            m_drag_key = m_drag_serial = m_drag_epoch = 0;
            m_ghost_valid                             = false;
            m_root_generation = m_root_epoch = 0;
            m_root_session                   = nullptr;
        }
        // Keep the live cache on the exact emitted geometry, including a dimmed
        // handle whose packed alpha rounded to zero. Never pick invisible idle
        // geometry while presenting a highlighted frame.
        if (BuildFrame(input, output, m_hits) != FrameBuildResult::Built)
        {
            Invalidate();
        }
    }

    void Viewport::BuildTranslationPresentation(EditorSession& session, FrameInput& input)
    {
        TranslationSnapshot snapshot = {};
        if (!session.ReadTranslationSnapshot(snapshot) || snapshot.Scene != m_target.Scene || snapshot.Target.UUID != m_target.Target.UUID || snapshot.Target.CachedEntity != m_target.Target.CachedEntity || snapshot.Target.SceneEpoch != m_target.Target.SceneEpoch)
        {
            return;
        }
        input.Active          = snapshot.Constraint;
        input.Pivot           = snapshot.Pivot;
        input.Drag.StartPivot = snapshot.StartPivot;
        input.Drag.Visible    = true;
        BuildTransformDecorations(session, input, snapshot.Target, snapshot.StartWorld, snapshot.World, snapshot.Key, snapshot.PointerSerial);
    }

    void Viewport::BuildRotationPresentation(EditorSession& session, FrameInput& input)
    {
        RotationSnapshot snapshot = {};
        if (!session.ReadRotationSnapshot(snapshot) || snapshot.Scene != m_target.Scene || snapshot.Target.UUID != m_target.Target.UUID || snapshot.Target.CachedEntity != m_target.Target.CachedEntity || snapshot.Target.SceneEpoch != m_target.Target.SceneEpoch)
        {
            return;
        }
        input.Active          = snapshot.Constraint;
        input.Pivot           = snapshot.Pivot;
        input.Rotation        = {.Axis = snapshot.Axis, .StartDirection = snapshot.StartDirection, .Angle = snapshot.Angle, .Visible = true};
        input.Drag.StartPivot = snapshot.Pivot;
        input.Drag.Visible    = true;
        BuildTransformDecorations(session, input, snapshot.Target, snapshot.StartWorld, snapshot.World, snapshot.Key, snapshot.PointerSerial);
    }

    void Viewport::BuildTransformDecorations(EditorSession& session, FrameInput& input, const EntityTarget& target, const Mat4f& start, const Mat4f& world, uint64_t key, uint64_t serial)
    {
        if (session.Selection().ActiveSceneTarget() != SceneTarget::None)
        {
            return; // Grid has no entity/mesh bounds or selected-root decoration.
        }
        if (session.Selection().Entities().size() > 1)
        {
            if (!BuildRootDecorations(session, input))
            {
                input.Drag.Visible = false;
            }
        }
        else
        {
            BuildBoxPresentation(session, input, target, start, world, key, serial);
        }
    }

    bool Viewport::BuildRootDecorations(EditorSession& session, FrameInput& input)
    {
        RootFrameView frames = {};
        if (!session.ReadGizmoRootFrames(frames))
        {
            return false;
        }
        const bool fresh = m_root_session != &session || m_root_generation != frames.Generation || m_root_epoch != session.SceneEpoch();
        if (fresh)
        {
            // Only the live visual prefix is initialized; no maximum-selection
            // arrays or full renderer frame are cleared on each presentation.
            for (uint32_t i = 0; i < frames.Frames.size(); ++i)
            {
                m_root_decorations[i] = {};
                m_root_bounds[i]      = {};
            }
            m_root_generation = frames.Generation;
            m_root_epoch      = session.SceneEpoch();
            m_root_session    = &session;
        }
        const auto* scene  = session.Scene();
        auto*       assets = Managers::AssetManager::Instance();
        for (uint32_t i = 0; i < frames.Frames.size(); ++i)
        {
            const auto& root      = frames.Frames[i];
            const auto* transform = scene->GetComponent<ECS::Components::TransformComponent>(root.Target.CachedEntity);
            if (!transform)
            {
                return false; // Whole-batch validation above normally guarantees this.
            }
            auto& decoration        = m_root_decorations[i];
            auto& bounds            = m_root_bounds[i];
            decoration.StartPivot   = {root.StartWorld(0, 3), root.StartWorld(1, 3), root.StartWorld(2, 3)};
            decoration.CurrentPivot = {transform->WorldTransform(0, 3), transform->WorldTransform(1, 3), transform->WorldTransform(2, 3)};
            decoration.Visible      = true;
            decoration.HasBox       = false;
            const auto* mesh        = scene->GetComponent<ECS::Components::MeshComponent>(root.Target.CachedEntity);
            // A mesh may become available late, but a different live binding
            // must never replace the original press-time ghost identity.
            if (!mesh || root.MeshUUID.is_nil() || mesh->MeshUUID != root.MeshUUID)
            {
                continue;
            }
            if (!bounds.Valid && assets)
            {
                bounds.Valid = assets->TryGetMeshAABB(root.MeshUUID, bounds.Minimum, bounds.Maximum) && Geometry::TransformBox(bounds.Minimum, bounds.Maximum, root.StartWorld, &decoration.Ghost);
            }
            if (bounds.Valid && Geometry::TransformBox(bounds.Minimum, bounds.Maximum, transform->WorldTransform, &decoration.Current))
            {
                decoration.MeshUUID = root.MeshUUID;
                decoration.HasBox   = true;
            }
        }
        input.Roots                  = {m_root_decorations.data(), frames.Frames.size()};
        input.RootDecorationsOmitted = frames.Frames.size() < frames.TotalRoots;
        return true;
    }

    void Viewport::BuildBoxPresentation(EditorSession& session, FrameInput& input, const EntityTarget& target, const Mat4f& start, const Mat4f& world, uint64_t key, uint64_t serial)
    {
        const auto* component = session.Scene()->GetComponent<ECS::Components::MeshComponent>(target.CachedEntity);
        const auto  mesh      = component ? component->MeshUUID : uuids::uuid{};
        if (m_drag_key != key || m_drag_serial != serial || m_drag_epoch != target.SceneEpoch)
        {
            m_drag_key    = key;
            m_drag_serial = serial;
            m_drag_epoch  = target.SceneEpoch;
            m_drag_mesh   = mesh;
            m_ghost_valid = false;
        }
        // Capture the start-pose corners once. Retry an initially unavailable CPU
        // asset, but never silently retarget the ghost to another mesh binding.
        if (mesh.is_nil() || mesh != m_drag_mesh)
        {
            return;
        }
        auto* assets = Managers::AssetManager::Instance();
        if (!m_ghost_valid && assets)
        {
            m_ghost_valid = assets->TryGetMeshAABB(mesh, m_drag_min, m_drag_max) && Geometry::TransformBox(m_drag_min, m_drag_max, start, &m_ghost);
        }
        if (!m_ghost_valid || !Geometry::TransformBox(m_drag_min, m_drag_max, world, &input.Drag.Current))
        {
            return;
        }
        input.Drag.Ghost    = m_ghost;
        input.Drag.MeshUUID = mesh;
        input.Drag.HasBox   = true;
    }

    void Viewport::BuildScalePresentation(EditorSession& session, FrameInput& input)
    {
        ScaleSnapshot snapshot = {};
        if (!session.ReadScaleSnapshot(snapshot) || snapshot.Target.UUID != m_target.Target.UUID || snapshot.Target.CachedEntity != m_target.Target.CachedEntity || snapshot.Target.SceneEpoch != m_target.Target.SceneEpoch)
        {
            return;
        }
        input.Active          = snapshot.Constraint;
        input.Pivot           = snapshot.Pivot;
        input.Axes            = snapshot.Axes;
        input.AuthoredScale   = snapshot.AuthoredScale;
        input.Scale           = {.Ratio = snapshot.Ratio, .HandleLength = snapshot.HandleLength, .Visible = true};
        input.Drag.StartPivot = snapshot.Pivot;
        input.Drag.Visible    = true;
        BuildTransformDecorations(session, input, snapshot.Target, snapshot.StartWorld, snapshot.World, snapshot.Key, snapshot.PointerSerial);
    }

    void Viewport::BuildPivotPresentation(EditorSession& session, FrameInput& input)
    {
        PivotSnapshot snapshot = {};
        if (!session.ReadPivotSnapshot(snapshot) || snapshot.Target.UUID != m_target.Target.UUID || snapshot.Target.CachedEntity != m_target.Target.CachedEntity || snapshot.Target.SceneEpoch != m_target.Target.SceneEpoch)
        {
            return;
        }
        input.Active            = Handle::PivotEdit;
        input.Pivot             = snapshot.Position;
        input.PivotMarker       = {.Origin = snapshot.Origin, .Visible = true};
        input.Drag.Visible      = true;
        input.Drag.GhostVisible = false;
        BuildBoxPresentation(session, input, snapshot.Target, m_target.World, m_target.World, snapshot.Key, snapshot.PointerSerial);
    }

    void Viewport::BuildPill(EditorSession& session, UI::ZUIContext* ctx)
    {
        if (!Bound(ctx))
        {
            return;
        }
        LightSnapshot  light  = {};
        CameraSnapshot camera = {};
        if (session.ReadCameraSnapshot(camera))
        {
            (void) DrawCameraPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, camera.Constraint, camera.Value);
            return;
        }
        if (session.ReadLightSnapshot(light))
        {
            (void) DrawLightPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, light.Constraint, light.Value);
            return;
        }
        LightHandle  hovered        = {};
        CameraHandle camera_hovered = {};
        if (!session.Active() && ResolveCameraHandle(m_camera_target, m_hovered, camera_hovered))
        {
            const float value = camera_hovered.Field == ECS::AuthoredFloatField::CameraFar ? m_camera_target.Camera.Far : camera_hovered.Field == ECS::AuthoredFloatField::CameraNear ? m_camera_target.Camera.Near : m_camera_target.Camera.FovY;
            (void) DrawCameraPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, m_hovered, value);
            return;
        }
        if (!session.Active() && ResolveLightHandle(m_light_target, m_hovered, hovered))
        {
            (void) DrawLightPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, m_hovered, hovered.Field == ECS::AuthoredFloatField::LightRange ? m_light_target.Light.Range : m_light_target.Light.SpotAngle);
            return;
        }
        if (session.PivotActive() || (m_target.Scene.Kind == SceneTarget::None && ctx->AltDown && IsCenterHandle(m_hovered)))
        {
            (void) DrawPivotPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]});
            return;
        }
        TranslationSnapshot snapshot = {};
        if (session.ReadTranslationSnapshot(snapshot))
        {
            (void) DrawTranslationPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, snapshot.Constraint, snapshot.Components, snapshot.Scene.Kind == SceneTarget::Grid ? AxisLabels::UNV : AxisLabels::XYZ);
        }
        else
        {
            RotationSnapshot rotation = {};
            if (session.ReadRotationSnapshot(rotation))
            {
                (void) DrawRotationPill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, rotation.Constraint, rotation.Angle, rotation.Scene.Kind == SceneTarget::Grid ? AxisLabels::UNV : AxisLabels::XYZ);
            }
            else
            {
                ScaleSnapshot scale = {};
                if (session.ReadScaleSnapshot(scale))
                {
                    (void) DrawScalePill(ctx, m_bounds, {ctx->MousePos[0], ctx->MousePos[1]}, scale.Constraint, scale.Ratio);
                }
            }
        }
    }
} // namespace Tetragrama::Gizmo
