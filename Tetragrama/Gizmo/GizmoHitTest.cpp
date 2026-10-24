#include <Tetragrama/Gizmo/GizmoHitTest.h>
#include <ZEngine/Core/Maths/Geometry.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace Tetragrama::Gizmo
{
    namespace
    {
        bool Finite(const Vec2f& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y);
        }

        double Cross(const Vec2f& a, const Vec2f& b, const Vec2f& c)
        {
            return (double(b.x) - a.x) * (double(c.y) - a.y) - (double(b.y) - a.y) * (double(c.x) - a.x);
        }

        bool NearSegment(const Vec2f& point, const Vec2f& a, const Vec2f& b, float padding)
        {
            float distance_squared = 0.f;
            return Geometry::DistanceToSegmentSquared(point, a, b, &distance_squared) && double(distance_squared) <= double(padding) * padding;
        }

        bool Contains(const HitShape& shape, const Vec2f& point)
        {
            const auto& p = shape.Points;
            if (!Finite(p[0]) || !Finite(p[1]))
            {
                return false;
            }
            switch (shape.Geometry)
            {
                case HitGeometry::Segment:
                    return NearSegment(point, p[0], p[1], shape.Padding);
                case HitGeometry::Rectangle:
                    return p[1].x > p[0].x && p[1].y > p[0].y && double(point.x) >= double(p[0].x) - shape.Padding && double(point.x) <= double(p[1].x) + shape.Padding && double(point.y) >= double(p[0].y) - shape.Padding && double(point.y) <= double(p[1].y) + shape.Padding;
                case HitGeometry::ConvexQuad:
                case HitGeometry::Triangle:
                {
                    const uint32_t count = shape.Geometry == HitGeometry::Triangle ? 3u : 4u;
                    if (!Finite(p[2]) || (count == 4 && !Finite(p[3])))
                    {
                        return false;
                    }
                    const double winding = Cross(p[0], p[1], p[2]);
                    if (winding == 0.0)
                    {
                        return false;
                    }
                    bool inside = true;
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        const auto&  a    = p[i];
                        const auto&  b    = p[(i + 1) % count];
                        const auto&  c    = p[(i + 2) % count];
                        const double turn = Cross(a, b, c);
                        if (turn == 0.0 || (turn > 0.0) != (winding > 0.0))
                        {
                            return false; // degenerate, concave or crossed projection
                        }
                        const double side  = Cross(a, b, point);
                        inside            &= side == 0.0 || (side > 0.0) == (winding > 0.0);
                    }
                    if (inside)
                    {
                        return true;
                    }
                    for (uint32_t i = 0; i < count; ++i)
                    {
                        if (NearSegment(point, p[i], p[(i + 1) % count], shape.Padding))
                        {
                            return true;
                        }
                    }
                    return false;
                }
            }
            return false;
        }

        bool Preferred(const HitShape& shape, const HitResult& best, uint32_t active_handle)
        {
            if (!best.Handle)
            {
                return true;
            }
            const bool active      = shape.Handle == active_handle;
            const bool best_active = best.Handle == active_handle;
            if (active != best_active)
            {
                return active;
            }
            if (shape.Priority != best.Priority)
            {
                return shape.Priority < best.Priority;
            }
            if (shape.Depth != best.Depth)
            {
                return shape.Depth < best.Depth;
            }
            return shape.Handle < best.Handle;
        }
    } // namespace

    HitResult HitTest(ZEngine::Core::Containers::ArrayView<const HitShape> shapes, const Vec2f& point, uint32_t active_handle)
    {
        HitResult best = {};
        if (!Finite(point) || !shapes.data())
        {
            return best;
        }
        for (const auto& shape : shapes)
        {
            if (!shape.Handle || shape.Priority > HitPriority::Component || !std::isfinite(shape.Depth) || shape.Depth <= 0.f || !std::isfinite(shape.Padding) || shape.Padding < 0.f || !Preferred(shape, best, active_handle))
            {
                continue;
            }
            if (Contains(shape, point))
            {
                best = {.Handle = shape.Handle, .Priority = shape.Priority, .Depth = shape.Depth};
            }
        }
        return best;
    }
} // namespace Tetragrama::Gizmo
