#include <ZEngine/ECS/Actor.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS
{
    bool Actor::IsAlive() const
    {
        return m_scene != nullptr && m_scene->IsAlive(m_entity_id);
    }
} // namespace ZEngine::ECS
