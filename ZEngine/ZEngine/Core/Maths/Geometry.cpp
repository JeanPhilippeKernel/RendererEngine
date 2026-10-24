#include <ZEngine/Core/Maths/Geometry.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace ZEngine::Core::Maths::Geometry
{
    bool TransformBox(const Vec3f& minimum, const Vec3f& maximum, const Mat4f& world, BoxCorners* out)
    {
        if (!out || world(3, 0) != 0.f || world(3, 1) != 0.f || world(3, 2) != 0.f || world(3, 3) != 1.f)
        {
            return false;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(minimum[axis]) || !std::isfinite(maximum[axis]) || minimum[axis] > maximum[axis])
            {
                return false;
            }
        }
        BoxCorners corners = {};
        for (uint32_t corner = 0; corner < 8; ++corner)
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                double value = double(world(row, 3));
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    value += double(world(row, axis)) * (corner & (1u << axis) ? maximum[axis] : minimum[axis]);
                }
                if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
                {
                    return false;
                }
                corners[corner][row] = float(value);
            }
        }
        *out = corners;
        return true;
    }

    namespace
    {
        // Representation tolerance, not a gizmo interaction/design threshold.
        constexpr double FloatRoundoff = 8.0 * std::numeric_limits<float>::epsilon();

        double           ReducedRotationAngle(double angle, bool& identity)
        {
            const double reduced = std::remainder(angle, 2.0 * std::numbers::pi);
            // Absorb double-roundoff around complete turns, not small genuine
            // rotations around zero. Snapped degrees may be one ulp off tau.
            identity             = reduced == 0.0 || (std::abs(angle) > std::numbers::pi && std::abs(reduced) <= 8.0 * std::numeric_limits<double>::epsilon() * std::abs(angle));
            return reduced;
        }

        bool Finite(const Vec2f& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y);
        }
        bool Finite(const Vec3f& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }
        bool Finite(const Mat4f& m)
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t col = 0; col < 4; ++col)
                {
                    if (!std::isfinite(m(row, col)))
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        bool Float(double value, float* out)
        {
            if (!out || !std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            *out = static_cast<float>(value);
            return true;
        }
        double Dot(const Vec3f& a, const Vec3f& b)
        {
            return double(a.x) * b.x + double(a.y) * b.y + double(a.z) * b.z;
        }
        bool Vector(double x, double y, double z, Vec3f* out)
        {
            Vec3f result = {};
            if (!out || !Float(x, &result.x) || !Float(y, &result.y) || !Float(z, &result.z))
            {
                return false;
            }
            *out = result;
            return true;
        }
        bool Unit(const Vec3f& v, Vec3f* out)
        {
            if (!Finite(v))
            {
                return false;
            }
            const double length = std::hypot(double(v.x), double(v.y), double(v.z));
            return length > 0.0 && Vector(v.x / length, v.y / length, v.z / length, out);
        }
        bool PlaneUnit(const Vec3f& v, const Vec3f& normal, Vec3f* out)
        {
            Vec3f unit = {};
            if (!Unit(v, &unit))
            {
                return false;
            }
            const double offset = Dot(unit, normal);
            const double x = unit.x - offset * normal.x, y = unit.y - offset * normal.y, z = unit.z - offset * normal.z;
            const double length = std::hypot(x, y, z);
            // A parallel vector can leave a float-roundoff residual for arbitrary
            // normals. Do not normalize that residual into an arbitrary angle.
            return length > FloatRoundoff && Vector(x / length, y / length, z / length, out);
        }
        bool Valid(const Viewport& viewport)
        {
            return Finite(viewport.Origin) && Finite(viewport.Size) && viewport.Size.x > 0.f && viewport.Size.y > 0.f;
        }
        bool Multiply(const Mat4f& a, const Mat4f& b, Mat4f* out)
        {
            Mat4f result = {};
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t col = 0; col < 4; ++col)
                {
                    double sum = 0.0;
                    for (uint32_t k = 0; k < 4; ++k)
                    {
                        sum += double(a(row, k)) * b(k, col);
                    }
                    if (!Float(sum, &result(row, col)))
                    {
                        return false;
                    }
                }
            }
            *out = result;
            return true;
        }
        void Transform(const Mat4f& m, double x, double y, double z, double w, double (&out)[4])
        {
            for (uint32_t row = 0; row < 4; ++row)
            {
                out[row] = double(m(row, 0)) * x + double(m(row, 1)) * y + double(m(row, 2)) * z + double(m(row, 3)) * w;
            }
        }
        bool Affine(const Mat4f& m)
        {
            return Finite(m) && std::abs(m(3, 0)) <= FloatRoundoff && std::abs(m(3, 1)) <= FloatRoundoff && std::abs(m(3, 2)) <= FloatRoundoff && std::abs(double(m(3, 3)) - 1.0) <= FloatRoundoff;
        }
        bool Matches(const Mat4f& a, const Mat4f& b, float error)
        {
            if (!Finite(a) || !Finite(b))
            {
                return false;
            }
            for (uint32_t row = 0; row < 4; ++row)
            {
                for (uint32_t col = 0; col < 4; ++col)
                {
                    if (std::abs(double(a(row, col)) - b(row, col)) > error)
                    {
                        return false;
                    }
                }
            }
            return true;
        }
        double NearAngle(double angle, double reference)
        {
            return reference + std::remainder(angle - reference, 2.0 * std::numbers::pi);
        }
        double EulerDistance(const Vec3f& a, const Vec3f& b)
        {
            const double dx = double(a.x) - b.x, dy = double(a.y) - b.y, dz = double(a.z) - b.z;
            return dx * dx + dy * dy + dz * dz;
        }
        bool Euler(const Mat4f& m, const Vec3f& reference, Vec3f* out)
        {
            const double cos_y = std::hypot(double(m(0, 0)), double(m(0, 1)));
            const double y     = std::atan2(-double(m(0, 2)), cos_y);
            if (cos_y <= FloatRoundoff)
            {
                // At +pi/2 only x-z is determined; at -pi/2 only x+z is.
                // Split the correction between x/z to stay closest to the base.
                const double sign       = m(0, 2) < 0.f ? 1.0 : -1.0;
                const double constraint = std::atan2(sign * m(1, 0), m(1, 1));
                const double base       = double(reference.x) - sign * reference.z;
                const double correction = std::remainder(constraint - base, 2.0 * std::numbers::pi) * 0.5;
                return Vector(double(reference.x) + correction, NearAngle(y, reference.y), double(reference.z) - sign * correction, out);
            }
            const double x     = std::atan2(m(1, 2), m(2, 2));
            const double z     = std::atan2(m(0, 1), m(0, 0));
            Vec3f        first = {}, second = {};
            if (!Vector(NearAngle(x, reference.x), NearAngle(y, reference.y), NearAngle(z, reference.z), &first) || !Vector(NearAngle(x + std::numbers::pi, reference.x), NearAngle(std::numbers::pi - y, reference.y), NearAngle(z + std::numbers::pi, reference.z), &second))
            {
                return false;
            }
            *out = EulerDistance(first, reference) <= EulerDistance(second, reference) ? first : second;
            return true;
        }
    } // namespace

    bool TryInverse(const Mat4f& matrix, Mat4f* out)
    {
        if (!out || !Finite(matrix))
        {
            return false;
        }
        double working[4][8] = {};
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                working[row][col] = matrix(row, col);
            }
            working[row][row + 4] = 1.0;
        }
        for (uint32_t col = 0; col < 4; ++col)
        {
            uint32_t pivot = col;
            for (uint32_t row = col + 1; row < 4; ++row)
            {
                if (std::abs(working[row][col]) > std::abs(working[pivot][col]))
                {
                    pivot = row;
                }
            }
            if (working[pivot][col] == 0.0)
            {
                return false;
            }
            if (pivot != col)
            {
                for (uint32_t k = 0; k < 8; ++k)
                {
                    std::swap(working[col][k], working[pivot][k]);
                }
            }
            const double reciprocal = 1.0 / working[col][col];
            for (uint32_t k = 0; k < 8; ++k)
            {
                working[col][k] *= reciprocal;
            }
            for (uint32_t row = 0; row < 4; ++row)
            {
                if (row != col)
                {
                    const double factor = working[row][col];
                    for (uint32_t k = 0; k < 8; ++k)
                    {
                        working[row][k] -= factor * working[col][k];
                    }
                }
            }
        }
        Mat4f result = {};
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t col = 0; col < 4; ++col)
            {
                if (!Float(working[row][col + 4], &result(row, col)))
                {
                    return false;
                }
            }
        }
        *out = result;
        return true;
    }

    bool MakeProjectionFrame(const Mat4f& view, const Mat4f& projection, const Vec3f& position, ProjectionKind kind, bool reverse_z, ProjectionFrame* out)
    {
        if (!out || !Affine(view) || !Finite(projection) || !Finite(position) || (kind != ProjectionKind::Perspective && kind != ProjectionKind::Orthographic))
        {
            return false;
        }
        ProjectionFrame result = {};
        if (!Multiply(projection, view, &result.ViewProjection) || !TryInverse(result.ViewProjection, &result.InverseViewProjection) || !Unit({-view(2, 0), -view(2, 1), -view(2, 2)}, &result.Forward) || projection(1, 1) == 0.f)
        {
            return false;
        }
        result.Position         = position;
        result.ProjectionYScale = std::abs(projection(1, 1));
        result.Kind             = kind;
        result.ReverseZ         = reverse_z;
        result.Valid            = true;
        *out                    = result;
        return true;
    }

    bool Project(const Vec3f& world, const ProjectionFrame& frame, const Viewport& viewport, ProjectedPoint* out)
    {
        if (!out || !frame.Valid || !Finite(world) || !Valid(viewport))
        {
            return false;
        }
        double clip[4] = {};
        Transform(frame.ViewProjection, world.x, world.y, world.z, 1.0, clip);
        if (clip[3] <= 0.0)
        {
            return false;
        }
        ProjectedPoint result = {};
        if (!Float(double(viewport.Origin.x) + (clip[0] / clip[3] + 1.0) * viewport.Size.x * 0.5, &result.Screen.x) || !Float(double(viewport.Origin.y) + (clip[1] / clip[3] + 1.0) * viewport.Size.y * 0.5, &result.Screen.y) || !Float(clip[2] / clip[3], &result.Depth) || !Float(clip[3], &result.ClipW))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool UnprojectAtDepth(const Vec2f& screen, float depth, const ProjectionFrame& frame, const Viewport& viewport, Vec3f* out)
    {
        if (!out || !frame.Valid || !Finite(screen) || !std::isfinite(depth) || !Valid(viewport))
        {
            return false;
        }
        const double x        = 2.0 * (double(screen.x) - viewport.Origin.x) / viewport.Size.x - 1.0;
        const double y        = 2.0 * (double(screen.y) - viewport.Origin.y) / viewport.Size.y - 1.0;
        double       world[4] = {};
        Transform(frame.InverseViewProjection, x, y, depth, 1.0, world);
        return world[3] != 0.0 && Vector(world[0] / world[3], world[1] / world[3], world[2] / world[3], out);
    }

    bool UnprojectRay(const Vec2f& screen, const ProjectionFrame& frame, const Viewport& viewport, Ray* out)
    {
        if (!out || !frame.Valid || !Finite(screen) || !Finite(frame.Position) || !Valid(viewport) || (frame.Kind != ProjectionKind::Perspective && frame.Kind != ProjectionKind::Orthographic))
        {
            return false;
        }
        if (frame.Kind == ProjectionKind::Perspective)
        {
            // The ray is the intersection of its two screen-coordinate planes.
            // Work from their normals, not a near-world point minus the camera:
            // that subtraction loses direction precision for small near planes
            // or translated cameras. This also avoids depth-row cancellation,
            // two homogeneous unprojections and any per-sample inversion.
            const double x             = 2.0 * (double(screen.x) - viewport.Origin.x) / viewport.Size.x - 1.0;
            const double y             = 2.0 * (double(screen.y) - viewport.Origin.y) / viewport.Size.y - 1.0;
            double       horizontal[3] = {}, vertical[3] = {};
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                horizontal[axis] = double(frame.ViewProjection(0, axis)) - x * frame.ViewProjection(3, axis);
                vertical[axis]   = double(frame.ViewProjection(1, axis)) - y * frame.ViewProjection(3, axis);
            }
            const double dx     = horizontal[1] * vertical[2] - horizontal[2] * vertical[1];
            const double dy     = horizontal[2] * vertical[0] - horizontal[0] * vertical[2];
            const double dz     = horizontal[0] * vertical[1] - horizontal[1] * vertical[0];
            const double length = std::hypot(dx, dy, dz);
            Ray          result = {.Origin = frame.Position};
            if (!std::isfinite(length) || length <= 0.0 || !Vector(dx / length, dy / length, dz / length, &result.Direction))
            {
                return false;
            }
            const double alignment = Dot(result.Direction, frame.Forward);
            if (!std::isfinite(alignment) || alignment == 0.0)
            {
                return false;
            }
            if (alignment < 0.0)
            {
                result.Direction = -result.Direction;
            }
            *out = result;
            return true;
        }
        if (frame.Kind == ProjectionKind::Orthographic && Affine(frame.ViewProjection))
        {
            // Reconstruct relative to the camera, not the large translation in
            // inverse VP. Reverse-Z plus a long depth range otherwise subtracts
            // large rounded terms and shifts each ray sideways at the near plane.
            // Differences along the ray do not change an axis/plane solution.
            double clip[4] = {};
            Transform(frame.ViewProjection, frame.Position.x, frame.Position.y, frame.Position.z, 1.0, clip);
            const double x      = 2.0 * (double(screen.x) - viewport.Origin.x) / viewport.Size.x - 1.0 - clip[0];
            const double y      = 2.0 * (double(screen.y) - viewport.Origin.y) / viewport.Size.y - 1.0 - clip[1];
            const double z      = (frame.ReverseZ ? 1.0 : 0.0) - clip[2];
            Ray          result = {};
            for (uint32_t row = 0; row < 3; ++row)
            {
                if (!Float(double(frame.Position[row]) + double(frame.InverseViewProjection(row, 0)) * x + double(frame.InverseViewProjection(row, 1)) * y + double(frame.InverseViewProjection(row, 2)) * z, &result.Origin[row]))
                {
                    return false;
                }
            }
            const float sign = frame.ReverseZ ? -1.f : 1.f;
            if (!Unit({sign * frame.InverseViewProjection(0, 2), sign * frame.InverseViewProjection(1, 2), sign * frame.InverseViewProjection(2, 2)}, &result.Direction))
            {
                return false;
            }
            *out = result;
            return true;
        }
        Vec3f near_point = {}, interior = {};
        // An interior depth avoids the infinite-far homogeneous w=0 singularity.
        if (!UnprojectAtDepth(screen, frame.ReverseZ ? 1.f : 0.f, frame, viewport, &near_point) || !UnprojectAtDepth(screen, 0.5f, frame, viewport, &interior))
        {
            return false;
        }
        Ray result    = {};
        result.Origin = frame.Kind == ProjectionKind::Perspective ? frame.Position : near_point;
        if (!Unit(interior - result.Origin, &result.Direction))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool WorldPerPixel(const ProjectionFrame& frame, const Vec3f& pivot, float viewport_height, float* out)
    {
        if (!out || !frame.Valid || !Finite(pivot) || !std::isfinite(viewport_height) || viewport_height <= 0.f || !std::isfinite(frame.ProjectionYScale) || frame.ProjectionYScale <= 0.f)
        {
            return false;
        }
        const double depth  = frame.Kind == ProjectionKind::Orthographic ? 1.0 : Dot(pivot - frame.Position, frame.Forward);
        float        result = 0.f;
        if (depth <= 0.0 || !Float(2.0 * depth / (double(frame.ProjectionYScale) * viewport_height), &result) || result <= 0.f)
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool ClosestPointOnAxis(const Ray& ray, const Vec3f& axis_origin, const Vec3f& axis_direction, float min_denominator, AxisPoint* out)
    {
        Vec3f axis = {}, direction = {};
        if (!out || !Finite(ray.Origin) || !Finite(axis_origin) || !std::isfinite(min_denominator) || min_denominator < 0.f || min_denominator >= 1.f || !Unit(axis_direction, &axis) || !Unit(ray.Direction, &direction))
        {
            return false;
        }
        const Vec3f  offset = axis_origin - ray.Origin;
        const double b = Dot(axis, direction), d = Dot(axis, offset), e = Dot(direction, offset);
        const double denominator = 1.0 - b * b;
        if (denominator <= 0.0 || denominator < min_denominator)
        {
            return false;
        }
        const double t      = (b * e - d) / denominator;
        const double s      = e + b * t;
        AxisPoint    result = {};
        if (s < 0.0 || !Float(t, &result.AxisParameter) || !Float(s, &result.RayParameter) || !Vector(double(axis_origin.x) + t * axis.x, double(axis_origin.y) + t * axis.y, double(axis_origin.z) + t * axis.z, &result.Position))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool RayPlane(const Ray& ray, const Vec3f& plane_point, const Vec3f& normal, float min_cosine, PlaneHit* out)
    {
        Vec3f n = {}, direction = {};
        if (!out || !Finite(ray.Origin) || !Finite(plane_point) || !std::isfinite(min_cosine) || min_cosine < 0.f || min_cosine >= 1.f || !Unit(normal, &n) || !Unit(ray.Direction, &direction))
        {
            return false;
        }
        const double cosine = Dot(direction, n);
        if (cosine == 0.0 || std::abs(cosine) < min_cosine)
        {
            return false;
        }
        const double distance = Dot(plane_point - ray.Origin, n) / cosine;
        PlaneHit     result   = {};
        if (distance < 0.0 || !Float(distance, &result.Distance) || !Vector(double(ray.Origin.x) + distance * direction.x, double(ray.Origin.y) + distance * direction.y, double(ray.Origin.z) + distance * direction.z, &result.Position))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool CaptureTranslation(const TranslationCapture& capture, TranslationReference* out)
    {
        if (!out || !capture.Projection.Valid || !Valid(capture.Bounds) || !Finite(capture.Pointer) || !Finite(capture.Anchor) || !Affine(capture.Basis) || !capture.Axes || capture.Axes > 7 || !std::isfinite(capture.AxisMinDenominator) || capture.AxisMinDenominator < 0.f || capture.AxisMinDenominator >= 1.f || !std::isfinite(capture.PlaneMinCosine) || capture.PlaneMinCosine < 0.f || capture.PlaneMinCosine >= 1.f || !std::isfinite(capture.MinimumBasisVolume) || capture.MinimumBasisVolume < 0.f || capture.MinimumBasisVolume >= 1.f)
        {
            return false;
        }
        TranslationReference result = {};
        result.Projection           = capture.Projection;
        result.Bounds               = capture.Bounds;
        result.Pointer              = capture.Pointer;
        result.Anchor               = capture.Anchor;
        result.Axes                 = capture.Axes;
        result.Basis                = Identity<Mat4f>();
        Vec3f    axes[3]            = {};
        uint32_t selected[3]        = {};
        uint32_t count              = 0;
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (capture.Basis(axis, 3) != 0.f)
            {
                return false;
            }
            if (!Unit({capture.Basis(0, axis), capture.Basis(1, axis), capture.Basis(2, axis)}, &axes[axis]))
            {
                return false;
            }
            for (uint32_t row = 0; row < 3; ++row)
            {
                result.Basis(row, axis) = axes[axis][row];
            }
            if (capture.Axes & (1u << axis))
            {
                selected[count++] = axis;
            }
        }
        // Skewed local frames are valid, but degenerate axes cannot define a
        // reversible constraint/snap basis. Translation never orthogonalizes it.
        const double volume = std::abs(Dot(axes[0], cross3d(axes[1], axes[2])));
        if (volume <= 0.0 || volume < capture.MinimumBasisVolume || !TryInverse(result.Basis, &result.InverseBasis))
        {
            return false;
        }
        result.Axis              = count == 1;
        result.MinimumSeparation = result.Axis ? capture.AxisMinDenominator : capture.PlaneMinCosine;
        if (result.Axis)
        {
            result.Direction = axes[selected[0]];
        }
        else if (!Unit(count == 2 ? cross3d(axes[selected[0]], axes[selected[1]]) : capture.Projection.Forward, &result.Direction))
        {
            return false;
        }
        Ray ray = {};
        if (!UnprojectRay(capture.Pointer, capture.Projection, capture.Bounds, &ray))
        {
            return false;
        }
        if (result.Axis)
        {
            AxisPoint hit = {};
            if (!ClosestPointOnAxis(ray, result.Anchor, result.Direction, result.MinimumSeparation, &hit))
            {
                return false;
            }
            result.StartAxisParameter = hit.AxisParameter;
        }
        else
        {
            PlaneHit hit = {};
            if (!RayPlane(ray, result.Anchor, result.Direction, result.MinimumSeparation, &hit))
            {
                return false;
            }
            result.StartHit = hit.Position;
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool SampleTranslation(const TranslationReference& reference, const Vec2f& pointer, float motion_scale, bool snap, float snap_step, TranslationSample* out)
    {
        if (!out || !reference.Valid || !Finite(pointer) || !std::isfinite(motion_scale) || motion_scale <= 0.f || (snap && (!std::isfinite(snap_step) || snap_step <= 0.f)))
        {
            return false;
        }
        TranslationSample result = {.Anchor = reference.Anchor};
        // Do not snap an off-grid anchor merely because the user clicked/released.
        // Returning to the immutable base also restores it exactly after snapping.
        if (pointer.x == reference.Pointer.x && pointer.y == reference.Pointer.y)
        {
            *out = result;
            return true;
        }
        Vec2f scaled = {};
        if (!Float(double(reference.Pointer.x) + (double(pointer.x) - reference.Pointer.x) * motion_scale, &scaled.x) || !Float(double(reference.Pointer.y) + (double(pointer.y) - reference.Pointer.y) * motion_scale, &scaled.y))
        {
            return false;
        }
        Ray ray = {};
        if (!UnprojectRay(scaled, reference.Projection, reference.Bounds, &ray))
        {
            return false;
        }
        Vec3f raw = {};
        if (reference.Axis)
        {
            AxisPoint hit = {};
            if (!ClosestPointOnAxis(ray, reference.Anchor, reference.Direction, reference.MinimumSeparation, &hit))
            {
                return false;
            }
            const double distance = double(hit.AxisParameter) - reference.StartAxisParameter;
            if (!Vector(distance * reference.Direction.x, distance * reference.Direction.y, distance * reference.Direction.z, &raw))
            {
                return false;
            }
        }
        else
        {
            PlaneHit hit = {};
            if (!RayPlane(ray, reference.Anchor, reference.Direction, reference.MinimumSeparation, &hit) || !Vector(double(hit.Position.x) - reference.StartHit.x, double(hit.Position.y) - reference.StartHit.y, double(hit.Position.z) - reference.StartHit.z, &raw))
            {
                return false;
            }
        }
        // Use a reciprocal basis, not dot products with skewed local axes. This
        // retains the captured plane/axis and snaps a single shared anchor delta.
        double coordinates[3] = {};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (reference.Axes & (1u << axis))
            {
                const Vec3f  dual(reference.InverseBasis(axis, 0), reference.InverseBasis(axis, 1), reference.InverseBasis(axis, 2));
                const double delta = Dot(dual, raw);
                const double start = Dot(dual, reference.Anchor);
                coordinates[axis]  = snap ? std::round((start + delta) / snap_step) * snap_step - start : delta;
            }
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            double delta = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                delta += double(reference.Basis(row, axis)) * coordinates[axis];
            }
            if (!Float(delta, &result.Delta[row]) || !Float(double(reference.Anchor[row]) + delta, &result.Anchor[row]))
            {
                return false;
            }
        }
        *out = result;
        return true;
    }

    bool CaptureTranslationTarget(const Mat4f& parent_world, const LocalTRS& before, float max_error, TranslationTarget* out)
    {
        if (!out || !Affine(parent_world) || !Finite(before.Position) || !Finite(before.Rotation) || !Finite(before.Scale) || !std::isfinite(max_error) || max_error < 0.f)
        {
            return false;
        }
        TranslationTarget result = {.ParentWorld = parent_world, .Before = before, .MaximumError = max_error};
        Mat4f             world  = {};
        if (!TryInverse(parent_world, &result.ParentInverse) || !Multiply(parent_world, ComposeTransformMatrix(before.Position, before.Rotation, before.Scale), &world))
        {
            return false;
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool TranslateLocal(const TranslationTarget& target, const Vec3f& world_delta, LocalTRS* out)
    {
        if (!out || !target.Valid || !Finite(world_delta))
        {
            return false;
        }
        LocalTRS result = target.Before;
        if (world_delta.x == 0.f && world_delta.y == 0.f && world_delta.z == 0.f)
        {
            *out = result;
            return true;
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            double delta = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                delta += double(target.ParentInverse(row, axis)) * world_delta[axis];
            }
            if (delta != 0.0 && !Float(double(target.Before.Position[row]) + delta, &result.Position[row]))
            {
                return false;
            }
        }
        // Validate the actual representable authored position, not just the
        // inverse result. Subtract local positions first so a large parent/world
        // translation cannot conceal lost motion through cancellation roundoff.
        for (uint32_t row = 0; row < 3; ++row)
        {
            double reconstructed = 0.0;
            double world         = target.ParentWorld(row, 3);
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                reconstructed += double(target.ParentWorld(row, axis)) * (double(result.Position[axis]) - target.Before.Position[axis]);
                world         += double(target.ParentWorld(row, axis)) * result.Position[axis];
            }
            if (!std::isfinite(reconstructed) || std::abs(reconstructed - world_delta[row]) > target.MaximumError || !std::isfinite(world) || std::abs(world) > std::numeric_limits<float>::max())
            {
                return false;
            }
        }
        *out = result;
        return true;
    }

    bool CaptureRotation(const RotationCapture& capture, RotationReference* out)
    {
        if (!out || !capture.Projection.Valid || !Valid(capture.Bounds) || !Finite(capture.Pointer) || !Finite(capture.Pivot) || !std::isfinite(capture.RadiusPx) || capture.RadiusPx <= 0.f || !std::isfinite(capture.TangentFallbackCosine) || capture.TangentFallbackCosine <= 0.f || capture.TangentFallbackCosine >= 1.f || !std::isfinite(capture.PlaneMinCosine) || capture.PlaneMinCosine < 0.f || capture.PlaneMinCosine >= capture.TangentFallbackCosine)
        {
            return false;
        }
        RotationReference result = {.Projection = capture.Projection, .Bounds = capture.Bounds, .Pointer = capture.Pointer, .Pivot = capture.Pivot, .RadiusPx = capture.RadiusPx, .PlaneMinCosine = capture.PlaneMinCosine};
        Ray               ray    = {};
        if (!Unit(capture.Axis, &result.Axis) || !UnprojectRay(capture.Pointer, capture.Projection, capture.Bounds, &ray))
        {
            return false;
        }
        result.Tangent = std::abs(Dot(result.Axis, ray.Direction)) < capture.TangentFallbackCosine;
        if (result.Tangent)
        {
            // Differentiate the actual projected ring tangent, including the
            // perspective divide and flipped viewport Y. A screen-space radial
            // perpendicular would reverse some edge-on rings or pick no direction.
            Vec3f radial = {};
            if (!Finite(capture.RingPoint) || !Vector(double(capture.RingPoint.x) - capture.Pivot.x, double(capture.RingPoint.y) - capture.Pivot.y, double(capture.RingPoint.z) - capture.Pivot.z, &radial) || !PlaneUnit(radial, result.Axis, &result.StartDirection))
            {
                return false;
            }
            const auto tangent  = cross3d(result.Axis, result.StartDirection);
            double     point[4] = {}, direction[4] = {};
            Transform(capture.Projection.ViewProjection, capture.RingPoint.x, capture.RingPoint.y, capture.RingPoint.z, 1.0, point);
            Transform(capture.Projection.ViewProjection, tangent.x, tangent.y, tangent.z, 0.0, direction);
            if (!std::isfinite(point[3]) || point[3] <= 0.0)
            {
                return false;
            }
            const double dx     = (direction[0] * point[3] - point[0] * direction[3]) * capture.Bounds.Size.x;
            const double dy     = (direction[1] * point[3] - point[1] * direction[3]) * capture.Bounds.Size.y;
            const double length = std::hypot(dx, dy);
            if (!std::isfinite(length) || length <= 0.0 || !Float(dx / length, &result.ScreenTangent.x) || !Float(dy / length, &result.ScreenTangent.y))
            {
                return false;
            }
        }
        else
        {
            PlaneHit hit    = {};
            Vec3f    radial = {};
            if (!RayPlane(ray, capture.Pivot, result.Axis, capture.PlaneMinCosine, &hit) || !Vector(double(hit.Position.x) - capture.Pivot.x, double(hit.Position.y) - capture.Pivot.y, double(hit.Position.z) - capture.Pivot.z, &radial) || !PlaneUnit(radial, result.Axis, &result.StartDirection))
            {
                return false;
            }
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool SampleRotation(const RotationReference& reference, const RotationProgress& previous, const Vec2f& pointer, float motion_scale, bool snap, double snap_step, RotationSample* out)
    {
        if (!out || !reference.Valid || !Finite(pointer) || !std::isfinite(previous.PreviousAngle) || !std::isfinite(previous.TotalAngle) || !std::isfinite(motion_scale) || motion_scale <= 0.f || (snap && (!std::isfinite(snap_step) || snap_step <= 0.f)))
        {
            return false;
        }
        RotationSample result = {};
        if (reference.Tangent)
        {
            const double dx = double(pointer.x) - reference.Pointer.x, dy = double(pointer.y) - reference.Pointer.y;
            result.Progress.TotalAngle    = (dx * reference.ScreenTangent.x + dy * reference.ScreenTangent.y) / reference.RadiusPx;
            result.Progress.PreviousAngle = result.Progress.TotalAngle;
        }
        else
        {
            Ray        ray      = {};
            PlaneHit   hit      = {};
            Vec3f      radial   = {};
            float      angle    = 0.f;
            const bool at_start = pointer.x == reference.Pointer.x && pointer.y == reference.Pointer.y;
            if (!at_start && (!UnprojectRay(pointer, reference.Projection, reference.Bounds, &ray) || !RayPlane(ray, reference.Pivot, reference.Axis, reference.PlaneMinCosine, &hit) || !Vector(double(hit.Position.x) - reference.Pivot.x, double(hit.Position.y) - reference.Pivot.y, double(hit.Position.z) - reference.Pivot.z, &radial) || !AngleOnPlane(reference.StartDirection, radial, reference.Axis, &angle)))
            {
                return false;
            }
            double delta = std::remainder(double(angle) - previous.PreviousAngle, 2.0 * std::numbers::pi);
            if (delta <= -std::numbers::pi)
            {
                delta += 2.0 * std::numbers::pi;
            }
            result.Progress.PreviousAngle = angle;
            // Rebuild from the principal angle plus whole turns, rather than
            // accumulating roundoff into an exact return to the press position.
            const double turns            = std::round((previous.TotalAngle + delta - angle) / (2.0 * std::numbers::pi));
            result.Progress.TotalAngle    = double(angle) + turns * (2.0 * std::numbers::pi);
        }
        result.Angle = result.Progress.TotalAngle * motion_scale;
        if (snap)
        {
            result.Angle = std::round(result.Angle / snap_step) * snap_step;
        }
        if (!std::isfinite(result.Progress.TotalAngle) || !std::isfinite(result.Angle))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool CaptureRotationTarget(const Mat4f& parent_world, const LocalTRS& before, const Vec3f& pivot, float max_error, RotationTarget* out)
    {
        if (!out || !Affine(parent_world) || !Finite(before.Position) || !Finite(before.Rotation) || !Finite(before.Scale) || !Finite(pivot) || !std::isfinite(max_error) || max_error < 0.f)
        {
            return false;
        }
        RotationTarget result      = {.ParentWorld = parent_world, .Before = before, .Pivot = pivot, .MaximumError = max_error};
        // Match the hierarchy's published start pose exactly, including its
        // float matrix-product rounding. Otherwise its own origin may compare
        // one ulp away from a separately double-accumulated frame and orbit by
        // that residual during a nominally in-place rotation.
        const auto     local_frame = ComposeTransformMatrix(before.Position, before.Rotation, {1.f, 1.f, 1.f});
        result.StartFrame          = parent_world * local_frame;
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                result.LocalRotation(row, column) = local_frame(row, column);
            }
        }
        if (!TryInverse(parent_world, &result.ParentInverse) || !Finite(result.StartFrame))
        {
            return false;
        }
        // Preflight the actual start world too. Removing local scale for the
        // decomposition must not admit an already overflowing authored matrix.
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                if (!std::isfinite(double(result.StartFrame(row, column)) * before.Scale[column]) || std::abs(double(result.StartFrame(row, column)) * before.Scale[column]) > std::numeric_limits<float>::max())
                {
                    return false;
                }
            }
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool RotateLocal(const RotationTarget& target, const Vec3f& world_axis, double angle, LocalTRS* out)
    {
        RotationDelta delta = {};
        return MakeRotationDelta(world_axis, angle, &delta) && RotateLocal(target, delta, out);
    }

    bool MakeRotationDelta(const Vec3f& world_axis, double angle, RotationDelta* out)
    {
        Vec3f axis = {};
        if (!out || !std::isfinite(angle) || !Unit(world_axis, &axis))
        {
            return false;
        }
        RotationDelta result  = {.Valid = true};
        const double  reduced = ReducedRotationAngle(angle, result.Identity);
        if (result.Identity)
        {
            for (uint32_t axis_index = 0; axis_index < 3; ++axis_index)
            {
                result.Coefficients[axis_index * 3 + axis_index] = 1.0;
            }
            *out = result;
            return true;
        }
        const double sine = std::sin(reduced), cosine = std::cos(reduced), remaining = 1.0 - cosine;
        auto&        rotation = result.Coefficients;
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                rotation[row * 3 + column] = remaining * double(axis[row]) * axis[column] + (row == column ? cosine : 0.0);
            }
        }
        rotation[1] -= sine * axis.z;
        rotation[2] += sine * axis.y;
        rotation[3] += sine * axis.z;
        rotation[5] -= sine * axis.x;
        rotation[6] -= sine * axis.y;
        rotation[7] += sine * axis.x;
        *out         = result;
        return true;
    }

    bool RotateOrientation(const Quatf& before, const Vec3f& world_axis, double angle, Quatf* out)
    {
        Vec3f        axis    = {};
        const double length2 = double(before.x) * before.x + double(before.y) * before.y + double(before.z) * before.z + double(before.w) * before.w;
        if (!out || !std::isfinite(angle) || !std::isfinite(length2) || length2 <= 1.e-16 || !Unit(world_axis, &axis))
        {
            return false;
        }
        bool         identity = false;
        const double reduced  = ReducedRotationAngle(angle, identity);
        if (identity)
        {
            *out = before;
            return true;
        }
        const double             inverse = 1.0 / std::sqrt(length2);
        const Quaternion<double> start   = {before.x * inverse, before.y * inverse, before.z * inverse, before.w * inverse};
        const auto               delta   = fromAxisAngle(Vec3<double>(axis.x, axis.y, axis.z), reduced);
        const auto               result  = delta * start;
        *out                             = {float(result.x), float(result.y), float(result.z), float(result.w)};
        return true;
    }

    bool RotateLocal(const RotationTarget& target, const RotationDelta& delta, LocalTRS* out)
    {
        if (!out || !target.Valid || !delta.Valid)
        {
            return false;
        }
        if (delta.Identity)
        {
            *out = target.Before;
            return true;
        }
        const auto& rotation = delta.Coefficients;
        Mat4f       desired  = Identity<Mat4f>();
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                double value = 0.0;
                for (uint32_t k = 0; k < 3; ++k)
                {
                    value += rotation[row * 3 + k] * target.StartFrame(k, column);
                }
                if (!Float(value, &desired(row, column)))
                {
                    return false;
                }
            }
            double position = target.Pivot[row];
            for (uint32_t k = 0; k < 3; ++k)
            {
                position += rotation[row * 3 + k] * (double(target.StartFrame(k, 3)) - target.Pivot[k]);
            }
            if (!Float(position, &desired(row, 3)))
            {
                return false;
            }
        }
        // Decompose with original local scale factored out. This retains zero
        // and negative authored scales without dividing by them, while still
        // detecting parent-induced shear instead of orthogonalizing it away.
        LocalTRS reference = target.Before;
        reference.Scale    = {1.f, 1.f, 1.f};
        LocalTRS result    = {};
        if (!LocalFromWorld(target.ParentInverse, desired, reference, target.MaximumError, &result))
        {
            return false;
        }
        if (target.Pivot.x == target.StartFrame(0, 3) && target.Pivot.y == target.StartFrame(1, 3) && target.Pivot.z == target.StartFrame(2, 3))
        {
            result.Position = target.Before.Position;
        }
        for (uint32_t axis_index = 0; axis_index < 3; ++axis_index)
        {
            if (result.Scale[axis_index] <= 0.f || !Float(double(result.Scale[axis_index]) * target.Before.Scale[axis_index], &result.Scale[axis_index]) || (target.Before.Scale[axis_index] != 0.f && result.Scale[axis_index] == 0.f))
            {
                return false;
            }
        }
        Mat4f actual = {};
        if (!Multiply(target.ParentWorld, ComposeTransformMatrix(result.Position, result.Rotation, result.Scale), &actual))
        {
            return false;
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            // Compare the orbit displacement before adding the world origin.
            // Rounding that absolute position must not hide unrepresentable
            // local motion on a distant root in the shared rotation batch.
            double displacement = 0.0, reconstructed = 0.0;
            for (uint32_t k = 0; k < 3; ++k)
            {
                displacement  += (rotation[row * 3 + k] - (row == k ? 1.0 : 0.0)) * (double(target.StartFrame(k, 3)) - target.Pivot[k]);
                reconstructed += double(target.ParentWorld(row, k)) * (double(result.Position[k]) - target.Before.Position[k]);
            }
            if (!std::isfinite(displacement) || std::abs(reconstructed - displacement) > target.MaximumError)
            {
                return false;
            }
            if (std::abs(double(actual(row, 3)) - desired(row, 3)) > target.MaximumError)
            {
                return false;
            }
            for (uint32_t column = 0; column < 3; ++column)
            {
                if (std::abs(double(actual(row, column)) - double(desired(row, column)) * target.Before.Scale[column]) > target.MaximumError)
                {
                    return false;
                }
            }
        }
        *out = result;
        return true;
    }

    bool CaptureScale(const ScaleCapture& capture, ScaleReference* out)
    {
        if (!out || !capture.Projection.Valid || !Valid(capture.Bounds) || !Finite(capture.Pointer) || !Finite(capture.Pivot) || !std::isfinite(capture.MinimumRatio) || capture.MinimumRatio <= 0.f || capture.MinimumRatio > 1.f)
        {
            return false;
        }
        ScaleReference result = {.Projection = capture.Projection, .Bounds = capture.Bounds, .Pointer = capture.Pointer, .Pivot = capture.Pivot, .AxisMinDenominator = capture.AxisMinDenominator, .UniformPixelsPerEfold = capture.UniformPixelsPerEfold, .MinimumRatio = capture.MinimumRatio, .Uniform = capture.Uniform};
        // Also validate the captured camera when uniform mapping does not need
        // a ray/axis solution. No invalid camera becomes a live interaction.
        Ray            ray    = {};
        ProjectedPoint pivot  = {};
        if (!Project(capture.Pivot, capture.Projection, capture.Bounds, &pivot) || !UnprojectRay(capture.Pointer, capture.Projection, capture.Bounds, &ray))
        {
            return false;
        }
        if (capture.Uniform)
        {
            if (!std::isfinite(capture.UniformPixelsPerEfold) || capture.UniformPixelsPerEfold <= 0.f)
            {
                return false;
            }
        }
        else
        {
            if (!std::isfinite(capture.HandleLength) || capture.HandleLength <= 0.f || !std::isfinite(capture.MinimumGrabFraction) || capture.MinimumGrabFraction <= 0.f || capture.MinimumGrabFraction > 1.f || !std::isfinite(capture.AxisMinDenominator) || capture.AxisMinDenominator <= 0.f || capture.AxisMinDenominator > 1.f || !Unit(capture.Axis, &result.Axis))
            {
                return false;
            }
            AxisPoint hit     = {};
            float     minimum = 0.f;
            if (!ClosestPointOnAxis(ray, capture.Pivot, result.Axis, capture.AxisMinDenominator, &hit) || !Float(double(capture.HandleLength) * capture.MinimumGrabFraction, &minimum) || minimum <= 0.f)
            {
                return false;
            }
            result.StartParameter = hit.AxisParameter;
            result.Denominator    = std::copysign(std::max(std::abs(hit.AxisParameter), minimum), hit.AxisParameter);
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool SampleScale(const ScaleReference& reference, const Vec2f& pointer, float motion_scale, bool snap, float snap_step, float* out)
    {
        if (!out || !reference.Valid || !Finite(pointer) || !std::isfinite(motion_scale) || motion_scale <= 0.f || (snap && (!std::isfinite(snap_step) || snap_step <= 0.f)))
        {
            return false;
        }
        if (pointer.x == reference.Pointer.x && pointer.y == reference.Pointer.y)
        {
            *out = 1.f;
            return true;
        }
        Vec2f displacement = {};
        if (!Float((double(pointer.x) - reference.Pointer.x) * motion_scale, &displacement.x) || !Float((double(pointer.y) - reference.Pointer.y) * motion_scale, &displacement.y))
        {
            return false;
        }
        double ratio = 1.0;
        if (reference.Uniform)
        {
            const double exponent = (double(displacement.x) - displacement.y) / (std::numbers::sqrt2 * reference.UniformPixelsPerEfold);
            if (exponent > std::log(double(std::numeric_limits<float>::max())))
            {
                return false;
            }
            ratio = std::exp(exponent);
        }
        else
        {
            Vec2f     scaled = {};
            Ray       ray    = {};
            AxisPoint hit    = {};
            if (!Float(double(reference.Pointer.x) + displacement.x, &scaled.x) || !Float(double(reference.Pointer.y) + displacement.y, &scaled.y) || !UnprojectRay(scaled, reference.Projection, reference.Bounds, &ray) || !ClosestPointOnAxis(ray, reference.Pivot, reference.Axis, reference.AxisMinDenominator, &hit))
            {
                return false;
            }
            ratio += (double(hit.AxisParameter) - reference.StartParameter) / reference.Denominator;
        }
        if (snap)
        {
            ratio = std::round(ratio / snap_step) * snap_step;
        }
        return Float(std::max(ratio, double(reference.MinimumRatio)), out);
    }

    bool CaptureScaleTarget(const Mat4f& parent_world, const LocalTRS& before, const Vec3f& pivot, float max_error, ScaleTarget* out)
    {
        if (!out || !Finite(pivot))
        {
            return false;
        }
        ScaleTarget result  = {};
        Mat4f       inverse = {};
        if (!CaptureTranslationTarget(parent_world, before, max_error, &result.Placement))
        {
            return false;
        }
        // Use the hierarchy's start-pose product, not a different accumulation
        // order: a target's own origin must not acquire an artificial orbit.
        const auto local_frame = ComposeTransformMatrix(before.Position, before.Rotation, {1.f, 1.f, 1.f});
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                result.LocalRotation(row, column) = local_frame(row, column);
            }
        }
        result.StartFrame = parent_world * local_frame;
        result.StartWorld = parent_world * ComposeTransformMatrix(before.Position, before.Rotation, before.Scale);
        if (!Finite(result.StartWorld) || !TryInverse(result.StartFrame, &inverse))
        {
            return false;
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            double value = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                value += double(inverse(row, axis)) * (double(pivot[axis]) - result.StartFrame(axis, 3));
            }
            if (!Float(value, &result.PivotOffset[row]))
            {
                return false;
            }
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool ScaleLocal(const ScaleTarget& target, const Vec3f& ratios, LocalTRS* out)
    {
        if (!out || !target.Valid || !target.Placement.Valid || !Finite(ratios) || ratios.x <= 0.f || ratios.y <= 0.f || ratios.z <= 0.f)
        {
            return false;
        }
        const auto& placement = target.Placement;
        LocalTRS    result    = placement.Before;
        if (ratios.x == 1.f && ratios.y == 1.f && ratios.z == 1.f)
        {
            *out = result;
            return true;
        }
        double delta[3] = {};
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                delta[row] += double(target.StartFrame(row, axis)) * target.PivotOffset[axis] * (1.0 - ratios[axis]);
            }
            if (ratios[row] != 1.f && (!Float(double(placement.Before.Scale[row]) * ratios[row], &result.Scale[row]) || (placement.Before.Scale[row] != 0.f && result.Scale[row] == 0.f)))
            {
                return false;
            }
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            double local_delta = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                local_delta += double(placement.ParentInverse(row, axis)) * delta[axis];
            }
            if (local_delta != 0.0 && !Float(double(placement.Before.Position[row]) + local_delta, &result.Position[row]))
            {
                return false;
            }
        }
        // Authored rotation never changes in a local-axis scale. Reuse its nine
        // captured coefficients instead of recomputing six trig calls per sample.
        Mat4f local = Identity<Mat4f>(), actual = {};
        for (uint32_t row = 0; row < 3; ++row)
        {
            local(row, 3) = result.Position[row];
            for (uint32_t column = 0; column < 3; ++column)
            {
                local(row, column) = target.LocalRotation(row, column) * result.Scale[column];
            }
        }
        if (!Multiply(placement.ParentWorld, local, &actual))
        {
            return false;
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            // Subtract before translating to world coordinates, so a large
            // world origin cannot hide a lost custom-pivot displacement.
            double reconstructed = 0.0;
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                reconstructed += double(placement.ParentWorld(row, axis)) * (double(result.Position[axis]) - placement.Before.Position[axis]);
            }
            if (!std::isfinite(delta[row]) || std::abs(reconstructed - delta[row]) > placement.MaximumError)
            {
                return false;
            }
            for (uint32_t column = 0; column < 3; ++column)
            {
                if (std::abs(double(actual(row, column)) - double(target.StartWorld(row, column)) * ratios[column]) > placement.MaximumError)
                {
                    return false;
                }
            }
        }
        *out = result;
        return true;
    }

    bool CaptureScaleFrame(const Mat4f& basis, float minimum_volume, ScaleFrame* out)
    {
        if (!out || !Affine(basis) || !std::isfinite(minimum_volume) || minimum_volume < 0.f || minimum_volume >= 1.f)
        {
            return false;
        }
        ScaleFrame result  = {.Basis = Identity<Mat4f>()};
        Vec3f      axes[3] = {};
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (basis(axis, 3) != 0.f || !Unit({basis(0, axis), basis(1, axis), basis(2, axis)}, &axes[axis]))
            {
                return false;
            }
            for (uint32_t row = 0; row < 3; ++row)
            {
                result.Basis(row, axis) = axes[axis][row];
            }
        }
        const double volume = std::abs(Dot(axes[0], cross3d(axes[1], axes[2])));
        if (volume <= 0.0 || volume < minimum_volume || !TryInverse(result.Basis, &result.InverseBasis))
        {
            return false;
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool ScaleInFrame(const RotationTarget& target, const ScaleFrame& frame, const Vec3f& ratios, LocalTRS* out)
    {
        ScaleDelta delta = {};
        return MakeScaleDelta(frame, ratios, &delta) && ScaleInFrame(target, delta, out);
    }

    bool MakeScaleDelta(const ScaleFrame& frame, const Vec3f& ratios, ScaleDelta* out)
    {
        if (!out || !frame.Valid || !Finite(ratios) || ratios.x <= 0.f || ratios.y <= 0.f || ratios.z <= 0.f)
        {
            return false;
        }
        ScaleDelta result = {.Ratios = ratios, .Uniform = ratios.x == ratios.y && ratios.x == ratios.z};
        if (result.Uniform)
        {
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                result.Coefficients[axis * 3 + axis] = double(ratios.x) - 1.0;
            }
        }
        else
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                for (uint32_t column = 0; column < 3; ++column)
                {
                    for (uint32_t axis = 0; axis < 3; ++axis)
                    {
                        result.Coefficients[row * 3 + column] += double(frame.Basis(row, axis)) * (double(ratios[axis]) - 1.0) * frame.InverseBasis(axis, column);
                    }
                }
            }
        }
        for (double value : result.Coefficients)
        {
            if (!std::isfinite(value))
            {
                return false;
            }
        }
        result.Valid = true;
        *out         = result;
        return true;
    }

    bool ScaleInFrame(const RotationTarget& target, const ScaleDelta& sample, LocalTRS* out)
    {
        if (!out || !target.Valid || !sample.Valid)
        {
            return false;
        }
        const auto& ratios  = sample.Ratios;
        const bool  uniform = sample.Uniform;
        if (ratios.x == 1.f && ratios.y == 1.f && ratios.z == 1.f)
        {
            *out = target.Before;
            return true;
        }
        const auto& delta   = sample.Coefficients;
        Mat4f       desired = target.StartFrame;
        for (uint32_t row = 0; row < 3; ++row)
        {
            for (uint32_t column = 0; column < 3; ++column)
            {
                double value = target.StartFrame(row, column);
                for (uint32_t k = 0; k < 3; ++k)
                {
                    value += delta[row * 3 + k] * target.StartFrame(k, column);
                }
                if (!Float(value, &desired(row, column)))
                {
                    return false;
                }
            }
            double displacement = 0.0;
            for (uint32_t k = 0; k < 3; ++k)
            {
                displacement += delta[row * 3 + k] * (double(target.StartFrame(k, 3)) - target.Pivot[k]);
            }
            if (!Float(double(target.StartFrame(row, 3)) + displacement, &desired(row, 3)))
            {
                return false;
            }
        }
        LocalTRS reference = target.Before, result = {};
        reference.Scale = {1.f, 1.f, 1.f};
        if (uniform)
        {
            // Uniform scaling cannot introduce shear. Keep authored Euler values
            // exact and avoid decomposition/trigonometry for this common path.
            result       = target.Before;
            result.Scale = ratios;
            for (uint32_t row = 0; row < 3; ++row)
            {
                double displacement = 0.0;
                for (uint32_t k = 0; k < 3; ++k)
                {
                    displacement += double(target.ParentInverse(row, k)) * (double(target.StartFrame(k, 3)) - target.Pivot[k]) * (double(ratios.x) - 1.0);
                }
                if (displacement != 0.0 && !Float(double(target.Before.Position[row]) + displacement, &result.Position[row]))
                {
                    return false;
                }
            }
        }
        else if (!LocalFromWorld(target.ParentInverse, desired, reference, target.MaximumError, &result))
        {
            return false;
        }
        if (target.Pivot.x == target.StartFrame(0, 3) && target.Pivot.y == target.StartFrame(1, 3) && target.Pivot.z == target.StartFrame(2, 3))
        {
            result.Position = target.Before.Position;
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (result.Scale[axis] <= 0.f || !Float(double(result.Scale[axis]) * target.Before.Scale[axis], &result.Scale[axis]) || (target.Before.Scale[axis] != 0.f && result.Scale[axis] == 0.f))
            {
                return false;
            }
        }
        Mat4f actual = {}, local = Identity<Mat4f>();
        if (uniform)
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                local(row, 3) = result.Position[row];
                for (uint32_t column = 0; column < 3; ++column)
                {
                    local(row, column) = target.LocalRotation(row, column) * result.Scale[column];
                }
            }
        }
        else
        {
            local = ComposeTransformMatrix(result.Position, result.Rotation, result.Scale);
        }
        if (!Multiply(target.ParentWorld, local, &actual))
        {
            return false;
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            // Verify displacements before adding a potentially very large world
            // origin; absolute-position float rounding must not hide lost motion.
            double displacement = 0.0, reconstructed = 0.0;
            for (uint32_t k = 0; k < 3; ++k)
            {
                displacement  += delta[row * 3 + k] * (double(target.StartFrame(k, 3)) - target.Pivot[k]);
                reconstructed += double(target.ParentWorld(row, k)) * (double(result.Position[k]) - target.Before.Position[k]);
            }
            if (!std::isfinite(displacement) || std::abs(reconstructed - displacement) > target.MaximumError)
            {
                return false;
            }
            for (uint32_t column = 0; column < 3; ++column)
            {
                if (std::abs(double(actual(row, column)) - double(desired(row, column)) * target.Before.Scale[column]) > target.MaximumError)
                {
                    return false;
                }
            }
        }
        *out = result;
        return true;
    }

    bool AngleOnPlane(const Vec3f& start, const Vec3f& current, const Vec3f& normal, float* out)
    {
        Vec3f n = {}, a = {}, b = {};
        if (!out || !Finite(start) || !Finite(current) || !Unit(normal, &n))
        {
            return false;
        }
        if (!PlaneUnit(start, n, &a) || !PlaneUnit(current, n, &b))
        {
            return false;
        }
        return Float(std::atan2(Dot(cross3d(a, b), n), Dot(a, b)), out);
    }

    bool UnwrapAngle(float angle, float previous_angle, float previous_total, float* out)
    {
        if (!std::isfinite(angle) || !std::isfinite(previous_angle) || !std::isfinite(previous_total))
        {
            return false;
        }
        double delta = std::remainder(double(angle) - previous_angle, 2.0 * std::numbers::pi);
        if (delta <= -std::numbers::pi)
        {
            delta += 2.0 * std::numbers::pi;
        }
        return Float(double(previous_total) + delta, out);
    }

    bool TangentAngle(const Vec2f& screen_delta, const Vec2f& tangent, float radius, float* out)
    {
        if (!Finite(screen_delta) || !Finite(tangent) || !std::isfinite(radius) || radius <= 0.f)
        {
            return false;
        }
        const double length = std::hypot(double(tangent.x), double(tangent.y));
        return length > 0.0 && Float((double(screen_delta.x) * tangent.x + double(screen_delta.y) * tangent.y) / (length * radius), out);
    }

    bool DistanceToSegmentSquared(const Vec2f& point, const Vec2f& start, const Vec2f& end, float* out)
    {
        if (!Finite(point) || !Finite(start) || !Finite(end))
        {
            return false;
        }
        const double dx = double(end.x) - start.x, dy = double(end.y) - start.y;
        const double px = double(point.x) - start.x, py = double(point.y) - start.y;
        const double squared = dx * dx + dy * dy;
        const double t       = squared > 0.0 ? std::clamp((px * dx + py * dy) / squared, 0.0, 1.0) : 0.0;
        return Float((px - t * dx) * (px - t * dx) + (py - t * dy) * (py - t * dy), out);
    }

    bool Snap(float value, float step, float* out)
    {
        return std::isfinite(value) && std::isfinite(step) && step > 0.f && Float(std::round(double(value) / step) * step, out);
    }

    bool SnapDelta(float anchor, float delta, float step, float* out)
    {
        return std::isfinite(anchor) && std::isfinite(delta) && std::isfinite(step) && step > 0.f && Float(std::round((double(anchor) + delta) / step) * step - anchor, out);
    }

    bool AxisScaleRatio(float parameter, float start_parameter, float minimum_start_magnitude, float minimum_ratio, float* out)
    {
        if (!std::isfinite(parameter) || !std::isfinite(start_parameter) || !std::isfinite(minimum_start_magnitude) || minimum_start_magnitude <= 0.f || !std::isfinite(minimum_ratio) || minimum_ratio <= 0.f)
        {
            return false;
        }
        const double start = std::copysign(std::max(std::abs(double(start_parameter)), double(minimum_start_magnitude)), start_parameter);
        return Float(std::max(double(parameter) / start, double(minimum_ratio)), out);
    }

    bool UniformScaleRatio(const Vec2f& screen_delta, float pixels_per_efold, float minimum_ratio, float* out)
    {
        if (!Finite(screen_delta) || !std::isfinite(pixels_per_efold) || pixels_per_efold <= 0.f || !std::isfinite(minimum_ratio) || minimum_ratio <= 0.f)
        {
            return false;
        }
        const double exponent = (double(screen_delta.x) - screen_delta.y) / (std::numbers::sqrt2 * pixels_per_efold);
        if (exponent > std::log(double(std::numeric_limits<float>::max())))
        {
            return false;
        }
        return Float(exponent <= std::log(double(minimum_ratio)) ? minimum_ratio : std::exp(exponent), out);
    }

    bool ClosestEuler(const Mat4f& rotation, const Vec3f& reference, float max_error, Vec3f* out)
    {
        Vec3f result = {};
        if (!out || !Finite(rotation) || !Finite(reference) || !std::isfinite(max_error) || max_error < 0.f || !Euler(rotation, reference, &result) || !Matches(rotation, ComposeTransformMatrix({}, result, {1.f, 1.f, 1.f}), max_error))
        {
            return false;
        }
        *out = result;
        return true;
    }

    bool DecomposeTRS(const Mat4f& local, const LocalTRS& reference, float max_error, LocalTRS* out)
    {
        if (!out || !Affine(local) || !Finite(reference.Position) || !Finite(reference.Rotation) || !Finite(reference.Scale) || !std::isfinite(max_error) || max_error < 0.f)
        {
            return false;
        }
        Vec3f       x = {}, y = {};
        const Vec3f column_x(local(0, 0), local(1, 0), local(2, 0));
        const Vec3f column_y(local(0, 1), local(1, 1), local(2, 1));
        const Vec3f column_z(local(0, 2), local(1, 2), local(2, 2));
        if (!Unit(column_x, &x) || !Vector(column_y.x - Dot(column_y, x) * x.x, column_y.y - Dot(column_y, x) * x.y, column_y.z - Dot(column_y, x) * x.z, &y) || !Unit(y, &y))
        {
            return false;
        }
        Vec3f z = {};
        if (!Unit(cross3d(x, y), &z))
        {
            return false;
        }
        const double scales[3] = {Dot(column_x, x), Dot(column_y, y), Dot(column_z, z)};
        if (scales[0] == 0.0 || scales[1] == 0.0 || scales[2] == 0.0)
        {
            return false;
        }
        LocalTRS best              = {};
        uint32_t best_sign_changes = UINT32_MAX;
        double   best_distance     = std::numeric_limits<double>::infinity();
        for (const float sign_x : {-1.f, 1.f})
        {
            for (const float sign_y : {-1.f, 1.f})
            {
                const float signs[3] = {sign_x, sign_y, sign_x * sign_y};
                const Vec3f axes[3]  = {x, y, z};
                Mat4f       rotation = Identity<Mat4f>();
                LocalTRS    value    = {
                    .Position = {local(0, 3), local(1, 3), local(2, 3)}
                };
                uint32_t sign_changes = 0;
                bool     valid        = true;
                for (uint32_t axis = 0; axis < 3; ++axis)
                {
                    if (!Float(scales[axis] * signs[axis], &value.Scale[axis]))
                    {
                        valid = false;
                    }
                    if (reference.Scale[axis] != 0.f && std::signbit(value.Scale[axis]) != std::signbit(reference.Scale[axis]))
                    {
                        ++sign_changes;
                    }
                    for (uint32_t row = 0; row < 3; ++row)
                    {
                        rotation(row, axis) = axes[axis][row] * signs[axis];
                    }
                }
                if (!valid || !Euler(rotation, reference.Rotation, &value.Rotation) || !Matches(local, ComposeTransformMatrix(value.Position, value.Rotation, value.Scale), max_error))
                {
                    continue;
                }
                const double distance = EulerDistance(value.Rotation, reference.Rotation);
                if (sign_changes < best_sign_changes || (sign_changes == best_sign_changes && distance < best_distance))
                {
                    best              = value;
                    best_sign_changes = sign_changes;
                    best_distance     = distance;
                }
            }
        }
        if (best_sign_changes == UINT32_MAX)
        {
            return false;
        }
        *out = best;
        return true;
    }

    bool LocalFromWorld(const Mat4f& parent_inverse, const Mat4f& world, const LocalTRS& reference, float max_error, LocalTRS* out)
    {
        Mat4f local = {};
        return Affine(parent_inverse) && Affine(world) && Multiply(parent_inverse, world, &local) && DecomposeTRS(local, reference, max_error, out);
    }
} // namespace ZEngine::Core::Maths::Geometry
