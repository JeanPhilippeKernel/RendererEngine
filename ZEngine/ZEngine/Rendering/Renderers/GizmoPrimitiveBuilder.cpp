#include <ZEngine/Rendering/Renderers/GizmoPrimitiveBuilder.h>
#include <algorithm>
#include <cmath>
#include <limits>

using namespace ZEngine::Core::Maths;

namespace ZEngine::Rendering::Renderers
{
    namespace
    {
        bool Finite(const Vec3f& point)
        {
            return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
        }
        bool Positive(float value)
        {
            return std::isfinite(value) && value > 0.f;
        }
        bool Nonnegative(float value)
        {
            return std::isfinite(value) && value >= 0.f;
        }
    } // namespace

    GizmoPrimitiveBuilder::GizmoPrimitiveBuilder(GizmoFrameData& output, const Geometry::ProjectionFrame& camera, float logical_height, uint32_t target_width, uint32_t target_height) : m_output(output), m_camera(camera)
    {
        if (!camera.Valid || !output.ValidCounts() || !Positive(logical_height) || !target_width || !target_height)
            return;
        m_pixel_scale          = float(target_height) / logical_height;
        m_target_viewport.Size = {float(target_width), float(target_height)};
        m_valid                = Positive(m_pixel_scale);
    }

    bool GizmoPrimitiveBuilder::Distance(const Vec3f& point, bool near_plane, double* distance) const
    {
        if (!Finite(point))
            return false;
        const auto&  vp = m_camera.ViewProjection;
        const double z  = double(vp(2, 0)) * point.x + double(vp(2, 1)) * point.y + double(vp(2, 2)) * point.z + vp(2, 3);
        const double w  = double(vp(3, 0)) * point.x + double(vp(3, 1)) * point.y + double(vp(3, 2)) * point.z + vp(3, 3);
        *distance       = m_camera.ReverseZ == near_plane ? w - z : z;
        return std::isfinite(*distance);
    }
    bool GizmoPrimitiveBuilder::Visible(const Vec3f& point) const
    {
        const auto&  vp = m_camera.ViewProjection;
        const double w  = double(vp(3, 0)) * point.x + double(vp(3, 1)) * point.y + double(vp(3, 2)) * point.z + vp(3, 3);
        return std::isfinite(w) && w > 0.0;
    }
    bool GizmoPrimitiveBuilder::Intersection(const Vec3f& a, const Vec3f& b, double da, double db, Vec3f* output)
    {
        const double denominator = da - db;
        if (denominator == 0.0)
            return false;
        const double t     = std::clamp(da / denominator, 0.0, 1.0);
        Vec3f        point = {};
        for (uint32_t i = 0; i < 3; ++i)
        {
            const double value = double(a[i]) * (1.0 - t) + double(b[i]) * t;
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                return false;
            point[i] = float(value);
        }
        *output = point;
        return true;
    }

    GizmoEmitResult GizmoPrimitiveBuilder::Line(const GizmoLine& line)
    {
        if (!m_valid || !m_output.ValidCounts() || !Finite(line.A) || !Finite(line.B) || !Positive(line.Width) || !Nonnegative(line.DashOn) || !Nonnegative(line.DashOff) || !std::isfinite(line.DashPhase) || !line.ValidJoins())
            return GizmoEmitResult::Invalid;
        GizmoLine clipped  = line;
        clipped.Width     *= m_pixel_scale;
        clipped.DashOn    *= m_pixel_scale;
        clipped.DashOff   *= m_pixel_scale;
        clipped.DashPhase *= m_pixel_scale;
        if (!Positive(clipped.Width) || !Nonnegative(clipped.DashOn) || !Nonnegative(clipped.DashOff) || !std::isfinite(clipped.DashPhase))
            return GizmoEmitResult::Invalid;
        for (bool near_plane : {true, false})
        {
            double da = 0, db = 0;
            if (!Distance(clipped.A, near_plane, &da) || !Distance(clipped.B, near_plane, &db))
                return GizmoEmitResult::Invalid;
            if (da < 0 && db < 0)
                return GizmoEmitResult::Culled;
            if (da >= 0 && db >= 0)
                continue;
            Vec3f intersection = {};
            if (!Intersection(clipped.A, clipped.B, da, db, &intersection))
                return GizmoEmitResult::Invalid;
            if (da < 0)
            {
                Geometry::ProjectedPoint start = {}, end = {};
                // Preserve dash rhythm where the original screen projection
                // exists; behind-eye starts have no finite visible phase anchor.
                if (Geometry::Project(clipped.A, m_camera, m_target_viewport, &start) && Geometry::Project(intersection, m_camera, m_target_viewport, &end))
                {
                    const double phase = double(clipped.DashPhase) + std::hypot(double(start.Screen.x) - end.Screen.x, double(start.Screen.y) - end.Screen.y);
                    if (!std::isfinite(phase) || phase > std::numeric_limits<float>::max())
                        return GizmoEmitResult::Invalid;
                    clipped.DashPhase = float(phase);
                }
                clipped.A               = intersection;
                clipped.StartJoinNormal = {};
            }
            else
            {
                clipped.B             = intersection;
                clipped.EndJoinNormal = {};
            }
        }
        if (!Visible(clipped.A) || !Visible(clipped.B))
            return GizmoEmitResult::Culled;
        if (m_output.LineCount == GizmoFrameData::MaxLines)
            return GizmoEmitResult::CapacityExceeded;
        m_output.Lines[m_output.LineCount++] = clipped;
        return GizmoEmitResult::Emitted;
    }

    GizmoEmitResult GizmoPrimitiveBuilder::Triangle(const GizmoTriangle& triangle)
    {
        if (!m_valid || !m_output.ValidCounts() || (triangle.AntiAliasEdges & ~7u))
            return GizmoEmitResult::Invalid;
        // Each half-space can add at most one vertex. A triangle clipped by
        // both depth planes has at most five; all storage is bounded and local.
        Vec3f    polygon[5] = {triangle.A, triangle.B, triangle.C};
        bool     edge_aa[5] = {(triangle.AntiAliasEdges & 4u) != 0, (triangle.AntiAliasEdges & 1u) != 0, (triangle.AntiAliasEdges & 2u) != 0};
        uint32_t count      = 3;
        for (bool near_plane : {true, false})
        {
            double distances[5] = {};
            bool   all_inside = true, all_outside = true;
            for (uint32_t i = 0; i < count; ++i)
            {
                if (!Distance(polygon[i], near_plane, &distances[i]))
                    return GizmoEmitResult::Invalid;
                all_inside  &= distances[i] >= 0.0;
                all_outside &= distances[i] < 0.0;
            }
            if (all_inside)
                continue;
            if (all_outside)
                return GizmoEmitResult::Culled;
            Vec3f    clipped[5]    = {};
            bool     clipped_aa[5] = {};
            uint32_t clipped_count = 0;
            for (uint32_t i = 0; i < count; ++i)
            {
                const uint32_t next = (i + 1) % count;
                if (distances[i] >= 0)
                {
                    if (clipped_count == 5)
                        return GizmoEmitResult::Invalid;
                    clipped_aa[clipped_count] = edge_aa[i];
                    clipped[clipped_count++]  = polygon[i];
                }
                if ((distances[i] < 0) != (distances[next] < 0))
                {
                    Vec3f point = {};
                    if (clipped_count == 5 || !Intersection(polygon[i], polygon[next], distances[i], distances[next], &point))
                        return GizmoEmitResult::Invalid;
                    clipped_aa[clipped_count] = distances[i] >= 0 || edge_aa[i];
                    clipped[clipped_count++]  = point;
                }
            }
            count = clipped_count;
            if (count < 3)
                return GizmoEmitResult::Culled;
            for (uint32_t i = 0; i < count; ++i)
            {
                polygon[i] = clipped[i];
                edge_aa[i] = clipped_aa[i];
            }
        }
        for (uint32_t i = 0; i < count; ++i)
            if (!Visible(polygon[i]))
                return GizmoEmitResult::Culled;
        const uint32_t emitted = count - 2;
        if (emitted > GizmoFrameData::MaxTriangles - m_output.TriangleCount)
            return GizmoEmitResult::CapacityExceeded;
        for (uint32_t i = 0; i < emitted; ++i)
        {
            // Only polygon boundary edges receive coverage. Never AA a fan's
            // internal diagonal; preserve caller masks for shared plane fills.
            const uint32_t mask                          = (edge_aa[i + 1] ? 1u : 0u) | ((i + 2 == count - 1 && edge_aa[count - 1]) ? 2u : 0u) | ((i == 0 && edge_aa[0]) ? 4u : 0u);
            m_output.Triangles[m_output.TriangleCount++] = {.A = polygon[0], .B = polygon[i + 1], .C = polygon[i + 2], .Color = triangle.Color, .AntiAliasEdges = mask};
        }
        return GizmoEmitResult::Emitted;
    }

    GizmoEmitResult GizmoPrimitiveBuilder::Sprite(const GizmoSprite& sprite)
    {
        double near_distance = 0, far_distance = 0;
        if (!m_valid || !m_output.ValidCounts() || !Distance(sprite.Position, true, &near_distance) || !Distance(sprite.Position, false, &far_distance) || !Positive(sprite.Size) || !Nonnegative(sprite.OutlineWidth) || sprite.OutlineWidth > sprite.Size * 0.5f)
            return GizmoEmitResult::Invalid;
        if (near_distance < 0 || far_distance < 0 || !Visible(sprite.Position))
            return GizmoEmitResult::Culled;
        GizmoSprite scaled   = sprite;
        scaled.Size         *= m_pixel_scale;
        scaled.OutlineWidth *= m_pixel_scale;
        if (!Positive(scaled.Size) || !Nonnegative(scaled.OutlineWidth))
            return GizmoEmitResult::Invalid;
        if (m_output.SpriteCount == GizmoFrameData::MaxSprites)
            return GizmoEmitResult::CapacityExceeded;
        m_output.Sprites[m_output.SpriteCount++] = scaled;
        return GizmoEmitResult::Emitted;
    }
} // namespace ZEngine::Rendering::Renderers
