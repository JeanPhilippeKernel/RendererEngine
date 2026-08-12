#pragma once
#include <ZEngine/ECS/EntityID.h>

namespace ZEngine::ECS
{
    // Type-erased base for ComponentStorage<T>.
    // Scene holds UnorderedHashMap<ComponentTypeID, IComponentStorage*> to
    // iterate all storages during DestroyEntity without knowing concrete types.
    struct IComponentStorage
    {
        virtual ~IComponentStorage()           = default;

        // Remove the component for this entity if present. No-op if absent.
        virtual void RemoveRaw(EntityID id)    = 0;

        // Returns true if this entity has a component in this storage.
        virtual bool HasRaw(EntityID id) const = 0;
    };
} // namespace ZEngine::ECS
