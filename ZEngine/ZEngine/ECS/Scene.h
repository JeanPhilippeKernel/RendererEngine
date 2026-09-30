#pragma once
#include <ZEngine/Core/Containers/Array.h>
#include <ZEngine/Core/Containers/UnorderedHashMap.h>
#include <ZEngine/Core/Memory/Allocator.h>
#include <ZEngine/ECS/ArchetypeMask.h>
#include <ZEngine/ECS/ComponentStorage.h>
#include <ZEngine/ECS/EntityRegistry.h>
#include <ZEngine/ECS/RenderableTransform.h>
#include <ZEngine/ECS/SceneHierarchy.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    class Scene
    {
    public:
        void                  Initialize(Core::Memory::ArenaAllocator* arena);
        void                  Shutdown();

        // Entity lifetime
        EntityID              CreateEntity();
        void                  DestroyEntity(EntityID id);
        bool                  IsAlive(EntityID id) const;
        ArchetypeMask         GetMask(EntityID id) const;
        /// @brief Reparent while preserving local TRS. Reject stale references and cycles atomically.
        [[nodiscard]] bool    CanSetParent(EntityID child, EntityID parent) const;
        [[nodiscard]] bool    SetParent(EntityID child, EntityID parent);
        void                  SynchronizeHierarchy();
        const SceneHierarchy& Hierarchy() const
        {
            return m_hierarchy;
        }

        // Component access
        template <typename T>
        void AddComponent(EntityID id, T component)
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(id), "Scene::AddComponent: entity is not alive")
            ComponentStorage<T>& storage = GetOrCreateStorage<T>();
            storage.Add(id, static_cast<T&&>(component));
            m_registry.SetMaskBit(id, MaskBit(ComponentTypeOf<T>()));
            ++m_structural_revision;
            m_hierarchy.Invalidate();
        }

        template <typename T>
        T* GetComponent(EntityID id)
        {
            ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage)
            {
                return nullptr;
            }
            return storage->Get(id);
        }

        template <typename T>
        const T* GetComponent(EntityID id) const
        {
            const ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage)
            {
                return nullptr;
            }
            return storage->Get(id);
        }

        template <typename T>
        bool HasComponent(EntityID id) const
        {
            const ComponentStorage<T>* storage = FindStorage<T>();
            return storage && storage->Has(id);
        }

        /// Main-thread component-instance identity; zero for an absent/stale component.
        /// A scene binding/epoch is still required before comparing tokens across scenes.
        template <typename T>
        uint64_t ComponentLifetime(EntityID id) const
        {
            const auto* storage = FindStorage<T>();
            return storage ? storage->Lifetime(id) : 0;
        }

        template <typename T>
        void RemoveComponent(EntityID id)
        {
            ZENGINE_VALIDATE_ASSERT(IsAlive(id), "Scene::RemoveComponent: entity is not alive")
            ComponentStorage<T>* storage = FindStorage<T>();
            if (!storage || !storage->Has(id))
            {
                return;
            }
            storage->Remove(id);
            m_registry.ClearMaskBit(id, MaskBit(ComponentTypeOf<T>()));
            ++m_structural_revision;
            m_hierarchy.Invalidate();
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
                {
                    return;
                }
                fn(id, *GetComponent<Ts>(id)...);
            });
        }

        uint32_t AliveCount() const
        {
            return m_registry.AliveCount();
        }

        template <typename Fn>
        void ForEachEntity(Fn&& fn) const
        {
            m_registry.ForEachAlive(static_cast<Fn&&>(fn));
        }

        /// @brief Main-thread cache invalidation token for entity/component membership.
        /// UUID identity is immutable after authoring; import/load must assign it before publishing.
        uint64_t StructuralRevision() const
        {
            return m_structural_revision;
        }

        Core::Memory::ArenaAllocator* GetArena() const
        {
            return m_arena;
        }

        /// Explicit structural-edit preflight; excludes the shared storage-map
        /// bookkeeping bound, which the caller must account for separately.
        template <typename T>
        size_t ComponentAdditionMemoryBound() const
        {
            const auto* storage = FindStorage<T>();
            return storage ? storage->AdditionMemoryBound() : ComponentStorage<T>::AllocationUpperBound(1, EntityRegistry::MAX_ENTITIES);
        }

        // Fixed-timestep interpolation support (game-loop.md).
        // Copies Position into PreviousPosition for all entities with a TransformComponent.
        // Call at the end of each fixed simulation step.
        void        SnapshotTransforms();

        // Linearly interpolates between PreviousPosition and Position using alpha [0,1]
        // and fills out with one RenderableTransform per entity that has a TransformComponent.
        // Call once per render frame before submitting to the renderer.
        void        FillRenderableTransforms(float alpha, Core::Containers::Array<RenderableTransform>& out);

        void        AddComponentRaw(EntityID id, ComponentTypeID type_id, uint32_t size = 0, uint32_t align = 0);
        void*       GetComponentRaw(EntityID id, ComponentTypeID type_id);
        const void* GetComponentRaw(EntityID id, ComponentTypeID type_id) const;

    private:
        EntityRegistry                                                          m_registry;
        Core::Containers::UnorderedHashMap<ComponentTypeID, IComponentStorage*> m_storages;
        Core::Containers::Array<IComponentStorage*>                             m_storages_list; // for iteration in DestroyEntity
        Core::Memory::ArenaAllocator*                                           m_arena               = nullptr;
        uint64_t                                                                m_structural_revision = 0;
        SceneHierarchy                                                          m_hierarchy           = {};

        template <typename T>
        ComponentStorage<T>& GetOrCreateStorage()
        {
            ComponentTypeID     type_id  = ComponentTypeOf<T>();
            IComponentStorage** existing = m_storages.find(type_id);
            if (existing)
            {
                return *static_cast<ComponentStorage<T>*>(*existing);
            }

            // Arena-allocate the storage object
            auto* storage = ZPushStructCtor<ComponentStorage<T>>(m_arena);
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
