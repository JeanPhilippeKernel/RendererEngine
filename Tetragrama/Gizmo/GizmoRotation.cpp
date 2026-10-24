#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <cmath>
#include <numbers>

using namespace ZEngine::Core::Maths;
namespace Geometry = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    static_assert(sizeof(Gizmo::RotationInteraction) <= 1024, "Rotation must remain bounded session-local value storage");

    bool EditorSession::BeginRotation(const Gizmo::RotationBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || !input.Key || !input.PointerSerial || m_gizmo.Mode != Gizmo::Operation::Rotate || input.Constraint < Gizmo::Handle::RotateX || input.Constraint > Gizmo::Handle::RotateView)
        {
            return false;
        }
        SynchronizeHierarchy();
        Gizmo::TargetFrame current = {};
        if (!Gizmo::ResolveTargetFrame(*this, current) || !Gizmo::SameTargetFrame(input.Expected, current))
        {
            return Reject("The rotation ring no longer matches the selected target frame.");
        }
        const bool                      view    = input.Constraint == Gizmo::Handle::RotateView;
        const Vec3f                     axis    = view ? -input.Projection.Forward : current.Axes[uint32_t(input.Constraint) - uint32_t(Gizmo::Handle::RotateX)];
        const Geometry::RotationCapture capture = {
            .Projection            = input.Projection,
            .Bounds                = input.Bounds,
            .Pointer               = input.Pointer,
            .Pivot                 = current.Pivot,
            .Axis                  = axis,
            .RingPoint             = input.RingPoint,
            .RadiusPx              = view ? Gizmo::Constants::ViewRingRadiusPx : Gizmo::Constants::RingRadiusPx,
            .TangentFallbackCosine = Gizmo::Constants::RotateTangentFallbackCosine,
            .PlaneMinCosine        = Gizmo::Constants::PlaneParallelCosine,
        };
        Gizmo::RotationInteraction interaction = {.StartWorld = current.World, .Snap = m_gizmo.RotationSnap, .Target = current.Target, .Scene = current.Scene, .Key = input.Key, .PointerSerial = input.PointerSerial, .Constraint = input.Constraint};
        if (!Geometry::CaptureRotation(capture, &interaction.Mapping))
        {
            return Reject("The rotation ring or parent frame cannot be captured safely.");
        }
        if (!(current.Scene.Kind == SceneTarget::Grid ? BeginGridSettings() : BeginGizmoTransform(current, Gizmo::Operation::Rotate)))
        {
            return false;
        }
        m_rotation = interaction;
        return true;
    }

    bool EditorSession::UpdateRotation(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_rotation.Key)
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
            return Reject("The rotation interaction was cancelled because its target changed.");
        }
        const bool               snap      = !fine && (m_rotation.Snap.Enabled != invert_snap);
        const double             radians   = double(m_rotation.Snap.Increment) * Gizmo::Constants::RadiansPerDegree;
        Geometry::RotationSample sample    = {};
        bool                     previewed = false;
        if (Geometry::SampleRotation(m_rotation.Mapping, m_rotation.Progress, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, snap, radians, &sample))
        {
            // Keep following valid pointer angles even when a parent-space TRS
            // candidate is rejected, so crossing pi cannot reverse later motion.
            // Presentation advances only after the shared preview is accepted.
            m_rotation.Progress = sample.Progress;
            if (m_active == Interaction::Grid)
            {
                previewed = PreviewGridRotation(m_rotation.Mapping.Axis, sample.Angle);
            }
            else
            {
                Geometry::RotationDelta delta = {};
                bool                    valid = Geometry::MakeRotationDelta(m_rotation.Mapping.Axis, sample.Angle, &delta);
                for (uint32_t i = 0; valid && i < Batch().Count; ++i)
                {
                    Geometry::LocalTRS candidate = {};
                    if (!Geometry::RotateLocal(m_gizmo_roots[i].Rotation, delta, &candidate))
                    {
                        valid = false;
                        break;
                    }
                    m_gizmo_values[i] = {candidate.Position, candidate.Rotation, candidate.Scale};
                }
                if (valid)
                {
                    previewed = PreviewTransforms({m_gizmo_values, Batch().Count});
                }
            }
            if (previewed)
            {
                m_rotation.Angle = sample.Angle;
            }
        }
        if (!pointer_down && Active())
        {
            return Commit();
        }
        return previewed;
    }

    bool EditorSession::ReadRotationSnapshot(Gizmo::RotationSnapshot& output)
    {
        if (!OnOwnerThread() || !RotationActive() || (m_active != Interaction::Transform && m_active != Interaction::Grid) || (m_active == Interaction::Transform && m_hierarchy_pending) || !ValidateActive())
        {
            return false;
        }
        Gizmo::RotationSnapshot result = {.Target = m_rotation.Target, .Scene = m_rotation.Scene, .StartWorld = m_rotation.StartWorld, .Pivot = m_rotation.Mapping.Pivot, .Axis = m_rotation.Mapping.Axis, .StartDirection = m_rotation.Mapping.StartDirection, .Angle = m_rotation.Angle, .Key = m_rotation.Key, .PointerSerial = m_rotation.PointerSerial, .Constraint = m_rotation.Constraint};
        if (m_active == Interaction::Grid)
        {
            Gizmo::TargetFrame current = {};
            if (!Gizmo::ResolveTargetFrame(*this, current) || current.Scene != result.Scene)
            {
                return false;
            }
            result.World = current.World;
        }
        else
        {
            const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(m_rotation.Target.CachedEntity);
            if (!transform)
            {
                return false;
            }
            result.World = transform->WorldTransform;
        }
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
