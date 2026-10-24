#include <Tetragrama/EditorSession.h>
#include <Tetragrama/SelectedRootCache.h>
#include <memory>
#include <type_traits>

using namespace ZEngine::ECS;
using ZEngine::Core::Containers::ArrayView;

namespace Tetragrama
{
    bool SelectedRootCache::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena, uint32_t max_targets)
    {
        if (!arena || m_storage.data() || !max_targets || max_targets > EntityRegistry::MAX_ENTITIES)
        {
            return false;
        }
        static_assert(std::is_trivially_copyable_v<EntityTarget> && std::is_trivially_destructible_v<EntityTarget>);
        const size_t target_bytes = sizeof(EntityTarget) * max_targets;
        // One allocation makes failure atomic. Coverage is initialized only for
        // live entity indices on a rebuild; unused capacity is never read/cleared.
        auto*        storage      = static_cast<EntityTarget*>(arena->AllocateNoZero(target_bytes + EntityRegistry::MAX_ENTITIES, alignof(EntityTarget)));
        if (!storage)
        {
            return false;
        }
        m_storage.set(storage, max_targets);
        m_coverage.set(reinterpret_cast<uint8_t*>(storage) + target_bytes, EntityRegistry::MAX_ENTITIES);
        return true;
    }

    void SelectedRootCache::Invalidate()
    {
        m_valid   = false;
        m_count   = 0;
        m_session = nullptr;
    }

    bool SelectedRootCache::Read(EditorSession& session, ArrayView<const EntityTarget>& output)
    {
        if (!m_storage.data() || !session.OnOwnerThread() || !session.Scene())
        {
            return false;
        }
        const auto& scene     = *session.Scene();
        const auto& selection = session.Selection();
        const auto& hierarchy = scene.Hierarchy();
        const auto  nodes     = hierarchy.Nodes();
        const auto  order     = hierarchy.Order();
        // A dirty hierarchy exposes no nodes/order. Never reuse an old root set
        // or silently interpret an unsynchronized nonempty scene as empty.
        if (nodes.size() != scene.AliveCount() || order.size() != nodes.size())
        {
            Invalidate();
            return false;
        }
        if (m_valid && m_session == &session && m_epoch == session.SceneEpoch() && m_selection_revision == selection.Revision() && m_structural_revision == scene.StructuralRevision() && m_hierarchy_revision == hierarchy.Revision())
        {
            output = {m_storage.data(), m_count};
            return true;
        }
        Invalidate();
        const auto selected = selection.Entities();
        if (selected.size() > m_storage.size())
        {
            return false;
        }
        if (!selected.empty())
        {
            constexpr uint8_t chosen = 1, covered = 2;
            // Direct generational-entity membership avoids UUID hashing for
            // every unselected node. Each live slot is initialized before use.
            for (const auto& node : nodes)
            {
                m_coverage[node.Entity.Index] = 0;
            }
            for (uint32_t i = 0; i < selected.size(); ++i)
            {
                const auto  entity = session.Resolve(selected[i]);
                const auto* node   = hierarchy.Find(entity);
                if (!node || !node->TopologyValid)
                {
                    return false;
                }
                std::construct_at(m_storage.data() + i, EntityTarget{selected[i].UUID, entity, session.SceneEpoch()});
                m_coverage[entity.Index] = chosen;
            }
            // SceneHierarchy already supplies parent-before-child order. The
            // inherited bit crosses unselected/transform-less intermediates.
            for (const uint32_t index : order)
            {
                const auto& node = nodes[index];
                if (!node.TopologyValid || node.ParentIndex == SceneHierarchy::NoIndex)
                {
                    continue;
                }
                const auto& parent = nodes[node.ParentIndex];
                if (m_coverage[parent.Entity.Index] & (chosen | covered))
                {
                    m_coverage[node.Entity.Index] |= covered;
                }
            }
            // Preserve selection order, not hierarchy traversal order, and do
            // not change the explicit active target when a child is pruned.
            for (uint32_t i = 0; i < selected.size(); ++i)
            {
                if (!(m_coverage[m_storage[i].CachedEntity.Index] & covered))
                {
                    m_storage[m_count++] = m_storage[i];
                }
            }
        }
        m_session             = &session;
        m_epoch               = session.SceneEpoch();
        m_selection_revision  = selection.Revision();
        m_structural_revision = scene.StructuralRevision();
        m_hierarchy_revision  = hierarchy.Revision();
        m_valid               = true;
        ++m_build_count;
        output = {m_storage.data(), m_count};
        return true;
    }
} // namespace Tetragrama
