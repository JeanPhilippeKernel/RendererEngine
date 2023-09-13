#include <pch.h>
#include <Rendering/Entities/GraphicSceneEntity.h>

namespace ZEngine::Rendering::Entities
{
    WeakRef<entt::registry> GraphicSceneEntity::s_weak_registry_ptr;

    GraphicSceneEntity GraphicSceneEntity::CreateWrapper(const Ref<entt::registry>& registry_ptr, entt::entity handle)
    {
        if (s_weak_registry_ptr.expired())
        {
            s_weak_registry_ptr = registry_ptr;
        }
        return GraphicSceneEntity(handle);
    }

    GraphicSceneEntity::GraphicSceneEntity(entt::entity handle) : m_entity_handle(handle) {}

    GraphicSceneEntity::GraphicSceneEntity(GraphicSceneEntity&& rhs) noexcept
    {
        m_entity_handle = rhs.m_entity_handle;
    }

    GraphicSceneEntity& GraphicSceneEntity::operator=(GraphicSceneEntity&& rhs) noexcept
    {
        m_entity_handle = rhs.m_entity_handle;
        return *this;
    }
} // namespace ZEngine::Rendering::Entities
