#include <Tetragrama/EditorSession.h>
#include <ZEngine/ECS/Components/MeshComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <algorithm>
#include <memory>
#include <type_traits>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;
using namespace ZEngine::Core::Maths;
namespace Geometry = ZEngine::Core::Maths::Geometry;

namespace Tetragrama
{
    bool EditorSession::BeginGizmoTransform(const Gizmo::TargetFrame& frame, Gizmo::Operation operation)
    {
        static_assert(std::is_trivially_copyable_v<Gizmo::TransformRoot> && std::is_trivially_destructible_v<Gizmo::TransformRoot>);
        static_assert(sizeof(Gizmo::TransformRoot) <= 512 && sizeof(Gizmo::TransformObserver) <= 96, "Keep bounded drag workspace compact");
        if (!Allowed() || Active() || !frame.Valid || frame.Scene.Kind != SceneTarget::None || (operation != Gizmo::Operation::Translate && operation != Gizmo::Operation::Rotate && operation != Gizmo::Operation::Scale))
        {
            return false;
        }
        ZEngine::Core::Containers::ArrayView<const EntityTarget> roots = {};
        if (!ReadSelectedRoots(roots) || roots.empty() || roots.size() > m_max_targets || !m_history.CanStore(sizeof(TransformBatchHeader) + roots.size() * sizeof(TransformChange)))
        {
            return Reject("The complete selected-root set cannot be captured in the transaction budget.");
        }
        const auto nodes       = m_scene->Hierarchy().Nodes();
        const bool group_scale = operation == Gizmo::Operation::Scale && m_selection.Entities().size() > 1;
        m_unique.clear();
        for (uint32_t i = 0; i < roots.size(); ++i)
        {
            const auto  entity    = m_targets.Resolve(roots[i]);
            const auto* transform = m_scene->GetComponent<TransformComponent>(entity);
            const auto* node      = m_scene->Hierarchy().Find(entity);
            if (!transform || !node || !node->FrameValid || entity != roots[i].CachedEntity)
            {
                return Reject("Every selected root must retain its original editable transform.");
            }
            Mat4f parent = Identity<Mat4f>();
            if (node->ParentTransform != SceneHierarchy::NoIndex)
            {
                const auto* ancestor = node->ParentTransform < nodes.size() ? m_scene->GetComponent<TransformComponent>(nodes[node->ParentTransform].Entity) : nullptr;
                if (!ancestor)
                {
                    return false;
                }
                parent = ancestor->WorldTransform;
            }
            const auto               value  = CaptureLocalTransform(*transform);
            const Geometry::LocalTRS before = {value.Position, value.Rotation, value.Scale};
            if (operation == Gizmo::Operation::Translate)
            {
                Geometry::TranslationTarget capture = {};
                if (!Geometry::CaptureTranslationTarget(parent, before, Gizmo::Constants::ParentLocalMaxError, &capture))
                {
                    return Reject("A selected root's translation parent frame is not invertible.");
                }
                std::construct_at(m_gizmo_roots + i, Gizmo::TransformRoot{.Translation = capture});
            }
            else if (operation == Gizmo::Operation::Rotate || group_scale)
            {
                Geometry::RotationTarget capture = {};
                if (!Geometry::CaptureRotationTarget(parent, before, frame.Pivot, Gizmo::Constants::ParentLocalMaxError, &capture))
                {
                    return Reject("A selected root's shared affine frame cannot be captured safely.");
                }
                std::construct_at(m_gizmo_roots + i, Gizmo::TransformRoot{.Rotation = capture});
            }
            else
            {
                Geometry::ScaleTarget capture = {};
                if (!Geometry::CaptureScaleTarget(parent, before, frame.Pivot, Gizmo::Constants::ParentLocalMaxError, &capture))
                {
                    return Reject("The scale target's parent frame cannot be captured safely.");
                }
                std::construct_at(m_gizmo_roots + i, Gizmo::TransformRoot{.Scale = capture});
            }
            std::construct_at(m_gizmo_values + i, value);
            m_unique.insert(roots[i].UUID, i);
        }
        uint32_t observers = 0;
        for (const auto& target : m_selection.Entities())
        {
            if (m_unique.contains(target.UUID))
            {
                continue;
            }
            const auto  entity    = m_targets.Resolve(target);
            const auto* transform = m_scene->GetComponent<TransformComponent>(entity);
            const auto* node      = m_scene->Hierarchy().Find(entity);
            if (!transform || !node || !node->FrameValid || entity != target.CachedEntity)
            {
                return Reject("A pruned selected descendant no longer has an editable transform.");
            }
            const auto before = CaptureLocalTransform(*transform);
            if (!ValidLocalTransform(before))
            {
                return false;
            }
            std::construct_at(
                m_gizmo_observers + observers++,
                Gizmo::TransformObserver{
                target, {before.Position, before.Rotation, before.Scale}
            });
        }
        if (!BeginTransform({roots.data(), roots.size()}, frame.Pivot, operation == Gizmo::Operation::Scale ? TransformSpacePolicy::Local : m_gizmo.EffectiveSpace()))
        {
            return false;
        }
        m_gizmo_observer_count = observers;
        // Latch the visual start matrix and mesh binding at pointer-down, even
        // when the first overlay is delayed. The active root wins decoration
        // pressure; a pruned active child does not become an extra authored root.
        const auto*    active  = m_unique.find(frame.Target.UUID);
        const uint32_t count   = std::min(uint32_t(roots.size()), Gizmo::RootFrameView::Capacity);
        uint32_t       next    = 0;
        for (uint32_t i = 0; i < count; ++i)
        {
            uint32_t index = 0;
            if (active && i == 0)
            {
                index = *active;
            }
            else
            {
                if (active && next == *active)
                {
                    ++next;
                }
                index = next++;
            }
            const auto* transform  = m_scene->GetComponent<TransformComponent>(roots[index].CachedEntity);
            const auto* mesh       = m_scene->GetComponent<MeshComponent>(roots[index].CachedEntity);
            m_gizmo_root_frames[i] = {.Target = roots[index], .StartWorld = transform->WorldTransform, .MeshUUID = mesh ? mesh->MeshUUID : uuids::uuid{}};
        }
        m_gizmo_root_frame_count = count;
        if (++m_gizmo_root_generation == 0)
        {
            ++m_gizmo_root_generation;
        }
        return true;
    }

    bool EditorSession::ReadGizmoRootFrames(Gizmo::RootFrameView& output)
    {
        if (!OnOwnerThread() || m_active != Interaction::Transform || (!TranslationActive() && !RotationActive() && !ScaleActive()) || m_hierarchy_pending || !m_gizmo_root_frame_count || !ValidateActive())
        {
            return false;
        }
        output = {
            .Frames = {m_gizmo_root_frames.data(), m_gizmo_root_frame_count},
              .Generation = m_gizmo_root_generation, .TotalRoots = Batch().Count
        };
        return true;
    }

    bool EditorSession::ValidateGizmoObservers()
    {
        for (uint32_t i = 0; i < m_gizmo_observer_count; ++i)
        {
            const auto& observer  = m_gizmo_observers[i];
            const auto  entity    = m_targets.Resolve(observer.Target);
            const auto* transform = m_scene->GetComponent<TransformComponent>(entity);
            const auto* node      = m_scene->Hierarchy().Find(entity);
            if (entity != observer.Target.CachedEntity || !transform || !node || !node->FrameValid || !SameLocalTransform(CaptureLocalTransform(*transform), {observer.Before.Position, observer.Before.Rotation, observer.Before.Scale}))
            {
                return false;
            }
        }
        return true;
    }
} // namespace Tetragrama
