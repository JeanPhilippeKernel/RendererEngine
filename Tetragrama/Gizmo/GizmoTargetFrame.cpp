#include <Tetragrama/EditorSession.h>
#include <Tetragrama/Gizmo/GizmoTargetFrame.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <cmath>

using namespace ZEngine::Core::Maths;

namespace Tetragrama::Gizmo
{
    bool ResolveTargetFrame(EditorSession& session, TargetFrame& output)
    {
        output = {};
        if (!session.OnOwnerThread())
        {
            return false;
        }
        const auto& selection = session.Selection();
        if (!session.Editable())
        {
            return false;
        }
        if (selection.ActiveSceneTarget() == SceneTarget::Grid)
        {
            EditorGridView                  view      = {};
            TargetFrame                     result    = {};
            ZEngine::ECS::SceneGridSettings canonical = {};
            if (!session.ReadGridSettings(view) || !view.Editable || !ZEngine::ECS::EncodeSceneGridBinary(view.Value, result.Grid) || !ZEngine::ECS::DecodeSceneGridBinary(result.Grid, canonical))
            {
                return false;
            }
            result.Scene         = {.UUID = view.SceneUUID, .Epoch = session.SceneEpoch(), .Kind = SceneTarget::Grid};
            result.World         = quaternionToMat4(canonical.Orientation);
            result.Pivot         = canonical.Origin;
            result.AuthoredScale = {1.f, 1.f, 1.f};
            // U x V = -N. Keep the quaternion's right-handed X/Y/Z basis as
            // U/N/V, not U/V/N; the same basis drives handles, snap and pills.
            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                result.World(axis, 3) = canonical.Origin[axis];
                result.Axes[axis]     = {result.World(0, axis), result.World(1, axis), result.World(2, axis)};
            }
            result.SelectionRevision = selection.Revision();
            result.SettingsRevision  = session.GizmoSettings().Revision;
            result.Valid             = true;
            output                   = result;
            return true;
        }
        if (selection.ActiveSceneTarget() != SceneTarget::None || selection.Entities().empty())
        {
            return false;
        }
        EntityTarget target = {};
        const auto*  scene  = session.Scene();
        if (!scene)
        {
            return false;
        }
        for (const auto& selected : selection.Entities())
        {
            const auto  entity    = session.Resolve(selected);
            const auto* transform = scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(entity);
            const auto* node      = scene->Hierarchy().Find(entity);
            if (!entity.IsValid() || entity != selected.CachedEntity || !node || !node->FrameValid || !transform || !ZEngine::ECS::ValidLocalTransform(ZEngine::ECS::CaptureLocalTransform(*transform)))
            {
                return false;
            }
            if (selected.UUID == selection.ActiveUUID())
            {
                target = selected;
            }
        }
        const auto entity = session.Resolve(target);
        if (!scene || !scene->IsAlive(entity))
        {
            return false;
        }
        const auto* node      = scene->Hierarchy().Find(entity);
        const auto* transform = scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(entity);
        if (!node || !node->FrameValid || !transform)
        {
            return false;
        }
        TargetFrame result         = {.Target = target, .World = transform->WorldTransform, .AuthoredScale = transform->Scale};
        result.Target.CachedEntity = entity;
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
        result.Pivot        = {result.World(0, 3), result.World(1, 3), result.World(2, 3)};
        result.CustomPivot  = session.ReadCustomPivot(result.Pivot);
        // Collapsed authored axes still need an orientation for their disabled
        // cubes. Do not invert/divide by a zero scale or replace the whole frame
        // with World axes. The nearest transform ancestor also covers grouping
        // entities that have no TransformComponent themselves.
        Mat4f      unscaled = {};
        const bool local    = session.GizmoSettings().EffectiveSpace() == TransformSpacePolicy::Local;
        if (local && (std::abs(transform->Scale.x) < Constants::MinimumEditableScale || std::abs(transform->Scale.y) < Constants::MinimumEditableScale || std::abs(transform->Scale.z) < Constants::MinimumEditableScale))
        {
            unscaled = ComposeTransformMatrix({}, transform->Rotation, {1.f, 1.f, 1.f});
            if (node->ParentTransform != ZEngine::ECS::SceneHierarchy::NoIndex)
            {
                const auto  nodes  = scene->Hierarchy().Nodes();
                const auto* parent = node->ParentTransform < nodes.size() ? scene->GetComponent<ZEngine::ECS::Components::TransformComponent>(nodes[node->ParentTransform].Entity) : nullptr;
                if (!parent)
                {
                    return false;
                }
                unscaled = parent->WorldTransform * unscaled;
            }
        }
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (session.GizmoSettings().EffectiveSpace() == TransformSpacePolicy::World)
            {
                result.Axes[axis][axis] = 1.f;
            }
            else if (local && std::abs(transform->Scale[axis]) < Constants::MinimumEditableScale)
            {
                const float sign  = std::signbit(transform->Scale[axis]) ? -1.f : 1.f;
                result.Axes[axis] = Vec3f(unscaled(0, axis), unscaled(1, axis), unscaled(2, axis)) * sign;
            }
            else
            {
                result.Axes[axis] = {result.World(0, axis), result.World(1, axis), result.World(2, axis)};
            }
        }
        result.SelectionRevision  = selection.Revision();
        result.StructuralRevision = scene->StructuralRevision();
        result.HierarchyRevision  = scene->Hierarchy().Revision();
        result.WorldRevision      = node->WorldRevision;
        result.SettingsRevision   = session.GizmoSettings().Revision;
        result.Valid              = true;
        output                    = result;
        return true;
    }

    bool SameTargetFrame(const TargetFrame& a, const TargetFrame& b)
    {
        if (!a.Valid || !b.Valid || a.Scene != b.Scene || (a.Scene.Kind == SceneTarget::Grid && a.Grid != b.Grid) || a.Target.UUID != b.Target.UUID || a.Target.SceneEpoch != b.Target.SceneEpoch || a.Target.CachedEntity != b.Target.CachedEntity || a.SelectionRevision != b.SelectionRevision || a.StructuralRevision != b.StructuralRevision || a.HierarchyRevision != b.HierarchyRevision || a.WorldRevision != b.WorldRevision || a.SettingsRevision != b.SettingsRevision || a.CustomPivot != b.CustomPivot || a.Pivot.x != b.Pivot.x || a.Pivot.y != b.Pivot.y ||
            a.Pivot.z != b.Pivot.z)
        {
            return false;
        }
        for (uint32_t row = 0; row < 4; ++row)
        {
            for (uint32_t column = 0; column < 4; ++column)
            {
                if (a.World(row, column) != b.World(row, column))
                {
                    return false;
                }
            }
        }
        // A fully collapsed matrix can stay identical while its authored local
        // orientation changes. Compare the actual presentation inputs as well.
        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (a.AuthoredScale[axis] != b.AuthoredScale[axis] || std::signbit(a.AuthoredScale[axis]) != std::signbit(b.AuthoredScale[axis]))
            {
                return false;
            }
            for (uint32_t row = 0; row < 3; ++row)
            {
                if (a.Axes[axis][row] != b.Axes[axis][row])
                {
                    return false;
                }
            }
        }
        return true;
    }
} // namespace Tetragrama::Gizmo
