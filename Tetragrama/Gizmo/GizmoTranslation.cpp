#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <cmath>
#include <limits>

using namespace ZEngine::Core::Maths;
namespace Geometry = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    static_assert(sizeof(Gizmo::TranslationInteraction) <= 1024, "Translation must remain bounded session-local value storage");

    bool EditorSession::BeginTranslation(const Gizmo::TranslationBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || !input.Key || !input.PointerSerial || m_gizmo.Mode != Gizmo::Operation::Translate)
        {
            return false;
        }
        uint8_t axes = 0;
        switch (input.Constraint)
        {
            case Gizmo::Handle::TranslateX:
                axes = 1;
                break;
            case Gizmo::Handle::TranslateY:
                axes = 2;
                break;
            case Gizmo::Handle::TranslateZ:
                axes = 4;
                break;
            case Gizmo::Handle::TranslateYZ:
                axes = 6;
                break;
            case Gizmo::Handle::TranslateZX:
                axes = 5;
                break;
            case Gizmo::Handle::TranslateXY:
                axes = 3;
                break;
            case Gizmo::Handle::TranslateView:
                axes = 7;
                break;
            default:
                return false;
        }
        SynchronizeHierarchy();
        Gizmo::TargetFrame current = {};
        if (!Gizmo::ResolveTargetFrame(*this, current) || !Gizmo::SameTargetFrame(input.Expected, current))
        {
            return Reject("The translation handle no longer matches the selected target frame.");
        }
        Geometry::TranslationCapture capture = {
            .Projection         = input.Projection,
            .Bounds             = input.Bounds,
            .Pointer            = input.Pointer,
            .Anchor             = current.Pivot,
            .AxisMinDenominator = Gizmo::Constants::AxisParallelDenominator,
            .PlaneMinCosine     = Gizmo::Constants::PlaneParallelCosine,
            .MinimumBasisVolume = Gizmo::Constants::FrameAxesMinimumVolume,
            .Axes               = axes,
        };
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            for (uint32_t row = 0; row < 3; ++row)
            {
                capture.Basis(row, axis) = current.Axes[axis][row];
            }
        }
        Gizmo::TranslationInteraction interaction = {.Snap = m_gizmo.TranslationSnap, .Target = current.Target, .Scene = current.Scene, .StartWorld = current.World, .Key = input.Key, .PointerSerial = input.PointerSerial, .Constraint = input.Constraint};
        if (!Geometry::CaptureTranslation(capture, &interaction.Mapping))
        {
            return Reject("The translation constraint or parent frame is not invertible.");
        }
        if (!(current.Scene.Kind == SceneTarget::Grid ? BeginGridSettings() : BeginGizmoTransform(current, Gizmo::Operation::Translate)))
        {
            return false;
        }
        m_translation = interaction;
        return true;
    }

    bool EditorSession::UpdateTranslation(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!m_translation.Key)
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
            return Reject("The translation interaction was cancelled because its target changed.");
        }
        Geometry::TranslationSample sample    = {};
        const bool                  snap      = !fine && (m_translation.Snap.Enabled != invert_snap);
        bool                        previewed = false;
        if (Geometry::SampleTranslation(m_translation.Mapping, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, snap, m_translation.Snap.Increment, &sample))
        {
            if (m_active == Interaction::Grid)
            {
                previewed = PreviewGridTranslation(sample.Anchor);
            }
            else
            {
                bool valid = true;
                for (uint32_t i = 0; i < Batch().Count; ++i)
                {
                    Geometry::LocalTRS candidate = {};
                    if (!Geometry::TranslateLocal(m_gizmo_roots[i].Translation, sample.Delta, &candidate))
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
        }
        // Degenerate/overflowing candidates leave the last valid preview intact.
        // Release still retires capture and commits only that shared authored state.
        if (!pointer_down && Active())
        {
            return Commit();
        }
        return previewed;
    }

    bool EditorSession::ReadTranslationSnapshot(Gizmo::TranslationSnapshot& output)
    {
        if (!OnOwnerThread() || !TranslationActive() || (m_active != Interaction::Transform && m_active != Interaction::Grid) || (m_active == Interaction::Transform && m_hierarchy_pending) || !ValidateActive())
        {
            return false;
        }
        Gizmo::TranslationSnapshot result = {.Target = m_translation.Target, .Scene = m_translation.Scene, .StartWorld = m_translation.StartWorld, .StartPivot = m_translation.Mapping.Anchor, .Key = m_translation.Key, .PointerSerial = m_translation.PointerSerial, .Constraint = m_translation.Constraint};
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
            const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(m_translation.Target.CachedEntity);
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
        for (uint32_t row = 0; row < 3; ++row)
        {
            const double value = double(result.StartPivot[row]) + (double(result.World(row, 3)) - result.StartWorld(row, 3));
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            result.Pivot[row] = float(value);
        }
        for (uint32_t row = 0; row < 3; ++row)
        {
            double value = 0.0;
            for (uint32_t column = 0; column < 3; ++column)
            {
                value += double(m_translation.Mapping.InverseBasis(row, column)) * (double(result.Pivot[column]) - result.StartPivot[column]);
            }
            if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max())
            {
                return false;
            }
            result.Components[row] = float(value);
        }
        output = result;
        return true;
    }
} // namespace Tetragrama
