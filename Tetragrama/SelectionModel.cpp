#include <Tetragrama/SelectionModel.h>

namespace Tetragrama
{
    void SelectionModel::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_targets)
    {
        ZENGINE_VALIDATE_ASSERT(arena && !m_capacity && max_targets > 0 && max_targets <= ZEngine::ECS::EntityRegistry::MAX_ENTITIES, "Invalid SelectionModel initialization")
        m_entities.init(arena, max_targets);
        m_pending.init(arena, max_targets);
        m_membership.init(arena, 2 * max_targets);
        m_pending_membership.init(arena, 2 * max_targets);
        m_capacity = max_targets;
    }

    void SelectionModel::RebuildMembership()
    {
        m_membership.clear();
        for (uint32_t i = 0; i < m_entities.size(); ++i)
        {
            m_membership.insert(m_entities[i].UUID, i);
        }
    }

    bool SelectionModel::Set(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active)
    {
        if (!Prepare(resolver, targets, active, false))
        {
            return false;
        }
        PublishPrepared();
        return true;
    }

    bool SelectionModel::Extend(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active)
    {
        if (targets.empty() && active.is_nil())
        {
            return m_capacity != 0;
        }
        if (!Prepare(resolver, targets, active, true))
        {
            return false;
        }
        PublishPrepared();
        return true;
    }

    bool SelectionModel::Prepare(EntityTargetResolver& resolver, std::span<const EntityTarget> targets, const uuids::uuid& active, bool additive)
    {
        if (!m_capacity || targets.size() > m_capacity)
        {
            return false;
        }
        m_pending.clear();
        m_pending_membership.clear();
        const auto append = [&](const EntityTarget& target) {
            const auto entity = resolver.Resolve(target);
            if (!entity.IsValid())
            {
                return false;
            }
            if (m_pending_membership.contains(target.UUID))
            {
                return true;
            }
            if (m_pending.size() == m_capacity)
            {
                return false;
            }
            m_pending_membership.insert(target.UUID, static_cast<uint32_t>(m_pending.size()));
            m_pending.push({target.UUID, entity, target.SceneEpoch});
            return true;
        };
        if (additive)
        {
            for (const auto& target : m_entities)
            {
                if (!append(target))
                {
                    return false;
                }
            }
        }
        for (const auto& target : targets)
        {
            if (!append(target))
            {
                return false;
            }
        }
        const uuids::uuid next_active = !active.is_nil() ? active : !targets.empty() ? targets.back().UUID : additive ? m_active : uuids::uuid{};
        if (!next_active.is_nil() && !m_pending_membership.contains(next_active))
        {
            return false;
        }
        m_pending_active = next_active;
        return true;
    }

    void SelectionModel::PublishPrepared()
    {
        bool changed = m_scene_target != SceneTarget::None || m_active != m_pending_active || m_entities.size() != m_pending.size();
        for (size_t i = 0; !changed && i < m_entities.size(); ++i)
        {
            changed = m_entities[i].UUID != m_pending[i].UUID || m_entities[i].SceneEpoch != m_pending[i].SceneEpoch;
        }
        if (!changed)
        {
            return;
        }

        m_entities.clear();
        for (const auto& target : m_pending)
        {
            m_entities.push(target);
        }
        m_active       = m_pending_active;
        m_scene_target = SceneTarget::None;
        RebuildMembership();
        ++m_revision;
    }

    bool SelectionModel::CanSelect(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity, SelectionMode mode)
    {
        if (!m_capacity || (mode != SelectionMode::Replace && mode != SelectionMode::Add && mode != SelectionMode::Toggle))
        {
            return false;
        }
        const auto target = resolver.TargetFor(entity);
        if (target.UUID.is_nil())
        {
            return false;
        }
        if (mode == SelectionMode::Replace)
        {
            return true;
        }
        // A scene switch is an explicit selection replacement, never an implicit mixed-epoch add.
        return (m_entities.empty() || m_entities.front().SceneEpoch == resolver.SceneEpoch()) && (m_membership.contains(target.UUID) || m_entities.size() < m_capacity);
    }

    bool SelectionModel::SetHovered(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity)
    {
        const EntityTarget target = resolver.TargetFor(entity);
        if (target.UUID.is_nil())
        {
            return false;
        }
        if (m_hovered.UUID == target.UUID && m_hovered.SceneEpoch == target.SceneEpoch)
        {
            return true;
        }
        m_hovered = target;
        ++m_hover_revision;
        return true;
    }

    void SelectionModel::ClearHovered()
    {
        if (m_hovered.UUID.is_nil())
        {
            return;
        }
        m_hovered = {};
        ++m_hover_revision;
    }

    bool SelectionModel::Select(EntityTargetResolver& resolver, ZEngine::ECS::EntityID entity, SelectionMode mode)
    {
        if (!CanSelect(resolver, entity, mode))
        {
            return false;
        }
        const auto target = resolver.TargetFor(entity);
        if (mode == SelectionMode::Replace)
        {
            return Set(resolver, {&target, 1}, target.UUID);
        }

        const auto* found = m_membership.find(target.UUID);
        if (found && mode == SelectionMode::Toggle)
        {
            const bool removed_active = m_active == target.UUID;
            m_entities.erase(*found);
            if (removed_active)
            {
                m_active = m_entities.empty() ? uuids::uuid{} : m_entities.back().UUID;
            }
            RebuildMembership();
            ++m_revision;
            return true;
        }
        if (found)
        {
            if (m_active != target.UUID)
            {
                m_active = target.UUID;
                ++m_revision;
            }
            return true;
        }
        m_membership.insert(target.UUID, static_cast<uint32_t>(m_entities.size()));
        m_entities.push(target);
        m_active       = target.UUID;
        m_scene_target = SceneTarget::None;
        ++m_revision;
        return true;
    }

    void SelectionModel::Clear()
    {
        if (m_entities.empty() && m_scene_target == SceneTarget::None)
        {
            ClearHovered();
            return;
        }
        m_entities.clear();
        m_membership.clear();
        m_active       = {};
        m_scene_target = SceneTarget::None;
        ClearHovered();
        ++m_revision;
    }

    void SelectionModel::SelectScene(SceneTarget target)
    {
        if (target != SceneTarget::None && target != SceneTarget::Grid)
        {
            return;
        }
        if (target == SceneTarget::None)
        {
            Clear();
            return;
        }
        if (!m_capacity || m_scene_target == target)
        {
            return;
        }
        m_entities.clear();
        m_membership.clear();
        m_active       = {};
        m_scene_target = target;
        ClearHovered();
        ++m_revision;
    }

    bool SelectionModel::PruneInvalid(EntityTargetResolver& resolver)
    {
        size_t kept = 0;
        for (const auto& target : m_entities)
        {
            const auto entity = resolver.Resolve(target);
            if (entity.IsValid())
            {
                m_entities[kept++] = {target.UUID, entity, target.SceneEpoch};
            }
        }
        const bool selection_changed = kept != m_entities.size();
        const bool hover_invalid     = !m_hovered.UUID.is_nil() && !resolver.Resolve(m_hovered).IsValid();
        if (!selection_changed && !hover_invalid)
        {
            return false;
        }
        while (m_entities.size() > kept)
        {
            m_entities.pop();
        }
        if (selection_changed)
        {
            RebuildMembership();
            if (!m_membership.contains(m_active))
            {
                m_active = m_entities.empty() ? uuids::uuid{} : m_entities.back().UUID;
            }
            ++m_revision;
        }
        if (hover_invalid)
        {
            ClearHovered();
        }
        return true;
    }
} // namespace Tetragrama
