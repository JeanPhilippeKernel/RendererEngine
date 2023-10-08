#pragma once
#include <ZEngineDef.h>
#include <type_traits>
#include <entt/entt.hpp>
#include <ZEngine/Logging/LoggerDefinition.h>

namespace ZEngine::Rendering::Entities
{
    class GraphicSceneEntity
    {
    public:
        static GraphicSceneEntity CreateWrapper(const Ref<entt::registry>& registry_ptr, entt::entity handle);

    public:
        GraphicSceneEntity() = default;
        GraphicSceneEntity(entt::entity handle);
        GraphicSceneEntity(GraphicSceneEntity&& rhs) noexcept;
        virtual ~GraphicSceneEntity() = default;

        GraphicSceneEntity& operator=(GraphicSceneEntity&& rhs) noexcept;

        template <typename TComponent>
        bool HasComponent() const
        {
            if (s_weak_registry_ptr.expired())
            {
                return false;
            }
            auto registry = s_weak_registry_ptr.lock();
            return registry->all_of<TComponent>(m_entity_handle);
        }

        template <typename TComponent, typename... Args>
        TComponent& AddComponent(Args&&... args)
        {
            if (HasComponent<TComponent>())
            {
                ZENGINE_CORE_ERROR("This component has already been added to this entity")
                return GetComponent<TComponent>();
            }
            auto registry = s_weak_registry_ptr.lock();
            return registry->emplace<TComponent>(m_entity_handle, std::forward<Args>(args)...);
        }

        template <typename TComponent>
        void RemoveComponent()
        {
            auto registry = s_weak_registry_ptr.lock();
            registry->remove<TComponent>(m_entity_handle);
        }

        template <typename TComponent>
        TComponent& GetComponent() const
        {
            auto registry = s_weak_registry_ptr.lock();
            return registry->get<TComponent>(m_entity_handle);
        }

        bool operator==(const GraphicSceneEntity& rhs)
        {
            return (this->m_entity_handle == rhs.m_entity_handle) && (this->s_weak_registry_ptr.lock().get() == rhs.s_weak_registry_ptr.lock().get());
        }

        bool operator!=(const GraphicSceneEntity& rhs)
        {
            return !((*this) == rhs);
        }

        operator uint32_t() const
        {
            return static_cast<uint32_t>(m_entity_handle);
        }

        operator entt::entity() const
        {
            return m_entity_handle;
        }

        operator bool() const
        {
            if (auto registry = s_weak_registry_ptr.lock())
            {
                return (m_entity_handle != entt::null);
            }
            return false;
        }

    private:
        entt::entity                   m_entity_handle{entt::null};
        static WeakRef<entt::registry> s_weak_registry_ptr;
    };

} // namespace ZEngine::Rendering::Entities
