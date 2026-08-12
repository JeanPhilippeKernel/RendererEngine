#include <ZEngine/ECS/Components/TransformComponent.h>
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
    }

    void Scene::Shutdown()
    {
        // Storage objects are arena-allocated — no individual delete needed.
        // The arena owner is responsible for freeing memory.
        m_arena = nullptr;
    }

    EntityID Scene::CreateEntity()
    {
        return m_registry.Create();
    }

    void Scene::DestroyEntity(EntityID id)
    {
        if (!IsAlive(id))
            return;

        // Remove all components for this entity across every registered storage
        for (size_t i = 0; i < m_storages_list.size(); ++i)
            m_storages_list[i]->RemoveRaw(id);

        m_registry.Destroy(id);
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

} // namespace ZEngine::ECS
