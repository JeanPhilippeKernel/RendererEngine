#include <ZEngine/ECS/Components/ParentComponent.h>
#include <ZEngine/ECS/Components/TransformComponent.h>
#include <ZEngine/ECS/Reflection/ComponentReflectionRegistry.h>
#include <ZEngine/ECS/Scene.h>
#include <ZEngine/ZEngineDef.h>

namespace ZEngine::ECS
{
    void Scene::Initialize(Core::Memory::ArenaAllocator* arena)
    {
        ZENGINE_VALIDATE_ASSERT(arena != nullptr, "Scene::Initialize: arena must not be null")
        m_arena = arena;
        m_registry.Initialize(arena);
        m_storages.init(arena, 64);
        m_storages_list.init(arena, 64);
        ZENGINE_VALIDATE_ASSERT(m_hierarchy.Initialize(arena), "Scene::Initialize: hierarchy storage is unavailable")
        ++m_structural_revision;
    }

    void Scene::Shutdown()
    {
        // Storage objects are arena-allocated — no individual delete needed.
        // The arena owner is responsible for freeing memory.
        m_arena = nullptr;
        m_hierarchy.Shutdown();
        ++m_structural_revision;
    }

    EntityID Scene::CreateEntity()
    {
        if (!m_hierarchy.Reserve(m_registry.AliveCount() + 1))
        {
            return INVALID_ENTITY;
        }
        const EntityID entity = m_registry.Create();
        ++m_structural_revision;
        m_hierarchy.Invalidate();
        return entity;
    }

    void Scene::DestroyEntity(EntityID id)
    {
        if (!IsAlive(id))
        {
            return;
        }

        // Remove all components for this entity across every registered storage
        for (size_t i = 0; i < m_storages_list.size(); ++i)
        {
            m_storages_list[i]->RemoveRaw(id);
        }

        m_registry.Destroy(id);
        ++m_structural_revision;
        m_hierarchy.Invalidate();
    }

    bool Scene::CanSetParent(EntityID child, EntityID parent) const
    {
        if (!IsAlive(child) || (parent != INVALID_ENTITY && !IsAlive(parent)))
        {
            return false;
        }
        uint32_t steps = 0;
        for (EntityID walk = parent; walk != INVALID_ENTITY;)
        {
            if (walk == child || !IsAlive(walk) || steps++ >= AliveCount())
            {
                return false;
            }
            const auto* link = GetComponent<Components::ParentComponent>(walk);
            walk             = link ? link->Parent : INVALID_ENTITY;
        }
        return true;
    }

    bool Scene::SetParent(EntityID child, EntityID parent)
    {
        if (!CanSetParent(child, parent))
        {
            return false;
        }
        auto*          link     = GetComponent<Components::ParentComponent>(child);
        const EntityID previous = link ? link->Parent : INVALID_ENTITY;
        if (previous == parent)
        {
            return true;
        }
        if (parent == INVALID_ENTITY)
        {
            RemoveComponent<Components::ParentComponent>(child);
        }
        else if (!link)
        {
            AddComponent<Components::ParentComponent>(child, {parent});
        }
        else
        {
            link->Parent = parent;
            m_hierarchy.Invalidate();
        }
        return true;
    }

    void Scene::SynchronizeHierarchy()
    {
        m_hierarchy.Synchronize(*this);
    }

    bool Scene::IsAlive(EntityID id) const
    {
        return m_registry.IsAlive(id);
    }

    ArchetypeMask Scene::GetMask(EntityID id) const
    {
        return m_registry.GetMask(id);
    }

    void Scene::SnapshotTransforms()
    {
        using namespace Components;
        ForEach<TransformComponent>([](EntityID, TransformComponent& t) { t.PreviousPosition = t.Position; });
    }

    void Scene::FillRenderableTransforms(float alpha, Core::Containers::Array<RenderableTransform>& out)
    {
        using namespace Components;
        ForEach<TransformComponent>([&](EntityID id, TransformComponent& t) {
            RenderableTransform rt{};
            rt.Entity   = id;
            rt.Position = {
                t.PreviousPosition.x + (t.Position.x - t.PreviousPosition.x) * alpha,
                t.PreviousPosition.y + (t.Position.y - t.PreviousPosition.y) * alpha,
                t.PreviousPosition.z + (t.Position.z - t.PreviousPosition.z) * alpha,
            };
            rt.Rotation = t.Rotation;
            rt.Scale    = t.Scale;
            out.push(rt);
        });
    }

    void Scene::AddComponentRaw(EntityID id, ComponentTypeID type_id, uint32_t size, uint32_t align)
    {
        if (!IsAlive(id) || MaskHas(m_registry.GetMask(id), type_id))
        {
            return;
        }

        const ComponentMeta* meta = ComponentReflectionRegistry::Get().Lookup(type_id);
        if (!meta)
        {
            return;
        }

        ZENGINE_VALIDATE_ASSERT(size == 0 || size == meta->Size, "Scene::AddComponentRaw: size does not match the registered component size")
        ZENGINE_VALIDATE_ASSERT(align == 0 || align == meta->Align, "Scene::AddComponentRaw: align does not match the registered component alignment")

        if (IComponentStorage** found = m_storages.find(type_id))
        {
            (*found)->AddRaw(id);
            m_registry.SetMaskBit(id, MaskBit(type_id));
            ++m_structural_revision;
            m_hierarchy.Invalidate();
            return;
        }

        if (meta->Add)
        {
            meta->Add(*this, id);
        }
    }

    void* Scene::GetComponentRaw(EntityID id, ComponentTypeID type_id)
    {
        IComponentStorage** found = m_storages.find(type_id);
        return found ? (*found)->GetRaw(id) : nullptr;
    }

    const void* Scene::GetComponentRaw(EntityID id, ComponentTypeID type_id) const
    {
        const IComponentStorage* const* found = m_storages.find(type_id);
        return found ? (*found)->GetRaw(id) : nullptr;
    }

} // namespace ZEngine::ECS
