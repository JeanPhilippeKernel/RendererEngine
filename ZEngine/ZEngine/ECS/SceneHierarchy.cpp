#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ECS/SceneHierarchy.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>

using namespace ZEngine::Core::Maths;
using namespace ZEngine::ECS::Components;

namespace ZEngine::ECS
{
    namespace
    {
        bool Finite(const Mat4f& matrix)
        {
            for (uint32_t column = 0; column < 4; ++column)
                for (uint32_t row = 0; row < 4; ++row)
                    if (!std::isfinite(matrix(row, column)))
                        return false;
            return true;
        }

        bool Same(const Mat4f& a, const Mat4f& b)
        {
            for (uint32_t column = 0; column < 4; ++column)
                for (uint32_t row = 0; row < 4; ++row)
                    if (a(row, column) != b(row, column))
                        return false;
            return true;
        }
    } // namespace

    bool SceneHierarchy::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        if (!arena || m_nodes || m_lookup || m_order)
            return false;
        constexpr uint32_t capacity = EntityRegistry::MAX_ENTITIES;
        m_arena                     = arena;
        m_lookup                    = static_cast<uint32_t*>(arena->AllocateNoZero(sizeof(uint32_t) * capacity, alignof(uint32_t)));
        if (!m_lookup || !Reserve(256))
        {
            Shutdown();
            return false;
        }
        // Only this index is initialized at capacity. Nodes/order initialize their
        // used prefix on a topology rebuild, never their unused storage per frame.
        std::fill_n(m_lookup, capacity, NoIndex);
        Invalidate();
        return true;
    }

    bool SceneHierarchy::Reserve(uint32_t entities)
    {
        if (!m_arena || entities > EntityRegistry::MAX_ENTITIES)
            return false;
        if (entities <= m_capacity)
            return true;
        const uint32_t capacity = std::min(EntityRegistry::MAX_ENTITIES, std::max(entities, m_capacity ? m_capacity * 2 : 256u));
        // One allocation makes growth failure atomic. Arena-backed geometric
        // growth is bounded by twice peak workspace; only initialized entries copy.
        auto*          storage  = static_cast<std::byte*>(m_arena->AllocateNoZero((sizeof(Node) + sizeof(uint32_t)) * capacity, alignof(Node)));
        if (!storage)
            return false;
        auto* nodes = reinterpret_cast<Node*>(storage);
        auto* order = reinterpret_cast<uint32_t*>(storage + sizeof(Node) * capacity);
        if (m_count)
        {
            std::uninitialized_copy_n(m_nodes, m_count, nodes);
            std::uninitialized_copy_n(m_order, m_count, order);
        }
        m_nodes    = nodes;
        m_order    = order;
        m_capacity = capacity;
        return true;
    }

    size_t SceneHierarchy::ReservationMemoryBound(uint32_t entities) const
    {
        ZENGINE_VALIDATE_ASSERT(entities <= EntityRegistry::MAX_ENTITIES, "Hierarchy reservation exceeds entity capacity")
        if (entities <= m_capacity)
            return 0;
        const uint32_t capacity = std::min(EntityRegistry::MAX_ENTITIES, std::max(entities, m_capacity ? m_capacity * 2 : 256u));
        return (sizeof(Node) + sizeof(uint32_t)) * capacity + alignof(Node) - 1;
    }

    void SceneHierarchy::Shutdown()
    {
        m_nodes  = nullptr;
        m_lookup = m_order    = nullptr;
        m_count               = 0;
        m_capacity            = 0;
        m_arena               = nullptr;
        m_structural_revision = UINT64_MAX;
        Invalidate();
    }

    void SceneHierarchy::Invalidate()
    {
        m_dirty = true;
        ++m_revision;
    }

    uint32_t SceneHierarchy::IndexOf(EntityID entity) const
    {
        if (!m_lookup || !m_nodes || !entity.IsValid() || entity.Index >= EntityRegistry::MAX_ENTITIES)
            return NoIndex;
        const uint32_t index = m_lookup[entity.Index];
        return index < m_count && m_nodes[index].Entity == entity ? index : NoIndex;
    }

    const SceneHierarchy::Node* SceneHierarchy::Find(EntityID entity) const
    {
        const uint32_t index = m_dirty ? NoIndex : IndexOf(entity);
        return index == NoIndex ? nullptr : &m_nodes[index];
    }

    uint64_t SceneHierarchy::ParentWorldRevision(EntityID entity) const
    {
        const auto* node = Find(entity);
        return node && node->ParentTransform != NoIndex ? m_nodes[node->ParentTransform].WorldRevision : 0;
    }

    void SceneHierarchy::RefreshTopology(Scene& scene)
    {
        if (m_dirty || m_structural_revision != scene.StructuralRevision())
        {
            m_count = 0;
            scene.ForEachEntity([&](EntityID entity) {
                Node node          = {};
                node.Entity        = entity;
                const auto* parent = scene.GetComponent<ParentComponent>(entity);
                node.Parent        = parent ? parent->Parent : INVALID_ENTITY;
                node.HasTransform  = scene.HasComponent<TransformComponent>(entity);
                std::construct_at(m_nodes + m_count, node);
                m_lookup[entity.Index] = m_count++;
            });
        }
        else
        {
            // Compatibility with legacy raw Parent writes: detect them in one
            // linear scan until every writer uses Scene::SetParent.
            for (uint32_t i = 0; i < m_count; ++i)
            {
                const auto* parent = scene.GetComponent<ParentComponent>(m_nodes[i].Entity);
                const auto  value  = parent ? parent->Parent : INVALID_ENTITY;
                if (value != m_nodes[i].Parent)
                {
                    m_nodes[i].Parent = value;
                    if (!m_dirty)
                        Invalidate();
                }
            }
        }
        if (!m_dirty && m_structural_revision == scene.StructuralRevision())
            return;
        BuildOrder();
        m_structural_revision = scene.StructuralRevision();
        m_dirty               = false;
        ++m_build_count;
    }

    void SceneHierarchy::BuildOrder()
    {
        for (uint32_t i = 0; i < m_count; ++i)
        {
            auto& node           = m_nodes[i];
            node.ParentIndex     = IndexOf(node.Parent);
            node.ParentTransform = node.FirstChild = node.NextSibling = NoIndex;
            node.TopologyValid                                        = node.Parent == INVALID_ENTITY || node.ParentIndex != NoIndex;
            node.Ordered                                              = false;
        }
        uint32_t count = 0;
        for (uint32_t i = 0; i < m_count; ++i)
        {
            auto& node = m_nodes[i];
            if (node.ParentIndex == NoIndex)
                m_order[count++] = i;
            else
            {
                node.NextSibling                     = m_nodes[node.ParentIndex].FirstChild;
                m_nodes[node.ParentIndex].FirstChild = i;
            }
        }
        // Kahn traversal: one incoming parent edge per node. The order array is
        // also the queue, so no recursion, extra per-frame scratch or depth limit.
        for (uint32_t read = 0; read < count; ++read)
        {
            auto& node   = m_nodes[m_order[read]];
            node.Ordered = true;
            if (node.ParentIndex != NoIndex)
            {
                const auto& parent   = m_nodes[node.ParentIndex];
                node.TopologyValid   = node.TopologyValid && parent.TopologyValid;
                node.ParentTransform = parent.HasTransform ? node.ParentIndex : parent.ParentTransform;
            }
            for (uint32_t child = node.FirstChild; child != NoIndex; child = m_nodes[child].NextSibling)
                m_order[count++] = child;
        }
        // Cycles and their descendants cannot enter the queue. Keep them visible
        // at finite local fallback poses, but never expose a valid editable frame.
        // Authored Parent/TRS values are not repaired or overwritten silently.
        for (uint32_t i = 0; i < m_count; ++i)
            if (!m_nodes[i].Ordered)
            {
                m_nodes[i].TopologyValid   = false;
                m_nodes[i].ParentTransform = NoIndex;
                m_order[count++]           = i;
            }
    }

    void SceneHierarchy::Synchronize(Scene& scene)
    {
        if (!m_nodes)
            return;
        RefreshTopology(scene);
        for (uint32_t order = 0; order < m_count; ++order)
        {
            auto&      node         = m_nodes[m_order[order]];
            const bool parent_valid = node.ParentIndex == NoIndex || m_nodes[node.ParentIndex].FrameValid;
            bool       valid        = node.TopologyValid && parent_valid;
            if (!node.HasTransform)
            {
                node.FrameValid    = valid;
                node.WorldRevision = node.ParentTransform != NoIndex ? m_nodes[node.ParentTransform].WorldRevision : 0;
                continue;
            }
            auto*      component    = scene.GetComponent<TransformComponent>(node.Entity);
            auto       local        = ComposeTransformMatrix(component->Position, component->Rotation, component->Scale);
            const bool finite_local = Finite(local);
            valid                   = valid && finite_local;
            Mat4f world             = finite_local ? local : Identity<Mat4f>();
            if (valid && node.ParentTransform != NoIndex)
            {
                const auto* parent    = scene.GetComponent<TransformComponent>(m_nodes[node.ParentTransform].Entity);
                const Mat4f candidate = parent->WorldTransform * local;
                valid                 = Finite(candidate);
                if (valid)
                    world = candidate;
            }
            if (!node.WorldRevision || node.FrameValid != valid || !Same(component->WorldTransform, world))
                node.WorldRevision = ++m_world_revision;
            component->WorldTransform = world;
            node.FrameValid           = valid;
        }
    }
} // namespace ZEngine::ECS
