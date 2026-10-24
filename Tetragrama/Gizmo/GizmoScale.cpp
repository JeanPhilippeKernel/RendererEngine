#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <cmath>
#include <limits>

using namespace ZEngine::Core::Maths;
namespace Geometry = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    static_assert(sizeof(Gizmo::ScaleInteraction) <= 1024, "Scale must remain bounded session-local value storage");

    bool EditorSession::BeginScale(const Gizmo::ScaleBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || m_selection.ActiveSceneTarget() != SceneTarget::None || !input.Key || !input.PointerSerial || m_gizmo.Mode != Gizmo::Operation::Scale || input.Constraint < Gizmo::Handle::ScaleX || input.Constraint > Gizmo::Handle::ScaleUniform)
        {
            return false;
        }
        SynchronizeHierarchy();
        Gizmo::TargetFrame current = {};
        if (!Gizmo::ResolveTargetFrame(*this, current) || !Gizmo::SameTargetFrame(input.Expected, current))
        {
            return Reject("The scale handle no longer matches the selected target frame.");
        }
        const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(current.Target.CachedEntity);
        if (!transform)
        {
            return false;
        }
        const bool     uniform = input.Constraint == Gizmo::Handle::ScaleUniform;
        const uint32_t axis    = uniform ? 0u : uint32_t(input.Constraint) - uint32_t(Gizmo::Handle::ScaleX);
        if (!uniform && std::abs(transform->Scale[axis]) < Gizmo::Constants::MinimumEditableScale)
        {
            return Reject("This scale component is too close to zero for an axis drag.");
        }
        float world_per_pixel = 0.f;
        if (!Geometry::WorldPerPixel(input.Projection, current.Pivot, input.Bounds.Size.y, &world_per_pixel))
        {
            return false;
        }
        const double length = double(world_per_pixel) * Gizmo::Constants::AxisLengthPx;
        if (!std::isfinite(length) || length > std::numeric_limits<float>::max())
        {
            return false;
        }
        const Geometry::ScaleCapture capture = {
            .Projection            = input.Projection,
            .Bounds                = input.Bounds,
            .Pointer               = input.Pointer,
            .Pivot                 = current.Pivot,
            .Axis                  = uniform ? Vec3f{} : current.Axes[axis],
            .HandleLength          = float(length),
            .MinimumGrabFraction   = Gizmo::Constants::AxisScaleMinimumGrabFraction,
            .AxisMinDenominator    = Gizmo::Constants::AxisParallelDenominator,
            .UniformPixelsPerEfold = Gizmo::Constants::UniformScalePixelsPerEfold,
            .MinimumRatio          = Gizmo::Constants::MinimumScaleRatio,
            .Uniform               = uniform,
        };
        Gizmo::ScaleInteraction interaction = {.StartWorld = current.World, .Axes = current.Axes, .AuthoredScale = current.AuthoredScale, .Snap = m_gizmo.ScaleSnap, .Target = current.Target, .HandleLength = float(length), .Key = input.Key, .PointerSerial = input.PointerSerial, .Constraint = input.Constraint, .Group = m_selection.Entities().size() > 1};
        Mat4f                   basis       = Identity<Mat4f>();
        for (uint32_t axis_index = 0; axis_index < 3; ++axis_index)
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                basis(row, axis_index) = current.Axes[axis_index][row];
            }
        }
        if (!Geometry::CaptureScale(capture, &interaction.Mapping) || (interaction.Group && !Geometry::CaptureScaleFrame(basis, Gizmo::Constants::FrameAxesMinimumVolume, &interaction.Frame)))
        {
            return Reject("The scale constraint or parent frame cannot be captured safely.");
        }
        if (!BeginGizmoTransform(current, Gizmo::Operation::Scale))
        {
            return false;
        }
        m_scale = interaction;
        return true;
    }

    bool EditorSession::UpdateScale(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_scale.Key)
        {
            return true;
        }
        if (!focused || cancel || capture_lost)
        {
            Cancel();
            return true;
        }
        if (!ValidateActive())
        {
            Cancel();
            return Reject("The scale interaction was cancelled because its target changed.");
        }
        const bool snap      = !fine && (m_scale.Snap.Enabled != invert_snap);
        float      ratio     = 1.f;
        bool       previewed = false;
        if (Geometry::SampleScale(m_scale.Mapping, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, snap, m_scale.Snap.Increment, &ratio))
        {
            Vec3f ratios(1.f, 1.f, 1.f);
            if (m_scale.Constraint == Gizmo::Handle::ScaleUniform)
            {
                ratios = {ratio, ratio, ratio};
            }
            else
            {
                ratios[uint32_t(m_scale.Constraint) - uint32_t(Gizmo::Handle::ScaleX)] = ratio;
            }
            Geometry::ScaleDelta delta = {};
            bool                 valid = !m_scale.Group || Geometry::MakeScaleDelta(m_scale.Frame, ratios, &delta);
            for (uint32_t i = 0; valid && i < Batch().Count; ++i)
            {
                Geometry::LocalTRS candidate = {};
                if (!(m_scale.Group ? Geometry::ScaleInFrame(m_gizmo_roots[i].Rotation, delta, &candidate) : Geometry::ScaleLocal(m_gizmo_roots[i].Scale, ratios, &candidate)))
                {
                    valid = false;
                    break;
                }
                m_gizmo_values[i] = {candidate.Position, candidate.Rotation, candidate.Scale};
            }
            if (valid)
            {
                previewed = PreviewTransforms({m_gizmo_values, Batch().Count});
                if (previewed)
                {
                    m_scale.Ratio = ratio;
                }
            }
        }
        if (!pointer_down && Active())
        {
            return Commit();
        }
        return previewed;
    }

    bool EditorSession::ReadScaleSnapshot(Gizmo::ScaleSnapshot& output)
    {
        if (!OnOwnerThread() || !ScaleActive() || m_active != Interaction::Transform || m_hierarchy_pending || !ValidateActive())
        {
            return false;
        }
        const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(m_scale.Target.CachedEntity);
        if (!transform)
        {
            return false;
        }
        Gizmo::ScaleSnapshot result = {.Target = m_scale.Target, .StartWorld = m_scale.StartWorld, .World = transform->WorldTransform, .Pivot = m_scale.Mapping.Pivot, .Axes = m_scale.Axes, .AuthoredScale = m_scale.AuthoredScale, .Ratio = m_scale.Ratio, .HandleLength = m_scale.HandleLength, .Key = m_scale.Key, .PointerSerial = m_scale.PointerSerial, .Constraint = m_scale.Constraint};
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                if (!std::isfinite(result.World(row, column)))
                {
                    return false;
                }
            }
        }
        output = result;
        return true;
    }
} // namespace Tetragrama
