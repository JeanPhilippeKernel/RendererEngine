#pragma once
#include <ZEngine/ECS/EntityID.h>
#include <ZEngine/ECS/Scene.h>

namespace ZEngine::ECS
{
    class ActorManager;

    class Actor
    {
    public:
        virtual ~Actor() = default;

        [[nodiscard]] EntityID GetEntityID() const
        {
            return m_entity_id;
        }
        [[nodiscard]] bool IsAlive() const;

        template <typename T>
        void AddComponent(T component)
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor::AddComponent: entity is not alive")
            m_scene->AddComponent<T>(m_entity_id, static_cast<T&&>(component));
        }

        template <typename T>
        [[nodiscard]] T* GetComponent()
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor::GetComponent: entity is not alive")
            return m_scene->GetComponent<T>(m_entity_id);
        }

        template <typename T>
        [[nodiscard]] const T* GetComponent() const
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor::GetComponent: entity is not alive")
            return m_scene->GetComponent<T>(m_entity_id);
        }

        template <typename T>
        [[nodiscard]] bool HasComponent() const
        {
            return m_scene && m_scene->HasComponent<T>(m_entity_id);
        }

        template <typename T>
        void RemoveComponent()
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(), "Actor::RemoveComponent: entity is not alive")
            m_scene->RemoveComponent<T>(m_entity_id);
        }

        // Reflection-driven access — returns void* to component data keyed by TypeID.
        [[nodiscard]] void* GetComponentRaw(ComponentTypeID type_id)
        {
            return m_scene ? m_scene->GetComponentRaw(m_entity_id, type_id) : nullptr;
        }

        // Bitmask of all components currently on this entity — used with MaskHas().
        [[nodiscard]] ArchetypeMask GetComponentMask() const
        {
            return m_scene ? m_scene->GetMask(m_entity_id) : ArchetypeMask{};
        }

        virtual void OnCreate() {}
        virtual void OnDestroy() {}
        virtual void OnTick(float dt) {}

    protected:
        Actor() = default;

    private:
        friend class ActorManager;

        EntityID m_entity_id = INVALID_ENTITY;
        Scene*   m_scene     = nullptr; // non-owning; set by ActorManager::Create
    };
} // namespace ZEngine::ECS
