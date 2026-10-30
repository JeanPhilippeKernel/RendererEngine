#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoCameraBuilder.h>
#include <Tetragrama/Gizmo/GizmoValuePill.h>
#include <Tetragrama/Gizmo/GizmoViewport.h>
#include <ZEngine/ECS/Components/CameraComponent.h>
#include <ZEngine/ECS/Components/LightComponent.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>
#include <ZEngine/Managers/AssetManager.h>
#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUIInteraction.h>
#include <ZEngine/UI/ZUITokens.h>
#include <cmath>
#include <limits>
#include <numbers>

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
        m_cache_valid = m_allow_hits = m_interaction_enabled = false;
        m_hovered                                            = Handle::None;
        m_target                                             = {};
        m_light_target                                       = {};
        m_camera_target                                      = {};
        m_hits.Reset();
        m_scratch.Reset();
        m_passive_component_count    = 0;
        m_passive_components_omitted = false;
        m_passive_selection_release  = 0;
    }

    void Viewport::RefreshPassiveComponents(EditorSession& session, const CameraFrameData& camera)
    {
        m_passive_component_count    = 0;
        m_passive_components_omitted = false;
        auto* scene                  = session.Scene();
        if (!scene || m_bounds.Size.x <= 0.f || m_bounds.Size.y <= 0.f)
        {
            return;
        }
        Geometry::ProjectionFrame projection = {};
        const auto                kind       = camera.ProjectionType == CameraType::PERSPECTIVE ? Geometry::ProjectionKind::Perspective : Geometry::ProjectionKind::Orthographic;
        if (!Geometry::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, kind, camera.UsesReverseZ, &projection))
        {
            return;
        }
        const auto add = [&](ECS::EntityID entity, const auto& transform, const auto& identity, PassiveComponentKind kind, bool camera_forward) {
            if (session.Selection().Contains(identity.Value))
            {
                return;
            }
            // The active transform/component gizmos can add up to sixteen
            // sprites. Keep enough room so every cached passive marker is
            // actually emitted and therefore pickable.
            if (m_passive_component_count == m_passive_components.size() - 16)
            {
                m_passive_components_omitted = true;
                return;
            }
            const Vec3f origin(transform.WorldTransform(0, 3), transform.WorldTransform(1, 3), transform.WorldTransform(2, 3));
            Vec3f       forward(transform.WorldTransform(0, 2), transform.WorldTransform(1, 2), transform.WorldTransform(2, 2));
            const auto  normalize = [](Vec3f& value) {
                const double length = std::hypot(double(value.x), double(value.y), double(value.z));
                if (!std::isfinite(length) || length <= 0.0)
                {
                    return false;
                }
                value = {float(value.x / length), float(value.y / length), float(value.z / length)};
                return true;
            };
            Geometry::ProjectedPoint projected = {};
            if (!normalize(forward) || !Geometry::Project(origin, projection, m_bounds, &projected))
            {
                return;
            }
            const float depth = float((double(origin.x) - camera.Position.x) * projection.Forward.x + (double(origin.y) - camera.Position.y) * projection.Forward.y + (double(origin.z) - camera.Position.z) * projection.Forward.z);
            if (!std::isfinite(depth) || depth <= 0.f)
            {
                return;
            }
            const float sign = camera_forward ? -1.f : 1.f;
            forward          = forward * sign;
            Vec3f right = {}, up = {};
            if (camera_forward)
            {
                right              = {transform.WorldTransform(0, 0), transform.WorldTransform(1, 0), transform.WorldTransform(2, 0)};
                const double along = double(right.x) * forward.x + double(right.y) * forward.y + double(right.z) * forward.z;
                right              = right - forward * float(along);
                if (!normalize(right))
                {
                    return;
                }
                up = cross3d(right, forward);
                if (!normalize(up))
                {
                    return;
                }
            }
            auto& marker = m_passive_components[m_passive_component_count++];
            marker       = {.Target = session.TargetFor(entity), .Origin = origin, .Right = right, .Up = up, .Forward = forward, .Screen = projected.Screen, .Depth = depth, .Kind = kind};
        };
        scene->ForEach<ECS::Components::LightComponent, ECS::Components::TransformComponent, ECS::Components::UUIDComponent>([&](ECS::EntityID entity, const auto& light, const auto& transform, const auto& identity) {
            const auto kind = light.LightType == ECS::Components::LightComponent::Type::Directional ? PassiveComponentKind::DirectionalLight : light.LightType == ECS::Components::LightComponent::Type::Point ? PassiveComponentKind::PointLight : PassiveComponentKind::SpotLight;
            add(entity, transform, identity, kind, false);
        });
        scene->ForEach<ECS::Components::CameraComponent, ECS::Components::TransformComponent, ECS::Components::UUIDComponent>([&](ECS::EntityID entity, const auto&, const auto& transform, const auto& identity) { add(entity, transform, identity, PassiveComponentKind::Camera, true); });
    }

    void Viewport::BuildPassiveComponents(const CameraFrameData& camera, uint32_t width, uint32_t height, Rendering::Renderers::GizmoFrameData& output) const
    {
        Geometry::ProjectionFrame projection = {};
        const auto                kind       = camera.ProjectionType == CameraType::PERSPECTIVE ? Geometry::ProjectionKind::Perspective : Geometry::ProjectionKind::Orthographic;
        if (!Geometry::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, kind, camera.UsesReverseZ, &projection))
        {
            return;
        }
        Rendering::Renderers::GizmoPrimitiveBuilder builder(output, projection, m_bounds.Size.y, width, height);
        const auto&                                 light_color         = UI::Tokens::PinVector;
        const auto&                                 camera_fill         = UI::Tokens::BgViewport;
        const uint32_t                              light_packed        = UI::ZUIPackColor(light_color.R, light_color.G, light_color.B, .8f);
        const uint32_t                              camera_fill_packed  = UI::ZUIPackColor(camera_fill.R, camera_fill.G, camera_fill.B, camera_fill.A);
        output.DecorationsOmitted                                      |= m_passive_components_omitted;
        for (uint32_t i = 0; builder.Valid() && i < m_passive_component_count; ++i)
        {
            const auto& marker = m_passive_components[i];
            if (marker.Kind == PassiveComponentKind::Camera)
            {
                CameraFrame frame = {.Origin = marker.Origin, .Right = marker.Right, .Up = marker.Up, .Forward = marker.Forward, .Valid = true};
                if (!BuildCameraIcon(frame, builder, camera_fill_packed))
                {
                    output.DecorationsOmitted = true;
                }
                continue;
            }

            const float sun_size = marker.Kind == PassiveComponentKind::PointLight ? Constants::PointSunSizePx : marker.Kind == PassiveComponentKind::SpotLight ? Constants::SpotSunSizePx : Constants::DirectionalSunSizePx;
            (void) builder.Sprite({.Position = marker.Origin, .Size = sun_size, .Fill = light_packed, .Round = true});
            Geometry::ProjectedPoint origin = {};
            if (Geometry::Project(marker.Origin, projection, m_bounds, &origin))
            {
                const float ray_start = sun_size * .5f + Constants::LightSunRayGapPx;
                for (uint32_t ray = 0; ray < 8; ++ray)
                {
                    const float angle = 2.f * std::numbers::pi_v<float> * float(ray) / 8.f;
                    const Vec2f ray_direction(std::cos(angle), std::sin(angle));
                    Vec3f       a = {}, b = {};
                    if (Geometry::UnprojectAtDepth(origin.Screen + ray_direction * ray_start, origin.Depth, projection, m_bounds, &a) && Geometry::UnprojectAtDepth(origin.Screen + ray_direction * (ray_start + Constants::LightSunRayLengthPx), origin.Depth, projection, m_bounds, &b))
                    {
                        (void) builder.Line({.A = a, .B = b, .Width = Constants::ComponentLineWidthPx, .Color = light_packed});
                    }
                }
            }
            if (marker.Kind == PassiveComponentKind::PointLight)
            {
                continue;
            }
            const Vec3f tip = marker.Origin + marker.Forward * Constants::DirectionalArrowWorldLength;
            (void) builder.Line({.A = marker.Origin, .B = tip, .Width = Constants::ComponentLineWidthPx, .Color = light_packed, .RoundCaps = true});
            Geometry::ProjectedPoint start = {}, end = {};
            if (!Geometry::Project(marker.Origin, projection, m_bounds, &start) || !Geometry::Project(tip, projection, m_bounds, &end))
            {
                continue;
            }
            const Vec2f  axis   = end.Screen - start.Screen;
            const double length = std::hypot(double(axis.x), double(axis.y));
            if (length <= 1.0)
            {
                continue;
            }
            const Vec2f direction(float(axis.x / length), float(axis.y / length));
            const Vec2f side(-direction.y, direction.x);
            const float head = float(std::min(double(Constants::ArrowLengthPx), length * .25));
            const Vec2f base = end.Screen - direction * head;
            Vec3f       a = {}, b = {};
            if (!Geometry::UnprojectAtDepth(base + side * (head * Constants::ArrowWidthPx / Constants::ArrowLengthPx * .5f), end.Depth, projection, m_bounds, &a) || !Geometry::UnprojectAtDepth(base - side * (head * Constants::ArrowWidthPx / Constants::ArrowLengthPx * .5f), end.Depth, projection, m_bounds, &b))
            {
                continue;
            }
            const auto result = builder.Triangle({.A = tip, .B = a, .C = b, .Color = light_packed});
            if (result == Rendering::Renderers::GizmoEmitResult::CapacityExceeded)
            {
                output.DecorationsOmitted = true;
            }
        }
    }

    bool Viewport::PickPassiveComponent(const Vec2f& point, EntityTarget& target) const
    {
        target      = {};
        float depth = std::numeric_limits<float>::max();
        for (uint32_t i = 0; i < m_passive_component_count; ++i)
        {
            const auto& marker = m_passive_components[i];
            Vec2f       extent = {};
            if (marker.Kind == PassiveComponentKind::Camera)
            {
                extent = Vec2f(24.f, 20.f) + Vec2f(Constants::BoxHitPaddingPx, Constants::BoxHitPaddingPx);
            }
            else
            {
                const float size = marker.Kind == PassiveComponentKind::PointLight ? Constants::PointSunSizePx : marker.Kind == PassiveComponentKind::SpotLight ? Constants::SpotSunSizePx : Constants::DirectionalSunSizePx;
                extent           = {size * .5f + Constants::BoxHitPaddingPx, size * .5f + Constants::BoxHitPaddingPx};
            }
            if (std::abs(point.x - marker.Screen.x) <= extent.x && std::abs(point.y - marker.Screen.y) <= extent.y && marker.Depth < depth)
            {
                target = marker.Target;
                depth  = marker.Depth;
            }
        }
        return target.CachedEntity != ECS::INVALID_ENTITY;
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

    bool Viewport::RayFromPointer(const float point[2], const CameraFrameData& camera, Geometry::Ray& ray) const
    {
        if (!point || !m_bound || m_bounds.Size.x <= 0.f || m_bounds.Size.y <= 0.f)
        {
            return false;
        }

        const auto                kind       = camera.ProjectionType == CameraType::ORTHOGRAPHIC ? Geometry::ProjectionKind::Orthographic : Geometry::ProjectionKind::Perspective;
        Geometry::ProjectionFrame projection = {};
        return Geometry::MakeProjectionFrame(camera.View, camera.Projection, camera.Position, kind, camera.UsesReverseZ, &projection) && Geometry::UnprojectRay({point[0], point[1]}, projection, m_bounds, &ray);
    }

    bool Viewport::ToRenderPixel(const float point[2], uint32_t width, uint32_t height, uint32_t& x, uint32_t& y) const
    {
        x = y = 0;
        if (!m_bound || !point || width == 0 || height == 0 || !std::isfinite(point[0]) || !std::isfinite(point[1]) || m_bounds.Size.x <= 0.f || m_bounds.Size.y <= 0.f)
        {
            return false;
        }
        const float local_x = point[0] - m_bounds.Origin.x;
        const float local_y = point[1] - m_bounds.Origin.y;
        if (local_x < 0.f || local_y < 0.f || local_x >= m_bounds.Size.x || local_y >= m_bounds.Size.y)
        {
            return false;
        }
        const double scaled_x = std::floor(double(local_x) * double(width) / double(m_bounds.Size.x));
        const double scaled_y = std::floor(double(local_y) * double(height) / double(m_bounds.Size.y));
        if (scaled_x < 0.0 || scaled_y < 0.0 || scaled_x >= double(width) || scaled_y >= double(height))
        {
            return false;
        }
        x = static_cast<uint32_t>(scaled_x);
        y = static_cast<uint32_t>(scaled_y);
        return true;
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
        if (!Bound(ctx))
        {
            Invalidate();
            return;
        }
        if (!focused)
        {
            // Losing OS focus cancels interaction, but the renderer retains
            // the last completed frame until the window becomes active again.
            m_allow_hits          = false;
            m_interaction_enabled = false;
            m_hovered             = Handle::None;
            return;
        }
        m_interaction_enabled       = true;
        TargetFrame target          = {};
        LightFrame  light           = {};
        CameraFrame frustum         = {};
        const auto  mode            = session.GizmoSettings().Mode;
        const bool  grid            = session.Selection().ActiveSceneTarget() == SceneTarget::Grid;
        pivot_editable              = pivot_editable && !grid;
        const bool transform        = mode != Operation::Select && (!grid || mode != Operation::Scale) && ResolveTargetFrame(session, target);
        const bool component        = ResolveLightFrame(session, light);
        const bool camera_component = ResolveCameraFrame(session, frustum);
        RefreshPassiveComponents(session, camera);
        if (!transform && !component && !camera_component)
        {
            m_cache_valid = m_allow_hits = false;
            m_hovered                    = Handle::None;
            m_target                     = {};
            m_light_target               = {};
            m_camera_target              = {};
            m_hits.Reset();
            m_scratch.Reset();
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
        if (!viewport.m_interaction_enabled || box->Key != viewport.m_hit_key)
        {
            return false;
        }
        if (viewport.m_allow_hits && viewport.m_cache_valid)
        {
            const auto handle = Handle(viewport.m_hits.Pick({point[0], point[1]}).Handle);
            if (handle != Handle::None && (!viewport.m_input.PivotEditable || IsCenterHandle(handle)))
            {
                return true;
            }
        }
        EntityTarget passive = {};
        return viewport.PickPassiveComponent({point[0], point[1]}, passive);
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
            BuildPassiveComponents(camera, width, height, output);
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
            return;
        }
        BuildPassiveComponents(camera, width, height, output);
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
