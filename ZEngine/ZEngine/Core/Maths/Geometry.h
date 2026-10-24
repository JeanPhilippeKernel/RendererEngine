#pragma once
#include <ZEngine/Core/Containers/FixedArray.h>
#include <ZEngine/Core/Maths/Matrix.h>
#include <ZEngine/Core/Maths/Quaternion.h>

namespace ZEngine::Core::Maths::Geometry
{
    struct Ray
    {
        Vec3f Origin    = {};
        Vec3f Direction = {0.f, 0.f, -1.f};
    };
    struct Viewport
    {
        Vec2f Origin = {};
        Vec2f Size   = {};
    };
    enum class ProjectionKind : uint8_t
    {
        Perspective,
        Orthographic,
    };
    /// Cached once for a camera/interaction frame; contains no renderer/editor types.
    struct ProjectionFrame
    {
        Mat4f          ViewProjection        = {};
        Mat4f          InverseViewProjection = {};
        Vec3f          Position              = {};
        Vec3f          Forward               = {0.f, 0.f, -1.f};
        float          ProjectionYScale      = 0.f;
        ProjectionKind Kind                  = ProjectionKind::Perspective;
        bool           ReverseZ              = false;
        bool           Valid                 = false;
    };
    struct ProjectedPoint
    {
        Vec2f Screen = {};
        float Depth  = 0.f;
        float ClipW  = 0.f;
    };
    struct AxisPoint
    {
        Vec3f Position      = {};
        float AxisParameter = 0.f;
        float RayParameter  = 0.f;
    };
    struct PlaneHit
    {
        Vec3f Position = {};
        float Distance = 0.f;
    };
    struct LocalTRS
    {
        Vec3f Position = {};
        Vec3f Rotation = {};
        Vec3f Scale    = {1.f, 1.f, 1.f};
    };

    using BoxCorners = Core::Containers::FixedArray<Vec3f, 8>;
    /// Transform the eight local extrema, not a world-axis re-fit. Corner bits
    /// 0/1/2 select maximum X/Y/Z. Finite degenerate/reflected/sheared affine
    /// boxes are valid. Failure preserves output; no allocation or inversion.
    [[nodiscard]] bool TransformBox(const Vec3f& minimum, const Vec3f& maximum, const Mat4f& world, BoxCorners* out);

    /// Value-only input for an immutable ray-based translation mapping. The
    /// columns of Basis define the constraint/snap frame, anchored at world zero.
    /// Bits 0/1/2 select X/Y/Z: one bit is an axis, two a plane, all three a
    /// camera-facing plane. Policies/thresholds belong to the caller.
    struct TranslationCapture
    {
        ProjectionFrame Projection         = {};
        Viewport        Bounds             = {};
        Vec2f           Pointer            = {};
        Vec3f           Anchor             = {};
        Mat4f           Basis              = Identity<Mat4f>();
        float           AxisMinDenominator = 0.f;
        float           PlaneMinCosine     = 0.f;
        float           MinimumBasisVolume = 0.f;
        uint8_t         Axes               = 0;
    };

    struct TranslationReference
    {
        ProjectionFrame Projection         = {};
        Viewport        Bounds             = {};
        Vec2f           Pointer            = {};
        Vec3f           Anchor             = {};
        Mat4f           Basis              = {};
        Mat4f           InverseBasis       = {};
        Vec3f           Direction          = {};
        Vec3f           StartHit           = {};
        float           StartAxisParameter = 0.f;
        float           MinimumSeparation  = 0.f;
        uint8_t         Axes               = 0;
        bool            Axis               = false;
        bool            Valid              = false;
    };

    struct TranslationSample
    {
        Vec3f Delta  = {};
        Vec3f Anchor = {};
    };

    /// Captured once per root. Translation preserves the exact authored rotation
    /// and scale, including signs/zeroes, without decomposition or Euler conversion.
    struct TranslationTarget
    {
        Mat4f    ParentWorld   = {};
        Mat4f    ParentInverse = {};
        LocalTRS Before        = {};
        float    MaximumError  = 0.f;
        bool     Valid         = false;
    };

    /// Immutable ring mapping. RingPoint is a picked world-space ring point and
    /// is needed only for the edge-on tangent fallback. All angles are radians;
    /// positive angles follow the right-hand rule, not the authored Euler signs.
    struct RotationCapture
    {
        ProjectionFrame Projection            = {};
        Viewport        Bounds                = {};
        Vec2f           Pointer               = {};
        Vec3f           Pivot                 = {};
        Vec3f           Axis                  = {};
        Vec3f           RingPoint             = {};
        float           RadiusPx              = 0.f;
        float           TangentFallbackCosine = 0.f;
        float           PlaneMinCosine        = 0.f;
    };

    struct RotationReference
    {
        ProjectionFrame Projection     = {};
        Viewport        Bounds         = {};
        Vec2f           Pointer        = {};
        Vec3f           Pivot          = {};
        Vec3f           Axis           = {};
        Vec3f           StartDirection = {};
        Vec2f           ScreenTangent  = {};
        float           RadiusPx       = 0.f;
        float           PlaneMinCosine = 0.f;
        bool            Tangent        = false;
        bool            Valid          = false;
    };

    /// Only accepted samples advance this caller-owned progress. Double precision
    /// avoids losing small angular changes after many turns. Captured geometry
    /// remains immutable; no authored pose is accumulated from preceding previews.
    struct RotationProgress
    {
        double PreviousAngle = 0.0;
        double TotalAngle    = 0.0;
    };

    struct RotationSample
    {
        RotationProgress Progress = {};
        double           Angle    = 0.0;
    };

    struct RotationTarget
    {
        Mat4f    ParentWorld   = {};
        Mat4f    ParentInverse = {};
        Mat4f    StartFrame    = {}; // World frame with authored local scale removed.
        Mat3f    LocalRotation = {}; // Captured unchanged-orientation coefficients.
        LocalTRS Before        = {};
        Vec3f    Pivot         = {};
        float    MaximumError  = 0.f;
        bool     Valid         = false;
    };

    /// Compute the shared axis normalization/trigonometry once per sample,
    /// then apply these immutable row-major coefficients to every root.
    struct RotationDelta
    {
        Core::Containers::FixedArray<double, 9> Coefficients = {};
        bool                                    Identity     = false;
        bool                                    Valid        = false;
    };

    /// One captured local-axis or uniform scale mapping. Policies are supplied
    /// by the editor; geometry does not depend on UI/renderer types or settings.
    struct ScaleCapture
    {
        ProjectionFrame Projection            = {};
        Viewport        Bounds                = {};
        Vec2f           Pointer               = {};
        Vec3f           Pivot                 = {};
        Vec3f           Axis                  = {};
        float           HandleLength          = 0.f;
        float           MinimumGrabFraction   = 0.f;
        float           AxisMinDenominator    = 0.f;
        float           UniformPixelsPerEfold = 0.f;
        float           MinimumRatio          = 0.f;
        bool            Uniform               = false;
    };

    struct ScaleReference
    {
        ProjectionFrame Projection            = {};
        Viewport        Bounds                = {};
        Vec2f           Pointer               = {};
        Vec3f           Pivot                 = {};
        Vec3f           Axis                  = {};
        float           StartParameter        = 0.f;
        float           Denominator           = 0.f;
        float           AxisMinDenominator    = 0.f;
        float           UniformPixelsPerEfold = 0.f;
        float           MinimumRatio          = 0.f;
        bool            Uniform               = false;
        bool            Valid                 = false;
    };

    /// Placement caches parent reconstruction and the exact authored before-pose.
    /// The unscaled frame remains invertible even when authored scale is zero.
    struct ScaleTarget
    {
        TranslationTarget Placement     = {};
        Mat4f             StartFrame    = {};
        Mat4f             StartWorld    = {};
        Mat3f             LocalRotation = {};
        Vec3f             PivotOffset   = {};
        bool              Valid         = false;
    };

    /// One normalized shared scale basis, captured from the active target. It
    /// may be skewed by ancestors; never silently orthogonalize authored axes.
    struct ScaleFrame
    {
        Mat4f Basis        = {};
        Mat4f InverseBasis = {};
        bool  Valid        = false;
    };

    /// Sample once for the entire root batch, not once per root. Coefficients
    /// store the shared world linear delta (B*diag(ratios)*inverse(B) - I).
    struct ScaleDelta
    {
        Core::Containers::FixedArray<double, 9> Coefficients = {};
        Vec3f                                   Ratios       = {1.f, 1.f, 1.f};
        bool                                    Uniform      = false;
        bool                                    Valid        = false;
    };

    // All checked functions preserve their output on failure. All storage is fixed
    // stack/value storage; none of these functions allocates or mutates source data.
    [[nodiscard]] bool TryInverse(const Mat4f& matrix, Mat4f* out);
    [[nodiscard]] bool MakeProjectionFrame(const Mat4f& view, const Mat4f& projection, const Vec3f& position, ProjectionKind kind, bool reverse_z, ProjectionFrame* out);
    /// Vulkan viewport convention: origin is top-left, +screen Y is down. The
    /// engine projection already flips world Y. Offscreen points are not clamped.
    [[nodiscard]] bool Project(const Vec3f& world, const ProjectionFrame& frame, const Viewport& viewport, ProjectedPoint* out);
    [[nodiscard]] bool UnprojectAtDepth(const Vec2f& screen, float depth, const ProjectionFrame& frame, const Viewport& viewport, Vec3f* out);
    [[nodiscard]] bool UnprojectRay(const Vec2f& screen, const ProjectionFrame& frame, const Viewport& viewport, Ray* out);
    [[nodiscard]] bool WorldPerPixel(const ProjectionFrame& frame, const Vec3f& pivot, float viewport_height, float* out);

    /// Direction magnitudes do not affect the answer; parameters are world lengths
    /// along normalized directions. min_denominator/min_cosine are caller policy,
    /// not duplicated editor constants. Parallel/behind-ray solutions are rejected.
    [[nodiscard]] bool ClosestPointOnAxis(const Ray& ray, const Vec3f& axis_origin, const Vec3f& axis_direction, float min_denominator, AxisPoint* out);
    [[nodiscard]] bool RayPlane(const Ray& ray, const Vec3f& plane_point, const Vec3f& normal, float min_cosine, PlaneHit* out);
    [[nodiscard]] bool CaptureTranslation(const TranslationCapture& capture, TranslationReference* out);
    /// Scale the pointer displacement before unprojection (fine control), not
    /// the resulting world displacement. Snap absolute coordinates only on the
    /// constrained basis axes. A return to the press point is an exact no-op.
    /// Off-viewport pointers are allowed; parallel/non-finite results fail without
    /// replacing the caller's last valid sample. No accumulated deltas/inversion.
    [[nodiscard]] bool SampleTranslation(const TranslationReference& reference, const Vec2f& pointer, float motion_scale, bool snap, float snap_step, TranslationSample* out);
    [[nodiscard]] bool CaptureTranslationTarget(const Mat4f& parent_world, const LocalTRS& before, float max_error, TranslationTarget* out);
    [[nodiscard]] bool TranslateLocal(const TranslationTarget& target, const Vec3f& world_delta, LocalTRS* out);
    [[nodiscard]] bool CaptureRotation(const RotationCapture& capture, RotationReference* out);
    /// Fix ray/plane versus tangent at capture. Unwrap plane samples in (-pi, pi]
    /// increments; scale the total angular motion for fine mode before snapping.
    /// Invalid/parallel/central samples preserve both output and previous progress.
    [[nodiscard]] bool SampleRotation(const RotationReference& reference, const RotationProgress& previous, const Vec2f& pointer, float motion_scale, bool snap, double snap_step, RotationSample* out);
    [[nodiscard]] bool CaptureRotationTarget(const Mat4f& parent_world, const LocalTRS& before, const Vec3f& pivot, float max_error, RotationTarget* out);
    /// Rotate the immutable start world frame about one shared world axis/pivot,
    /// then reconstruct local TRS. A local axis transformed to world is equivalent
    /// to postmultiplying the start rotation. Preserve scale signs and zeroes; reject
    /// unrepresentable shear/overflow, rather than replacing the last valid pose.
    [[nodiscard]] bool RotateLocal(const RotationTarget& target, const Vec3f& world_axis, double angle, LocalTRS* out);
    [[nodiscard]] bool MakeRotationDelta(const Vec3f& world_axis, double angle, RotationDelta* out);
    [[nodiscard]] bool RotateLocal(const RotationTarget& target, const RotationDelta& delta, LocalTRS* out);
    /// Apply a world-axis delta to an immutable quaternion without an Euler
    /// round trip. Changed results are unit length; zero/full turns preserve
    /// the original exactly. Invalid inputs preserve output. Sign policy belongs
    /// to the caller's authored-value codec, not the geometry library.
    [[nodiscard]] bool RotateOrientation(const Quatf& before, const Vec3f& world_axis, double angle, Quatf* out);
    [[nodiscard]] bool CaptureScale(const ScaleCapture& capture, ScaleReference* out);
    /// Absolute displacement from the immutable press; no accumulated pose or
    /// per-sample matrix inversion. Fine mode scales pointer motion before ray
    /// construction. The clamped axis denominator retains the original grab
    /// offset, so clicking/returning to the press is exactly ratio 1, even off-snap.
    [[nodiscard]] bool SampleScale(const ScaleReference& reference, const Vec2f& pointer, float motion_scale, bool snap, float snap_step, float* out);
    [[nodiscard]] bool CaptureScaleTarget(const Mat4f& parent_world, const LocalTRS& before, const Vec3f& pivot, float max_error, ScaleTarget* out);
    /// Positive local-axis ratios preserve authored rotation and scale signs,
    /// including zeroes. A custom-pivot position uses captured parent-local
    /// reconstruction. Reject overflow, nonzero-scale underflow or world error.
    [[nodiscard]] bool ScaleLocal(const ScaleTarget& target, const Vec3f& ratios, LocalTRS* out);
    [[nodiscard]] bool CaptureScaleFrame(const Mat4f& basis, float minimum_volume, ScaleFrame* out);
    [[nodiscard]] bool MakeScaleDelta(const ScaleFrame& frame, const Vec3f& ratios, ScaleDelta* out);
    /// Apply B * diag(ratios) * inverse(B) about the captured pivot to a root's
    /// unscaled start frame. Reconstruct all local TRS without dividing by its
    /// authored scale, retaining signs/zeroes; reject shear and world error.
    [[nodiscard]] bool ScaleInFrame(const RotationTarget& target, const ScaleFrame& frame, const Vec3f& ratios, LocalTRS* out);
    [[nodiscard]] bool ScaleInFrame(const RotationTarget& target, const ScaleDelta& delta, LocalTRS* out);
    [[nodiscard]] bool AngleOnPlane(const Vec3f& start, const Vec3f& current, const Vec3f& normal, float* out);
    [[nodiscard]] bool UnwrapAngle(float angle, float previous_angle, float previous_total, float* out);
    [[nodiscard]] bool TangentAngle(const Vec2f& screen_delta, const Vec2f& tangent, float radius, float* out);
    [[nodiscard]] bool DistanceToSegmentSquared(const Vec2f& point, const Vec2f& start, const Vec2f& end, float* out);

    [[nodiscard]] bool Snap(float value, float step, float* out);
    /// Snap one shared anchor, then apply this delta to every selected root.
    [[nodiscard]] bool SnapDelta(float anchor, float delta, float step, float* out);
    [[nodiscard]] bool AxisScaleRatio(float parameter, float start_parameter, float minimum_start_magnitude, float minimum_ratio, float* out);
    [[nodiscard]] bool UniformScaleRatio(const Vec2f& screen_delta, float pixels_per_efold, float minimum_ratio, float* out);

    /// Matches ComposeTransformMatrix's actual Euler convention, including the
    /// closest equivalent solution at gimbal lock. Preserve captured scale signs
    /// when possible; reject singular/non-TRS/sheared results above max_error.
    [[nodiscard]] bool ClosestEuler(const Mat4f& rotation, const Vec3f& reference, float max_error, Vec3f* out);
    [[nodiscard]] bool DecomposeTRS(const Mat4f& local, const LocalTRS& reference, float max_error, LocalTRS* out);
    [[nodiscard]] bool LocalFromWorld(const Mat4f& parent_inverse, const Mat4f& world, const LocalTRS& reference, float max_error, LocalTRS* out);
} // namespace ZEngine::Core::Maths::Geometry
