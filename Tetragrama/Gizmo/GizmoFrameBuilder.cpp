#include <Tetragrama/Gizmo/GizmoAppearance.h>
#include <Tetragrama/Gizmo/GizmoCameraBuilder.h>
#include <Tetragrama/Gizmo/GizmoConstants.h>
#include <Tetragrama/Gizmo/GizmoFrameBuilder.h>
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

namespace Tetragrama::Gizmo
{
    namespace
    {
        bool Unit(const Vec3f& value, Vec3f* result)
        {
            const double length = std::hypot(double(value.x), double(value.y), double(value.z));
            if (!std::isfinite(length) || length <= 0.0)
            {
                return false;
            }
            *result = {float(value.x / length), float(value.y / length), float(value.z / length)};
            return true;
        }

        double Dot(const Vec3f& a, const Vec3f& b)
        {
            return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
        }

        double Depth(const Vec3f& point, const G::ProjectionFrame& camera)
        {
            // Subtract in double so large finite positions do not overflow first.
            return (double(point.x) - camera.Position.x) * camera.Forward.x + (double(point.y) - camera.Position.y) * camera.Forward.y + (double(point.z) - camera.Position.z) * camera.Forward.z;
        }

        bool TranslateHandle(Handle handle)
        {
            return handle >= Handle::TranslateX && handle <= Handle::TranslateView;
        }

        bool RotateHandle(Handle handle)
        {
            return handle >= Handle::RotateX && handle <= Handle::RotateView;
        }

        bool ScaleHandle(Handle handle)
        {
            return handle >= Handle::ScaleX && handle <= Handle::ScaleUniform;
        }

        bool Emphasized(const FrameInput& input, Handle handle)
        {
            return handle == (input.Active != Handle::None ? input.Active : input.Hovered);
        }

        float StateAlpha(const FrameInput& input, Handle handle)
        {
            if (input.Active != Handle::None)
            {
                return input.Active == handle ? 1.f : C::OtherActiveAlpha;
            }
            if (input.Hovered != Handle::None)
            {
                return input.Hovered == handle ? 1.f : C::OtherHoveredAlpha;
            }
            return 1.f;
        }

        uint32_t Color(const UI::Tokens::Color& token, float alpha = 1.f)
        {
            return UI::ZUIPackColor(token.R, token.G, token.B, token.A * alpha);
        }

        // Wrap the existing checked clipping sink. Picking reads the actual
        // emitted prefixes, not the unclipped input geometry. Decorations have
        // no hit handle and never increase the picking footprint.
        struct Sink
        {
            GizmoFrameData&           output;
            HitFrame&                 hits;
            const G::ProjectionFrame& camera;
            const G::Viewport&        viewport;
            GizmoPrimitiveBuilder     builder;
            uint32_t                  DecorationReserve = 0;

            Sink(GizmoFrameData& frame, HitFrame& hit_frame, const G::ProjectionFrame& projection, const FrameInput& input) : output(frame), hits(hit_frame), camera(projection), viewport(input.Viewport), builder(frame, projection, input.Viewport.Size.y, input.TargetWidth, input.TargetHeight) {}

            bool Add(HitShape shape, double depth)
            {
                if (hits.Count == HitFrame::MaxShapes || !std::isfinite(depth) || depth <= 0.0 || depth > std::numeric_limits<float>::max())
                {
                    return false;
                }
                shape.Depth               = float(depth);
                hits.Shapes[hits.Count++] = shape;
                return true;
            }

            bool Line(const GizmoLine& line, Handle handle = Handle::None)
            {
                if (!(line.Color >> 24))
                {
                    return true;
                }
                const uint32_t before = output.LineCount;
                const auto     result = builder.Line(line);
                if (result == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (result != GizmoEmitResult::Emitted)
                {
                    return false;
                }
                if (handle == Handle::None)
                {
                    return true;
                }
                const auto&       clipped = output.Lines[before];
                G::ProjectedPoint a = {}, b = {};
                if (!G::Project(clipped.A, camera, viewport, &a) || !G::Project(clipped.B, camera, viewport, &b))
                {
                    return false;
                }
                HitShape shape  = {.Handle = uint32_t(handle), .Priority = HitPriority::AxisLine, .Padding = C::LineHitTolerancePx};
                shape.Points[0] = a.Screen;
                shape.Points[1] = b.Screen;
                return Add(shape, (Depth(clipped.A, camera) + Depth(clipped.B, camera)) * 0.5);
            }

            bool Triangle(const GizmoTriangle& triangle, Handle handle, HitPriority priority)
            {
                if (!(triangle.Color >> 24))
                {
                    return true;
                }
                const uint32_t before = output.TriangleCount;
                const auto     result = builder.Triangle(triangle);
                if (result == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (result != GizmoEmitResult::Emitted)
                {
                    return false;
                }
                for (uint32_t i = before; i < output.TriangleCount; ++i)
                {
                    const auto&       clipped = output.Triangles[i];
                    G::ProjectedPoint a = {}, b = {}, c = {};
                    if (!G::Project(clipped.A, camera, viewport, &a) || !G::Project(clipped.B, camera, viewport, &b) || !G::Project(clipped.C, camera, viewport, &c))
                    {
                        return false;
                    }
                    HitShape shape  = {.Handle = uint32_t(handle), .Priority = priority, .Geometry = HitGeometry::Triangle, .Padding = C::PlaneHitPaddingPx};
                    shape.Points[0] = a.Screen;
                    shape.Points[1] = b.Screen;
                    shape.Points[2] = c.Screen;
                    if (priority == HitPriority::Arrowhead)
                    {
                        // GZ4: eight px from the arrow centreline, not eight px
                        // beyond the already 11-px-wide triangle's perimeter.
                        shape.Geometry  = HitGeometry::Segment;
                        shape.Padding   = C::LineHitTolerancePx;
                        shape.Points[1] = (b.Screen + c.Screen) * 0.5f;
                    }
                    if (!Add(shape, (Depth(clipped.A, camera) + Depth(clipped.B, camera) + Depth(clipped.C, camera)) / 3.0))
                    {
                        return false;
                    }
                }
                return true;
            }

            bool RingLine(const GizmoLine& line, Handle handle, const Vec3f& pivot, const Vec3<double>& toward_eye, bool view_ring)
            {
                if (!(line.Color >> 24))
                {
                    return true;
                }
                const uint32_t first  = output.LineCount;
                const auto     result = builder.Line(line);
                if (result == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (result != GizmoEmitResult::Emitted)
                {
                    return false;
                }
                auto a = output.Lines[first].A, b = output.Lines[first].B;
                if (!view_ring)
                {
                    const auto facing = [&](const Vec3f& point) {
                        double value = 0.0;
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            value += (double(point[axis]) - pivot[axis]) * toward_eye[axis];
                        }
                        return value;
                    };
                    const double da = facing(a), db = facing(b);
                    if (da <= 0.0 && db <= 0.0)
                    {
                        return true;
                    }
                    if ((da <= 0.0) != (db <= 0.0))
                    {
                        const double fraction = std::clamp(da / (da - db), 0.0, 1.0);
                        Vec3f        boundary = {};
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            boundary[axis] = float(double(a[axis]) * (1.0 - fraction) + double(b[axis]) * fraction);
                        }
                        (da <= 0.0 ? a : b) = boundary;
                    }
                }
                G::ProjectedPoint start = {}, end = {};
                if (!G::Project(a, camera, viewport, &start) || !G::Project(b, camera, viewport, &end))
                {
                    return false;
                }
                HitShape shape       = {.Handle = uint32_t(handle), .Priority = view_ring ? HitPriority::ViewRing : HitPriority::AxisRing, .Padding = C::LineHitTolerancePx};
                shape.Points[0]      = start.Screen;
                shape.Points[1]      = end.Screen;
                const uint32_t index = hits.Count;
                if (!Add(shape, (Depth(a, camera) + Depth(b, camera)) * .5))
                {
                    return false;
                }
                hits.RingSpans[index] = {.A = a, .B = b};
                return true;
            }

            bool Sprite(const GizmoSprite& sprite, Handle handle, HitPriority priority)
            {
                if (!(sprite.Fill >> 24) && !(sprite.Outline >> 24))
                {
                    return true;
                }
                const auto result = builder.Sprite(sprite);
                if (result == GizmoEmitResult::Culled)
                {
                    return true;
                }
                if (result != GizmoEmitResult::Emitted)
                {
                    return false;
                }
                if (handle == Handle::None)
                {
                    return true;
                }
                G::ProjectedPoint point = {};
                if (!G::Project(sprite.Position, camera, viewport, &point))
                {
                    return false;
                }
                HitShape    shape = {.Handle = uint32_t(handle), .Priority = priority, .Geometry = HitGeometry::Rectangle, .Padding = C::BoxHitPaddingPx};
                const Vec2f half(sprite.Size * 0.5f, sprite.Size * 0.5f);
                shape.Points[0] = point.Screen - half;
                shape.Points[1] = point.Screen + half;
                return Add(shape, Depth(sprite.Position, camera));
            }

            bool Center(const Vec3f& pivot, bool pick = true)
            {
                return Sprite({.Position = pivot, .Size = C::CenterSizePx, .Fill = Color(UI::Tokens::AccentOrange)}, pick ? Handle::TranslateView : Handle::None, HitPriority::Center);
            }
        };

        // Four rotation paths, one emphasized glow, two wedge edges and seven
        // pivot lines. Reserve shrinks only as those later phases are consumed.
        constexpr uint32_t RotationLineReserve = 6 * C::RingSegments + 2 + 7;
        constexpr uint32_t LinearLineReserve   = 6 + 12 + 7; // shafts/glow, planes or scale ghosts, pivot
        static_assert(GizmoFrameData::MaxLines >= RotationLineReserve + 25);

        void DecorationLines(Sink& sink, Core::Containers::ArrayView<const GizmoLine> lines, const uuids::uuid& mesh = {})
        {
            auto& output = sink.output;
            if (output.LineCount > GizmoFrameData::MaxLines - sink.DecorationReserve || lines.size() > GizmoFrameData::MaxLines - sink.DecorationReserve - output.LineCount || (!mesh.is_nil() && output.MeshLineGroupCount == GizmoFrameData::MaxMeshLineGroups))
            {
                output.DecorationsOmitted = true;
                return;
            }
            const uint32_t first = output.LineCount;
            for (const auto& line : lines)
            {
                const auto result = sink.builder.Line(line);
                if (result != GizmoEmitResult::Emitted && result != GizmoEmitResult::Culled)
                {
                    output.LineCount          = first;
                    output.DecorationsOmitted = true;
                    return;
                }
            }
            if (!mesh.is_nil() && output.LineCount != first)
            {
                output.MeshLineGroups[output.MeshLineGroupCount++] = {.MeshUUID = mesh, .First = first, .Count = output.LineCount - first};
            }
        }

        void BuildTargetDecorations(const FrameInput& input, const TransformDecorations& drag, const Vec3f& current_pivot, Sink& sink)
        {
            if (!drag.Visible)
            {
                return;
            }
            if (input.Mode == Operation::Translate && input.Active != Handle::PivotEdit)
            {
                const GizmoLine guide = {.A = drag.StartPivot, .B = current_pivot, .Width = C::GuideWidthPx, .DashOn = C::GuideDashOnPx, .DashOff = C::GuideDashOffPx, .Color = Color(ConstraintColor(input.Active), C::GuideAlpha)};
                DecorationLines(sink, {&guide, 1});
            }
            if (drag.HasBox)
            {
                if (drag.MeshUUID.is_nil())
                {
                    sink.output.DecorationsOmitted = true;
                    return;
                }
                GizmoLine lines[24] = {};
                uint32_t  count     = 0;
                // Bit-indexed corners produce each of the twelve edges once.
                // Live first, ghost next; both stay behind the active axes.
                for (bool ghost : {false, true})
                {
                    if (ghost && !drag.GhostVisible)
                    {
                        continue;
                    }
                    for (uint32_t corner = 0; corner < 8; ++corner)
                    {
                        for (uint32_t axis = 0; axis < 3; ++axis)
                        {
                            if (!(corner & (1u << axis)))
                            {
                                const auto& points = ghost ? drag.Ghost : drag.Current;
                                lines[count++]     = {.A = points[corner], .B = points[corner | (1u << axis)], .Width = C::SelectionBoxWidthPx, .DashOn = ghost ? C::GhostDashOnPx : 0.f, .DashOff = ghost ? C::GhostDashOffPx : 0.f, .Color = Color(UI::Tokens::AccentOrange, ghost ? C::GhostAlpha : 1.f)};
                            }
                        }
                    }
                }
                DecorationLines(sink, {lines, count}, drag.MeshUUID);
                return;
            }
            if (input.Active == Handle::PivotEdit)
            {
                return; // Pivot-only edits have no fabricated box/start-pose ghost.
            }
            // A non-mesh/unavailable mesh has a dashed start-centre marker, not
            // a fabricated unit box. It is screen-facing like the live handle.
            G::ProjectedPoint pivot = {};
            if (!G::Project(drag.StartPivot, sink.camera, input.Viewport, &pivot))
            {
                sink.output.DecorationsOmitted = true;
                return;
            }
            Core::Containers::FixedArray<Vec3f, 4> points = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                const Vec2f offset((i == 1 || i == 2 ? 1.f : -1.f) * C::CenterSizePx * 0.5f, (i >= 2 ? 1.f : -1.f) * C::CenterSizePx * 0.5f);
                if (!G::UnprojectAtDepth(pivot.Screen + offset, pivot.Depth, sink.camera, input.Viewport, &points[i]))
                {
                    sink.output.DecorationsOmitted = true;
                    return;
                }
            }
            GizmoLine lines[4] = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                lines[i] = {.A = points[i], .B = points[(i + 1) % 4], .Width = C::SelectionBoxWidthPx, .DashOn = C::GhostDashOnPx, .DashOff = C::GhostDashOffPx, .DashPhase = C::CenterSizePx * i, .Color = Color(UI::Tokens::AccentOrange, C::GhostAlpha)};
            }
            DecorationLines(sink, {lines, 4});
        }

        void BuildDecorations(const FrameInput& input, Sink& sink)
        {
            sink.output.DecorationsOmitted |= input.RootDecorationsOmitted;
            if (!input.Drag.Visible || input.Active == Handle::None)
            {
                return;
            }
            if (input.Roots.empty())
            {
                BuildTargetDecorations(input, input.Drag, input.Pivot, sink);
                return;
            }
            if (!input.Roots.data() || input.Roots.size() > GizmoFrameData::MaxMeshLineGroups)
            {
                sink.output.DecorationsOmitted = true;
                return;
            }
            for (const auto& root : input.Roots)
            {
                BuildTargetDecorations(input, root, root.CurrentPivot, sink);
            }
        }

        const Core::Containers::FixedArray<Vec2f, C::RingSegments + 1>& Circle()
        {
            // Shared immutable samples, computed once; not four sets of trig
            // evaluations per UI/render frame. Close with the identical first point.
            static const auto points = [] {
                Core::Containers::FixedArray<Vec2f, C::RingSegments + 1> result = {};
                for (uint32_t i = 0; i < C::RingSegments; ++i)
                {
                    const double angle = i * (2.0 * std::numbers::pi / C::RingSegments);
                    result[i]          = {float(std::cos(angle)), float(std::sin(angle))};
                }
                result[C::RingSegments] = result[0];
                return result;
            }();
            return points;
        }

        bool RingPoint(const Vec3f& pivot, const Vec3f& u, const Vec3f& v, float radius, double cosine, double sine, Vec3f& output)
        {
            Vec3f point = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const double value = double(pivot[axis]) + double(radius) * (cosine * u[axis] + sine * v[axis]);
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                point[axis] = float(value);
            }
            output = point;
            return true;
        }

        bool RingBasis(const Vec3f& normal, Vec3f& u, Vec3f& v)
        {
            uint32_t least = 0;
            for (uint32_t axis = 1; axis < 3; ++axis)
            {
                if (std::abs(normal[axis]) < std::abs(normal[least]))
                {
                    least = axis;
                }
            }
            Vec3f helper  = {};
            helper[least] = 1.f;
            return Unit(cross3d(normal, helper), &u) && Unit(cross3d(normal, u), &v);
        }

        bool Wedge(const FrameInput& input, Sink& sink, float world_per_pixel)
        {
            Vec3f normal = {}, start = {}, tangent = {};
            if (!std::isfinite(input.Rotation.Angle) || !Unit(input.Rotation.Axis, &normal))
            {
                return false;
            }
            const double offset = Dot(input.Rotation.StartDirection, normal);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                start[axis] = float(double(input.Rotation.StartDirection[axis]) - offset * normal[axis]);
            }
            if (!Unit(start, &start) || !Unit(cross3d(normal, start), &tangent))
            {
                return false;
            }
            const double angle  = input.Rotation.Angle;
            const double sweep  = std::clamp(angle, -2.0 * std::numbers::pi, 2.0 * std::numbers::pi);
            const float  radius = (input.Active == Handle::RotateView ? C::ViewRingRadiusPx : C::RingRadiusPx) * world_per_pixel;
            Vec3f        first = {}, current = {};
            if (!RingPoint(input.Pivot, start, tangent, radius, 1.0, 0.0, first) || !RingPoint(input.Pivot, start, tangent, radius, std::cos(std::remainder(angle, 2.0 * std::numbers::pi)), std::sin(std::remainder(angle, 2.0 * std::numbers::pi)), current))
            {
                return false;
            }
            const auto color = ConstraintColor(input.Active);
            if (sweep != 0.0)
            {
                const uint32_t segments = std::max(1u, uint32_t(std::ceil(std::abs(sweep) * (C::RingSegments / (2.0 * std::numbers::pi)))));
                Vec3f          previous = first;
                for (uint32_t i = 1; i <= segments; ++i)
                {
                    const double phase = sweep * (double(i) / segments);
                    Vec3f        next  = {};
                    if (!RingPoint(input.Pivot, start, tangent, radius, std::cos(phase), std::sin(phase), next))
                    {
                        return false;
                    }
                    const auto emitted = sink.builder.Triangle({.A = input.Pivot, .B = previous, .C = next, .Color = Color(color, C::RotationWedgeAlpha), .AntiAliasEdges = 1});
                    if (emitted != GizmoEmitResult::Emitted && emitted != GizmoEmitResult::Culled)
                    {
                        return false;
                    }
                    previous = next;
                }
                if (!sink.Line({.A = input.Pivot, .B = first, .Width = C::RotationWedgeWidthPx, .DashOn = C::GhostDashOnPx, .DashOff = C::GhostDashOffPx, .Color = Color(color)}))
                {
                    return false;
                }
            }
            return sink.Line({.A = input.Pivot, .B = current, .Width = C::RotationWedgeWidthPx, .Color = Color(color)});
        }

        bool BuildRotation(const FrameInput& input, Sink& sink, const Core::Containers::FixedArray<Vec3f, 3>& axes, float world_per_pixel)
        {
            // Orthographic rays are parallel: lateral camera/target motion must
            // not rotate the pickable half-space. Perspective retains the
            // pivot-to-eye vector, subtracting in double for finite extremes.
            Vec3<double> toward_eye = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                toward_eye[axis] = sink.camera.Kind == G::ProjectionKind::Orthographic ? -double(sink.camera.Forward[axis]) : double(sink.camera.Position[axis]) - input.Pivot[axis];
            }
            const auto& circle = Circle();
            for (bool emphasis_pass : {false, true})
            {
                for (uint32_t index = 0; index < 4; ++index)
                {
                    const auto handle   = Handle(uint32_t(Handle::RotateX) + index);
                    const bool emphasis = Emphasized(input, handle), view = index == 3;
                    if (emphasis != emphasis_pass)
                    {
                        continue;
                    }
                    Vec3f normal = view ? -sink.camera.Forward : axes[index];
                    if (input.Active == handle && input.Rotation.Visible)
                    {
                        Vec3f captured = {};
                        if (Unit(input.Rotation.Axis, &captured))
                        {
                            normal = captured;
                        }
                        else
                        {
                            sink.output.DecorationsOmitted = true;
                        }
                    }
                    Vec3f u = {}, v = {};
                    if (!RingBasis(normal, u, v))
                    {
                        return false;
                    }
                    const float                                                          radius      = (view ? C::ViewRingRadiusPx : C::RingRadiusPx) * world_per_pixel;
                    const float                                                          alpha       = StateAlpha(input, handle) * (view && !emphasis ? C::ViewRingIdleAlpha : 1.f);
                    const auto                                                           color       = ConstraintColor(handle);
                    Core::Containers::FixedArray<Vec3f, C::RingSegments + 1>             points      = {};
                    Core::Containers::FixedArray<G::ProjectedPoint, C::RingSegments + 1> projected   = {};
                    Core::Containers::FixedArray<bool, C::RingSegments + 1>              projectable = {};
                    Core::Containers::FixedArray<GizmoJoinNormal, C::RingSegments>       joins       = {};
                    const G::Viewport                                                    pixels      = {
                        {},
                        {float(input.TargetWidth), float(input.TargetHeight)}
                    };
                    for (uint32_t i = 0; i < C::RingSegments; ++i)
                    {
                        if (!RingPoint(input.Pivot, u, v, radius, circle[i].x, circle[i].y, points[i]))
                        {
                            return false;
                        }
                        projectable[i] = G::Project(points[i], sink.camera, pixels, &projected[i]);
                    }
                    points[C::RingSegments]      = points[0];
                    projected[C::RingSegments]   = projected[0];
                    projectable[C::RingSegments] = projectable[0];
                    for (uint32_t i = 0; i < C::RingSegments; ++i)
                    {
                        const auto previous = (i + C::RingSegments - 1) % C::RingSegments;
                        if (!projectable[previous] || !projectable[i] || !projectable[i + 1])
                        {
                            continue;
                        }
                        const double ax       = double(projected[i].Screen.x) - projected[previous].Screen.x;
                        const double ay       = double(projected[i].Screen.y) - projected[previous].Screen.y;
                        const double bx       = double(projected[i + 1].Screen.x) - projected[i].Screen.x;
                        const double by       = double(projected[i + 1].Screen.y) - projected[i].Screen.y;
                        const double a_length = std::hypot(ax, ay), b_length = std::hypot(bx, by);
                        if (!std::isfinite(a_length) || !std::isfinite(b_length) || a_length <= C::StrokeJoinMinimumLengthPx || b_length <= C::StrokeJoinMinimumLengthPx)
                        {
                            continue;
                        }
                        const double x = ax / a_length + bx / b_length, y = ay / a_length + by / b_length;
                        const double length = std::hypot(x, y);
                        if (std::isfinite(length) && length > C::StrokeJoinMinimumDirection)
                        {
                            joins[i] = GizmoJoinNormal::FromUnit(x / length, y / length);
                        }
                    }
                    // Emit the complete glow first, then the complete path. The
                    // hit records come only from the base, visible ring stroke.
                    // Figma presents the idle View ring over BgViewport. Put a
                    // viewport-coloured underlay behind its unchanged approved
                    // half-alpha foreground stroke so it remains legible when a
                    // scene happens to be much brighter than that reference.
                    if (view && !emphasis)
                    {
                        for (uint32_t segment = 0; segment < C::RingSegments; ++segment)
                        {
                            const GizmoLine outline = {.A = points[segment], .B = points[segment + 1], .Width = C::ViewRingContrastOutlineWidthPx, .Color = Color(UI::Tokens::BgViewport), .RoundCaps = true, .StartJoinNormal = joins[segment], .EndJoinNormal = joins[(segment + 1) % C::RingSegments]};
                            if (!sink.Line(outline))
                            {
                                return false;
                            }
                        }
                    }
                    for (bool glow : {true, false})
                    {
                        if (glow && (!emphasis || view))
                        {
                            continue;
                        }
                        for (uint32_t segment = 0; segment < C::RingSegments; ++segment)
                        {
                            const GizmoLine line = {.A = points[segment], .B = points[segment + 1], .Width = glow ? C::GlowWidthPx : emphasis ? C::RingEmphasizedWidthPx : C::RingIdleWidthPx, .Color = Color(color, alpha * (glow ? C::GlowAlpha : 1.f)), .RoundCaps = true, .StartJoinNormal = joins[segment], .EndJoinNormal = joins[(segment + 1) % C::RingSegments]};
                            if (!(glow ? sink.Line(line) : sink.RingLine(line, handle, input.Pivot, toward_eye, view)))
                            {
                                return false;
                            }
                        }
                    }
                }
            }
            if (input.Rotation.Visible && RotateHandle(input.Active))
            {
                const auto triangles = sink.output.TriangleCount, lines = sink.output.LineCount;
                if (!Wedge(input, sink, world_per_pixel))
                {
                    sink.output.TriangleCount      = triangles;
                    sink.output.LineCount          = lines;
                    sink.output.DecorationsOmitted = true;
                }
            }
            if (input.PivotEditable)
            {
                return sink.Sprite({.Position = input.Pivot, .Size = C::CenterSizePx, .Fill = Color(UI::Tokens::AccentOrange)}, Handle::PivotEdit, HitPriority::Center);
            }
            return sink.Center(input.Pivot, false);
        }

        void BuildPivotMarker(const FrameInput& input, Sink& sink)
        {
            if (!input.PivotMarker.Visible)
            {
                return;
            }
            G::ProjectedPoint pivot = {};
            if (!G::Project(input.Pivot, sink.camera, input.Viewport, &pivot))
            {
                sink.output.DecorationsOmitted = true;
                return;
            }
            const uint32_t first = sink.output.SpriteCount;
            const auto     ring  = sink.builder.Sprite({.Position = input.Pivot, .Size = C::PivotRingSizePx, .OutlineWidth = C::PivotRingWidthPx, .Outline = Color(UI::Tokens::AccentOrange), .Round = true});
            if (ring != GizmoEmitResult::Emitted && ring != GizmoEmitResult::Culled)
            {
                sink.output.DecorationsOmitted = true;
            }
            Core::Containers::FixedArray<Vec3f, 4> ends = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                Vec2f offset  = {};
                offset[i / 2] = (i % 2 ? 1.f : -1.f) * C::PivotCrossSizePx * .5f;
                if (!G::UnprojectAtDepth(pivot.Screen + offset, pivot.Depth, sink.camera, input.Viewport, &ends[i]))
                {
                    sink.output.SpriteCount        = first;
                    sink.output.DecorationsOmitted = true;
                    return;
                }
            }
            const GizmoLine lines[] = {
                {.A = input.PivotMarker.Origin, .B = input.Pivot, .Width = C::PivotLinkWidthPx, .DashOn = C::GhostDashOnPx, .DashOff = C::GhostDashOffPx, .Color = Color(UI::Tokens::TextPrimary, C::PivotLinkAlpha)},
                {.A = ends[0], .B = ends[1], .Width = C::PivotCrossWidthPx, .Color = Color(UI::Tokens::AccentOrange)},
                {.A = ends[2], .B = ends[3], .Width = C::PivotCrossWidthPx, .Color = Color(UI::Tokens::AccentOrange)},
            };
            DecorationLines(sink, {lines, 3});
            // The small dashed origin marker uses the same checked screen-plane
            // reconstruction; no origin sprite can occlude the ring.
            G::ProjectedPoint                      origin  = {};
            Core::Containers::FixedArray<Vec3f, 4> corners = {};
            if (!G::Project(input.PivotMarker.Origin, sink.camera, input.Viewport, &origin))
            {
                sink.output.DecorationsOmitted = true;
                return;
            }
            for (uint32_t i = 0; i < 4; ++i)
            {
                const Vec2f offset((i == 1 || i == 2 ? 1.f : -1.f) * C::PivotOriginSizePx * .5f, (i >= 2 ? 1.f : -1.f) * C::PivotOriginSizePx * .5f);
                if (!G::UnprojectAtDepth(origin.Screen + offset, origin.Depth, sink.camera, input.Viewport, &corners[i]))
                {
                    sink.output.DecorationsOmitted = true;
                    return;
                }
            }
            GizmoLine marker[4] = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                marker[i] = {.A = corners[i], .B = corners[(i + 1) % 4], .Width = C::PivotCrossWidthPx, .DashOn = C::GhostDashOnPx, .DashOff = C::GhostDashOffPx, .DashPhase = C::PivotOriginSizePx * i, .Color = Color(UI::Tokens::AccentOrange)};
            }
            DecorationLines(sink, {marker, 4});
        }

        void ScaleGhost(const FrameInput& input, Sink& sink, const Vec3f& position, Handle handle)
        {
            G::ProjectedPoint                      point   = {};
            Core::Containers::FixedArray<Vec3f, 4> corners = {};
            if (!G::Project(position, sink.camera, input.Viewport, &point))
            {
                sink.output.DecorationsOmitted = true;
                return;
            }
            for (uint32_t i = 0; i < 4; ++i)
            {
                const Vec2f offset((i == 1 || i == 2 ? 1.f : -1.f) * C::ScaleCubeSizePx * .5f, (i >= 2 ? 1.f : -1.f) * C::ScaleCubeSizePx * .5f);
                if (!G::UnprojectAtDepth(point.Screen + offset, point.Depth, sink.camera, input.Viewport, &corners[i]))
                {
                    sink.output.DecorationsOmitted = true;
                    return;
                }
            }
            GizmoLine lines[4] = {};
            for (uint32_t i = 0; i < 4; ++i)
            {
                lines[i] = {.A = corners[i], .B = corners[(i + 1) % 4], .Width = C::SelectionBoxWidthPx, .DashOn = C::ScaleGhostDashOnPx, .DashOff = C::ScaleGhostDashOffPx, .DashPhase = C::ScaleCubeSizePx * i, .Color = Color(ConstraintColor(handle), C::GhostAlpha)};
            }
            DecorationLines(sink, {lines, 4});
        }

        bool BuildScale(const FrameInput& input, Sink& sink, const Core::Containers::FixedArray<Vec3f, 3>& axes, const Vec3f& view, float world_per_pixel)
        {
            const bool                             uniform    = input.Active == Handle::ScaleUniform;
            const float                            length     = input.Scale.Visible ? input.Scale.HandleLength : C::AxisLengthPx * world_per_pixel;
            Core::Containers::FixedArray<float, 3> visibility = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                visibility[axis]  = float(std::clamp((C::AxisHide - std::abs(Dot(axes[axis], view))) / (C::AxisHide - C::AxisFadeStart), 0.0, 1.0));
                const auto handle = Handle(uint32_t(Handle::ScaleX) + axis);
                if (input.Scale.Visible && visibility[axis] > 0.f && (uniform || input.Active == handle))
                {
                    Vec3f ghost = {};
                    if (RingPoint(input.Pivot, axes[axis], {}, length, 1.0, 0.0, ghost))
                    {
                        ScaleGhost(input, sink, ghost, handle);
                    }
                    else
                    {
                        sink.output.DecorationsOmitted = true;
                    }
                }
            }
            // Retain the captured start length/orientation. Previewing a ratio
            // must not feed the live scaled basis back into the handle mapping.
            for (bool emphasis_pass : {false, true})
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const auto handle   = Handle(uint32_t(Handle::ScaleX) + axis);
                    const bool enabled  = std::abs(input.AuthoredScale[axis]) >= C::MinimumEditableScale;
                    const bool emphasis = enabled && Emphasized(input, handle);
                    if (emphasis != emphasis_pass)
                    {
                        continue;
                    }
                    const float alpha = visibility[axis] * (enabled ? StateAlpha(input, handle) : C::ScaleDisabledAlpha);
                    const auto  color = ConstraintColor(handle);
                    if (!(Color(color, alpha) >> 24))
                    {
                        continue;
                    }
                    const float ratio = input.Scale.Visible && (uniform || input.Active == handle) ? input.Scale.Ratio : 1.f;
                    Vec3f       tip = {}, start = {};
                    if (!RingPoint(input.Pivot, axes[axis], {}, length, ratio, 0.0, tip) || !RingPoint(input.Pivot, axes[axis], {}, world_per_pixel, C::AxisStartPx, 0.0, start))
                    {
                        return false;
                    }
                    // At the minimum ratio the cube can lie inside the shaft's
                    // start gap. Do not reverse the shaft into the centre cube.
                    if (double(length) * ratio > double(C::AxisStartPx) * world_per_pixel)
                    {
                        if (emphasis && !sink.Line({.A = start, .B = tip, .Width = C::GlowWidthPx, .Color = Color(color, alpha * C::GlowAlpha), .RoundCaps = true}))
                        {
                            return false;
                        }
                        if (!sink.Line({.A = start, .B = tip, .Width = emphasis ? C::AxisEmphasizedWidthPx : C::AxisIdleWidthPx, .Color = Color(color, alpha), .RoundCaps = true}, enabled ? handle : Handle::None))
                        {
                            return false;
                        }
                    }
                    if (!sink.Sprite({.Position = tip, .Size = emphasis ? C::ScaleEmphasizedCubeSizePx : C::ScaleCubeSizePx, .Fill = Color(color, alpha)}, enabled ? handle : Handle::None, HitPriority::ScaleCube))
                    {
                        return false;
                    }
                }
            }
            const bool emphasis = Emphasized(input, Handle::ScaleUniform);
            return sink.Sprite({.Position = input.Pivot, .Size = emphasis ? C::ScaleUniformCubeSizePx : C::ScaleCubeSizePx, .OutlineWidth = emphasis ? C::ScaleUniformOutlinePx : 0.f, .Fill = Color(UI::Tokens::AccentOrange), .Outline = emphasis ? Color(UI::Tokens::TextPrimary) : 0}, Handle::ScaleUniform, HitPriority::Center);
        }

        bool Build(const FrameInput& input, const G::ProjectionFrame& camera, const Core::Containers::FixedArray<Vec3f, 3>& axes, const Vec3f& view, float world_per_pixel, bool transforms, GizmoFrameData& output, HitFrame& hits)
        {
            Sink sink(output, hits, camera, input);
            if (!sink.builder.Valid())
            {
                return false;
            }
            BuildLightGeometry(input, camera, output, hits);
            BuildCameraGeometry(input, camera, output, hits);
            if (!transforms)
            {
                return true;
            }
            sink.DecorationReserve = input.Mode == Operation::Rotate ? RotationLineReserve : LinearLineReserve;
            BuildDecorations(input, sink);
            if (input.Mode == Operation::Rotate)
            {
                if (!BuildRotation(input, sink, axes, world_per_pixel))
                {
                    return false;
                }
                sink.DecorationReserve = 0;
                BuildPivotMarker(input, sink);
                return true;
            }
            if (input.Mode == Operation::Scale)
            {
                sink.DecorationReserve = 6 + 7; // the twelve ghost edges consume their own earlier reserve
                if (!BuildScale(input, sink, axes, view, world_per_pixel))
                {
                    return false;
                }
                sink.DecorationReserve = 0;
                BuildPivotMarker(input, sink);
                return true;
            }
            const Core::Containers::FixedArray<UI::Tokens::Color, 3> colors = {
                {UI::Tokens::AccentRed, UI::Tokens::AccentGreen, UI::Tokens::AccentBlue}
            };
            // Plane index is its missing/normal axis: YZ red, ZX green, XY blue.
            for (uint32_t normal = 0; normal < 3; ++normal)
            {
                Vec3f plane_normal = {};
                if (!Unit(cross3d(axes[(normal + 1) % 3], axes[(normal + 2) % 3]), &plane_normal))
                {
                    return false;
                }
                const float    visibility = float(std::clamp((std::abs(Dot(plane_normal, view)) - C::PlaneHide) / (C::PlaneFadeEnd - C::PlaneHide), 0.0, 1.0));
                const auto     handle     = Handle(uint32_t(Handle::TranslateYZ) + normal);
                const float    alpha      = visibility * StateAlpha(input, handle);
                const bool     emphasis   = Emphasized(input, handle);
                const uint32_t fill       = Color(colors[normal], alpha * (emphasis ? C::PlaneEmphasizedAlpha : C::PlaneIdleAlpha));
                if (!(fill >> 24))
                {
                    continue;
                }
                const auto&                                  u   = axes[(normal + 1) % 3];
                const auto&                                  v   = axes[(normal + 2) % 3];
                const float                                  low = C::PlaneStartPx * world_per_pixel, high = C::PlaneEndPx * world_per_pixel;
                const Core::Containers::FixedArray<Vec3f, 4> points = {
                    {input.Pivot + u * low + v * low, input.Pivot + u * high + v * low, input.Pivot + u * high + v * high, input.Pivot + u * low + v * high}
                };
                // Shared diagonal has no AA coverage, so the fill has no seam.
                if (!sink.Triangle({.A = points[0], .B = points[1], .C = points[2], .Color = fill, .AntiAliasEdges = 5}, handle, HitPriority::Plane) || !sink.Triangle({.A = points[0], .B = points[2], .C = points[3], .Color = fill, .AntiAliasEdges = 3}, handle, HitPriority::Plane))
                {
                    return false;
                }
                const uint32_t border = Color(colors[normal], alpha * (emphasis ? 1.f : C::PlaneIdleBorderAlpha));
                for (uint32_t edge = 0; edge < 4; ++edge)
                {
                    if (!sink.Line({.A = points[edge], .B = points[(edge + 1) % 4], .Width = emphasis ? C::PlaneEmphasizedBorderWidthPx : C::PlaneIdleBorderWidthPx, .Color = border}))
                    {
                        return false;
                    }
                }
            }

            G::ProjectedPoint pivot = {};
            if (!G::Project(input.Pivot, camera, input.Viewport, &pivot))
            {
                return false;
            }
            // Emit emphasized shafts last; arrays, not hit order, own paint order.
            for (bool emphasized_pass : {false, true})
            {
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    const auto handle   = Handle(uint32_t(Handle::TranslateX) + axis);
                    const bool emphasis = Emphasized(input, handle);
                    if (emphasis != emphasized_pass)
                    {
                        continue;
                    }
                    const float    visibility = float(std::clamp((C::AxisHide - std::abs(Dot(axes[axis], view))) / (C::AxisHide - C::AxisFadeStart), 0.0, 1.0));
                    const float    alpha      = visibility * StateAlpha(input, handle);
                    const uint32_t color      = Color(colors[axis], alpha);
                    if (!(color >> 24))
                    {
                        continue;
                    }
                    const Vec3f       start      = input.Pivot + axes[axis] * (C::AxisStartPx * world_per_pixel);
                    const Vec3f       tip        = input.Pivot + axes[axis] * (C::AxisLengthPx * world_per_pixel);
                    Vec3f             shaft_end  = tip;
                    G::ProjectedPoint tip_screen = {}, start_screen = {};
                    if (G::Project(tip, camera, input.Viewport, &tip_screen) && G::Project(start, camera, input.Viewport, &start_screen))
                    {
                        const Vec2f  delta  = tip_screen.Screen - pivot.Screen;
                        const double length = std::hypot(double(delta.x), double(delta.y));
                        if (length <= C::ArrowLengthPx)
                        {
                            continue;
                        }
                        const Vec2f direction(float(delta.x / length), float(delta.y / length));
                        const Vec2f across(-direction.y, direction.x);
                        const Vec2f base = tip_screen.Screen - direction * C::ArrowLengthPx;
                        if ((double(base.x) - start_screen.Screen.x) * direction.x + (double(base.y) - start_screen.Screen.y) * direction.y <= 0.0)
                        {
                            continue;
                        }
                        const float fraction   = float(1.0 - C::ArrowLengthPx / length);
                        const float base_depth = pivot.Depth * (1.f - fraction) + tip_screen.Depth * fraction;
                        Vec3f       a = {}, b = {};
                        if (!G::UnprojectAtDepth(base, base_depth, camera, input.Viewport, &shaft_end) || !G::UnprojectAtDepth(base + across * (C::ArrowWidthPx * 0.5f), tip_screen.Depth, camera, input.Viewport, &a) || !G::UnprojectAtDepth(base - across * (C::ArrowWidthPx * 0.5f), tip_screen.Depth, camera, input.Viewport, &b) || !sink.Triangle({.A = tip, .B = a, .C = b, .Color = color}, handle, HitPriority::Arrowhead))
                        {
                            return false;
                        }
                    }
                    if (emphasis && !sink.Line({.A = start, .B = tip, .Width = C::GlowWidthPx, .Color = Color(colors[axis], alpha * C::GlowAlpha), .RoundCaps = true}))
                    {
                        return false;
                    }
                    if (!sink.Line({.A = start, .B = shaft_end, .Width = emphasis ? C::AxisEmphasizedWidthPx : C::AxisIdleWidthPx, .Color = color, .RoundCaps = true}, handle))
                    {
                        return false;
                    }
                }
            }
            if (!sink.Center(input.Pivot))
            {
                return false;
            }
            sink.DecorationReserve = 0;
            BuildPivotMarker(input, sink);
            return true;
        }
    } // namespace

    HitResult HitFrame::Pick(const Vec2f& point, Handle active) const
    {
        if (!std::isfinite(Viewport.Origin.x) || !std::isfinite(Viewport.Origin.y) || !std::isfinite(Viewport.Size.x) || !std::isfinite(Viewport.Size.y))
        {
            return {};
        }
        if (!(Viewport.Size.x > 0.f && Viewport.Size.y > 0.f && point.x >= Viewport.Origin.x && point.y >= Viewport.Origin.y && double(point.x) < double(Viewport.Origin.x) + Viewport.Size.x && double(point.y) < double(Viewport.Origin.y) + Viewport.Size.y))
        {
            return {};
        }
        return HitTest(View(), point, uint32_t(active));
    }

    bool HitFrame::PickRingPoint(Handle handle, const Vec2f& point, const G::ProjectionFrame& camera, Vec3f& output) const
    {
        if (!RotateHandle(handle) || !std::isfinite(point.x) || !std::isfinite(point.y) || Count > MaxShapes)
        {
            return false;
        }
        double closest = std::numeric_limits<double>::infinity();
        Vec3f  result  = {};
        bool   found   = false;
        for (uint32_t i = 0; i < Count; ++i)
        {
            const auto& shape = Shapes[i];
            if (shape.Handle != uint32_t(handle) || shape.Priority != (handle == Handle::RotateView ? HitPriority::ViewRing : HitPriority::AxisRing) || shape.Geometry != HitGeometry::Segment || !std::isfinite(shape.Depth) || shape.Depth <= 0.f || !std::isfinite(shape.Padding) || shape.Padding < 0.f)
            {
                continue;
            }
            const auto& a = shape.Points[0];
            const auto& b = shape.Points[1];
            if (!std::isfinite(a.x) || !std::isfinite(a.y) || !std::isfinite(b.x) || !std::isfinite(b.y))
            {
                continue;
            }
            const double dx = double(b.x) - a.x, dy = double(b.y) - a.y;
            const double length = dx * dx + dy * dy;
            const double t      = length > 0.0 ? std::clamp(((double(point.x) - a.x) * dx + (double(point.y) - a.y) * dy) / length, 0.0, 1.0) : 0.0;
            const double ex = double(point.x) - a.x - t * dx, ey = double(point.y) - a.y - t * dy;
            const double distance = ex * ex + ey * ey;
            if (!std::isfinite(distance) || distance >= closest || distance > double(shape.Padding) * shape.Padding)
            {
                continue;
            }
            G::ProjectedPoint start = {}, end = {};
            const auto&       span = RingSpans[i];
            if (!G::Project(span.A, camera, Viewport, &start) || !G::Project(span.B, camera, Viewport, &end))
            {
                continue;
            }
            const double denominator = (1.0 - t) * end.ClipW + t * start.ClipW;
            if (denominator <= 0.0)
            {
                continue;
            }
            const double fraction  = t * start.ClipW / denominator;
            Vec3f        candidate = {};
            bool         finite    = true;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                const double value  = (1.0 - fraction) * span.A[axis] + fraction * span.B[axis];
                finite             &= std::isfinite(value) && std::abs(value) <= std::numeric_limits<float>::max();
                if (finite)
                {
                    candidate[axis] = float(value);
                }
            }
            if (!finite)
            {
                continue;
            }
            closest = distance;
            result  = candidate;
            found   = true;
        }
        if (found)
        {
            output = result;
        }
        return found;
    }

    FrameBuildResult BuildFrame(const FrameInput& input, GizmoFrameData& output, HitFrame& hits)
    {
        output.Reset();
        hits.Reset();
        bool transforms = input.TransformsVisible && input.Mode != Operation::Select;
        if (!input.Editable || (input.Mode != Operation::Select && input.Mode != Operation::Translate && input.Mode != Operation::Rotate && input.Mode != Operation::Scale) || (!transforms && !input.Light.Valid && !input.Frustum.Valid))
        {
            return FrameBuildResult::Hidden;
        }
        const auto valid_handle = input.Mode == Operation::Rotate ? RotateHandle : input.Mode == Operation::Scale ? ScaleHandle : TranslateHandle;
        const auto valid        = [&](Handle handle) {
            LightHandle  light  = {};
            CameraHandle camera = {};
            return handle == Handle::None || (transforms && (handle == Handle::PivotEdit || valid_handle(handle))) || ResolveLightHandle(input.Light, handle, light) || ResolveCameraHandle(input.Frustum, handle, camera);
        };
        if (!valid(input.Hovered) || !valid(input.Active) || !std::isfinite(input.Viewport.Size.x) || !std::isfinite(input.Viewport.Size.y) || !std::isfinite(input.Viewport.Origin.x) || !std::isfinite(input.Viewport.Origin.y) || input.Viewport.Size.x <= 0.f || input.Viewport.Size.y <= 0.f || !input.TargetWidth || !input.TargetHeight)
        {
            return FrameBuildResult::Invalid;
        }
        if (transforms && input.Mode == Operation::Scale)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (!std::isfinite(input.AuthoredScale[axis]))
                {
                    return FrameBuildResult::Invalid;
                }
            }
            if (input.Scale.Visible && (!ScaleHandle(input.Active) || !std::isfinite(input.Scale.Ratio) || input.Scale.Ratio < C::MinimumScaleRatio || !std::isfinite(input.Scale.HandleLength) || input.Scale.HandleLength <= 0.f))
            {
                return FrameBuildResult::Invalid;
            }
        }
        const auto type = input.Camera.ProjectionType;
        if (type != Rendering::Cameras::CameraType::PERSPECTIVE && type != Rendering::Cameras::CameraType::ORTHOGRAPHIC)
        {
            return FrameBuildResult::Invalid;
        }
        G::ProjectionFrame camera = {};
        const auto         kind   = type == Rendering::Cameras::CameraType::PERSPECTIVE ? G::ProjectionKind::Perspective : G::ProjectionKind::Orthographic;
        if (!G::MakeProjectionFrame(input.Camera.View, input.Camera.Projection, input.Camera.Position, kind, input.Camera.UsesReverseZ, &camera))
        {
            return FrameBuildResult::Invalid;
        }
        Core::Containers::FixedArray<Vec3f, 3> axes = {};
        for (uint32_t i = 0; transforms && i < 3; ++i)
        {
            if (!Unit(input.Axes[i], &axes[i]))
            {
                return FrameBuildResult::Invalid;
            }
        }
        if (transforms && std::abs(Dot(axes[0], cross3d(axes[1], axes[2]))) < C::FrameAxesMinimumVolume)
        {
            return FrameBuildResult::Invalid;
        }
        // At view-space (0, 0, -distance, 1), the actual near plane is z=0
        // (standard) or w-z=0 (reverse). Use the projection directly: subtracting
        // two world points would lose the near distance at large camera positions.
        const auto&  projection = input.Camera.Projection;
        const double near_z     = camera.ReverseZ ? double(projection(3, 2)) - projection(2, 2) : projection(2, 2);
        const double near_w     = camera.ReverseZ ? double(projection(3, 3)) - projection(2, 3) : projection(2, 3);
        if (near_z == 0.0)
        {
            return FrameBuildResult::Invalid;
        }
        const double near_distance = near_w / near_z, pivot_distance = Depth(input.Pivot, camera);
        if (!std::isfinite(near_distance) || near_distance <= 0.0 || (transforms && !std::isfinite(pivot_distance)))
        {
            return FrameBuildResult::Invalid;
        }
        if (transforms && pivot_distance < near_distance * C::NearDistanceMultiplier)
        {
            transforms = false;
            if (!input.Light.Valid && !input.Frustum.Valid)
            {
                return FrameBuildResult::Hidden;
            }
        }
        Vec3f view = camera.Forward;
        if (transforms && camera.Kind == G::ProjectionKind::Perspective && !Unit(input.Pivot - camera.Position, &view))
        {
            return FrameBuildResult::Invalid;
        }
        float world_per_pixel = 0.f;
        if ((transforms && !G::WorldPerPixel(camera, input.Pivot, input.Viewport.Size.y, &world_per_pixel)) || !Build(input, camera, axes, view, world_per_pixel, transforms, output, hits))
        {
            output.Reset();
            hits.Reset();
            return FrameBuildResult::Invalid;
        }
        hits.Viewport = input.Viewport;
        return output.Empty() ? FrameBuildResult::Hidden : FrameBuildResult::Built;
    }
} // namespace Tetragrama::Gizmo
