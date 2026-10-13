#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/Systems/HierarchySystem.h>
#include <ZEngine/ECS/Tools/TransformEditSession.h>
#include <cmath>

using namespace ZEngine::Core::Containers;
using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS::Components;

namespace ZEngine::ECS::Tools
{
    namespace
    {
        [[nodiscard]] GizmoLocalTransform ToLocalTransform(const TransformComponent& transform)
        {
            return {
                .Position = transform.Position,
                .Rotation = transform.Rotation,
                .Scale    = transform.Scale,
            };
        }

        void ApplyLocalTransform(TransformComponent& transform, const GizmoLocalTransform& local)
        {
            transform.Position = local.Position;
            transform.Rotation = local.Rotation;
            transform.Scale    = local.Scale;
        }

        [[nodiscard]] bool SameMatrix(const Mat4f& left, const Mat4f& right, float tolerance = 1.0e-4f)
        {
            for (size_t row = 0; row < 4; ++row)
            {
                for (size_t column = 0; column < 4; ++column)
                {
                    if (!std::isfinite(left(row, column)) || !std::isfinite(right(row, column)))
                        return false;
                    const float scale = std::fmax(1.f, std::fmax(std::fabs(left(row, column)), std::fabs(right(row, column))));
                    if (std::fabs(left(row, column) - right(row, column)) > tolerance * scale)
                        return false;
                }
            }
            return true;
        }

        [[nodiscard]] bool SameLocalTransform(const GizmoLocalTransform& left, const GizmoLocalTransform& right)
        {
            return left.Position.x == right.Position.x && left.Position.y == right.Position.y && left.Position.z == right.Position.z && left.Rotation.x == right.Rotation.x && left.Rotation.y == right.Rotation.y && left.Rotation.z == right.Rotation.z && left.Scale.x == right.Scale.x && left.Scale.y == right.Scale.y && left.Scale.z == right.Scale.z;
        }
    } // namespace

    void TransformEditHistory::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t maximum_commands, uint32_t maximum_records)
    {
        m_commands.init(arena, maximum_commands);
        m_records.init(arena, maximum_records);
        m_cursor = 0;
    }

    bool TransformEditHistory::CanApply(const CommandRange& command, Scene& scene) const
    {
        if (command.RecordCount == 0 || command.RecordOffset > m_records.size() || command.RecordCount > m_records.size() - command.RecordOffset)
            return false;

        for (uint32_t i = 0; i < command.RecordCount; ++i)
        {
            const TransformEditRecord& record = m_records[command.RecordOffset + i];
            if (!scene.IsAlive(record.Entity) || !scene.HasComponent<TransformComponent>(record.Entity))
                return false;
        }
        return true;
    }

    void TransformEditHistory::Apply(const CommandRange& command, Scene& scene, bool use_after) const
    {
        for (uint32_t i = 0; i < command.RecordCount; ++i)
        {
            const TransformEditRecord& record    = m_records[command.RecordOffset + i];
            TransformComponent*        transform = scene.GetComponent<TransformComponent>(record.Entity);
            ApplyLocalTransform(*transform, use_after ? record.After : record.Before);
        }
        Systems::SyncHierarchy(scene);
    }

    void TransformEditHistory::DiscardRedo()
    {
        if (m_cursor == m_commands.size())
            return;

        const uint32_t redo_offset = m_commands[m_cursor].RecordOffset;
        m_records.m_size           = redo_offset;
        m_commands.m_size          = m_cursor;
    }

    void TransformEditHistory::DiscardOldestCommand()
    {
        if (m_commands.empty())
            return;

        const uint32_t removed_records = m_commands[0].RecordCount;
        for (size_t index = removed_records; index < m_records.size(); ++index)
            m_records[index - removed_records] = m_records[index];
        m_records.m_size -= removed_records;

        m_commands.erase(0);
        for (CommandRange& command : m_commands)
            command.RecordOffset -= removed_records;
        if (m_cursor > 0)
            --m_cursor;
    }

    bool TransformEditHistory::Push(const TransformEditRecord* records, uint32_t record_count)
    {
        if (!records || record_count == 0 || record_count > m_records.capacity() || m_commands.capacity() == 0)
            return false;

        DiscardRedo();
        while (!m_commands.empty() && (m_commands.size() == m_commands.capacity() || m_records.size() + record_count > m_records.capacity()))
            DiscardOldestCommand();

        if (m_commands.size() == m_commands.capacity() || m_records.size() + record_count > m_records.capacity())
            return false;

        CommandRange command = {
            .RecordOffset = static_cast<uint32_t>(m_records.size()),
            .RecordCount  = record_count,
        };
        for (uint32_t i = 0; i < record_count; ++i)
            m_records.push(records[i]);
        m_commands.push(command);
        m_cursor = static_cast<uint32_t>(m_commands.size());
        return true;
    }

    bool TransformEditHistory::Undo(Scene& scene)
    {
        if (!CanUndo())
            return false;

        const CommandRange& command = m_commands[m_cursor - 1];
        if (!CanApply(command, scene))
            return false;
        Apply(command, scene, false);
        --m_cursor;
        return true;
    }

    bool TransformEditHistory::Redo(Scene& scene)
    {
        if (!CanRedo())
            return false;

        const CommandRange& command = m_commands[m_cursor];
        if (!CanApply(command, scene))
            return false;
        Apply(command, scene, true);
        ++m_cursor;
        return true;
    }

    bool TransformEditHistory::CanUndo() const
    {
        return m_cursor > 0;
    }

    bool TransformEditHistory::CanRedo() const
    {
        return m_cursor < m_commands.size();
    }

    uint32_t TransformEditHistory::UndoCount() const
    {
        return m_cursor;
    }

    uint32_t TransformEditHistory::RedoCount() const
    {
        return static_cast<uint32_t>(m_commands.size()) - m_cursor;
    }

    void TransformEditHistory::Clear()
    {
        m_commands.clear();
        m_records.clear();
        m_cursor = 0;
    }

    void TransformEditSession::Initialize(Core::Memory::ArenaAllocator* arena, uint32_t maximum_selection, uint32_t maximum_history_commands, uint32_t maximum_history_records)
    {
        m_selection.init(arena, maximum_selection);
        m_targets.init(arena, maximum_selection);
        m_staged.init(arena, maximum_selection);
        m_view.init(arena, maximum_selection);
        m_history.Initialize(arena, maximum_history_commands, maximum_history_records);
        m_active              = INVALID_ENTITY;
        m_is_active           = false;
        m_last_commit_changed = false;
    }

    bool TransformEditSession::SetSelection(Scene& scene, ArrayView<const EntityID> entities, EntityID active)
    {
        if (m_is_active || entities.size() > m_selection.capacity())
            return false;

        m_selection.clear();
        for (size_t i = 0; i < entities.size(); ++i)
        {
            const EntityID entity = entities[i];
            if (!scene.IsAlive(entity) || !scene.HasComponent<TransformComponent>(entity))
                continue;

            bool duplicate = false;
            for (const EntityID selected : m_selection)
            {
                if (selected == entity)
                {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate)
                m_selection.push(entity);
        }

        m_active = Contains(active) ? active : (m_selection.empty() ? INVALID_ENTITY : m_selection.front());
        return true;
    }

    bool TransformEditSession::AddToSelection(Scene& scene, EntityID entity, bool make_active)
    {
        if (m_is_active || !scene.IsAlive(entity) || !scene.HasComponent<TransformComponent>(entity))
            return false;

        if (Contains(entity))
        {
            if (make_active)
                m_active = entity;
            return true;
        }
        if (m_selection.size() == m_selection.capacity())
            return false;

        m_selection.push(entity);
        if (make_active || !m_active.IsValid())
            m_active = entity;
        return true;
    }

    bool TransformEditSession::RemoveFromSelection(EntityID entity)
    {
        if (m_is_active)
            return false;

        for (size_t i = 0; i < m_selection.size(); ++i)
        {
            if (m_selection[i] != entity)
                continue;
            m_selection.erase(i);
            if (m_active == entity)
                m_active = m_selection.empty() ? INVALID_ENTITY : m_selection.front();
            return true;
        }
        return false;
    }

    bool TransformEditSession::ToggleSelection(Scene& scene, EntityID entity)
    {
        return Contains(entity) ? RemoveFromSelection(entity) : AddToSelection(scene, entity, true);
    }

    void TransformEditSession::ClearSelection()
    {
        if (m_is_active)
            return;
        m_selection.clear();
        m_active = INVALID_ENTITY;
    }

    ArrayView<const EntityID> TransformEditSession::Selection() const
    {
        return {m_selection.data(), m_selection.size()};
    }

    EntityID TransformEditSession::ActiveEntity() const
    {
        return m_active;
    }

    bool TransformEditSession::Contains(EntityID entity) const
    {
        for (const EntityID selected : m_selection)
            if (selected == entity)
                return true;
        return false;
    }

    bool TransformEditSession::HasSelectedAncestor(Scene& scene, EntityID entity) const
    {
        EntityID current = entity;
        for (uint32_t depth = 0; depth < scene.AliveCount(); ++depth)
        {
            const ParentComponent* parent = scene.GetComponent<ParentComponent>(current);
            if (!parent || !parent->Parent.IsValid() || !scene.IsAlive(parent->Parent) || !scene.HasComponent<TransformComponent>(parent->Parent))
                return false;
            current = parent->Parent;
            if (Contains(current))
                return true;
        }
        // A parent loop cannot be represented as a safe root manipulation.
        return true;
    }

    bool TransformEditSession::Begin(Scene& scene)
    {
        if (m_is_active || m_selection.empty())
            return false;

        m_last_commit_changed = false;
        Systems::SyncHierarchy(scene);
        m_targets.clear();
        m_staged.clear();
        m_view.clear();
        for (const EntityID entity : m_selection)
        {
            if (!scene.IsAlive(entity) || !scene.HasComponent<TransformComponent>(entity))
            {
                EndTransaction();
                return false;
            }
            if (HasSelectedAncestor(scene, entity))
                continue;

            const TransformComponent* transform = scene.GetComponent<TransformComponent>(entity);
            ActiveTarget&             target    = m_targets.push_use({});
            target.Record.Entity                = entity;
            target.Record.Before                = ToLocalTransform(*transform);
            target.Record.After                 = target.Record.Before;
            target.InitialWorld                 = transform->WorldTransform;

            const ParentComponent* parent       = scene.GetComponent<ParentComponent>(entity);
            if (parent && parent->Parent.IsValid() && scene.IsAlive(parent->Parent) && scene.HasComponent<TransformComponent>(parent->Parent))
            {
                const TransformComponent* parent_transform = scene.GetComponent<TransformComponent>(parent->Parent);
                target.CapturedParentWorld                 = parent_transform->WorldTransform;
                target.HasCapturedParent                   = true;
            }
        }

        if (m_targets.empty())
        {
            EndTransaction();
            return false;
        }

        m_staged.m_size = m_targets.size();
        for (const ActiveTarget& target : m_targets)
            m_view.push(target.Record);
        m_is_active = true;
        return true;
    }

    bool TransformEditSession::ValidateActiveTargets(Scene& scene, bool validate_captured_parents) const
    {
        if (!m_is_active || m_targets.empty())
            return false;

        for (const ActiveTarget& target : m_targets)
        {
            if (!scene.IsAlive(target.Record.Entity) || !scene.HasComponent<TransformComponent>(target.Record.Entity))
                return false;

            if (validate_captured_parents && target.HasCapturedParent)
            {
                const ParentComponent* parent = scene.GetComponent<ParentComponent>(target.Record.Entity);
                if (!parent || !parent->Parent.IsValid() || !scene.IsAlive(parent->Parent))
                    return false;
                const TransformComponent* parent_transform = scene.GetComponent<TransformComponent>(parent->Parent);
                if (!parent_transform || !SameMatrix(parent_transform->WorldTransform, target.CapturedParentWorld))
                    return false;
            }
        }
        return true;
    }

    bool TransformEditSession::StageLocal(ArrayView<const GizmoLocalTransform> transforms)
    {
        if (!m_is_active || transforms.size() != m_targets.size())
            return false;

        for (size_t i = 0; i < transforms.size(); ++i)
        {
            const GizmoLocalTransform& supplied  = transforms[i];
            const Mat4f                matrix    = ComposeTransformMatrix(supplied.Position, supplied.Rotation, supplied.Scale);
            GizmoLocalTransform        canonical = {};
            if (!TryDecomposeTransformTRS(matrix, canonical.Position, canonical.Rotation, canonical.Scale))
                return false;
            m_staged[i] = canonical;
        }
        return true;
    }

    bool TransformEditSession::ApplyStaged(Scene& scene)
    {
        if (!ValidateActiveTargets(scene, true))
            return false;

        for (size_t i = 0; i < m_targets.size(); ++i)
        {
            TransformComponent* transform = scene.GetComponent<TransformComponent>(m_targets[i].Record.Entity);
            ApplyLocalTransform(*transform, m_staged[i]);
            m_targets[i].Record.After = m_staged[i];
            m_view[i]                 = m_targets[i].Record;
        }
        Systems::SyncHierarchy(scene);
        return true;
    }

    bool TransformEditSession::PreviewLocal(Scene& scene, ArrayView<const GizmoLocalTransform> transforms)
    {
        if (!m_is_active)
            return false;
        Systems::SyncHierarchy(scene);
        return StageLocal(transforms) && ApplyStaged(scene);
    }

    bool TransformEditSession::PreviewWorld(Scene& scene, ArrayView<const Mat4f> transforms)
    {
        if (!m_is_active || transforms.size() != m_targets.size())
            return false;

        Systems::SyncHierarchy(scene);
        if (!ValidateActiveTargets(scene, true))
            return false;
        for (size_t i = 0; i < transforms.size(); ++i)
        {
            const Mat4f* parent = m_targets[i].HasCapturedParent ? &m_targets[i].CapturedParentWorld : nullptr;
            if (!TryWorldToLocalGizmoTransform(transforms[i], parent, m_staged[i]))
                return false;
        }
        return ApplyStaged(scene);
    }

    bool TransformEditSession::Commit(Scene& scene)
    {
        if (!m_is_active)
            return false;
        m_last_commit_changed = false;
        if (!ValidateActiveTargets(scene, true))
        {
            (void) Cancel(scene);
            return false;
        }

        bool changed = false;
        for (const ActiveTarget& target : m_targets)
        {
            if (!SameLocalTransform(target.Record.Before, target.Record.After))
            {
                changed = true;
                break;
            }
        }
        if (!changed)
        {
            EndTransaction();
            return true;
        }

        m_view.clear();
        for (const ActiveTarget& target : m_targets)
            m_view.push(target.Record);
        if (!m_history.Push(m_view.data(), static_cast<uint32_t>(m_view.size())))
        {
            (void) Cancel(scene);
            return false;
        }

        m_last_commit_changed = true;
        EndTransaction();
        return true;
    }

    bool TransformEditSession::Cancel(Scene& scene)
    {
        if (!m_is_active)
            return false;

        bool restored_all = true;
        for (const ActiveTarget& target : m_targets)
        {
            if (!scene.IsAlive(target.Record.Entity))
            {
                restored_all = false;
                continue;
            }
            TransformComponent* transform = scene.GetComponent<TransformComponent>(target.Record.Entity);
            if (!transform)
            {
                restored_all = false;
                continue;
            }
            ApplyLocalTransform(*transform, target.Record.Before);
        }
        Systems::SyncHierarchy(scene);
        m_last_commit_changed = false;
        EndTransaction();
        return restored_all;
    }

    bool TransformEditSession::Undo(Scene& scene)
    {
        return !m_is_active && m_history.Undo(scene);
    }

    bool TransformEditSession::Redo(Scene& scene)
    {
        return !m_is_active && m_history.Redo(scene);
    }

    bool TransformEditSession::IsActive() const
    {
        return m_is_active;
    }

    bool TransformEditSession::LastCommitChanged() const
    {
        return m_last_commit_changed;
    }

    ArrayView<const TransformEditRecord> TransformEditSession::ActiveTargets() const
    {
        return {m_view.data(), m_is_active ? m_targets.size() : 0};
    }

    TransformEditHistory& TransformEditSession::History()
    {
        return m_history;
    }

    const TransformEditHistory& TransformEditSession::History() const
    {
        return m_history;
    }

    void TransformEditSession::EndTransaction()
    {
        m_targets.clear();
        m_staged.clear();
        m_view.clear();
        m_is_active = false;
    }
} // namespace ZEngine::ECS::Tools
