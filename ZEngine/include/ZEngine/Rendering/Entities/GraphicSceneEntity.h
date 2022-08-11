#pragma once
#include <ZEngineDef.h>
#include <type_traits>
#include <entt/entt.hpp>
#include <ZEngine/Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Entities {
    class GraphicSceneEntity {
    public:
        GraphicSceneEntity() = default;
        GraphicSceneEntity(entt::entity handle, Ref<entt::registry> registry_ptr);
        GraphicSceneEntity(GraphicSceneEntity&& rhs) noexcept;
        virtual ~GraphicSceneEntity() = default;

        GraphicSceneEntity& operator=(GraphicSceneEntity&& rhs) noexcept;

        template <typename TComponent>
        bool HasComponent() const {
            if (m_weak_registry_ptr.expired()) {
                return false;
            }
            auto registry = m_weak_registry_ptr.lock();
            return registry->all_of<TComponent>(m_entity_handle);
        }

        template <typename TComponent, typename... Args>
        TComponent& AddComponent(Args&&... args) {
            if (HasComponent<TComponent>()) {
                ZENGINE_CORE_ERROR("This component has already been added to this entity")
                return GetComponent<TComponent>();
            }
            auto registry = m_weak_registry_ptr.lock();
            return registry->emplace<TComponent>(m_entity_handle, std::forward<Args>(args)...);
        }

        template <typename TComponent>
        void RemoveComponent() {
            auto registry = m_weak_registry_ptr.lock();
            registry->remove<TComponent>(m_entity_handle);
        }

        template <typename TComponent>
        TComponent& GetComponent() const {
            auto registry = m_weak_registry_ptr.lock();
            return registry->get<TComponent>(m_entity_handle);
        }

        bool operator==(const GraphicSceneEntity& rhs) {
            return (this->m_entity_handle == rhs.m_entity_handle) && (this->m_weak_registry_ptr.lock().get() == rhs.m_weak_registry_ptr.lock().get());
        }

        bool operator!=(const GraphicSceneEntity& rhs) {
            return !((*this) == rhs);
        }

        operator uint32_t() const {
            return static_cast<uint32_t>(m_entity_handle);
        }

        operator entt::entity() const {
            return m_entity_handle;
        }

        operator bool() const {
            return (m_entity_handle != entt::null) && (m_weak_registry_ptr != nullptr) && !m_weak_registry_ptr.expired();
        }

    private:
        entt::entity            m_entity_handle{entt::null};
        WeakRef<entt::registry> m_weak_registry_ptr;
    };

} // namespace ZEngine::Rendering::Entities
