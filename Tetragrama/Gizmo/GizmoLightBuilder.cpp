#include <Tetragrama/Gizmo/GizmoLightBuilder.h>
#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <ZEngine/UI/ZUIDrawList.h>
#include <ZEngine/UI/ZUITokens.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

using namespace ZEngine;
using namespace Core::Maths;
using namespace Rendering::Renderers;
namespace G = Core::Maths::Geometry;
namespace C = Tetragrama::Gizmo::Constants;
using Type  = ECS::Components::LightComponent::Type;

namespace Tetragrama::Gizmo
{
    namespace
    {
        uint32_t Color(float alpha = 1.f)
        {
            const auto& color = UI::Tokens::PinVector;
            return UI::ZUIPackColor(color.R, color.G, color.B, alpha);
        }

        const Core::Containers::FixedArray<Vec2f, C::RingSegments + 1>& Circle()
        {
            static const auto points = [] {
                Core::Containers::FixedArray<Vec2f, C::RingSegments + 1> result = {};
                for (uint32_t i = 0; i < C::RingSegments; ++i)
                {
                    const double angle = 2.0 * std::numbers::pi * i / C::RingSegments;
                    result[i]          = {float(std::cos(angle)), float(std::sin(angle))};
                }
                result[C::RingSegments] = result[0];
                return result;
            }();
            return points;
        }

        bool Point(const Vec3f& origin, const Vec3f& a, double x, const Vec3f& b, double y, Vec3f& result)
        {
            Vec3f point = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const double value = double(origin[axis]) + double(a[axis]) * x + double(b[axis]) * y;
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                point[axis] = float(value);
            }
            result = point;
            return true;
        }

        struct Sink
        {
            const FrameInput&         Input;
            const G::ProjectionFrame& Camera;
            GizmoFrameData&           Output;
            HitFrame&                 Hits;
            GizmoPrimitiveBuilder     Builder;

            Sink(const FrameInput& input, const G::ProjectionFrame& camera, GizmoFrameData& output, HitFrame& hits) : Input(input), Camera(camera), Output(output), Hits(hits), Builder(output, camera, input.Viewport.Size.y, input.TargetWidth, input.TargetHeight) {}

            bool Line(const Vec3f& a, const Vec3f& b, float width = C::ComponentLineWidthPx, float dash_on = 0.f, float dash_off = 0.f, float alpha = 1.f, float* phase = nullptr)
            {
                float next_phase = 0.f;
                if (phase && dash_on + dash_off > 0.f)
                {
                    G::ProjectedPoint start = {}, end = {};
                    // Advance across the original projected segment, including
                    // portions outside the depth range. Behind-eye projections
                    // have no finite phase anchor: restart at the next span.
                    if (G::Project(a, Camera, Input.Viewport, &start) && G::Project(b, Camera, Input.Viewport, &end))
                    {
                        next_phase = float(std::fmod(double(*phase) + std::hypot(double(end.Screen.x) - start.Screen.x, double(end.Screen.y) - start.Screen.y), double(dash_on) + dash_off));
                    }
                }
                const auto emitted = Builder.Line({.A = a, .B = b, .Width = width, .DashOn = dash_on, .DashOff = dash_off, .DashPhase = phase ? *phase : 0.f, .Color = Color(alpha)});
                if (emitted != GizmoEmitResult::Emitted && emitted != GizmoEmitResult::Culled)
                {
                    return false;
                }
                if (phase)
                {
                    *phase = next_phase;
                }
                return true;
            }

            bool Ring(const Vec3f& origin, const Vec3f& a, const Vec3f& b, double radius, bool dashed)
            {
                float phase    = 0.f;
                Vec3f previous = {};
                if (!Point(origin, a, radius, {}, 0.0, previous))
                {
                    return false;
                }
                const auto& circle = Circle();
                for (uint32_t i = 1; i <= C::RingSegments; ++i)
                {
                    Vec3f next = {};
                    if (!Point(origin, a, radius * circle[i].x, b, radius * circle[i].y, next) || !Line(previous, next, C::ComponentLineWidthPx, dashed ? C::GhostDashOnPx : 0.f, dashed ? C::GhostDashOffPx : 0.f, 1.f, &phase))
                    {
                        return false;
                    }
                    previous = next;
                }
                return true;
            }

            bool Sun()
            {
                const float size      = Input.Light.Light.Type == Type::Point ? C::PointSunSizePx : Input.Light.Light.Type == Type::Spot ? C::SpotSunSizePx : C::DirectionalSunSizePx;
                const float ray_start = size * .5f + C::LightSunRayGapPx;
                const auto  emitted   = Builder.Sprite({.Position = Input.Light.Origin, .Size = size, .Fill = Color(), .Round = true});
                if (emitted != GizmoEmitResult::Emitted && emitted != GizmoEmitResult::Culled)
                {
                    return false;
                }
                G::ProjectedPoint origin = {};
                if (!G::Project(Input.Light.Origin, Camera, Input.Viewport, &origin))
                {
                    return false;
                }
                const auto& circle = Circle();
                for (uint32_t i = 0; i < 8; ++i)
                {
                    const auto direction = circle[i * C::RingSegments / 8];
                    Vec3f      a = {}, b = {};
                    if (!G::UnprojectAtDepth(origin.Screen + direction * ray_start, origin.Depth, Camera, Input.Viewport, &a) || !G::UnprojectAtDepth(origin.Screen + direction * (ray_start + C::LightSunRayLengthPx), origin.Depth, Camera, Input.Viewport, &b) || !Line(a, b))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool HandleSprite(Handle handle)
            {
                LightHandle value = {};
                if (!ResolveLightHandle(Input.Light, handle, value))
                {
                    return false;
                }
                const bool  emphasized = handle == (Input.Active != Handle::None ? Input.Active : Input.Hovered);
                const float size       = emphasized ? C::ComponentHandleEmphasizedPx : C::ComponentHandleSizePx;
                const auto  emitted    = Builder.Sprite({.Position = value.Position, .Size = size, .Fill = Color()});
                if (emitted == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (emitted != GizmoEmitResult::Emitted)
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
                    return true; // visible but cannot establish a safe axis mapping
                }
                if (Hits.Count == HitFrame::MaxShapes)
                {
                    return false;
                }
                const double depth = (double(value.Position.x) - Camera.Position.x) * Camera.Forward.x + (double(value.Position.y) - Camera.Position.y) * Camera.Forward.y + (double(value.Position.z) - Camera.Position.z) * Camera.Forward.z;
                if (!std::isfinite(depth) || depth <= 0.0 || depth > std::numeric_limits<float>::max())
                {
                    return false;
                }
                HitShape shape            = {.Handle = uint32_t(handle), .Priority = HitPriority::Component, .Geometry = HitGeometry::Rectangle, .Depth = float(depth), .Padding = C::BoxHitPaddingPx};
                shape.Points[0]           = point.Screen - Vec2f(size * .5f, size * .5f);
                shape.Points[1]           = point.Screen + Vec2f(size * .5f, size * .5f);
                Hits.Shapes[Hits.Count++] = shape;
                return true;
            }

            bool PointLight()
            {
                const Vec3f x(1.f, 0.f, 0.f), y(0.f, 1.f, 0.f), z(0.f, 0.f, 1.f);
                if (!Ring(Input.Light.Origin, x, y, Input.Light.Light.Range, true) || !Ring(Input.Light.Origin, x, z, Input.Light.Light.Range, true) || !Ring(Input.Light.Origin, y, z, Input.Light.Light.Range, true))
                {
                    return false;
                }
                for (uint32_t handle = uint32_t(Handle::LightRangePositiveX); handle <= uint32_t(Handle::LightRangeNegativeZ); ++handle)
                {
                    if (!HandleSprite(Handle(handle)))
                    {
                        return false;
                    }
                }
                return Sun();
            }

            bool SpotLight()
            {
                const auto&  light  = Input.Light;
                const double radius = double(light.Light.Range) * std::tan(double(light.Light.SpotAngle) * C::HalfRadiansPerDegree);
                Vec3f        base   = {};
                if (!Point(light.Origin, light.Forward, light.Light.Range, {}, 0.0, base) || !Ring(base, light.Right, light.Up, radius, false))
                {
                    return false;
                }
                for (uint32_t i = 0; i < 4; ++i)
                {
                    const auto angle = Circle()[i * C::RingSegments / 4];
                    Vec3f      edge  = {};
                    if (!Point(base, light.Right, radius * angle.x, light.Up, radius * angle.y, edge) || !Line(light.Origin, edge))
                    {
                        return false;
                    }
                }
                return Line(light.Origin, base, C::ComponentLineWidthPx, C::GhostDashOnPx, C::GhostDashOffPx) && HandleSprite(Handle::SpotRange) && HandleSprite(Handle::SpotOuterAngle) && Sun();
            }

            bool DirectionalLight()
            {
                const auto& light = Input.Light;
                Vec3f       tip   = {};
                if (!Point(light.Origin, light.Forward, C::DirectionalArrowWorldLength, {}, 0.0, tip) || !Line(light.Origin, tip, C::AxisIdleWidthPx))
                {
                    return false;
                }
                for (float sign : {-1.f, 1.f})
                {
                    Vec3f a = {}, b = {};
                    if (!Point(light.Origin, light.Right, sign * C::DirectionalRayWorldOffset, {}, 0.0, a) || !Point(tip, light.Right, sign * C::DirectionalRayWorldOffset, {}, 0.0, b) || !Line(a, b, C::ComponentLineWidthPx, C::GhostDashOnPx, C::GhostDashOffPx, C::DirectionalRayAlpha))
                    {
                        return false;
                    }
                }
                // Screen-facing head on the world direction, using the existing
                // logical arrow metrics and checked world reconstruction.
                G::ProjectedPoint start = {}, end = {};
                if (!G::Project(light.Origin, Camera, Input.Viewport, &start) || !G::Project(tip, Camera, Input.Viewport, &end))
                {
                    return Sun(); // clipped line remains valid; no fabricated head
                }
                const Vec2f  axis   = end.Screen - start.Screen;
                const double length = std::hypot(double(axis.x), double(axis.y));
                if (length > 1.0)
                {
                    const Vec2f direction(float(axis.x / length), float(axis.y / length));
                    const Vec2f side(-direction.y, direction.x);
                    const float head = float(std::min(double(C::ArrowLengthPx), length * .25));
                    Vec3f       a = {}, b = {};
                    const Vec2f base = end.Screen - direction * head;
                    if (!G::UnprojectAtDepth(base + side * (head * C::ArrowWidthPx / C::ArrowLengthPx * .5f), end.Depth, Camera, Input.Viewport, &a) || !G::UnprojectAtDepth(base - side * (head * C::ArrowWidthPx / C::ArrowLengthPx * .5f), end.Depth, Camera, Input.Viewport, &b))
                    {
                        return false;
                    }
                    const auto result = Builder.Triangle({.A = tip, .B = a, .C = b, .Color = Color()});
                    if (result != GizmoEmitResult::Emitted && result != GizmoEmitResult::Culled)
                    {
                        return false;
                    }
                }
                return Sun();
            }
        };
    } // namespace

    void BuildLightGeometry(const FrameInput& input, const G::ProjectionFrame& camera, GizmoFrameData& output, HitFrame& hits)
    {
        if (!input.Light.Valid)
        {
            return;
        }
        const uint32_t     lines = output.LineCount, triangles = output.TriangleCount, sprites = output.SpriteCount, shapes = hits.Count;
        constexpr uint32_t line_budget = 3 * C::RingSegments + 16, transform_line_reserve = 5 * C::RingSegments;
        if (!ValidLightGeometry(input.Light) || lines > GizmoFrameData::MaxLines - line_budget - transform_line_reserve || sprites > GizmoFrameData::MaxSprites - 11 || triangles > GizmoFrameData::MaxTriangles - 8 || shapes > HitFrame::MaxShapes - HitFrame::MaxComponentShapes)
        {
            output.DecorationsOmitted = true;
            return;
        }
        const auto&  projection = input.Camera.Projection;
        const double near_z     = camera.ReverseZ ? double(projection(3, 2)) - projection(2, 2) : projection(2, 2);
        const double near_w     = camera.ReverseZ ? double(projection(3, 3)) - projection(2, 3) : projection(2, 3);
        const auto&  origin     = input.Light.Origin;
        const double depth      = (double(origin.x) - camera.Position.x) * camera.Forward.x + (double(origin.y) - camera.Position.y) * camera.Forward.y + (double(origin.z) - camera.Position.z) * camera.Forward.z;
        if (near_z != 0.0 && std::isfinite(depth) && depth < near_w / near_z * C::NearDistanceMultiplier)
        {
            return;
        }
        Sink       sink(input, camera, output, hits);
        const bool built = sink.Builder.Valid() && (input.Light.Light.Type == Type::Point ? sink.PointLight() : input.Light.Light.Type == Type::Spot ? sink.SpotLight() : sink.DirectionalLight());
        if (!built)
        {
            output.LineCount          = lines;
            output.TriangleCount      = triangles;
            output.SpriteCount        = sprites;
            hits.Count                = shapes;
            output.DecorationsOmitted = true;
        }
    }
} // namespace Tetragrama::Gizmo
