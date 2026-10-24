#include <Tetragrama/EditorTarget.h>
#include <ZEngine/ECS/Components/UUIDComponent.h>

using namespace ZEngine::ECS;
using namespace ZEngine::ECS::Components;

namespace Tetragrama
{
    void EntityTargetResolver::Initialize(ZEngine::Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena && !m_initialized, "EntityTargetResolver must be initialized once")
        // One bounded allocation; structural refreshes and per-frame resolves never grow it.
        m_index.init(arena, 2 * EntityRegistry::MAX_ENTITIES);
        m_initialized = true;
    }

    void EntityTargetResolver::Bind(Scene* scene, uint64_t scene_epoch)
    {
        m_scene       = scene;
        m_scene_epoch = scene ? scene_epoch : 0;
        Invalidate();
    }

    void EntityTargetResolver::Invalidate()
    {
        m_index_valid = false;
    }

    bool EntityTargetResolver::Refresh()
    {
        if (!m_initialized || !m_scene || !m_scene->GetArena() || m_scene_epoch == 0)
        {
            return false;
        }
        if (m_index_valid && m_index_revision == m_scene->StructuralRevision())
        {
            return true;
        }

        m_index.clear();
        m_scene->ForEach<UUIDComponent>([&](EntityID entity, UUIDComponent& identity) {
            if (identity.Value.is_nil())
            {
                return;
            }
            if (auto* existing = m_index.find(identity.Value))
            {
                *existing = INVALID_ENTITY;
            }
            else
            {
                m_index.insert(identity.Value, entity);
            }
        });
        m_index_revision = m_scene->StructuralRevision();
        m_index_valid    = true;
        return true;
    }

    EntityID EntityTargetResolver::Resolve(const uuids::uuid& identity)
    {
        if (identity.is_nil() || !Refresh())
        {
            return INVALID_ENTITY;
        }
        const auto* entity = m_index.find(identity);
        if (!entity || !m_scene->IsAlive(*entity))
        {
            return INVALID_ENTITY;
        }
        const auto* current = m_scene->GetComponent<UUIDComponent>(*entity);
        return current && current->Value == identity ? *entity : INVALID_ENTITY;
    }

    EntityID EntityTargetResolver::Resolve(const EntityTarget& target)
    {
        if (target.SceneEpoch != m_scene_epoch)
        {
            return INVALID_ENTITY;
        }
        // The index also rejects duplicate UUIDs. A cached generational ID alone cannot do so.
        return Resolve(target.UUID);
    }

    EntityTarget EntityTargetResolver::TargetFor(EntityID entity)
    {
        if (!Refresh() || !m_scene->IsAlive(entity))
        {
            return {};
        }
        const auto* identity = m_scene->GetComponent<UUIDComponent>(entity);
        if (!identity || Resolve(identity->Value) != entity)
        {
            return {};
        }
        return {identity->Value, entity, m_scene_epoch};
    }
} // namespace Tetragrama
