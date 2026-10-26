#include <Tetragrama/Gizmo/GizmoCameraBuilder.h>
#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUITokens.h>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine;
using namespace Core::Maths;
using namespace Rendering::Renderers;
namespace G = Core::Maths::Geometry;
namespace C = Tetragrama::Gizmo::Constants;

namespace Tetragrama::Gizmo
{
    bool BuildCameraIcon(const CameraFrame& frame, GizmoPrimitiveBuilder& builder, uint32_t fill_color)
    {
        if (!frame.Valid || !builder.Valid())
        {
            return false;
        }
        const auto                             point                  = [&](double right, double up, double forward, Vec3f& output) { return CameraPoint(frame, right, up, forward, output); };
        const float                            body_left              = -C::CameraLensLengthWorld - C::CameraBodyWidthWorld;
        const float                            body_right             = -C::CameraLensLengthWorld;
        const float                            body_half_height       = C::CameraBodyHeightWorld * .5f;
        const float                            lens_inner_half_height = C::CameraLensInnerHeightWorld * .5f;
        const float                            lens_outer_half_height = C::CameraLensOuterHeightWorld * .5f;
        Core::Containers::FixedArray<Vec3f, 8> body                   = {};
        Core::Containers::FixedArray<Vec3f, 8> lens                   = {};
        for (uint32_t face = 0; face < 2; ++face)
        {
            const float    forward = face ? C::CameraIconDepthWorld * .5f : -C::CameraIconDepthWorld * .5f;
            const uint32_t offset  = face * 4;
            // The original glyph's horizontal axis is the camera's looking
            // direction. Extrude it across Right so its lens and its frustum
            // always point together in world space.
            if (!point(forward, body_half_height, body_left, body[offset]) || !point(forward, body_half_height, body_right, body[offset + 1]) || !point(forward, -body_half_height, body_right, body[offset + 2]) || !point(forward, -body_half_height, body_left, body[offset + 3]) || !point(forward, lens_inner_half_height, body_right, lens[offset]) || !point(forward, lens_outer_half_height, 0.f, lens[offset + 1]) || !point(forward, -lens_outer_half_height, 0.f, lens[offset + 2]) || !point(forward, -lens_inner_half_height, body_right, lens[offset + 3]))
            {
                return false;
            }
        }
        const auto triangle = [&](const Vec3f& a, const Vec3f& b, const Vec3f& c) {
            const auto result = builder.Triangle({.A = a, .B = b, .C = c, .Color = fill_color, .AntiAliasEdges = 0});
            return result == GizmoEmitResult::Emitted || result == GizmoEmitResult::Culled;
        };
        const auto quad = [&](const Vec3f& a, const Vec3f& b, const Vec3f& c, const Vec3f& d) {
            // Adjacent volume faces share vertices. Coverage filtering each
            // face separately leaves pinholes there, so the solid uses its
            // regular rasterized edges and no per-triangle edge coverage.
            return triangle(a, b, c) && triangle(a, c, d);
        };
        for (uint32_t face = 0; face < 2; ++face)
        {
            const uint32_t offset = face * 4;
            if (!quad(body[offset], body[offset + 1], body[offset + 2], body[offset + 3]) || !quad(lens[offset], lens[offset + 1], lens[offset + 2], lens[offset + 3]))
            {
                return false;
            }
        }
        for (uint32_t corner = 0; corner < 4; ++corner)
        {
            const uint32_t next = (corner + 1) % 4;
            if (!quad(body[corner], body[next], body[next + 4], body[corner + 4]))
            {
                return false;
            }
            // Lens edge 3 -> 0 lies inside the body end-cap. Emitting it would
            // create coplanar triangles and flicker as the viewport moves.
            if (corner != 3 && !quad(lens[corner], lens[next], lens[next + 4], lens[corner + 4]))
            {
                return false;
            }
        }
        return true;
    }

    namespace
    {
        uint32_t Packed(const UI::Tokens::Color& token)
        {
            return UI::ZUIPackColor(token.R, token.G, token.B, token.A);
        }
        struct Sink
        {
            const FrameInput&         Input;
            const G::ProjectionFrame& Camera;
            HitFrame&                 Hits;
            GizmoPrimitiveBuilder     Builder;
            Sink(const FrameInput& input, const G::ProjectionFrame& camera, GizmoFrameData& output, HitFrame& hits) : Input(input), Camera(camera), Hits(hits), Builder(output, camera, input.Viewport.Size.y, input.TargetWidth, input.TargetHeight) {}
            bool Line(const Vec3f& a, const Vec3f& b, const UI::Tokens::Color& color = UI::Tokens::TextPrimary, float width = C::ComponentLineWidthPx, bool dashed = false)
            {
                const auto result = Builder.Line({.A = a, .B = b, .Width = width, .DashOn = dashed ? C::GhostDashOnPx : 0.f, .DashOff = dashed ? C::GhostDashOffPx : 0.f, .Color = Packed(color)});
                return result == GizmoEmitResult::Emitted || result == GizmoEmitResult::Culled;
            }
            bool HandleSprite(Handle handle)
            {
                CameraHandle value = {};
                if (!ResolveCameraHandle(Input.Frustum, handle, value))
                {
                    return false;
                }
                const bool emphasized = handle == (Input.Active != Handle::None ? Input.Active : Input.Hovered);
                const auto result     = Builder.Sprite({.Position = value.Position, .Size = emphasized ? C::ComponentHandleEmphasizedPx : C::CameraHandleSizePx, .Fill = Packed(UI::Tokens::TextPrimary)});
                if (result == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (result != GizmoEmitResult::Emitted)
                {
                    return false;
                }
                G::ProjectedPoint point = {};
                G::Ray            ray   = {};
                if (!G::Project(value.Position, Camera, Input.Viewport, &point) || !G::UnprojectRay(point.Screen, Camera, Input.Viewport, &ray))
                {
                    return false;
                }
                const double along = double(ray.Direction.x) * value.Direction.x + double(ray.Direction.y) * value.Direction.y + double(ray.Direction.z) * value.Direction.z;
                if (1.0 - along * along <= C::AxisParallelDenominator)
                {
                    return true;
                }
                const double depth = (double(value.Position.x) - Camera.Position.x) * Camera.Forward.x + (double(value.Position.y) - Camera.Position.y) * Camera.Forward.y + (double(value.Position.z) - Camera.Position.z) * Camera.Forward.z;
                if (!std::isfinite(depth) || depth <= 0.0 || depth > std::numeric_limits<float>::max() || Hits.Count >= HitFrame::MaxShapes)
                {
                    return false;
                }
                const float size          = emphasized ? C::ComponentHandleEmphasizedPx : C::CameraHandleSizePx;
                HitShape    shape         = {.Handle = uint32_t(handle), .Priority = HitPriority::Component, .Geometry = HitGeometry::Rectangle, .Depth = float(depth), .Padding = C::BoxHitPaddingPx};
                shape.Points[0]           = point.Screen - Vec2f(size * .5f, size * .5f);
                shape.Points[1]           = point.Screen + Vec2f(size * .5f, size * .5f);
                Hits.Shapes[Hits.Count++] = shape;
                return true;
            }
            bool Build()
            {
                const auto&                            frame       = Input.Frustum;
                Core::Containers::FixedArray<Vec3f, 4> near_points = {}, far_points = {};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!CameraCorner(frame, i, frame.Camera.Near, near_points[i]) || !CameraCorner(frame, i, frame.Camera.Far, far_points[i]))
                    {
                        return false;
                    }
                }
                constexpr uint32_t order[] = {0, 1, 3, 2};
                for (uint32_t i = 0; i < 4; ++i)
                {
                    if (!Line(near_points[order[i]], near_points[order[(i + 1) % 4]]) || !Line(far_points[order[i]], far_points[order[(i + 1) % 4]]) || !Line(near_points[i], far_points[i]))
                    {
                        return false;
                    }
                }
                const double half   = double(frame.Camera.FovY) * C::HalfRadiansPerDegree;
                const double radius = double(frame.Camera.Far) * C::CameraFovArcFraction;
                const double step   = 2.0 * half / C::CameraFovArcSegments;
                const double cosine = std::cos(step), sine = std::sin(step);
                double       x = std::cos(-half), y = std::sin(-half);
                Vec3f        previous = {};
                if (!CameraPoint(frame, 0.0, y * radius, x * radius, previous))
                {
                    return false;
                }
                for (uint32_t i = 1; i <= C::CameraFovArcSegments; ++i)
                {
                    const double next_x = x * cosine - y * sine;
                    y                   = y * cosine + x * sine;
                    x                   = next_x;
                    Vec3f next          = {};
                    if (!CameraPoint(frame, 0.0, y * radius, x * radius, next) || !Line(previous, next, UI::Tokens::AccentBlue))
                    {
                        return false;
                    }
                    previous = next;
                }
                Vec3f axis = {};
                if (!CameraPoint(frame, 0.0, 0.0, double(frame.Camera.Far) * .5, axis) || !Line(frame.Origin, axis, UI::Tokens::AccentBlue, C::ComponentLineWidthPx, true))
                {
                    return false;
                }
                for (uint32_t handle = uint32_t(Handle::CameraFovUpperLeft); handle <= uint32_t(Handle::CameraNearRight); ++handle)
                {
                    if (!HandleSprite(Handle(handle)))
                    {
                        return false;
                    }
                }
                return BuildCameraIcon(frame, Builder, Packed(UI::Tokens::BgViewport));
            }
        };
    } // namespace
    void BuildCameraGeometry(const FrameInput& input, const G::ProjectionFrame& camera, GizmoFrameData& output, HitFrame& hits)
    {
        if (!input.Frustum.Valid)
        {
            return;
        }
        const uint32_t     lines = output.LineCount, triangles = output.TriangleCount, sprites = output.SpriteCount, shapes = hits.Count;
        constexpr uint32_t line_budget = 12 + C::CameraFovArcSegments + 1;
        if (!ValidCameraGeometry(input.Frustum) || lines > GizmoFrameData::MaxLines - line_budget - 5 * C::RingSegments || triangles > GizmoFrameData::MaxTriangles - 22 || sprites > GizmoFrameData::MaxSprites - 10 || shapes > HitFrame::MaxComponentShapes - 6)
        {
            output.DecorationsOmitted = true;
            return;
        }
        const auto&  projection = input.Camera.Projection;
        const double near_z     = camera.ReverseZ ? double(projection(3, 2)) - projection(2, 2) : projection(2, 2);
        const double near_w     = camera.ReverseZ ? double(projection(3, 3)) - projection(2, 3) : projection(2, 3);
        const auto&  origin     = input.Frustum.Origin;
        const double depth      = (double(origin.x) - camera.Position.x) * camera.Forward.x + (double(origin.y) - camera.Position.y) * camera.Forward.y + (double(origin.z) - camera.Position.z) * camera.Forward.z;
        if (near_z != 0.0 && std::isfinite(depth) && depth < near_w / near_z * C::NearDistanceMultiplier)
        {
            return;
        }
        Sink sink(input, camera, output, hits);
        if (!sink.Builder.Valid() || !sink.Build())
        {
            output.LineCount          = lines;
            output.TriangleCount      = triangles;
            output.SpriteCount        = sprites;
            hits.Count                = shapes;
            output.DecorationsOmitted = true;
        }
    }
} // namespace Tetragrama::Gizmo
