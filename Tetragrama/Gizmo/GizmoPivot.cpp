#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/TransformComponent.h>

using namespace ZEngine::Core::Maths;
namespace Geometry = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    bool EditorSession::ReadCustomPivot(Vec3f& output) const
    {
        if (!OnOwnerThread() || !m_custom_pivot.Valid || m_custom_pivot.SceneEpoch != m_epoch || m_custom_pivot.SelectionRevision != m_selection.Revision())
        {
            return false;
        }
        output = m_custom_pivot.Position;
        return true;
    }

    void EditorSession::SetCustomPivot(const Gizmo::PivotState& value)
    {
        const auto& old = m_custom_pivot;
        if (old.Valid == value.Valid && old.SelectionRevision == value.SelectionRevision && old.SceneEpoch == value.SceneEpoch && old.Position.x == value.Position.x && old.Position.y == value.Position.y && old.Position.z == value.Position.z)
        {
            return;
        }
        m_custom_pivot = value;
        if (++m_gizmo.Revision == 0)
        {
            ++m_gizmo.Revision;
        }
    }

    bool EditorSession::ClearCustomPivot()
    {
        if (!Allowed() || Active() || !m_custom_pivot.Valid)
        {
            return false;
        }
        SetCustomPivot({});
        return true;
    }

    bool EditorSession::BeginPivot(const Gizmo::PivotBegin& input)
    {
        if (!Allowed() || !Editable() || Active() || m_selection.ActiveSceneTarget() != SceneTarget::None || !input.Key || !input.PointerSerial || m_gizmo.Mode == Gizmo::Operation::Select)
        {
            return false;
        }
        SynchronizeHierarchy();
        Gizmo::TargetFrame frame = {};
        if (!Gizmo::ResolveTargetFrame(*this, frame) || !Gizmo::SameTargetFrame(input.Expected, frame))
        {
            return Reject("The pivot handle no longer matches the selected target frame.");
        }
        const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(frame.Target.CachedEntity);
        if (!transform)
        {
            return false;
        }
        const Geometry::TranslationCapture capture = {
            .Projection         = input.Projection,
            .Bounds             = input.Bounds,
            .Pointer            = input.Pointer,
            .Anchor             = frame.Pivot,
            .AxisMinDenominator = Gizmo::Constants::AxisParallelDenominator,
            .PlaneMinCosine     = Gizmo::Constants::PlaneParallelCosine,
            .MinimumBasisVolume = Gizmo::Constants::FrameAxesMinimumVolume,
            .Axes               = 7,
        };
        Gizmo::PivotInteraction interaction = {
            .Frame               = frame,
            .Pose                = ZEngine::ECS::CaptureLocalTransform(*transform),
            .Before              = m_custom_pivot,
            .Position            = frame.Pivot,
            .Snap                = m_gizmo.TranslationSnap,
            .Key                 = input.Key,
            .PointerSerial       = input.PointerSerial,
            .ParentWorldRevision = m_scene->Hierarchy().ParentWorldRevision(frame.Target.CachedEntity),
        };
        if (!Geometry::CaptureTranslation(capture, &interaction.Mapping))
        {
            return Reject("The pivot's captured view plane cannot be resolved safely.");
        }
        m_pivot = interaction;
        StartInteraction(Interaction::Pivot);
        return true;
    }

    bool EditorSession::ValidatePivot()
    {
        const auto& target = m_pivot.Frame.Target;
        const auto  entity = m_targets.Resolve(target);
        if (entity != target.CachedEntity || !m_scene->IsAlive(entity))
        {
            return false;
        }
        const auto* transform = m_scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(entity);
        const auto* frame     = m_scene->Hierarchy().Find(entity);
        return transform && frame && frame->FrameValid && frame->WorldRevision == m_pivot.Frame.WorldRevision && m_scene->Hierarchy().ParentWorldRevision(entity) == m_pivot.ParentWorldRevision && ZEngine::ECS::SameLocalTransform(ZEngine::ECS::CaptureLocalTransform(*transform), m_pivot.Pose);
    }

    bool EditorSession::UpdatePivot(const Vec2f& pointer, bool pointer_down, bool fine, bool invert_snap, bool focused, bool cancel, bool capture_lost)
    {
        if (!Allowed())
        {
            return false;
        }
        if (!PivotActive())
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
            return Reject("The pivot edit was cancelled because its target changed.");
        }
        Geometry::TranslationSample sample   = {};
        const bool                  snap     = !fine && (m_pivot.Snap.Enabled != invert_snap);
        const bool                  accepted = Geometry::SampleTranslation(m_pivot.Mapping, pointer, fine ? Gizmo::Constants::FineMotionFactor : 1.f, snap, m_pivot.Snap.Increment, &sample);
        if (accepted)
        {
            m_pivot.Position = sample.Anchor;
            if (sample.Delta.x == 0.f && sample.Delta.y == 0.f && sample.Delta.z == 0.f)
            {
                SetCustomPivot(m_pivot.Before);
            }
            else
            {
                SetCustomPivot({.Position = sample.Anchor, .SelectionRevision = m_selection.Revision(), .SceneEpoch = m_epoch, .Valid = true});
            }
        }
        // A malformed/parallel release still retires at the last valid point.
        return !pointer_down ? Commit() : accepted;
    }

    bool EditorSession::ReadPivotSnapshot(Gizmo::PivotSnapshot& output)
    {
        if (!OnOwnerThread() || !PivotActive() || m_hierarchy_pending || !ValidateActive())
        {
            return false;
        }
        const auto& world = m_pivot.Frame.World;
        output            = {
            .Target = m_pivot.Frame.Target, .Position = m_pivot.Position, .Origin = {world(0, 3), world(1, 3), world(2, 3)},
                    .Key = m_pivot.Key, .PointerSerial = m_pivot.PointerSerial
        };
        return true;
    }
} // namespace Tetragrama
