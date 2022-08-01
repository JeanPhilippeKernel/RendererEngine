#include <pch.h>
#include <Rendering/Entities/GraphicSceneEntity.h>

namespace ZEngine::Rendering::Entities {
    GraphicSceneEntity::GraphicSceneEntity(entt::entity handle, Ref<entt::registry> registry_ptr) : m_entity_handle(handle), m_weak_registry_ptr(registry_ptr) {}
    GraphicSceneEntity::GraphicSceneEntity(GraphicSceneEntity&& rhs) noexcept {
        m_entity_handle = rhs.m_entity_handle;
        rhs.m_weak_registry_ptr.swap(m_weak_registry_ptr);
    }

    GraphicSceneEntity& GraphicSceneEntity::operator=(GraphicSceneEntity&& rhs) noexcept {
        m_entity_handle = rhs.m_entity_handle;
        rhs.m_weak_registry_ptr.swap(m_weak_registry_ptr);
        return *this;
    }
} // namespace ZEngine::Rendering::Entities
