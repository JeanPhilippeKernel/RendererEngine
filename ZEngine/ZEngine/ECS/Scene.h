#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/ComponentStorage.h>
#include <ZEngine/ECS/EntityRegistry.h>
#include <ZEngine/ECS/RenderableTransform.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    class Scene
    {
    public:
        void          Initialize(Core::Memory::ArenaAllocator* arena);
        void          Shutdown();

        // Entity lifetime
        EntityID      CreateEntity();
        void          DestroyEntity(EntityID id);
        bool          IsAlive(EntityID id) const;
        ArchetypeMask GetMask(EntityID id) const;

        // Component access
        template <typename T>
        void AddComponent(EntityID id, T component)
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(id), "Scene::AddComponent: entity is not alive")
            ComponentStorage<T>& storage = GetOrCreateStorage<T>();
            storage.Add(id, static_cast<T&&>(component));
            m_registry.SetMaskBit(id, MaskBit(ComponentTypeOf<T>()));
        }

        template <typename T>
        T* GetComponent(EntityID id)
        {
            ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage)
                return nullptr;
            return storage->Get(id);
        }

        template <typename T>
        const T* GetComponent(EntityID id) const
        {
            const ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage)
                return nullptr;
            return storage->Get(id);
        }

        template <typename T>
        bool HasComponent(EntityID id) const
        {
            const ComponentStorage<T>* storage = FindStorage<T>();
            return storage && storage->Has(id);
        }

        template <typename T>
        void RemoveComponent(EntityID id)
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(id), "Scene::RemoveComponent: entity is not alive")
            ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage)
                return;
            storage->Remove(id);
            m_registry.ClearMaskBit(id, MaskBit(ComponentTypeOf<T>()));
        }

        // Query — hits ALL living entities (Tier 1 Actors + Tier 2 pure ECS).
        // The ArchetypeMask bitmask check is the critical early-exit guard.
        // Fn must be callable as: void(EntityID, Ts&...)
        template <typename... Ts, typename Fn>
        void ForEach(Fn&& fn)
        {
            const ArchetypeMask required = (MaskBit(ComponentTypeOf<Ts>()) | ...);
            m_registry.ForEachAlive([&](EntityID id) {
                if (!MaskMatches(m_registry.GetMask(id), required))
                    return;
                fn(id, *GetComponent<Ts>(id)...);
            });
        }

        uint32_t AliveCount() const
        {
            return m_registry.AliveCount();
        }

        Core::Memory::ArenaAllocator* GetArena() const
        {
            return m_arena;
        }

        // Fixed-timestep interpolation support (game-loop.md).
        // Copies Position into PreviousPosition for all entities with a TransformComponent.
        // Call at the end of each fixed simulation step.
        void SnapshotTransforms();

        // Linearly interpolates between PreviousPosition and Position using alpha [0,1]
        // and fills out with one RenderableTransform per entity that has a TransformComponent.
        // Call once per render frame before submitting to the renderer.
        void FillRenderableTransforms(float alpha, Core::Containers::Array<RenderableTransform>& out);

    private:
        EntityRegistry                                                          m_registry;
        Core::Containers::UnorderedHashMap<ComponentTypeID, IComponentStorage*> m_storages;
        Core::Containers::Array<IComponentStorage*>                             m_storages_list; // for iteration in DestroyEntity
        Core::Memory::ArenaAllocator*                                           m_arena = nullptr;

        template <typename T>
        ComponentStorage<T>& GetOrCreateStorage()
        {
            ComponentTypeID     type_id  = ComponentTypeOf<T>();
            IComponentStorage** existing = m_storages.find(type_id);
            if (existing)
                return *static_cast<ComponentStorage<T>*>(*existing);

            // Arena-allocate the storage object
            auto* storage = ZPushStructCtor(m_arena, ComponentStorage<T>);
            storage->Initialize(m_arena, EntityRegistry::MAX_ENTITIES);
            auto* iface = static_cast<IComponentStorage*>(storage);
            m_storages.insert(type_id, iface);
            m_storages_list.push(iface);
            return *storage;
        }

        template <typename T>
        ComponentStorage<T>* FindStorage()
        {
            ComponentTypeID     type_id = ComponentTypeOf<T>();
            IComponentStorage** found   = m_storages.find(type_id);
            return found ? static_cast<ComponentStorage<T>*>(*found) : nullptr;
        }

        template <typename T>
        const ComponentStorage<T>* FindStorage() const
        {
            ComponentTypeID                 type_id = ComponentTypeOf<T>();
            const IComponentStorage* const* found   = m_storages.find(type_id);
            return found ? static_cast<const ComponentStorage<T>*>(*found) : nullptr;
        }
    };
} // namespace ZEngine::ECS
